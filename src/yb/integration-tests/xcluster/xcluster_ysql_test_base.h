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
  void SetUp() override;
  Status InitClusters(const MiniClusterOptions& opts) override;
  Status Initialize(uint32_t replication_factor, uint32_t num_masters = 1);

  static std::string GetCompleteTableName(const client::YBTableName& table);

  Result<std::string> GetNamespaceId(YBClient* client);
  Result<std::string> GetUniverseId(Cluster* cluster);

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

  Status DropYsqlTable(
      Cluster* cluster,
      const std::string& namespace_name,
      const std::string& schema_name,
      const std::string& table_name);

  static void WriteWorkload(
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

  static Result<std::vector<xrepl::StreamId>> BootstrapCluster(
      const std::vector<std::shared_ptr<client::YBTable>>& tables,
      XClusterTestBase::Cluster* cluster);

 private:
  // Not thread safe. FLAGS_pgsql_proxy_webserver_port is modified each time this is called so this
  // is not safe to run in parallel.
  Status InitPostgres(Cluster* cluster, const size_t pg_ts_idx, uint16_t pg_port);
};

}  // namespace yb
