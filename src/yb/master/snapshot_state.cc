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

#include "yb/docdb/docdb.pb.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/value_type.h"

#include "yb/master/master_backup.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/snapshot_coordinator_context.h"

#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.pb.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"

using namespace std::literals;

DEFINE_UNKNOWN_uint64(snapshot_coordinator_cleanup_delay_ms, 30000,
              "Delay for snapshot cleanup after deletion.");

DEFINE_RUNTIME_int64(max_concurrent_snapshot_rpcs, -1,
    "Maximum number of tablet snapshot RPCs that can be outstanding. "
    "Only used if its value is >= 0. If its value is 0 then it means that "
    "INT_MAX number of snapshot rpcs can be concurrent. "
    "If its value is < 0 then the max_concurrent_snapshot_rpcs_per_tserver gflag and "
    "the number of TServers in the primary cluster are used to determine "
    "the number of maximum number of tablet snapshot RPCs that can be outstanding.");

DEFINE_RUNTIME_int64(max_concurrent_snapshot_rpcs_per_tserver, 1,
    "Maximum number of tablet snapshot RPCs per tserver that can be outstanding. "
    "Only used if the value of the gflag max_concurrent_snapshot_rpcs is < 0. "
    "When used it is multiplied with the number of TServers in the active cluster "
    "(not read-replicas) to obtain the total maximum concurrent snapshot RPCs. If "
    "the cluster config is not found and we are not able to determine the number of "
    "live tservers then the total maximum concurrent snapshot RPCs is just the "
    "value of this flag.");

DEFINE_test_flag(bool, treat_hours_as_milliseconds_for_snapshot_expiry, false,
    "Test only flag to expire snapshots after x milliseconds instead of x hours. Used "
    "to speed up tests");

