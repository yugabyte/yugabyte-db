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

#ifndef YB_MASTER_MASTER_SNAPSHOT_COORDINATOR_H
#define YB_MASTER_MASTER_SNAPSHOT_COORDINATOR_H

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/operations/operation.h"
#include "yb/tablet/snapshot_coordinator.h"

#include "yb/tserver/backup.pb.h"

#include "yb/util/status.h"

namespace yb {
namespace master {

// Class that coordinates transaction aware snapshots at master.
class MasterSnapshotCoordinator : public tablet::SnapshotCoordinator {
 public:
  explicit MasterSnapshotCoordinator(SnapshotCoordinatorContext* context);
  ~MasterSnapshotCoordinator();

  Result<TxnSnapshotId> Create(
      const SysRowEntries& entries, bool imported, CoarseTimePoint deadline);

  CHECKED_STATUS Delete(const TxnSnapshotId& snapshot_id, CoarseTimePoint deadline);

  // As usual negative leader_term means that this operation was replicated at the follower.
  CHECKED_STATUS CreateReplicated(
      int64_t leader_term, const tablet::SnapshotOperationState& state) override;

  CHECKED_STATUS DeleteReplicated(
      int64_t leader_term, const tablet::SnapshotOperationState& state) override;

  CHECKED_STATUS RestoreSysCatalogReplicated(
      int64_t leader_term, const tablet::SnapshotOperationState& state) override;

  CHECKED_STATUS ListSnapshots(
      const TxnSnapshotId& snapshot_id, bool list_deleted, ListSnapshotsResponsePB* resp);

  Result<TxnSnapshotRestorationId> Restore(const TxnSnapshotId& snapshot_id, HybridTime restore_at);

  CHECKED_STATUS ListRestorations(
      const TxnSnapshotRestorationId& restoration_id, const TxnSnapshotId& snapshot_id,
      ListSnapshotRestorationsResponsePB* resp);

  Result<SnapshotScheduleId> CreateSchedule(
      const CreateSnapshotScheduleRequestPB& request, CoarseTimePoint deadline);

  CHECKED_STATUS ListSnapshotSchedules(
      const SnapshotScheduleId& snapshot_schedule_id, ListSnapshotSchedulesResponsePB* resp);

  // Load snapshots data from system catalog.
  CHECKED_STATUS Load(tablet::Tablet* tablet) override;

  // Check whether we have write request for snapshot while replaying write request during
  // bootstrap. And upsert snapshot from it in this case.
  // key and value are entry from the write batch.
  CHECKED_STATUS ApplyWritePair(const Slice& key, const Slice& value) override;

  CHECKED_STATUS FillHeartbeatResponse(TSHeartbeatResponsePB* resp);

  void SysCatalogLoaded(int64_t term);

  // For each returns map from schedule id to sorted vectors of tablets id in this schedule.
  Result<SnapshotSchedulesToTabletsMap> MakeSnapshotSchedulesToTabletsMap();

  void Start();

  void Shutdown();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_SNAPSHOT_COORDINATOR_H
