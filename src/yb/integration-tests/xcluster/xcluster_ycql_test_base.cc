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

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include "yb/util/flags.h"
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/consensus/log.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster/xcluster_ycql_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/faststring.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

using std::string;

using namespace std::literals;

DECLARE_bool(enable_ysql);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(cdc_max_apply_batch_num_records);

namespace yb {

using OK = Status::OK;
using client::YBTableName;

void XClusterYcqlTestBase::SetUp() { XClusterTestBase::SetUp(); }

Result<YBTableName> XClusterYcqlTestBase::CreateTable(
    YBClient* client, const std::string& namespace_name, const std::string& table_name,
    uint32_t num_tablets) {
  return XClusterTestBase::CreateTable(client, namespace_name, table_name, num_tablets, &schema_);
}

Status XClusterYcqlTestBase::CreateTable(
    uint32_t idx, uint32_t num_tablets, YBClient* client, std::vector<YBTableName>* tables) {
  auto table =
      VERIFY_RESULT(CreateTable(client, namespace_name, Format("test_table_$0", idx), num_tablets));
  tables->push_back(table);
  return Status::OK();
}

Status XClusterYcqlTestBase::CreateTable(
    uint32_t idx, uint32_t num_tablets, YBClient* client, client::YBSchema schema,
    std::vector<YBTableName>* tables) {
  auto table = VERIFY_RESULT(XClusterTestBase::CreateTable(
      client, namespace_name, Format("test_table_$0", idx), num_tablets, &schema));
  tables->push_back(table);
  return Status::OK();
}

void XClusterYcqlTestBase::WriteWorkload(
    uint32_t start, uint32_t end, YBClient* client, const YBTableName& table, bool delete_op) {
  auto session = client->NewSession(kRpcTimeout * 1s);
  client::TableHandle table_handle;
  ASSERT_OK(table_handle.Open(table, client));
  std::vector<std::shared_ptr<client::YBqlOp>> ops;

  LOG(INFO) << "Writing " << end - start << (delete_op ? " deletes" : " inserts");
  for (uint32_t i = start; i < end; i++) {
    auto op = delete_op ? table_handle.NewDeleteOp() : table_handle.NewInsertOp();
    int32_t key = i;
    auto req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
  }
}

Status XClusterYcqlTestBase::DoVerifyWrittenRecords(
    const YBTableName& producer_table, const YBTableName& consumer_table, YBClient* prod_client,
    YBClient* cons_client, int timeout_secs) {
  std::vector<std::string> producer_results, consumer_results;
  if (!prod_client) {
    prod_client = producer_client();
  }
  if (!cons_client) {
    cons_client = consumer_client();
  }
  const auto s = LoggedWaitFor(
      [producer_table, consumer_table, &producer_results, &consumer_results, prod_client,
       cons_client]() -> Result<bool> {
        producer_results = ScanTableToStrings(producer_table, prod_client);
        consumer_results = ScanTableToStrings(consumer_table, cons_client);
        if (producer_results != consumer_results) {
          LOG(INFO) << "Intermediate results: Producer records: "
                    << JoinStrings(producer_results, ",")
                    << "; Consumer records: " << JoinStrings(consumer_results, ",");
        }
        return producer_results == consumer_results;
      },
      MonoDelta::FromSeconds(timeout_secs),
      Format(
          "Verify written records from $0 to $1", producer_table.ToString(),
          consumer_table.ToString()));
  if (!s.ok()) {
    LOG(ERROR) << "Producer records: " << JoinStrings(producer_results, ",")
               << "; Consumer records: " << JoinStrings(consumer_results, ",");
  }
  return s;
}

Status XClusterYcqlTestBase::DoVerifyNumRecords(
    const YBTableName& table, YBClient* client, size_t expected_size) {
  return LoggedWaitFor(
      [table, client, expected_size]() -> Result<bool> {
        auto results = ScanTableToStrings(table, client);
        return results.size() == expected_size;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Verify number of records");
}

Result<std::unique_ptr<XClusterTestBase::Cluster>> XClusterYcqlTestBase::AddCluster(
    YBClusters* clusters, uint32 cluster_id, bool is_producer, uint32_t num_tservers) {
  auto cluster_id_str =
      Format("additional_$0_$1", is_producer ? "producer" : "consumer", cluster_id);
  auto prefix = Format("$0$1", is_producer ? "AP" : "AC", cluster_id);
  std::unique_ptr<Cluster> additional_cluster =
      VERIFY_RESULT(CreateCluster(cluster_id_str, prefix, num_tservers));
  additional_cluster->txn_mgr_.emplace(
      additional_cluster->client_.get(), GetClock(), client::LocalTabletFilter());
  clusters->push_back(std::move(additional_cluster.get()));
  return additional_cluster;
}

Status XClusterYcqlTestBase::CreateAdditionalClusterTables(
    YBClient* client, YBTables* tables, uint32_t num_tablets_per_table, size_t num_tables) {
  for (uint32_t i = 0; i < num_tables; ++i) {
    auto table = VERIFY_RESULT(
        CreateTable(client, namespace_name, Format("test_table_$0", i), num_tablets_per_table));
    std::shared_ptr<client::YBTable> new_table;
    RETURN_NOT_OK(client->OpenTable(table, &new_table));
    tables->push_back(new_table);
  }
  return Status::OK();
}

Result<std::unique_ptr<XClusterTestBase::Cluster>> XClusterYcqlTestBase::AddClusterWithTables(
    YBClusters* clusters, YBTables* tables, uint32 cluster_id, size_t num_tables,
    uint32_t num_tablets_per_table, bool is_producer, uint32_t num_tservers) {
  std::unique_ptr<Cluster> additional_cluster =
      VERIFY_RESULT(AddCluster(clusters, cluster_id, is_producer, num_tservers));
  YBClient* additional_cluster_client = additional_cluster->client_.get();

  // Create the tables on the additional cluster.
  RETURN_NOT_OK(CreateAdditionalClusterTables(
      additional_cluster_client, tables, num_tablets_per_table, num_tables));
  return additional_cluster;
}

}  // namespace yb