namespace yb {
namespace master {

Result<dockv::KeyBytes> EncodedSnapshotKey(
    const TxnSnapshotId& id, SnapshotCoordinatorContext* context) {
  return EncodedKey(SysRowEntryType::SNAPSHOT, id.AsSlice(), context);
}

namespace {

std::string MakeSnapshotStateLogPrefix(
    const TxnSnapshotId& id, const std::string& schedule_id_str) {
  auto schedule_id = TryFullyDecodeSnapshotScheduleId(schedule_id_str);
  if (schedule_id) {
    return Format("Snapshot[$0/$1]: ", id, schedule_id);
  }
  return Format("Snapshot[$0]: ", id);
}

} // namespace

SnapshotState::SnapshotState(
    SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
    const tserver::TabletSnapshotOpRequestPB& request, uint64_t throttle_limit)
    : StateWithTablets(context, SysSnapshotEntryPB::CREATING,
                       MakeSnapshotStateLogPrefix(id, request.schedule_id())),
      id_(id), snapshot_hybrid_time_(request.snapshot_hybrid_time()),
      previous_snapshot_hybrid_time_(HybridTime::FromPB(request.previous_snapshot_hybrid_time())),
      schedule_id_(TryFullyDecodeSnapshotScheduleId(request.schedule_id())), version_(1),
      throttler_(throttle_limit) {
  InitTabletIds(request.tablet_id(),
                request.imported() ? SysSnapshotEntryPB::COMPLETE : SysSnapshotEntryPB::CREATING);
  request.extra_data().UnpackTo(&entries_);
  if (request.retention_duration_hours()) {
    retention_duration_hours_ = request.retention_duration_hours();
  }
}

SnapshotState::SnapshotState(
    SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
    const SysSnapshotEntryPB& entry)
    : StateWithTablets(context, entry.state(),
                       MakeSnapshotStateLogPrefix(id, entry.schedule_id())),
      id_(id), snapshot_hybrid_time_(entry.snapshot_hybrid_time()),
      previous_snapshot_hybrid_time_(HybridTime::FromPB(entry.previous_snapshot_hybrid_time())),
      schedule_id_(TryFullyDecodeSnapshotScheduleId(entry.schedule_id())),
      version_(entry.version()) {
  InitTablets(entry.tablet_snapshots());
  *entries_.mutable_entries() = entry.entries();
  if (entry.has_retention_duration_hours()) {
    retention_duration_hours_ = entry.retention_duration_hours();
  }
}

std::string SnapshotState::ToString() const {
  return Format(
      "{ id: $0 snapshot_hybrid_time: $1 schedule_id: $2 previous_snapshot_hybrid_time: $3 "
          "version: $4 initial_state: $5 tablets: $6 }",
      id_, snapshot_hybrid_time_, schedule_id_, previous_snapshot_hybrid_time_, version_,
      InitialStateName(), tablets());
}

Status SnapshotState::ToPB(
    SnapshotInfoPB* out, ListSnapshotsDetailOptionsPB options) const {
  out->set_id(id_.data(), id_.size());
  return ToEntryPB(out->mutable_entry(), ForClient::kTrue, options);
}

Status SnapshotState::ToEntryPB(
    SysSnapshotEntryPB* out, ForClient for_client,
    ListSnapshotsDetailOptionsPB options) const {
  out->set_state(for_client ? VERIFY_RESULT(AggregatedState()) : initial_state());
  out->set_snapshot_hybrid_time(snapshot_hybrid_time_.ToUint64());
  if (previous_snapshot_hybrid_time_) {
    out->set_previous_snapshot_hybrid_time(previous_snapshot_hybrid_time_.ToUint64());
  }

  TabletsToPB(out->mutable_tablet_snapshots());
  for (const auto& entry : entries_.entries()) {
    if ((entry.type() == SysRowEntryType::NAMESPACE && options.show_namespace_details()) ||
        (entry.type() == SysRowEntryType::UDTYPE && options.show_udtype_details()) ||
        (entry.type() == SysRowEntryType::TABLE && options.show_table_details()) ||
        (entry.type() == SysRowEntryType::TABLET && options.show_tablet_details())) {
      *out->add_entries() = entry;
    }
  }

  if (schedule_id_) {
    out->set_schedule_id(schedule_id_.data(), schedule_id_.size());
  }

  if (retention_duration_hours_) {
    out->set_retention_duration_hours(*retention_duration_hours_);
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
  value.push_back(dockv::ValueEntryTypeAsChar::kString);
  SysSnapshotEntryPB entry;
  RETURN_NOT_OK(ToEntryPB(&entry, ForClient::kFalse, ListSnapshotsDetailOptionsPB()));
  RETURN_NOT_OK(pb_util::AppendToString(entry, &value));
  pair->set_value(value.data(), value.size());
  return Status::OK();
}

Status SnapshotState::TryStartDelete() {
  if (initial_state() == SysSnapshotEntryPB::DELETING || delete_started_) {
    if (AllInState(SysSnapshotEntryPB::DELETED)) {
      return STATUS(NotFound, "The snapshot was deleted", id_.ToString(),
                    MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
    }
    return STATUS(NotFound, "The snapshot is being deleted", id_.ToString(),
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }
  delete_started_ = true;

  return Status::OK();
}

bool SnapshotState::delete_started() const {
  return delete_started_;
}

void SnapshotState::DeleteAborted(const Status& status) {
  delete_started_ = false;
}

void SnapshotState::PrepareOperations(TabletSnapshotOperations* out) {
  DoPrepareOperations([this, out](const TabletData& tablet) -> bool {
    if (Throttler().Throttle()) {
      return false;
    }
    out->push_back(TabletSnapshotOperation {
      .tablet_id = tablet.id,
      .schedule_id = schedule_id_,
      .snapshot_id = id_,
      .state = initial_state(),
      .snapshot_hybrid_time = snapshot_hybrid_time_,
    });
    return true;
  });
}

void SnapshotState::SetVersion(int value) {
  version_ = value;
}

bool SnapshotState::NeedCleanup() const {
  return initial_state() == SysSnapshotEntryPB::DELETING &&
         PassedSinceCompletion(
            GetAtomicFlag(&FLAGS_snapshot_coordinator_cleanup_delay_ms) * 1ms) &&
         !cleanup_tracker_.Started();
}

std::optional<SysSnapshotEntryPB::State> SnapshotState::GetTerminalStateForStatus(
    const Status& status) {
  // Table was removed.
  if (status.IsExpired()) {
    return SysSnapshotEntryPB::FAILED;
  }
  // Would not be able to create snapshot at specific time, since history was garbage collected.
  if (TransactionError(status) == TransactionErrorCode::kSnapshotTooOld) {
    return SysSnapshotEntryPB::FAILED;
  }
  // Trying to delete a snapshot of an already deleted tablet.
  if (tserver::TabletServerError(status) == tserver::TabletServerErrorPB::TABLET_NOT_FOUND &&
      initial_state() == SysSnapshotEntryPB::DELETING) {
    return SysSnapshotEntryPB::DELETED;
  }
  return std::nullopt;
}

bool SnapshotState::ShouldUpdate(const SnapshotState& other) const {
  // Backward compatibility mode
  auto other_version = other.version() == 0 ? version() + 1 : other.version();
  // If we have several updates for single snapshot, they are loaded in chronological order.
  // So latest update should be picked.
  return version() < other_version;
}

Result<bool> SnapshotState::Complete() const {
  return VERIFY_RESULT(AggregatedState()) == SysSnapshotEntryPB::COMPLETE;
}

Result<tablet::CreateSnapshotData> SnapshotState::SysCatalogSnapshotData(
    const tablet::SnapshotOperation& operation) const {
  if (!schedule_id_) {
    static Status result(STATUS(Uninitialized, ""));
    return result;
  }

  return tablet::CreateSnapshotData {
    .snapshot_hybrid_time = snapshot_hybrid_time_,
    .hybrid_time = operation.hybrid_time(),
    .op_id = operation.op_id(),
    .snapshot_dir = VERIFY_RESULT(operation.GetSnapshotDir()),
    .schedule_id = schedule_id_,
  };
}

Status SnapshotState::CheckDoneStatus(const Status& status) {
  if (initial_state() != SysSnapshotEntryPB::DELETING) {
    return status;
  }
  MasterError error(status);
  if (error == MasterErrorPB::TABLET_NOT_RUNNING || error == MasterErrorPB::TABLE_NOT_RUNNING) {
    return Status::OK();
  }
  return status;
}

bool SnapshotState::HasExpired(HybridTime now) const {
  if (schedule_id_ || !retention_duration_hours_ ||
      *retention_duration_hours_ < 0 || !snapshot_hybrid_time_) {
    return false;
  }
  auto delta = FLAGS_TEST_treat_hours_as_milliseconds_for_snapshot_expiry ?
      MonoDelta::FromMilliseconds(*retention_duration_hours_) :
      MonoDelta::FromHours(*retention_duration_hours_);
  HybridTime expiry_time = snapshot_hybrid_time_.AddDelta(delta);
  return now > expiry_time;
}

} // namespace master
} // namespace yb
