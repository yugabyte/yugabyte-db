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

#include "yb/client/schema.h"

#include "yb/integration-tests/xcluster/xcluster_test_base.h"

#include "yb/server/hybrid_clock.h"

namespace yb {

using YBTables = std::vector<std::shared_ptr<client::YBTable>>;
using YBClusters = std::vector<XClusterTestBase::Cluster*>;

constexpr int kWaitForRowCountTimeout = 5 * kTimeMultiplier;

class XClusterYcqlTestBase : public XClusterTestBase {
 public:
  void SetUp() override;

  Result<YBTables> SetUpWithParams(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets,
      uint32_t replication_factor,
      uint32_t num_consumers = 1,
      uint32_t num_masters = 1,
      uint32_t num_tservers = 1);

  Result<client::YBTableName> CreateTable(
      YBClient* client, const std::string& namespace_name, const std::string& table_name,
      uint32_t num_tablets);

  Status CreateTable(
      uint32_t idx, uint32_t num_tablets, YBClient* client,
      std::vector<client::YBTableName>* tables);

  Status CreateTable(
      uint32_t idx, uint32_t num_tablets, YBClient* client, client::YBSchema schema,
      std::vector<client::YBTableName>* tables);

  void WriteWorkload(
      uint32_t start, uint32_t end, YBClient* client, const client::YBTableName& table,
      bool delete_op = false);

  Status DoVerifyWrittenRecords(
      const client::YBTableName& producer_table, const client::YBTableName& consumer_table,
      YBClient* prod_client = nullptr, YBClient* cons_client = nullptr,
      int timeout_secs = kRpcTimeout);

  Status DoVerifyNumRecords(
      const client::YBTableName& table, YBClient* client, size_t expected_size);

  server::ClockPtr GetClock() { return clock_; }

  client::YBSchema* GetSchema() { return &schema_; }

  Result<std::unique_ptr<Cluster>> AddCluster(
      YBClusters* clusters, uint32 cluster_id, bool is_producer,
      uint32_t num_tservers = 1);

  Status CreateAdditionalClusterTables(
      YBClient* client, YBTables* tables, uint32_t num_tablets_per_table, size_t num_tables);

  Result<std::unique_ptr<XClusterTestBase::Cluster>> AddClusterWithTables(
      YBClusters* clusters, YBTables* tables, uint32 cluster_id, size_t num_tables,
      uint32_t num_tablets_per_table, bool is_producer, uint32_t num_tservers = 1);

 private:
  server::ClockPtr clock_{new server::HybridClock()};

  client::YBSchema schema_;
};

}  // namespace yb
