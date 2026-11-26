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

#include <map>
#include <string>
#include <vector>

#include "yb/cdc/xcluster_types.h"

#include "yb/common/entity_ids_types.h"

#include "yb/master/leader_epoch.h"
#include "yb/master/multi_step_monitored_task.h"

namespace yb::master {

class CatalogManager;
class XClusterManager;
class XClusterTargetManager;

// Asks the source to delete the WAL anchor streams of the tables that have a pending deletion
// marker, and clears the markers once the source confirms. Only one task runs at a time, so a slow
// source cannot pile up work. Markers that this task does not manage to clear are picked up by the
// next task.
class XClusterWalAnchorDeletionTask : public MultiStepMonitoredTask {
 public:
  XClusterWalAnchorDeletionTask(
      CatalogManager& catalog_manager, rpc::Messenger& messenger,
      XClusterTargetManager& target_manager, const LeaderEpoch& epoch);

  server::MonitoredTaskType type() const override;
  std::string type_name() const override;
  std::string description() const override;

 private:
  struct Batch {
    std::vector<TableId> consumer_table_ids;
    std::vector<TableId> source_table_ids;
  };

  Status RegisterTask() override;
  void UnregisterTask() override;
  Status ValidateRunnable() override;

  // Get the a snapshot of table ids set that have a pending deletion marker, then
  // send delete requests to the source for each replication group.
  Status FirstStep() override;

  Status SendDeleteRequests();

  Status DeleteWalAnchorStreamsOnSource(
      const xcluster::ReplicationGroupId& replication_group_id, const Batch& batch);

  xcluster::ReplicationGroupId FindAutomaticModeReplicationGroup(const NamespaceId& namespace_id);

  CatalogManager& catalog_manager_;
  XClusterManager& xcluster_manager_;
  XClusterTargetManager& target_manager_;
  const LeaderEpoch epoch_;

  std::map<xcluster::ReplicationGroupId, Batch> batch_by_group_;
};

}  // namespace yb::master
