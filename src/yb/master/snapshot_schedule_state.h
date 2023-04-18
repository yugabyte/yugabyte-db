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

#pragma once

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_backup.pb.h"

#include "yb/util/async_task_util.h"
#include "yb/util/tostring.h"

namespace yb {
namespace master {

YB_DEFINE_ENUM(SnapshotScheduleOperationType, (kCreateSnapshot)(kCleanup));

struct SnapshotScheduleOperation {
  SnapshotScheduleOperationType type;
  SnapshotScheduleId schedule_id;
  TxnSnapshotId snapshot_id;
  SnapshotScheduleFilterPB filter;
  HybridTime previous_snapshot_hybrid_time;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        type, schedule_id, snapshot_id, filter, previous_snapshot_hybrid_time);
  }
};

struct CreatingSnapshotData {
  CoarseTimePoint start_time;
  TxnSnapshotId snapshot_id = TxnSnapshotId::Nil();
};

using SnapshotScheduleOperations = std::vector<SnapshotScheduleOperation>;

class SnapshotScheduleState {
 public:
  SnapshotScheduleState(
      SnapshotCoordinatorContext* context, const SnapshotScheduleOptionsPB& req);

  SnapshotScheduleState(
      SnapshotCoordinatorContext* context, const SnapshotScheduleId& id,
      const SnapshotScheduleOptionsPB& options);

  static Result<SnapshotScheduleState> Create(
      SnapshotCoordinatorContext* context, const SnapshotScheduleOptionsPB& options);

  const SnapshotScheduleId& id() const {
    return id_;
  }

  bool ShouldUpdate(const SnapshotScheduleState& other) const {
    return true;
  }

  const SnapshotScheduleOptionsPB& options() const {
    return options_;
  }

  SnapshotScheduleOptionsPB& mutable_options() {
    return options_;
  }

  AsyncTaskTracker& CleanupTracker() {
    return cleanup_tracker_;
  }

  bool deleted() const;

  void PrepareOperations(
      HybridTime last_snapshot_time, HybridTime now, SnapshotScheduleOperations* operations);
  Result<SnapshotScheduleOperation> ForceCreateSnapshot(HybridTime last_snapshot_time);
  void SnapshotFinished(const TxnSnapshotId& snapshot_id, const Status& status);

  Result<dockv::KeyBytes> EncodedKey() const;
  static Result<dockv::KeyBytes> EncodedKey(
      const SnapshotScheduleId& schedule_id, SnapshotCoordinatorContext* context);

  Status StoreToWriteBatch(docdb::KeyValueWriteBatchPB* write_batch) const;
  Status StoreToWriteBatch(
      const SnapshotScheduleOptionsPB& options, docdb::KeyValueWriteBatchPB* out) const;
  Status ToPB(SnapshotScheduleInfoPB* pb) const;
  std::string ToString() const;

  const CreatingSnapshotData& creating_snapshot_data() const {
    return creating_snapshot_data_;
  }

  Result<SnapshotScheduleOptionsPB> GetUpdatedOptions(
      const EditSnapshotScheduleRequestPB& edit_request) const;
  static Status ValidateOptions(const SnapshotScheduleOptionsPB& new_options);

 private:
  std::string LogPrefix() const;

  SnapshotScheduleOperation MakeCreateSnapshotOperation(HybridTime last_snapshot_time);

  SnapshotCoordinatorContext& context_;
  SnapshotScheduleId id_;
  SnapshotScheduleOptionsPB options_;

  // When snapshot is being created for this schedule, this field contains id of this snapshot.
  // To prevent creating other snapshots during that time.
  CreatingSnapshotData creating_snapshot_data_;

  AsyncTaskTracker cleanup_tracker_;
};

} // namespace master
} // namespace yb
