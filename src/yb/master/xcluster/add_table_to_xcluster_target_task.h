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

#include <string>

#include "yb/cdc/xcluster_types.h"
#include "yb/client/client_fwd.h"
#include "yb/common/hybrid_time.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/post_tablet_create_task_base.h"

namespace yb {

namespace client {
class XClusterRemoteClientHolder;
}  // namespace client

namespace master {

class UniverseReplicationInfo;

// This task adds a newly created table in the consumer xCluster universe to transactional
// replication group. The table must be in PREPARING state with all tablets created at the start of
// the task. The task performs bootstrap of the producer table, adds it to the replication group,
// waits for the xCluster Safe Time to include the new table, and finally marks the table as
// RUNNING.
class AddTableToXClusterTargetTask : public PostTabletCreateTaskBase {
 public:
  AddTableToXClusterTargetTask(
      scoped_refptr<UniverseReplicationInfo> universe, CatalogManager& catalog_manager,
      rpc::Messenger& messenger, TableInfoPtr table_info, const LeaderEpoch& epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kAddTableToXClusterTarget;
  }

  std::string type_name() const override { return "Add Table to xCluster Target replication"; }

  std::string description() const override;

 private:
  Status FirstStep() override;
  Status AddTableToReplicationGroup(client::BootstrapProducerResult bootstrap_result);
  Status WaitForSetupUniverseReplicationToFinish();
  Status RefreshAndGetXClusterSafeTime();
  Status WaitForXClusterSafeTimeCaughtUp();

  // Returns nullopt if the namespace is no longer part of xCluster replication, otherwise returns a
  // valid safe time.
  Result<std::optional<HybridTime>> GetXClusterSafeTimeWithoutDdlQueue();

  HybridTime bootstrap_time_ = HybridTime::kInvalid;
  HybridTime initial_xcluster_safe_time_ = HybridTime::kInvalid;
  scoped_refptr<UniverseReplicationInfo> universe_;
  std::shared_ptr<client::XClusterRemoteClientHolder> remote_client_;
  XClusterManagerIf& xcluster_manager_;
  bool is_db_scoped_ = false;
};

}  // namespace master
}  // namespace yb
