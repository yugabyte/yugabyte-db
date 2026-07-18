// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/multi_step_monitored_task.h"
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/util/status_callback.h"

namespace yb::master {

class XClusterTargetManager;

// Runs the entire xCluster failover-restore flow for a replication group.
// Creates a snapshot, performs restore to xCluster safetime per-namespace,
// tears down xCluster replication, then handles cleanup.
class XClusterFailoverTask : public MultiStepMonitoredTask {
 public:
  XClusterFailoverTask(
      Master* master, ThreadPool& async_task_pool, rpc::Messenger& messenger,
      XClusterManager& xcluster_manager, XClusterTargetManager& target_manager,
      const xcluster::ReplicationGroupId& replication_group_id,
      std::vector<NamespaceId> namespaces, const LeaderEpoch& epoch,
      StdStatusCallback completion_callback);

  server::MonitoredTaskType type() const override;
  std::string type_name() const override;
  std::string description() const override;

  const xcluster::ReplicationGroupId& replication_group_id() const {
    return replication_group_id_;
  }

 private:
  Status FirstStep() override;
  Status RegisterTask() override;
  void UnregisterTask() override;
  Status ValidateRunnable() override;

  Status PauseReplicationAndFetchSafeTimes();
  Status CreateSnapshotsStep();
  Status RestoreSnapshotsStep();
  Status PollForCurrentRestoration();
  Status DeleteReplicationStep();
  Status CleanupSnapshotsStep();

  void TryCleanupSnapshots();

  void TaskCompleted(const Status& status) override;

  Master* const master_;
  XClusterManager& xcluster_manager_;
  XClusterTargetManager& target_manager_;

  const xcluster::ReplicationGroupId replication_group_id_;

  struct FailoverInfo {
    NamespaceId namespace_id;
    TxnSnapshotId snapshot_id;
    TxnSnapshotRestorationId restoration_id;
    HybridTime restore_time;
  };

  std::vector<FailoverInfo> failovers_;
  const LeaderEpoch epoch_;
  const CoarseTimePoint deadline_;

  CoarseTimePoint failover_restoration_deadline_;

  size_t current_restoration_index_ = 0;

};

}  // namespace yb::master
