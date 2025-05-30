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

#include "yb/cdc/xcluster_types.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/tools/tools_test_utils.h"

namespace yb {

class XClusterDDLReplicationTestBase : public XClusterYsqlTestBase {
 public:
  XClusterDDLReplicationTestBase() = default;
  ~XClusterDDLReplicationTestBase() = default;

  virtual void SetUp() override;

  bool UseAutomaticMode() override {
    // All these tests use automatic.
    return true;
  }

  Status SetUpClusters(bool is_colocated = false, bool start_yb_controller_servers = false);

  virtual Status CheckpointReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id = kReplicationGroupId,
      bool require_no_bootstrap_needed = true) override {
    return XClusterYsqlTestBase::CheckpointReplicationGroup(
        replication_group_id, require_no_bootstrap_needed);
  }

  // Unlike the previous method, this one does not fail if bootstrap is required.
  Status CheckpointReplicationGroupOnNamespaces(const std::vector<NamespaceName>& namespace_names);

  // A empty list for namespace_names (the default) means just the namespace namespace_name.
  // Saves backups in TmpDir directories.
  Status BackupFromProducer(std::vector<NamespaceName> namespace_names = {});

  // A empty list for namespace_names (the default) means just the namespace namespace_name.
  // Restores backups saved by BackupFromProducer.
  Status RestoreToConsumer(std::vector<NamespaceName> namespace_names = {});

  Status RunBackupCommand(const std::vector<std::string>& args, MiniClusterBase* cluster);

  std::string GetTempDir(const std::string& subdir) { return tmp_dir_ / subdir; }

  Result<std::shared_ptr<client::YBTable>> GetProducerTable(
      const client::YBTableName& producer_table_name);

  Result<std::shared_ptr<client::YBTable>> GetConsumerTable(
      const client::YBTableName& producer_table_name);

  void InsertRowsIntoProducerTableAndVerifyConsumer(
      const client::YBTableName& producer_table_name, uint32_t start = 0, uint32_t end = 50,
      const xcluster::ReplicationGroupId replication_group = kReplicationGroupId);

  Status WaitForSafeTimeToAdvanceToNowWithoutDDLQueue();

  Status PrintDDLQueue(Cluster& cluster);

  Result<xcluster::SafeTimeBatch> FetchSafeTimeBatchFromReplicatedDdls();

  Status StepDownDdlQueueTablet(Cluster& cluster);

  // We require at least one colocated table to exist before setting up replication.
  Status CreateInitialColocatedTable();

  Result<std::string> GetReplicationRole(Cluster& cluster, const NamespaceName& database = "");

  Status ValidateReplicationRole(
      Cluster& cluster, const std::string& expected_role, const NamespaceName& database = "");

  // Swaps producer_cluster_ and consumer_cluster_ if the replication_direction is different.
  // Returns if the direction was changed.
  // This allows us to reuse existing test functions for switchovers.
  //
  // NOTE: This does not change the log prefixes, search for "Switched replication direction" in
  // logs to determine the current direction.
  virtual bool SetReplicationDirection(ReplicationDirection replication_direction);

  const std::string kInitialColocatedTableName = "initial_colocated_table";

 private:
  tools::TmpDirProvider tmp_dir_;

  ReplicationDirection replication_direction_ = ReplicationDirection::AToB;
};

}  // namespace yb
