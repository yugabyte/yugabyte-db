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

#include "yb/master/snapshot_schedule_state.h"

#include "yb/docdb/docdb.pb.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/value_type.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_error.h"
#include "yb/master/snapshot_coordinator_context.h"

#include "yb/server/clock.h"

#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

namespace yb {
namespace master {

Result<SnapshotScheduleState> SnapshotScheduleState::Create(
    SnapshotCoordinatorContext* context, const SnapshotScheduleOptionsPB& options) {
  RETURN_NOT_OK(ValidateOptions(options));
  return SnapshotScheduleState(context, options);
}

SnapshotScheduleState::SnapshotScheduleState(
    SnapshotCoordinatorContext* context, const SnapshotScheduleId& id,
    const SnapshotScheduleOptionsPB& options)
    : context_(*context), id_(id), options_(options) {
}

SnapshotScheduleState::SnapshotScheduleState(
    SnapshotCoordinatorContext* context, const SnapshotScheduleOptionsPB& options)
    : context_(*context), id_(SnapshotScheduleId::GenerateRandom()), options_(options) {
}

Result<dockv::KeyBytes> SnapshotScheduleState::EncodedKey(
    const SnapshotScheduleId& schedule_id, SnapshotCoordinatorContext* context) {
  return master::EncodedKey(SysRowEntryType::SNAPSHOT_SCHEDULE, schedule_id.AsSlice(), context);
}

Result<dockv::KeyBytes> SnapshotScheduleState::EncodedKey() const {
  return EncodedKey(id_, &context_);
}

Status SnapshotScheduleState::StoreToWriteBatch(docdb::KeyValueWriteBatchPB* out) const {
  return StoreToWriteBatch(options_, out);
}

Status SnapshotScheduleState::StoreToWriteBatch(
    const SnapshotScheduleOptionsPB& options, docdb::KeyValueWriteBatchPB* out) const {
  auto encoded_key = VERIFY_RESULT(EncodedKey());
  auto pair = out->add_write_pairs();
  pair->set_key(encoded_key.AsSlice().cdata(), encoded_key.size());
  auto* value = pair->mutable_value();
  value->push_back(dockv::ValueEntryTypeAsChar::kString);
  return pb_util::AppendPartialToString(options, value);
}

Status SnapshotScheduleState::ToPB(SnapshotScheduleInfoPB* pb) const {
  pb->set_id(id_.data(), id_.size());
  *pb->mutable_options() = options_;
  return Status::OK();
}

Result<SnapshotScheduleOptionsPB> SnapshotScheduleState::GetUpdatedOptions(
    const EditSnapshotScheduleRequestPB& edit_request) const {
  SnapshotScheduleOptionsPB updated_options = options_;
  if (edit_request.has_interval_sec()) {
    updated_options.set_interval_sec(edit_request.interval_sec());
  }
  if (edit_request.has_retention_duration_sec()) {
    updated_options.set_retention_duration_sec(edit_request.retention_duration_sec());
  }
  RETURN_NOT_OK(ValidateOptions(updated_options));
  return updated_options;
}

Status SnapshotScheduleState::ValidateOptions(const SnapshotScheduleOptionsPB& options) {
  if (options.interval_sec() == 0) {
    return STATUS(InvalidArgument, "Zero interval");
  } else if (options.retention_duration_sec() == 0) {
    return STATUS(InvalidArgument, "Zero retention");
  } else if (options.interval_sec() >= options.retention_duration_sec()) {
    return STATUS(InvalidArgument, "Interval must be strictly less than retention");
  } else {
    return Status::OK();
  }
}

std::string SnapshotScheduleState::ToString() const {
  return YB_CLASS_TO_STRING(id, options);
}

bool SnapshotScheduleState::deleted() const {
  return HybridTime::FromPB(options_.delete_time()).is_valid();
}

void SnapshotScheduleState::PrepareOperations(
    HybridTime last_snapshot_time, HybridTime now, SnapshotScheduleOperations* operations) {
  if (creating_snapshot_data_.snapshot_id) {
    return;
  }
  auto delete_time = HybridTime::FromPB(options_.delete_time());
  if (delete_time) {
    // Check whether we are ready to cleanup deleted schedule.
    if (now > delete_time.AddMilliseconds(FLAGS_snapshot_coordinator_cleanup_delay_ms) &&
        !CleanupTracker().Started()) {
      LOG_WITH_PREFIX(INFO) << "Snapshot Schedule " << id() << " cleanup started.";
      if (!CleanupTracker().Start().ok()) {
        LOG(DFATAL) << "Snapshot Schedule " << id() << " cleanup was already started previously.";
      }
      operations->push_back(SnapshotScheduleOperation {
        .type = SnapshotScheduleOperationType::kCleanup,
        .schedule_id = id_,
        .snapshot_id = TxnSnapshotId::Nil(),
        .filter = {},
        .previous_snapshot_hybrid_time = {},
      });
    }
    return;
  }
  if (last_snapshot_time && last_snapshot_time.AddSeconds(options_.interval_sec()) > now) {
    // Time from the last snapshot did not passed yet.
    return;
  }
  operations->push_back(MakeCreateSnapshotOperation(last_snapshot_time));
}

SnapshotScheduleOperation SnapshotScheduleState::MakeCreateSnapshotOperation(
    HybridTime last_snapshot_time) {
  creating_snapshot_data_.snapshot_id = TxnSnapshotId::GenerateRandom();
  creating_snapshot_data_.start_time = CoarseMonoClock::now();
  VLOG_WITH_PREFIX_AND_FUNC(4) << creating_snapshot_data_.snapshot_id;
  return SnapshotScheduleOperation {
    .type = SnapshotScheduleOperationType::kCreateSnapshot,
    .schedule_id = id_,
    .snapshot_id = creating_snapshot_data_.snapshot_id,
    .filter = options_.filter(),
    .previous_snapshot_hybrid_time = last_snapshot_time,
  };
}

Result<SnapshotScheduleOperation> SnapshotScheduleState::ForceCreateSnapshot(
    HybridTime last_snapshot_time) {
  if (creating_snapshot_data_.snapshot_id) {
    auto passed = CoarseMonoClock::now() - creating_snapshot_data_.start_time;
    return STATUS_EC_FORMAT(
        IllegalState, MasterError(MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION),
        "Creating snapshot in progress: $0 (passed $1)",
        creating_snapshot_data_.snapshot_id, passed);
  }
  return MakeCreateSnapshotOperation(last_snapshot_time);
}

void SnapshotScheduleState::SnapshotFinished(
    const TxnSnapshotId& snapshot_id, const Status& status) {
  if (creating_snapshot_data_.snapshot_id != snapshot_id) {
    return;
  }
  creating_snapshot_data_.snapshot_id = TxnSnapshotId::Nil();
}

std::string SnapshotScheduleState::LogPrefix() const {
  return Format("$0: ", id_);
}

} // namespace master
} // namespace yb
