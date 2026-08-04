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

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_tasks.h"

namespace yb {

namespace client {
class XClusterRemoteClientHolder;
}  // namespace client

namespace master {

// Wait for the index DocDB table on the other universe to get created, reach the backfill stage
// and then add the index to replication.
// For colocated indexes exits after waiting for the backfill to start, since parent tablet is
// already part of replication.
class AddBiDirectionalIndexToXClusterTargetTask : public MultiStepTableTaskBase {
 public:
  AddBiDirectionalIndexToXClusterTargetTask(
      TableInfoPtr index_table_info, scoped_refptr<UniverseReplicationInfo> universe,
      std::shared_ptr<client::XClusterRemoteClientHolder> remote_client, Master& master,
      const LeaderEpoch& epoch, CoarseTimePoint deadline);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddTableToXClusterTarget;
  }

  std::string type_name() const override { return "Add Index to bi-directional xCluster"; }

  std::string description() const override;

 private:
  Status FirstStep() override;
  Status GetSourceIndexTableId();
  Status WaitForBackfillIndexToStartOnSource();
  Status GetSourceIndexStreamId();
  Status AddIndexToReplicationGroup(const xrepl::StreamId& stream_id);
  Status WaitForSetupReplication();

  scoped_refptr<UniverseReplicationInfo> universe_;
  std::shared_ptr<client::XClusterRemoteClientHolder> remote_client_;
  XClusterManagerIf& xcluster_manager_;
  const CoarseTimePoint deadline_;

  TableId source_index_table_id_;
  TableId source_indexed_table_id_;
};

}  // namespace master
}  // namespace yb
