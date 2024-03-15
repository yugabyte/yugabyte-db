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

#include "yb/cdc/cdc_types.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/tools/admin-test-base.h"
#include "yb/util/test_util.h"

namespace yb {

class XClusterExternalMiniClusterBase : public YBTest {
 public:
  void SetUp() override;

  ExternalMiniCluster* SourceCluster() { return source_cluster_.cluster_.get(); }
  ExternalMiniCluster* TargetCluster() { return target_cluster_.cluster_.get(); }
  client::YBClient* SourceClient() { return source_cluster_.client_.get(); }
  client::YBClient* TargetClient() { return target_cluster_.client_.get(); }
  const std::vector<std::shared_ptr<client::YBTable>>& SourceTables() {
    return source_cluster_.tables_;
  }
  const std::vector<std::shared_ptr<client::YBTable>>& TargetTables() {
    return target_cluster_.tables_;
  }
  client::YBTable* SourceTable() { return source_cluster_.tables_.front().get(); }
  client::YBTable* TargetTable() { return target_cluster_.tables_.front().get(); }

  virtual Status SetupClusters();
  virtual Status SetupClustersAndReplicationGroup();

  Status SetupReplication(
      xcluster::ReplicationGroupId replication_group_id = kReplicationGroupId,
      std::vector<std::shared_ptr<client::YBTable>> source_tables = {});

  Status VerifyReplicationError(
      const client::YBTable* consumer_table, const xrepl::StreamId& stream_id,
      const std::optional<ReplicationErrorPb> expected_replication_error);

  Result<uint32> PromoteAutoFlags(
      ExternalMiniCluster* cluster, AutoFlagClass flag_class = AutoFlagClass::kExternal,
      bool force = false);

 protected:
  struct ClusterSetupOptions {
    uint32_t num_tservers = 1;
    uint32_t num_masters = 1;
    std::vector<std::string> master_flags;
    std::vector<std::string> tserver_flags;
  };

  struct Cluster {
    std::unique_ptr<ExternalMiniCluster> cluster_;
    std::unique_ptr<client::YBClient> client_;
    std::unique_ptr<master::MasterReplicationProxy> master_proxy_;

    std::vector<std::shared_ptr<client::YBTable>> tables_;
    ClusterSetupOptions setup_opts_;  // Cluster specific setup options.
  };

  virtual void AddCommonOptions();

  template <class... Args>
  Result<std::string> RunYbAdmin(Cluster* cluster, Args&&... args) {
    return tools::RunAdminToolCommand(
        cluster->cluster_->GetMasterAddresses(), std::forward<Args>(args)...);
  }

  Status RunOnBothClusters(std::function<Status(Cluster*)> run_on_cluster);

  Result<Cluster> CreateCluster(
      const std::string& cluster_id, const std::string& cluster_short_name, uint32_t num_masters,
      uint32_t num_tservers, const std::vector<std::string>& master_flags,
      const std::vector<std::string>& tserver_flags);

  Result<client::TableHandle> CreateTable(
      int num_tablets, client::YBClient* client, const client::YBTableName& table_name);

  Result<xrepl::StreamId> GetStreamId(client::YBTable* table = nullptr);

  ClusterSetupOptions setup_opts_;
  Cluster source_cluster_;
  Cluster target_cluster_;
};

}  // namespace yb
