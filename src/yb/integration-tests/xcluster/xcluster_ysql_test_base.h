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

#include "yb/integration-tests/xcluster/xcluster_test_base.h"

namespace yb {
constexpr int kWaitForRowCountTimeout = 5 * kTimeMultiplier;

class XClusterYsqlTestBase : public XClusterTestBase {
 public:
  struct SetupParams {
    std::vector<uint32_t> num_consumer_tablets = {3};
    std::vector<uint32_t> num_producer_tablets = {3};
    uint32_t replication_factor = 3;
    uint32_t num_masters = 1;
    bool ranged_partitioned = false;
    bool is_colocated = false;
  };

  void SetUp() override;
  Status InitClusters(const MiniClusterOptions& opts) override;

  Status SetUpWithParams(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets, uint32_t replication_factor,
      uint32_t num_masters = 1, const bool ranged_partitioned = false);

  Status SetUpClusters();

  Status SetUpClusters(const SetupParams& params);

  Status InitProducerClusterOnly(const MiniClusterOptions& opts);
  Status Initialize(uint32_t replication_factor, uint32_t num_masters = 1);

  static std::string GetCompleteTableName(const client::YBTableName& table);

  Result<NamespaceId> GetNamespaceId(YBClient* client);
  Result<NamespaceId> GetNamespaceId(YBClient* client, const NamespaceName& ns_name);
  Result<std::string> GetUniverseId(Cluster* cluster);
  Result<master::SysClusterConfigEntryPB> GetClusterConfig(Cluster& cluster);

  Result<client::YBTableName> CreateYsqlTable(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& schema_name,
      const std::string& table_name,
      const boost::optional<std::string>& tablegroup_name,
      uint32_t num_tablets,
      bool colocated = false,
      const ColocationId colocation_id = 0,
      const bool ranged_partitioned = false);

  Result<client::YBTableName> CreateYsqlTable(
      uint32_t idx, uint32_t num_tablets, Cluster* cluster,
      const boost::optional<std::string>& tablegroup_name = {}, bool colocated = false,
      const bool ranged_partitioned = false);

  Result<client::YBTableName> GetYsqlTable(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& schema_name,
      const std::string& table_name,
      bool verify_table_name = true,
      bool verify_schema_name = false,
      bool exclude_system_tables = true);

  Result<bool> IsTableDeleted(Cluster* cluster, const client::YBTableName& table_name);

  Status DropYsqlTable(
      Cluster* cluster, const std::string& namespace_name, const std::string& schema_name,
      const std::string& table_name, bool is_index = false);

  Status DropYsqlTable(Cluster& cluster, const client::YBTable& table);

  static Status WriteWorkload(
      const client::YBTableName& table, uint32_t start, uint32_t end, Cluster* cluster);

  static Result<pgwrapper::PGResultPtr> ScanToStrings(
      const client::YBTableName& table_name, Cluster* cluster);

  static Result<int> GetRowCount(
      const client::YBTableName& table_name, Cluster* cluster, bool read_latest = false);

  static Status WaitForRowCount(
      const client::YBTableName& table_name, uint32_t row_count, Cluster* cluster,
      bool allow_greater = false);

  static Status ValidateRows(
      const client::YBTableName& table_name, int row_count, Cluster* cluster);

  Status VerifyWrittenRecords(
      std::shared_ptr<client::YBTable> producer_table = {},
      std::shared_ptr<client::YBTable> consumer_table = {});

  Status VerifyWrittenRecords(
      const client::YBTableName& producer_table_name,
      const client::YBTableName& consumer_table_name);
  static Result<std::vector<xrepl::StreamId>> BootstrapCluster(
      const std::vector<std::shared_ptr<client::YBTable>>& tables,
      XClusterTestBase::Cluster* cluster);

  void BumpUpSchemaVersionsWithAlters(const std::vector<std::shared_ptr<client::YBTable>>& tables);

  Status InsertRowsInProducer(
      uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table = {},
      bool use_transaction = false);

  Status DeleteRowsInProducer(
      uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table = {},
      bool use_transaction = false);

  Status InsertGenerateSeriesOnProducer(
      uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table = {});

  Status InsertTransactionalBatchOnProducer(
      uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table = {},
      bool commit_transaction = true);

  Status WriteWorkload(
      uint32_t start, uint32_t end, Cluster* cluster, const client::YBTableName& table,
      bool delete_op = false, bool use_transaction = false);

  Status CheckpointReplicationGroup();
  Status CreateReplicationFromCheckpoint(const std::string& target_master_addresses = {});
  Status WaitForCreateReplicationToFinish(const std::string& target_master_addresses);

 protected:
  void TestReplicationWithSchemaChanges(TableId producer_table_id, bool bootstrap);

 private:
  void InitFlags(const MiniClusterOptions& opts);

  // Not thread safe. FLAGS_pgsql_proxy_webserver_port is modified each time this is called so this
  // is not safe to run in parallel.
  Status InitPostgres(Cluster* cluster, const size_t pg_ts_idx, uint16_t pg_port);

  Status WriteGenerateSeries(
      uint32_t start, uint32_t end, Cluster* cluster, const client::YBTableName& table);

  Status WriteTransactionalWorkload(
      uint32_t start, uint32_t end, Cluster* cluster, const client::YBTableName& table,
      bool commit_transaction = true);
};

}  // namespace yb
