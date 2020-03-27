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

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/master/master_fwd.h"

#include "yb/tablet/snapshot_coordinator.h"

#include "yb/tserver/backup.pb.h"

#include "yb/util/status.h"

namespace yb {
namespace master {

using TabletSnapshotOperationCallback =
    std::function<void(Result<const tserver::TabletSnapshotOpResponsePB&>)>;

// Context class for MasterSnapshotCoordinator.
class SnapshotCoordinatorContext {
 public:
  // Return tablet infos for specified tablet ids.
  // The returned vector is always of the same length as the input vector,
  // with null entries returned for unknown tablet ids.
  virtual TabletInfos GetTabletInfos(const std::vector<TabletId>& id) = 0;

  virtual void SendCreateTabletSnapshotRequest(
      const scoped_refptr<TabletInfo>& tablet, const std::string& snapshot_id,
      HybridTime snapshot_hybrid_time, TabletSnapshotOperationCallback callback) = 0;

  virtual void SendRestoreTabletSnapshotRequest(
      const scoped_refptr<TabletInfo>& tablet, const std::string& snapshot_id,
      TabletSnapshotOperationCallback callback) = 0;

  virtual ~SnapshotCoordinatorContext() = default;
};

// Class that coordinates transaction aware snapshots at master.
class MasterSnapshotCoordinator : public tablet::SnapshotCoordinator {
 public:
  explicit MasterSnapshotCoordinator(SnapshotCoordinatorContext* context);
  ~MasterSnapshotCoordinator();

  // As usual negative leader_term means that this operation was replicated at the follower.
  CHECKED_STATUS Replicated(
      int64_t leader_term, const tablet::SnapshotOperationState& state) override;

  CHECKED_STATUS ListSnapshots(const TxnSnapshotId& snapshot_id, ListSnapshotsResponsePB* resp);

  Result<TxnSnapshotRestorationId> Restore(const TxnSnapshotId& snapshot_id);

  CHECKED_STATUS ListRestorations(
      const TxnSnapshotRestorationId& snapshot_id, ListSnapshotRestorationsResponsePB* resp);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_SNAPSHOT_COORDINATOR_H
