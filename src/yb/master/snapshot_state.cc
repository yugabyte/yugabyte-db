// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/master/snapshot_state.h"

#include "yb/common/transaction_error.h"

#include "yb/docdb/key_bytes.h"

#include "yb/master/master_error.h"
#include "yb/master/snapshot_coordinator_context.h"

#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/pb_util.h"

using namespace std::literals;

DEFINE_uint64(snapshot_coordinator_cleanup_delay_ms, 30000,
              "Delay for snapshot cleanup after deletion.");

namespace yb {
namespace master {

Result<docdb::KeyBytes> EncodedSnapshotKey(
    const TxnSnapshotId& id, SnapshotCoordinatorContext* context) {
  return EncodedKey(SysRowEntry::SNAPSHOT, id.AsSlice(), context);
}

SnapshotState::SnapshotState(
    SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
    const tserver::TabletSnapshotOpRequestPB& request)
    : StateWithTablets(context, SysSnapshotEntryPB::CREATING),
      id_(id), snapshot_hybrid_time_(request.snapshot_hybrid_time()),
      schedule_id_(TryFullyDecodeSnapshotScheduleId(request.schedule_id())), version_(1) {
  InitTabletIds(request.tablet_id(),
                request.imported() ? SysSnapshotEntryPB::COMPLETE : SysSnapshotEntryPB::CREATING);
  request.extra_data().UnpackTo(&entries_);
}

SnapshotState::SnapshotState(
    SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
    const SysSnapshotEntryPB& entry)
    : StateWithTablets(context, entry.state()),
      id_(id), snapshot_hybrid_time_(entry.snapshot_hybrid_time()),
      schedule_id_(TryFullyDecodeSnapshotScheduleId(entry.schedule_id())),
      version_(entry.version()) {
  InitTablets(entry.tablet_snapshots());
  *entries_.mutable_entries() = entry.entries();
}

std::string SnapshotState::ToString() const {
  return Format(
      "{ id: $0 snapshot_hybrid_time: $1 schedule_id: $2 version: $3 initial_state: $4 "
          "tablets: $5 }",
      id_, snapshot_hybrid_time_, schedule_id_, version_, InitialStateName(), tablets());
}

Status SnapshotState::ToPB(SnapshotInfoPB* out) {
  out->set_id(id_.data(), id_.size());
  return ToEntryPB(out->mutable_entry(), ForClient::kTrue);
}

Status SnapshotState::ToEntryPB(SysSnapshotEntryPB* out, ForClient for_client) {
  out->set_state(for_client ? VERIFY_RESULT(AggregatedState()) : initial_state());
  out->set_snapshot_hybrid_time(snapshot_hybrid_time_.ToUint64());

  TabletsToPB(out->mutable_tablet_snapshots());

  *out->mutable_entries() = entries_.entries();

  if (schedule_id_) {
    out->set_schedule_id(schedule_id_.data(), schedule_id_.size());
  }

  out->set_version(version_);

  return Status::OK();
}

Status SnapshotState::StoreToWriteBatch(docdb::KeyValueWriteBatchPB* out) {
  ++version_;
  auto encoded_key = VERIFY_RESULT(EncodedSnapshotKey(id_, &context()));
  auto pair = out->add_write_pairs();
  pair->set_key(encoded_key.AsSlice().cdata(), encoded_key.size());
  faststring value;
  value.push_back(docdb::ValueTypeAsChar::kString);
  SysSnapshotEntryPB entry;
  RETURN_NOT_OK(ToEntryPB(&entry, ForClient::kFalse));
  pb_util::AppendToString(entry, &value);
  pair->set_value(value.data(), value.size());
  return Status::OK();
}

Status SnapshotState::TryStartDelete() {
  if (AllInState(SysSnapshotEntryPB::DELETED)) {
    return STATUS(NotFound, "The snapshot was deleted", id_.ToString(),
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }
  if (delete_started_ || HasInState(SysSnapshotEntryPB::DELETING)) {
    return STATUS(NotFound, "The snapshot is being deleted", id_.ToString(),
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }
  delete_started_ = true;

  return Status::OK();
}

void SnapshotState::DeleteAborted(const Status& status) {
  delete_started_ = false;
}

void SnapshotState::PrepareOperations(TabletSnapshotOperations* out) {
  DoPrepareOperations([this, out](const TabletData& tablet) {
    out->push_back(TabletSnapshotOperation {
      .tablet_id = tablet.id,
      .schedule_id = schedule_id_,
      .snapshot_id = id_,
      .state = initial_state(),
      .snapshot_hybrid_time = snapshot_hybrid_time_,
    });
  });
}

void SnapshotState::SetVersion(int value) {
  version_ = value;
}

bool SnapshotState::NeedCleanup() const {
  return initial_state() == SysSnapshotEntryPB::DELETING &&
         PassedSinceCompletion(GetAtomicFlag(&FLAGS_snapshot_coordinator_cleanup_delay_ms) * 1ms);
}

bool SnapshotState::IsTerminalFailure(const Status& status) {
  // Table was removed.
  if (status.IsExpired()) {
    return true;
  }
  // Would not be able to create snapshot at specific time, since history was garbage collected.
  if (TransactionError(status) == TransactionErrorCode::kSnapshotTooOld) {
    return true;
  }
  return false;
}

bool SnapshotState::ShouldUpdate(const SnapshotState& other) const {
  // Backward compatibility mode
  int other_version = other.version() == 0 ? version() + 1 : other.version();
  // If we have several updates for single snapshot, they are loaded in chronological order.
  // So latest update should be picked.
  return version() < other_version;
}

Result<tablet::CreateSnapshotData> SnapshotState::SysCatalogSnapshotData(
    const tablet::SnapshotOperationState& state) const {
  if (!schedule_id_) {
    static Status result(STATUS(Uninitialized, ""));
    return result;
  }

  return tablet::CreateSnapshotData {
    .snapshot_hybrid_time = snapshot_hybrid_time_,
    .hybrid_time = state.hybrid_time(),
    .op_id = OpId::FromPB(state.op_id()),
    .snapshot_dir = VERIFY_RESULT(state.GetSnapshotDir()),
    .schedule_id = schedule_id_,
  };
}

} // namespace master
} // namespace yb
