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
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/twodc_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_util.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master-test-util.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/util/atomic.h"
#include "yb/util/faststring.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_int32(replication_factor);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_int32(pggate_rpc_timeout_secs);
DECLARE_bool(enable_delete_truncate_xcluster_replicated_table);
DECLARE_int32(update_min_cdc_indices_interval_secs);
DECLARE_int32(log_cache_size_limit_mb);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int64(remote_bootstrap_rate_limit_bytes_per_sec);
DECLARE_int32(log_max_seconds_to_retain);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_bool(check_bootstrap_required);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_uint64(ysql_packed_row_size_limit);

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBError;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableAlterer;
using client::YBTableCreator;
using client::YBTableType;
using client::YBTableName;
using master::GetNamespaceInfoResponsePB;
using master::MiniMaster;
using tserver::MiniTabletServer;
using tserver::enterprise::CDCConsumer;

using pgwrapper::ToString;
using pgwrapper::GetInt32;
using pgwrapper::PGConn;
using pgwrapper::PGResultPtr;
using pgwrapper::PgSupervisor;

namespace enterprise {

constexpr static const char* const kKeyColumnName = "key";

class TwoDCYsqlTest : public TwoDCTestBase, public testing::WithParamInterface<TwoDCTestParams> {
 public:
  void ValidateRecordsTwoDCWithCDCSDK(bool update_min_cdc_indices_interval = false,
                                      bool enable_cdc_sdk_in_producer = false,
                                      bool do_explict_transaction = false);

    Status Initialize(uint32_t replication_factor, uint32_t num_masters = 1) {
    master::SetDefaultInitialSysCatalogSnapshotFlags();
    TwoDCTestBase::SetUp();
    FLAGS_enable_ysql = true;
    FLAGS_master_auto_run_initdb = true;
    FLAGS_hide_pg_catalog_table_creation_logs = true;
    FLAGS_pggate_rpc_timeout_secs = 120;
    FLAGS_cdc_max_apply_batch_num_records = GetParam().batch_size;
    FLAGS_cdc_enable_replicate_intents = GetParam().enable_replicate_intents;

    MiniClusterOptions opts;
    opts.num_tablet_servers = replication_factor;
    opts.num_masters = num_masters;
    FLAGS_replication_factor = replication_factor;
    opts.cluster_id = "producer";
    producer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(opts);
    RETURN_NOT_OK(producer_cluster()->StartSync());
    RETURN_NOT_OK(producer_cluster()->WaitForTabletServerCount(replication_factor));
    RETURN_NOT_OK(WaitForInitDb(producer_cluster()));

    opts.cluster_id = "consumer";
    consumer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(opts);
    RETURN_NOT_OK(consumer_cluster()->StartSync());
    RETURN_NOT_OK(consumer_cluster()->WaitForTabletServerCount(replication_factor));
    RETURN_NOT_OK(WaitForInitDb(consumer_cluster()));

    producer_cluster_.client_ = VERIFY_RESULT(producer_cluster()->CreateClient());
    consumer_cluster_.client_ = VERIFY_RESULT(consumer_cluster()->CreateClient());

    RETURN_NOT_OK(InitPostgres(&producer_cluster_));
    RETURN_NOT_OK(InitPostgres(&consumer_cluster_));

    return Status::OK();
  }

  Result<std::vector<std::shared_ptr<client::YBTable>>>
      SetUpWithParams(std::vector<uint32_t> num_consumer_tablets,
                      std::vector<uint32_t> num_producer_tablets,
                      uint32_t replication_factor,
                      uint32_t num_masters = 1,
                      bool colocated = false,
                      boost::optional<std::string> tablegroup_name = boost::none) {
    RETURN_NOT_OK(Initialize(replication_factor, num_masters));

    if (num_consumer_tablets.size() != num_producer_tablets.size()) {
      return STATUS(IllegalState,
                    Format("Num consumer tables: $0 num producer tables: $1 must be equal.",
                           num_consumer_tablets.size(), num_producer_tablets.size()));
    }

    RETURN_NOT_OK(CreateDatabase(&producer_cluster_, kNamespaceName, colocated));
    RETURN_NOT_OK(CreateDatabase(&consumer_cluster_, kNamespaceName, colocated));

    if (tablegroup_name.has_value()) {
      RETURN_NOT_OK(CreateTablegroup(&producer_cluster_, kNamespaceName, tablegroup_name.get()));
      RETURN_NOT_OK(CreateTablegroup(&consumer_cluster_, kNamespaceName, tablegroup_name.get()));
    }

    std::vector<YBTableName> tables;
    std::vector<std::shared_ptr<client::YBTable>> yb_tables;
    for (uint32_t i = 0; i < num_consumer_tablets.size(); i++) {
      RETURN_NOT_OK(CreateTable(i, num_producer_tablets[i], &producer_cluster_,
                                &tables, tablegroup_name, colocated));
      std::shared_ptr<client::YBTable> producer_table;
      RETURN_NOT_OK(producer_client()->OpenTable(tables[i * 2], &producer_table));
      yb_tables.push_back(producer_table);

      RETURN_NOT_OK(CreateTable(i, num_consumer_tablets[i], &consumer_cluster_,
                                &tables, tablegroup_name, colocated));
      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client()->OpenTable(tables[(i * 2) + 1], &consumer_table));
      yb_tables.push_back(consumer_table);
    }

    return yb_tables;
  }

  Status InitPostgres(Cluster* cluster) {
    auto pg_ts = RandomElement(cluster->mini_cluster_->mini_tablet_servers());
    auto port = cluster->mini_cluster_->AllocateFreePort();
    yb::pgwrapper::PgProcessConf pg_process_conf =
        VERIFY_RESULT(yb::pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
            yb::ToString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
            pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
            pg_ts->server()->GetSharedMemoryFd()));
    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
    pg_process_conf.force_disable_log_file = true;
    FLAGS_pgsql_proxy_webserver_port = cluster->mini_cluster_->AllocateFreePort();

    LOG(INFO) << "Starting PostgreSQL server listening on "
              << pg_process_conf.listen_addresses << ":" << pg_process_conf.pg_port << ", data: "
              << pg_process_conf.data_dir
              << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
    cluster->pg_supervisor_ = std::make_unique<pgwrapper::PgSupervisor>(
        pg_process_conf, nullptr /* tserver */);
    RETURN_NOT_OK(cluster->pg_supervisor_->Start());

    cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
    return Status::OK();
  }

  Status CreateDatabase(Cluster* cluster,
                        const std::string& namespace_name = kNamespaceName,
                        bool colocated = false) {
    auto conn = EXPECT_RESULT(cluster->Connect());
    EXPECT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1",
                                 namespace_name, colocated ? " colocated = true" : ""));
    return Status::OK();
  }

  Result<string> GetUniverseId(Cluster* cluster) {
    master::GetMasterClusterConfigRequestPB req;
    master::GetMasterClusterConfigResponsePB resp;

    master::MasterClusterProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy.GetMasterClusterConfig(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Error getting cluster config");
    }
    return resp.cluster_config().cluster_uuid();
  }

  Result<YBTableName> CreateTable(Cluster* cluster,
                                  const std::string& namespace_name,
                                  const std::string& schema_name,
                                  const std::string& table_name,
                                  const boost::optional<std::string>& tablegroup_name,
                                  uint32_t num_tablets,
                                  bool colocated = false,
                                  const ColocationId colocation_id = 0) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
    std::string colocation_id_string = "";
    if (colocation_id > 0) {
      colocation_id_string = Format("colocation_id = $0", colocation_id);
    }
    if (!schema_name.empty()) {
      EXPECT_OK(conn.Execute(Format("CREATE SCHEMA IF NOT EXISTS $0;", schema_name)));
    }
    std::string full_table_name =
        schema_name.empty() ? table_name
                            : Format("$0.$1", schema_name, table_name);
    std::string query = Format(
        "CREATE TABLE $0($1 int PRIMARY KEY) ", full_table_name, kKeyColumnName);
    // One cannot use tablegroup together with split into tablets.
    if (tablegroup_name.has_value()) {
      std::string with_clause =
          colocation_id_string.empty() ? "" : Format("WITH ($0) ", colocation_id_string);
      std::string tablegroup_clause = Format("TABLEGROUP $0", tablegroup_name.value());
      query += Format("$0$1", with_clause, tablegroup_clause);
    } else {
      std::string colocated_clause = Format("colocated = $0", colocated);
      std::string with_clause =
          colocation_id_string.empty() ? colocated_clause
                                       : Format("$0, $1", colocation_id_string, colocated_clause);
      query += Format("WITH ($0)", with_clause);
      if (!colocated) {
         query += Format(" SPLIT INTO $0 TABLETS", num_tablets);
      }
    }
    EXPECT_OK(conn.Execute(query));
    return GetTable(cluster, namespace_name, schema_name, table_name,
                    true /* verify_table_name */, !schema_name.empty() /* verify_schema_name*/);
  }

  Status CreateTable(uint32_t idx, uint32_t num_tablets, Cluster* cluster,
                     std::vector<YBTableName>* tables,
                     const boost::optional<std::string>& tablegroup_name,
                     bool colocated = false) {
    // Generate colocation_id based on index so that we have the same colocation_id for
    // producer/consumer.
    const int colocation_id = (tablegroup_name.has_value() || colocated) ? (idx + 1) * 111111 : 0;
    auto table = VERIFY_RESULT(CreateTable(cluster, kNamespaceName, "" /* schema_name */,
                                           Format("test_table_$0", idx), tablegroup_name,
                                           num_tablets, colocated, colocation_id));
    tables->push_back(table);
    return Status::OK();
  }

  std::string GetCompleteTableName(const YBTableName& table) {
    // Append schema name before table name, if schema is available.
    return table.has_pgschema_name()
        ? Format("$0.$1", table.pgschema_name(), table.table_name())
        : table.table_name();
  }

  Result<YBTableName> GetTable(Cluster* cluster,
                               const std::string& namespace_name,
                               const std::string& schema_name,
                               const std::string& table_name,
                               bool verify_table_name = true,
                               bool verify_schema_name = false,
                               bool exclude_system_tables = true) {
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;

    req.set_name_filter(table_name);
    req.mutable_namespace_()->set_name(namespace_name);
    req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
    if (!exclude_system_tables) {
      req.set_exclude_system_tables(true);
      req.add_relation_type_filter(master::USER_TABLE_RELATION);
    }

    master::MasterDdlProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy.ListTables(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed listing tables");
    }

    // Now need to find the table and return it.
    for (const auto& table : resp.tables()) {
      // If !verify_table_name, just return the first table.
      if (!verify_table_name ||
          (table.name() == table_name && table.namespace_().name() == namespace_name)) {

        // In case of a match, further check for match in schema_name.
        if (!verify_schema_name ||
            (!table.has_pgschema_name() && schema_name.empty()) ||
            (table.has_pgschema_name() && table.pgschema_name() == schema_name)) {

          YBTableName yb_table;
          yb_table.set_table_id(table.id());
          yb_table.set_table_name(table_name);
          yb_table.set_namespace_id(table.namespace_().id());
          yb_table.set_namespace_name(namespace_name);
          yb_table.set_pgschema_name(table.has_pgschema_name() ? table.pgschema_name() : "");
          return yb_table;
        }
      }
    }
    return STATUS(IllegalState,
                  strings::Substitute("Unable to find table $0 in namespace $1",
                                      table_name, namespace_name));
  }

  /*
   * TODO (#11597): Given one is not able to get tablegroup ID by name, currently this works by
   * getting the first available tablegroup appearing in the namespace.
   */
  Result<TablegroupId> GetTablegroup(Cluster* cluster, const std::string& namespace_name) {
    // Lookup the namespace id from the namespace name.
    std::string namespace_id;
    {
      master::ListNamespacesRequestPB req;
      master::ListNamespacesResponsePB resp;
      master::MasterDdlProxy master_proxy(
          &cluster->client_->proxy_cache(),
          VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

      rpc::RpcController rpc;
      rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

      RETURN_NOT_OK(master_proxy.ListNamespaces(req, &resp, &rpc));
      if (resp.has_error()) {
        return STATUS(IllegalState, "Failed to get namespace info");
      }

      // Find and return the namespace id.
      bool namespaceFound = false;
      for (const auto& entry : resp.namespaces()) {
        if (entry.name() == namespace_name) {
          namespaceFound = true;
          namespace_id = entry.id();
          break;
        }
      }

      if (!namespaceFound) {
        return STATUS(IllegalState, "Failed to find namespace");
      }
    }

    master::ListTablegroupsRequestPB req;
    master::ListTablegroupsResponsePB resp;
    master::MasterDdlProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

    req.set_namespace_id(namespace_id);

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    RETURN_NOT_OK(master_proxy.ListTablegroups(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed listing tablegroups");
    }

    // Find and return the tablegroup.
    if (resp.tablegroups().empty()) {
      return STATUS(IllegalState,
                    Format("Unable to find tablegroup in namespace $0", namespace_name));
    }

    return master::GetTablegroupParentTableId(resp.tablegroups()[0].id());
  }

  Status CreateTablegroup(Cluster* cluster,
                          const std::string& namespace_name,
                          const std::string& tablegroup_name) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
    EXPECT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", tablegroup_name));
    return Status::OK();
  }

  void WriteWorkload(uint32_t start, uint32_t end, Cluster* cluster, const YBTableName& table,
                     bool delete_op = false) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table);

    LOG(INFO) << "Writing " << end-start << (delete_op ? " deletes" : " inserts");
    for (uint32_t i = start; i < end; i++) {
      if (delete_op) {
        EXPECT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = $2",
                                     table_name_str, kKeyColumnName, i));
      } else {
        // TODO(#6582) transactions currently don't work, so don't use ON CONFLICT DO NOTHING now.
        EXPECT_OK(conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2)", // ON CONFLICT DO NOTHING",
                                     table_name_str, kKeyColumnName, i));
      }
    }
  }

  void WriteGenerateSeries(uint32_t start, uint32_t end, Cluster* cluster,
                           const YBTableName& table) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    auto generate_string = Format("INSERT INTO $0 VALUES (generate_series($1, $2))",
                                  table.table_name(), start, end);
    ASSERT_OK(conn.ExecuteFormat(generate_string));
  }

  void WriteTransactionalWorkload(uint32_t start, uint32_t end, Cluster* cluster,
                                  const YBTableName& table) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table);
    EXPECT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; i++) {
      EXPECT_OK(conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2) ON CONFLICT DO NOTHING",
                                   table_name_str, kKeyColumnName, i));
    }
    EXPECT_OK(conn.Execute("COMMIT"));
  }

  void DeleteWorkload(uint32_t start, uint32_t end, Cluster* cluster, const YBTableName& table) {
    WriteWorkload(start, end, cluster, table, true /* delete_op */);
  }

  PGResultPtr ScanToStrings(const YBTableName& table_name, Cluster* cluster) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table_name.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table_name);
    auto result = EXPECT_RESULT(conn.FetchFormat(
        "SELECT * FROM $0 ORDER BY $1", table_name_str, kKeyColumnName));
    return result;
  }

  Status VerifyWrittenRecords(const YBTableName& producer_table,
                              const YBTableName& consumer_table) {
    return LoggedWaitFor([this, producer_table, consumer_table]() -> Result<bool> {
      auto producer_results = ScanToStrings(producer_table, &producer_cluster_);
      auto consumer_results = ScanToStrings(consumer_table, &consumer_cluster_);
      if (PQntuples(producer_results.get()) != PQntuples(consumer_results.get())) {
        return false;
      }
      for (int i = 0; i < PQntuples(producer_results.get()); ++i) {
        auto prod_val = EXPECT_RESULT(ToString(producer_results.get(), i, 0));
        auto cons_val = EXPECT_RESULT(ToString(consumer_results.get(), i, 0));
        if (prod_val != cons_val) {
          return false;
        }
      }
      return true;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify written records");
  }

  Status VerifyNumRecords(const YBTableName& table, Cluster* cluster, int expected_size) {
    return LoggedWaitFor([this, table, cluster, expected_size]() -> Result<bool> {
      auto results = ScanToStrings(table, cluster);
      return PQntuples(results.get()) == expected_size;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify number of records");
  }

  Status TruncateTable(Cluster* cluster,
                       std::vector<string> table_ids) {
    RETURN_NOT_OK(cluster->client_->TruncateTables(table_ids));
    return Status::OK();
  }
};
INSTANTIATE_TEST_CASE_P(
    TwoDCTestParams, TwoDCYsqlTest,
    ::testing::Values(
        TwoDCTestParams(1, true, true), TwoDCTestParams(1, false, false),
        TwoDCTestParams(0, true, true), TwoDCTestParams(0, false, false)));

class TwoDCYsqlTestWithEnableIntentsReplication : public TwoDCYsqlTest {
};

INSTANTIATE_TEST_CASE_P(
    TwoDCTestParams, TwoDCYsqlTestWithEnableIntentsReplication,
    ::testing::Values(TwoDCTestParams(0, true, true), TwoDCTestParams(1, true, true)));

TEST_P(TwoDCYsqlTestWithEnableIntentsReplication, GenerateSeries) {
  YB_SKIP_TEST_IN_TSAN();
  auto tables = ASSERT_RESULT(SetUpWithParams({4}, {4}, 3, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  auto producer_table = tables[0];
  auto consumer_table = tables[1];
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, {producer_table}));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table->id());

  ASSERT_NO_FATALS(WriteGenerateSeries(0, 50, &producer_cluster_, producer_table->name()));

  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
}

TEST_P(TwoDCYsqlTestWithEnableIntentsReplication, GenerateSeriesMultipleTransactions) {
  YB_SKIP_TEST_IN_TSAN();
  // Use a 4 -> 1 mapping to ensure that multiple transactions are processed by the same tablet.
  auto tables = ASSERT_RESULT(SetUpWithParams({1}, {4}, 3, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  auto producer_table = tables[0];
  auto consumer_table = tables[1];
  ASSERT_NO_FATALS(WriteGenerateSeries(0, 50, &producer_cluster_, producer_table->name()));
  ASSERT_NO_FATALS(WriteGenerateSeries(51, 100, &producer_cluster_, producer_table->name()));
  ASSERT_NO_FATALS(WriteGenerateSeries(101, 150, &producer_cluster_, producer_table->name()));
  ASSERT_NO_FATALS(WriteGenerateSeries(151, 200, &producer_cluster_, producer_table->name()));
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, {producer_table}));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table->id());
  ASSERT_OK(VerifyWrittenRecords(producer_table->name(), consumer_table->name()));
}

TEST_P(TwoDCYsqlTest, SetupUniverseReplication) {
  YB_SKIP_TEST_IN_TSAN();
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4}, {6, 6}, 3, 1, false /* colocated */));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
  ASSERT_EQ(resp.entry().tables_size(), producer_tables.size());
  for (size_t i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.entry().tables(narrow_cast<int>(i)), producer_tables[i]->id());
  }

  // Verify that CDC streams were created on producer for all tables.
  for (size_t i = 0; i < producer_tables.size(); i++) {
    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_tables[i]->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_tables[i]->id());
  }

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

TEST_P(TwoDCYsqlTest, SimpleReplication) {
  YB_SKIP_TEST_IN_TSAN();
  FLAGS_ysql_enable_packed_row = false;

  constexpr auto kNumRecords = 1000;
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, kNumRecords, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(kNumRecords, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < kNumRecords; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_cluster(), consumer_cluster(), consumer_client(),
                                     kUniverseId, producer_tables));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);
      auto consumer_results_size = PQntuples(consumer_results.get());
      if (num_results != consumer_results_size) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(kNumRecords); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // 4. Write more data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(kNumRecords, kNumRecords + 5, &producer_cluster_, producer_table->name());
  }

  // 5. Make sure this data is also replicated now.
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 5); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));

  // Enable packing
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_packed_row_size_limit = 1_KB;

  // Disable the replication and ensure no tablets are being polled
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 0));

  // 6. Write packed data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(kNumRecords + 5, kNumRecords + 10, &producer_cluster_, producer_table->name());
  }

  // 7. Disable packing and resume replication
  FLAGS_ysql_enable_packed_row = false;
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 10); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));

  // 8. Write some non-packed data on consumer.
  for (const auto& consumer_table : consumer_tables) {
    WriteWorkload(kNumRecords + 10, kNumRecords + 15, &producer_cluster_, consumer_table->name());
  }

  // 9. Make sure full scan works now.
  ASSERT_OK(WaitFor(
      [&]() { return data_replicated_correctly(kNumRecords + 15); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));
}

TEST_P(TwoDCYsqlTest, SetupUniverseReplicationWithProducerBootstrapId) {
  YB_SKIP_TEST_IN_TSAN();
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 3));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));
  auto* producer_leader_mini_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  auto producer_master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      producer_leader_mini_master->bound_rpc_addr());

  std::unique_ptr<client::YBClient> client;
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data so that we can verify that only new records get replicated.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  SleepFor(MonoDelta::FromSeconds(10));
  cdc::BootstrapProducerRequestPB req;
  cdc::BootstrapProducerResponsePB resp;

  for (const auto& producer_table : producer_tables) {
    req.add_table_ids(producer_table->id());
  }

  rpc::RpcController rpc;
  ASSERT_OK(producer_cdc_proxy->BootstrapProducer(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());

  ASSERT_EQ(resp.cdc_bootstrap_ids().size(), producer_tables.size());

  int table_idx = 0;
  for (const auto& bootstrap_id : resp.cdc_bootstrap_ids()) {
    LOG(INFO) << "Got bootstrap id " << bootstrap_id
              << " for table " << producer_tables[table_idx++]->name().table_name();
  }

  std::unordered_map<std::string, int> tablet_bootstraps;

  // Verify that for each of the table's tablets, a new row in cdc_state table with the returned
  // id was inserted.
  client::TableHandle table;
  client::YBTableName cdc_state_table(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  ASSERT_OK(table.Open(cdc_state_table, producer_client()));

  // 2 tables with 8 tablets each.
  ASSERT_EQ(tables_vector.size() * kNTabletsPerTable, boost::size(client::TableRange(table)));
  int nrows = 0;
  for (const auto& row : client::TableRange(table)) {
    nrows++;
    string stream_id = row.column(0).string_value();
    tablet_bootstraps[stream_id]++;

    string checkpoint = row.column(2).string_value();
    auto s = OpId::FromString(checkpoint);
    ASSERT_OK(s);
    OpId op_id = *s;
    ASSERT_GT(op_id.index, 0);

    LOG(INFO) << "Bootstrap id " << stream_id
              << " for tablet " << row.column(1).string_value();
  }

  ASSERT_EQ(tablet_bootstraps.size(), producer_tables.size());
  // Check that each bootstrap id has 8 tablets.
  for (const auto& e : tablet_bootstraps) {
    ASSERT_EQ(e.second, kNTabletsPerTable);
  }

  // Map table -> bootstrap_id. We will need when setting up replication.
  std::unordered_map<TableId, std::string> table_bootstrap_ids;
  for (int i = 0; i < resp.cdc_bootstrap_ids_size(); i++) {
    table_bootstrap_ids[req.table_ids(i)] = resp.cdc_bootstrap_ids(i);
  }

  // 2. Setup replication.
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kUniverseId);
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());

  setup_universe_req.mutable_producer_table_ids()->Reserve(
      narrow_cast<int>(producer_tables.size()));
  for (const auto& producer_table : producer_tables) {
    setup_universe_req.add_producer_table_ids(producer_table->id());
    const auto& iter = table_bootstrap_ids.find(producer_table->id());
    ASSERT_NE(iter, table_bootstrap_ids.end());
    setup_universe_req.add_producer_bootstrap_ids(iter->second);
  }

  auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  master::MasterReplicationProxy master_proxy(
      &consumer_client()->proxy_cache(),
      consumer_leader_mini_master->bound_rpc_addr());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy.SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  // 4. Write more data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(1000, 1005, &producer_cluster_, producer_table->name());
  }

  // 5. Verify that only new writes get replicated to consumer since we bootstrapped the producer
  // after we had already written some data, therefore the old data (whatever was there before we
  // bootstrapped the producer) should not be replicated.
  auto data_replicated_correctly = [&]() -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (5 != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < 5; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if ((1000 + i) != result) {
          return false;
        }
      }
    }
    return true;
  };

  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));
}

TEST_P(TwoDCYsqlTest, ColocatedDatabaseReplication) {
  YB_SKIP_TEST_IN_TSAN();
  constexpr int kNTabletsPerColocatedTable = 1;
  constexpr int kNTabletsPerTable = 3;
  std::vector<uint32_t> tables_vector = {kNTabletsPerColocatedTable, kNTabletsPerColocatedTable};
  // Create two colocated tables on each cluster.
  auto colocated_tables =
      ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 3, 1, true /* colocated */));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // Also create an additional non-colocated table in each database.
  auto non_colocated_table = ASSERT_RESULT(CreateTable(&producer_cluster_,
                                                       kNamespaceName,
                                                       "" /* schema_name */,
                                                       "test_table_2",
                                                       boost::none /* tablegroup */,
                                                       kNTabletsPerTable,
                                                       false /* colocated */));
  std::shared_ptr<client::YBTable> non_colocated_producer_table;
  ASSERT_OK(producer_client()->OpenTable(non_colocated_table, &non_colocated_producer_table));
  non_colocated_table = ASSERT_RESULT(CreateTable(&consumer_cluster_,
                                                  kNamespaceName,
                                                  "" /* schema_name */,
                                                  "test_table_2",
                                                  boost::none /* tablegroup */,
                                                  kNTabletsPerTable,
                                                  false /* colocated */));
  std::shared_ptr<client::YBTable> non_colocated_consumer_table;
  ASSERT_OK(consumer_client()->OpenTable(non_colocated_table, &non_colocated_consumer_table));

  // colocated_tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  std::vector<std::shared_ptr<client::YBTable>> colocated_producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> colocated_consumer_tables;
  producer_tables.reserve(colocated_tables.size() / 2 + 1);
  consumer_tables.reserve(colocated_tables.size() / 2 + 1);
  colocated_producer_tables.reserve(colocated_tables.size() / 2);
  colocated_consumer_tables.reserve(colocated_tables.size() / 2);
  for (size_t i = 0; i < colocated_tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(colocated_tables[i]);
      colocated_producer_tables.push_back(colocated_tables[i]);
    } else {
      consumer_tables.push_back(colocated_tables[i]);
      colocated_consumer_tables.push_back(colocated_tables[i]);
    }
  }
  producer_tables.push_back(non_colocated_producer_table);
  consumer_tables.push_back(non_colocated_consumer_table);

  // 1. Write some data to all tables.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // 2. Setup replication for only the colocated tables.
  // Get the producer namespace id, so we can construct the colocated parent table id.
  GetNamespaceInfoResponsePB ns_resp;
  ASSERT_OK(producer_client()->GetNamespaceInfo("", kNamespaceName, YQL_DATABASE_PGSQL, &ns_resp));

  rpc::RpcController rpc;
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kUniverseId);
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
  // Only need to add the colocated parent table id.
  setup_universe_req.mutable_producer_table_ids()->Reserve(1);
  setup_universe_req.add_producer_table_ids(
      master::GetColocatedDbParentTableId(ns_resp.namespace_().id()));
  auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      consumer_leader_mini_master->bound_rpc_addr());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), kNTabletsPerColocatedTable));

  // 4. Check that colocated tables are being replicated.
  auto data_replicated_correctly = [&](int num_results, bool onlyColocated) -> Result<bool> {
    auto &tables = onlyColocated ? colocated_consumer_tables : consumer_tables;
    for (const auto& consumer_table : tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(100, true); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));
  // Ensure that the non colocated table is not replicated.
  auto non_coloc_results = ScanToStrings(non_colocated_consumer_table->name(), &consumer_cluster_);
  ASSERT_EQ(0, PQntuples(non_coloc_results.get()));

  // 5. Add the regular table to replication.
  // Prepare and send AlterUniverseReplication command.
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  alter_req.set_producer_id(kUniverseId);
  alter_req.add_producer_table_ids_to_add(non_colocated_producer_table->id());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
  ASSERT_FALSE(alter_resp.has_error());
  // Wait until we have 2 tables (colocated tablet + regular table) logged.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    master::GetUniverseReplicationResponsePB tmp_resp;
    return VerifyUniverseReplication(consumer_cluster(), consumer_client(),
        kUniverseId, &tmp_resp).ok() &&
        tmp_resp.entry().tables_size() == 2;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), kNTabletsPerColocatedTable + kNTabletsPerTable));
  // Check that all data is replicated for the new table as well.
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(100, false); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // 6. Add additional data to all tables
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(100, 150, &producer_cluster_, producer_table->name());
  }

  // 7. Verify all tables are properly replicated.
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(150, false); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));
}

TEST_P(TwoDCYsqlTest, ColocatedDatabaseDifferentColocationIds) {
  YB_SKIP_TEST_IN_TSAN();
  auto colocated_tables = ASSERT_RESULT(SetUpWithParams({}, {}, 3, 1, true /* colocated */));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // Create two tables with different colocation ids.
  auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(kNamespaceName));
  auto table_info = ASSERT_RESULT(CreateTable(&producer_cluster_,
                                              kNamespaceName,
                                              "" /* schema_name */,
                                              "test_table_0",
                                              boost::none /* tablegroup */,
                                              1 /* num_tablets */,
                                              true /* colocated */,
                                              123456 /* colocation_id */));
  ASSERT_RESULT(CreateTable(&consumer_cluster_,
                            kNamespaceName,
                            "" /* schema_name */,
                            "test_table_0",
                            boost::none /* tablegroup */,
                            1 /* num_tablets */,
                            true /* colocated */,
                            123457 /* colocation_id */));
  std::shared_ptr<client::YBTable> producer_table;
  ASSERT_OK(producer_client()->OpenTable(table_info, &producer_table));

  // Try to setup replication, should fail on schema validation due to different colocation ids.
  ASSERT_OK(SetupUniverseReplication(producer_cluster(), consumer_cluster(), consumer_client(),
                                     kUniverseId, {producer_table}));
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_NOK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
}

TEST_P(TwoDCYsqlTest, TablegroupReplication) {
  YB_SKIP_TEST_IN_TSAN();

  std::vector<uint32_t> tables_vector = {1, 1};
  boost::optional<std::string> kTablegroupName("mytablegroup");
  auto tables = ASSERT_RESULT(
      SetUpWithParams(tables_vector, tables_vector, 1, 1, false /* colocated */, kTablegroupName));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data to all tables.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // 2. Setup replication for the tablegroup.
  auto tablegroup_id = ASSERT_RESULT(GetTablegroup(&producer_cluster_, kNamespaceName));
  LOG(INFO) << "Tablegroup id to replicate: " << tablegroup_id;

  rpc::RpcController rpc;
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kUniverseId);
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
  setup_universe_req.mutable_producer_table_ids()->Reserve(1);
  setup_universe_req.add_producer_table_ids(tablegroup_id);

  auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      consumer_leader_mini_master->bound_rpc_addr());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 1));

  // 4. Check that tables are being replicated.
  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };

  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(100); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // 5. Write more data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(100, 105, &producer_cluster_, producer_table->name());
  }

  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(105); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));
}

TEST_P(TwoDCYsqlTest, TablegroupReplicationMismatch) {
  YB_SKIP_TEST_IN_TSAN();
  ASSERT_OK(Initialize(1 /* replication_factor */));

  boost::optional<std::string> tablegroup_name("mytablegroup");

  ASSERT_OK(CreateDatabase(&producer_cluster_, kNamespaceName, false /* colocated */));
  ASSERT_OK(CreateDatabase(&consumer_cluster_, kNamespaceName, false /* colocated */));
  ASSERT_OK(CreateTablegroup(&producer_cluster_, kNamespaceName, tablegroup_name.get()));
  ASSERT_OK(CreateTablegroup(&consumer_cluster_, kNamespaceName, tablegroup_name.get()));

  // We intentionally set up so that the number of producer and consumer tables don't match.
  // The replication should fail during validation.
  const uint32_t num_producer_tables = 2;
  const uint32_t num_consumer_tables = 3;
  std::vector<YBTableName> tables;
  for (uint32_t i = 0; i < num_producer_tables; i++) {
    ASSERT_OK(CreateTable(i, 1 /* num_tablets */, &producer_cluster_,
                          &tables, tablegroup_name, false /* colocated */));
  }
  for (uint32_t i = 0; i < num_consumer_tables; i++) {
    ASSERT_OK(CreateTable(i, 1 /* num_tablets */, &consumer_cluster_,
                          &tables, tablegroup_name, false /* colocated */));
  }

  auto tablegroup_id = ASSERT_RESULT(GetTablegroup(&producer_cluster_, kNamespaceName));

  // Try to set up replication.
  rpc::RpcController rpc;
  master::SetupUniverseReplicationRequestPB setup_universe_req;
  master::SetupUniverseReplicationResponsePB setup_universe_resp;
  setup_universe_req.set_producer_id(kUniverseId);
  string master_addr = producer_cluster()->GetMasterAddresses();
  auto hp_vec = ASSERT_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, setup_universe_req.mutable_producer_master_addresses());
  setup_universe_req.mutable_producer_table_ids()->Reserve(1);
  setup_universe_req.add_producer_table_ids(tablegroup_id);

  auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      consumer_leader_mini_master->bound_rpc_addr());

  rpc.Reset();
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  ASSERT_OK(master_proxy->SetupUniverseReplication(setup_universe_req, &setup_universe_resp, &rpc));
  ASSERT_FALSE(setup_universe_resp.has_error());

  // The schema validation should fail.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_NOK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
}

// Checks that in regular replication set up, bootstrap is not required
TEST_P(TwoDCYsqlTest, IsBootstrapRequiredNotFlushed) {
  YB_SKIP_TEST_IN_TSAN();
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data.
  for (const auto& producer_table : producer_tables) {
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(100, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < 100; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // 2. Setup replication.
  FLAGS_check_bootstrap_required = true;
  ASSERT_OK(SetupUniverseReplication(producer_cluster(), consumer_cluster(), consumer_client(),
                                     kUniverseId, producer_tables));
  master::GetUniverseReplicationResponsePB verify_resp;
  ASSERT_OK(VerifyUniverseReplication(
    consumer_cluster(), consumer_client(), kUniverseId, &verify_resp));

  std::unique_ptr<client::YBClient> client;
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));

  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(producer_tables[0]->id(), &stream_resp));
  ASSERT_EQ(stream_resp.streams_size(), 1);

  std::vector<TabletId> tablet_ids;
  if (producer_tables[0]) {
    ASSERT_OK(producer_cluster_.client_->GetTablets(producer_tables[0]->name(),
                                                    (int32_t) 3,
                                                    &tablet_ids,
                                                    NULL));
    ASSERT_GT(tablet_ids.size(), 0);
  }

  rpc::RpcController rpc;
  cdc::IsBootstrapRequiredRequestPB req;
  cdc::IsBootstrapRequiredResponsePB resp;
  auto stream_id = stream_resp.streams(0).stream_id();
  req.set_stream_id(stream_id);
  req.add_tablet_ids(tablet_ids[0]);

  ASSERT_OK(producer_cdc_proxy->IsBootstrapRequired(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());
  ASSERT_FALSE(resp.bootstrap_required());

  auto should_bootstrap = ASSERT_RESULT(producer_cluster_.client_->IsBootstrapRequired(
                                          {producer_tables[0]->id()}, stream_id));
  ASSERT_FALSE(should_bootstrap);

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

// Checks that with missing logs, replication will require bootstrapping
TEST_P(TwoDCYsqlTest, IsBootstrapRequiredFlushed) {
  YB_SKIP_TEST_IN_TSAN();

  FLAGS_enable_load_balancing = false;
  FLAGS_log_cache_size_limit_mb = 1;
  FLAGS_log_segment_size_bytes = 5_KB;
  FLAGS_log_min_segments_to_retain = 1;
  FLAGS_log_min_seconds_to_retain = 1;

  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  auto table = producer_tables[0];
  // Write some data.
  WriteWorkload(0, 50, &producer_cluster_, table->name());

  auto tablet_ids = ListTabletIdsForTable(producer_cluster(), table->id());
  ASSERT_EQ(tablet_ids.size(), 1);
  auto tablet_to_flush = *tablet_ids.begin();

  std::unique_ptr<client::YBClient> client;
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  client = ASSERT_RESULT(consumer_cluster()->CreateClient());
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
  ASSERT_OK(WaitFor([this, tablet_to_flush]() -> Result<bool> {
    LOG(INFO) << "Cleaning tablet logs";
    RETURN_NOT_OK(producer_cluster()->CleanTabletLogs());
    auto leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
    if (leaders.empty()) {
      return false;
    }
    // locate the leader with the expected logs
    tablet::TabletPeerPtr tablet_peer = leaders.front();
    for (auto leader : leaders) {
      LOG(INFO) << leader->tablet_id() << " @OpId " << leader->GetLatestLogEntryOpId().index;
      if (leader->tablet_id() == tablet_to_flush) {
        tablet_peer = leader;
        break;
      }
    }
    SCHECK(tablet_peer, InternalError, "Missing tablet peer with the WriteWorkload");

    RETURN_NOT_OK(producer_cluster()->FlushTablets());
    RETURN_NOT_OK(producer_cluster()->CleanTabletLogs());

    // Check that first log was garbage collected, so remote bootstrap will be required.
    consensus::ReplicateMsgs replicates;
    int64_t starting_op;
    yb::SchemaPB schema;
    uint32_t schema_version;
    return !tablet_peer->log()->GetLogReader()->ReadReplicatesInRange(
        1, 2, 0, &replicates, &starting_op, &schema, &schema_version).ok();
  }, MonoDelta::FromSeconds(30), "Logs cleaned"));

  auto leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
  // locate the leader with the expected logs
  tablet::TabletPeerPtr tablet_peer = leaders.front();
  for (auto leader : leaders) {
    if (leader->tablet_id() == tablet_to_flush) {
      tablet_peer = leader;
      break;
    }
  }

  // IsBootstrapRequired for this specific tablet should fail.
  rpc::RpcController rpc;
  cdc::IsBootstrapRequiredRequestPB req;
  cdc::IsBootstrapRequiredResponsePB resp;
  req.add_tablet_ids(tablet_peer->log()->tablet_id());

  ASSERT_OK(producer_cdc_proxy->IsBootstrapRequired(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());
  ASSERT_TRUE(resp.bootstrap_required());

  // The high level API should also fail.
  auto should_bootstrap = ASSERT_RESULT(producer_client()->IsBootstrapRequired({table->id()}));
  ASSERT_TRUE(should_bootstrap);

  // Setup replication should fail if this check is enabled.
  FLAGS_check_bootstrap_required = true;
  ASSERT_OK(SetupUniverseReplication(producer_cluster(), consumer_cluster(), consumer_client(),
                                     kUniverseId, producer_tables));
  master::IsSetupUniverseReplicationDoneResponsePB is_resp;
  ASSERT_OK(VerifyUniverseReplicationFailed(consumer_cluster(), consumer_client(),
                                            kUniverseId, &is_resp));
  ASSERT_TRUE(is_resp.has_replication_error());
  ASSERT_TRUE(StatusFromPB(is_resp.replication_error()).IsIllegalState());
}

// TODO adapt rest of twodc-test.cc tests.

TEST_P(TwoDCYsqlTest, DeleteTableChecks) {
  YB_SKIP_TEST_IN_TSAN();
  constexpr int kNT = 1; // Tablets per table.
  std::vector<uint32_t> tables_vector = {kNT, kNT, kNT}; // Each entry is a table. (Currently 3)
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(100, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < 100; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // Set aside one table for AlterUniverseReplication.
  std::shared_ptr<client::YBTable> producer_alter_table, consumer_alter_table;
  producer_alter_table = producer_tables.back();
  producer_tables.pop_back();
  consumer_alter_table = consumer_tables.back();
  consumer_tables.pop_back();

  // 2a. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_cluster(), consumer_cluster(), consumer_client(),
                                     kUniverseId, producer_tables));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(producer_tables.size() * kNT)));

  // 2b. Alter Replication
  {
    auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        consumer_leader_mini_master->bound_rpc_addr());
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_producer_id(kUniverseId);
    alter_req.add_producer_table_ids_to_add(producer_alter_table->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());
    // Wait until we have the new table listed in the existing universe config.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          RETURN_NOT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(),
                                                  kUniverseId, &tmp_resp));
          return tmp_resp.entry().tables_size() == static_cast<int64>(producer_tables.size() + 1);
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(), narrow_cast<uint32_t>((producer_tables.size() + 1) * kNT)));
  }
  producer_tables.push_back(producer_alter_table);
  consumer_tables.push_back(consumer_alter_table);

  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(100); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // Attempt to destroy the producer and consumer tables.
  for (size_t i = 0; i < producer_tables.size(); ++i) {
    string producer_table_id = producer_tables[i]->id();
    string consumer_table_id = consumer_tables[i]->id();
    // GH issue #12003, allow deletion of YSQL tables under replication for now.
    ASSERT_OK(producer_client()->DeleteTable(producer_table_id));
    ASSERT_OK(consumer_client()->DeleteTable(consumer_table_id));
  }

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));

  // TODO(jhe) re-enable these checks after we disallow deletion of YSQL xCluster tables, part
  // of gh issue #753.

  // for (size_t i = 0; i < producer_tables.size(); ++i) {
  //   string producer_table_id = producer_tables[i]->id();
  //   string consumer_table_id = consumer_tables[i]->id();
  //   ASSERT_OK(producer_client()->DeleteTable(producer_table_id));
  //   ASSERT_OK(consumer_client()->DeleteTable(consumer_table_id));
  // }
}

TEST_P(TwoDCYsqlTest, TruncateTableChecks) {
  YB_SKIP_TEST_IN_TSAN();
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 1. Write some data.
  for (const auto& producer_table : producer_tables) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 100, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables) {
    auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
    ASSERT_EQ(100, PQntuples(producer_results.get()));
    int result;
    for (int i = 0; i < 100; ++i) {
      result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
      ASSERT_EQ(i, result);
    }
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_cluster(), consumer_cluster(), consumer_client(),
                                     kUniverseId, producer_tables));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    for (const auto& consumer_table : consumer_tables) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

      if (num_results != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < num_results; ++i) {
        result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
        if (i != result) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(100); },
                    MonoDelta::FromSeconds(20), "IsDataReplicatedCorrectly"));

  // Attempt to Truncate the producer and consumer tables.
  string producer_table_id = producer_tables[0]->id();
  string consumer_table_id = consumer_tables[0]->id();
  ASSERT_NOK(TruncateTable(&producer_cluster_, {producer_table_id}));
  ASSERT_NOK(TruncateTable(&consumer_cluster_, {consumer_table_id}));

  FLAGS_enable_delete_truncate_xcluster_replicated_table = true;
  ASSERT_OK(TruncateTable(&producer_cluster_, {producer_table_id}));
  ASSERT_OK(TruncateTable(&consumer_cluster_, {consumer_table_id}));
}

void PrepareChangeRequest(
    cdc::GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
  change_req->set_stream_id(stream_id);
  change_req->set_tablet_id(tablets.Get(0).tablet_id());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_term(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_index(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_key("");
  change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(0);
}

void PrepareChangeRequest(
    cdc::GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
    const cdc::CDCSDKCheckpointPB& cp) {
  change_req->set_stream_id(stream_id);
  change_req->set_tablet_id(tablets.Get(0).tablet_id());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
}

Result<cdc::GetChangesResponsePB> GetChangesFromCDC(
    const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy, const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
    const cdc::CDCSDKCheckpointPB* cp = nullptr) {
  cdc::GetChangesRequestPB change_req;
  cdc::GetChangesResponsePB change_resp;

  if (!cp) {
    PrepareChangeRequest(&change_req, stream_id, tablets);
  } else {
    PrepareChangeRequest(&change_req, stream_id, tablets, *cp);
  }

  rpc::RpcController get_changes_rpc;
  RETURN_NOT_OK(cdc_proxy->GetChanges(change_req, &change_resp, &get_changes_rpc));

  if (change_resp.has_error()) {
    return StatusFromPB(change_resp.error().status());
  }

  return change_resp;
}

// Initialize a CreateCDCStreamRequest to be used while creating a DB stream ID.
void InitCreateStreamRequest(
    cdc::CreateCDCStreamRequestPB* create_req,
    const cdc::CDCCheckpointType& checkpoint_type,
    const std::string& namespace_name) {
  create_req->set_namespace_name(namespace_name);
  create_req->set_checkpoint_type(checkpoint_type);
  create_req->set_record_type(cdc::CDCRecordType::CHANGE);
  create_req->set_record_format(cdc::CDCRecordFormat::PROTO);
  create_req->set_source_type(cdc::CDCSDK);
}

// This creates a DB stream on the database kNamespaceName by default.
Result<CDCStreamId> CreateDBStream(const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy,
                                   cdc::CDCCheckpointType checkpoint_type) {
  cdc::CreateCDCStreamRequestPB req;
  cdc::CreateCDCStreamResponsePB resp;

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

  InitCreateStreamRequest(&req, checkpoint_type, kNamespaceName);
  RETURN_NOT_OK(cdc_proxy->CreateCDCStream(req, &resp, &rpc));
  return resp.db_stream_id();
}

void PrepareSetCheckpointRequest(
    cdc::SetCDCCheckpointRequestPB* set_checkpoint_req, const CDCStreamId stream_id,
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets) {
  set_checkpoint_req->set_stream_id(stream_id);
  set_checkpoint_req->set_initial_checkpoint(true);
  set_checkpoint_req->set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_index(0);
}

Status SetInitialCheckpoint(const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy,
    YBClient* client,
    const CDCStreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
  rpc::RpcController set_checkpoint_rpc;
  cdc::SetCDCCheckpointRequestPB set_checkpoint_req;
  cdc::SetCDCCheckpointResponsePB set_checkpoint_resp;
  auto deadline = CoarseMonoClock::now() + client->default_rpc_timeout();
  set_checkpoint_rpc.set_deadline(deadline);
  PrepareSetCheckpointRequest(&set_checkpoint_req, stream_id, tablets);

  return cdc_proxy->SetCDCCheckpoint(set_checkpoint_req,
                                      &set_checkpoint_resp,
                                      &set_checkpoint_rpc);
}

void TwoDCYsqlTest::ValidateRecordsTwoDCWithCDCSDK(bool update_min_cdc_indices_interval,
                                                   bool enable_cdc_sdk_in_producer,
                                                   bool do_explict_transaction) {
  constexpr int kNTabletsPerTable = 1;
  // Change the default value from 60 secs to 1 secs.
  if (update_min_cdc_indices_interval) {
    // Intent should not be cleaned up, even if updatepeers thread keeps updating the
    // minimum checkpoint op_id to all the tablet peers every second, because the
    // intents are still not consumed by the clients.
    FLAGS_update_min_cdc_indices_interval_secs = 1;
  }
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  auto tables = ASSERT_RESULT(SetUpWithParams(tables_vector, tables_vector, 1));
  const string kUniverseId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  // Tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  producer_tables.reserve(tables.size() / 2);
  consumer_tables.reserve(tables.size() / 2);
  for (size_t i = 0; i < tables.size(); ++i) {
    if (i % 2 == 0) {
      producer_tables.push_back(tables[i]);
    } else {
      consumer_tables.push_back(tables[i]);
    }
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_cluster(),
                                     consumer_cluster(), consumer_client(),
                                     kUniverseId, producer_tables));

  // 3. Create cdc proxy according the flag.
  rpc::ProxyCache* proxy_cache;
  Endpoint endpoint;
  if (enable_cdc_sdk_in_producer) {
    proxy_cache = &producer_client()->proxy_cache();
    endpoint = producer_cluster()->mini_tablet_servers().front()->bound_rpc_addr();
  } else {
    proxy_cache = &consumer_client()->proxy_cache();
    endpoint = consumer_cluster()->mini_tablet_servers().front()->bound_rpc_addr();
  }
  std::unique_ptr<cdc::CDCServiceProxy> sdk_proxy =
      std::make_unique<cdc::CDCServiceProxy>(proxy_cache, HostPort::FromBoundEndpoint(endpoint));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  std::shared_ptr<client::YBTable> cdc_enabled_table;
  YBClient* client;
  if (enable_cdc_sdk_in_producer) {
    cdc_enabled_table = producer_tables[0];
    client = producer_client();
  } else {
    cdc_enabled_table = consumer_tables[0];
    client = consumer_client();
  }
  ASSERT_OK(client->GetTablets(cdc_enabled_table->name(),
                               0,
                               &tablets, /* partition_list_version =*/
                               nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId db_stream_id = ASSERT_RESULT(CreateDBStream(sdk_proxy,
                                                          cdc::CDCCheckpointType::IMPLICIT));
  ASSERT_OK(SetInitialCheckpoint(sdk_proxy, client, db_stream_id, tablets));

  // 1. Write some data.
  const auto& producer_table = producer_tables[0];
  LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
  if (do_explict_transaction) {
    WriteTransactionalWorkload(0, 10, &producer_cluster_, producer_table->name());
  } else {
    WriteWorkload(0, 10, &producer_cluster_, producer_table->name());
  }

  // Verify data is written on the producer.
  int batch_insert_count = 10;
  auto producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
  ASSERT_EQ(batch_insert_count, PQntuples(producer_results.get()));
  int result;

  for (int i = 0; i < batch_insert_count; ++i) {
    result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
    ASSERT_EQ(i, result);
  }

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId,
                                      &get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  const auto &consumer_table = consumer_tables[0];
  auto data_replicated_correctly = [&](int num_results) -> Result<bool> {
    LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
    auto consumer_results = ScanToStrings(consumer_table->name(), &consumer_cluster_);

    if (num_results != PQntuples(consumer_results.get())) {
      return false;
    }
    int result;
    for (int i = 0; i < num_results; ++i) {
      result = VERIFY_RESULT(GetInt32(consumer_results.get(), i, 0));
      if (i != result) {
        return false;
      }
    }
    return true;
  };

  ASSERT_OK(WaitFor([&]() -> Result<bool> { return data_replicated_correctly(10); },
                    MonoDelta::FromSeconds(30), "IsDataReplicatedCorrectly"));

  // Call GetChanges for CDCSDK
  cdc::GetChangesRequestPB change_req;
  cdc::GetChangesResponsePB change_resp;

  // Get first change request.
  rpc::RpcController change_rpc;
  PrepareChangeRequest(&change_req, db_stream_id, tablets);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(sdk_proxy, db_stream_id, tablets));
  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  uint32_t ins_count = 0;
  uint32_t expected_record = 0;
  uint32_t expected_record_count = 10;

  LOG(INFO) << "Record received after the first call to GetChanges: " << record_size;
  for (uint32_t i = 0; i < record_size; ++i) {
    const cdc::CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (record.row_message().op() == cdc::RowMessage::INSERT) {
      ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), expected_record++);
      ins_count++;
    }

  }
  LOG(INFO) << "Got " << ins_count << " insert records";
  ASSERT_EQ(expected_record_count, ins_count);

  // Do more insert into producer.
  LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
  if (do_explict_transaction) {
    WriteTransactionalWorkload(10, 20, &producer_cluster_, producer_table->name());
  } else {
    WriteWorkload(10, 20, &producer_cluster_, producer_table->name());
  }
  // Verify data is written on the producer, which should previous plus
  // current new insert.
  producer_results = ScanToStrings(producer_table->name(), &producer_cluster_);
  ASSERT_EQ(batch_insert_count * 2, PQntuples(producer_results.get()));
  for (int i = 10; i < 20; ++i) {
    result = ASSERT_RESULT(GetInt32(producer_results.get(), i, 0));
    ASSERT_EQ(i, result);
  }
  // 5. Make sure this data is also replicated now.
  ASSERT_OK(WaitFor([&]() { return data_replicated_correctly(20); },
                    MonoDelta::FromSeconds(30), "IsDataReplicatedCorrectly"));
  cdc::GetChangesRequestPB change_req2;
  cdc::GetChangesResponsePB change_resp2;

  // Checkpoint from previous GetChanges call.
  cdc::CDCSDKCheckpointPB cp = change_resp.cdc_sdk_checkpoint();
  PrepareChangeRequest(&change_req2, db_stream_id, tablets, cp);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(sdk_proxy, db_stream_id, tablets, &cp));

  record_size = change_resp.cdc_sdk_proto_records_size();
  ins_count = 0;
  expected_record = 10;
  for (uint32_t i = 0; i < record_size; ++i) {
    const cdc::CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (record.row_message().op() == cdc::RowMessage::INSERT) {
      ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), expected_record++);
      ins_count++;
    }
  }
  LOG(INFO) << "Got " << ins_count << " insert records";
  ASSERT_EQ(expected_record_count, ins_count);
}

TEST_P(TwoDCYsqlTest, TwoDCWithCDCSDKEnabled) {
  YB_SKIP_TEST_IN_TSAN();
  FLAGS_ysql_enable_packed_row = false;
  ValidateRecordsTwoDCWithCDCSDK(false, false, false);
}

TEST_P(TwoDCYsqlTest, TwoDCWithCDCSDKPackedRowsEnabled) {
  YB_SKIP_TEST_IN_TSAN();
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_packed_row_size_limit = 1_KB;
  ValidateRecordsTwoDCWithCDCSDK(false, false, false);
}

TEST_P(TwoDCYsqlTest, TwoDCWithCDCSDKExplictTransaction) {
  YB_SKIP_TEST_IN_TSAN();
  FLAGS_ysql_enable_packed_row = false;
  ValidateRecordsTwoDCWithCDCSDK(false, true, true);
}

TEST_P(TwoDCYsqlTest, TwoDCWithCDCSDKExplictTranPackedRows) {
  YB_SKIP_TEST_IN_TSAN();
  FLAGS_ysql_enable_packed_row = true;
  FLAGS_ysql_packed_row_size_limit = 1_KB;
  ValidateRecordsTwoDCWithCDCSDK(false, true, true);
}

TEST_P(TwoDCYsqlTest, TwoDCWithCDCSDKUpdateCDCInterval) {
  YB_SKIP_TEST_IN_TSAN();
  ValidateRecordsTwoDCWithCDCSDK(true, true, false);
}

TEST_P(TwoDCYsqlTest, SetupSameNameDifferentSchemaUniverseReplication) {
  YB_SKIP_TEST_IN_TSAN();
  constexpr int kNumTables = 3;
  constexpr int kNTabletsPerTable = 3;
  auto tables = ASSERT_RESULT(SetUpWithParams({}, {}, 1));

  // Create 3 producer tables with the same name but different schema-name.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<YBTableName> producer_table_names;
  producer_tables.reserve(kNumTables);
  producer_table_names.reserve(kNumTables);
  for (int i = 0; i < kNumTables; i++) {
    auto t = ASSERT_RESULT(CreateTable(
        &producer_cluster_, kNamespaceName, Format("test_schema_$0", i),
        "test_table_1", boost::none /* tablegroup */, kNTabletsPerTable));
    producer_table_names.push_back(t);

    std::shared_ptr<client::YBTable> producer_table;
    ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
    producer_tables.push_back(producer_table);
  }

  // Create 3 consumer tables with similar setting but in reverse order to complicate the test.
  std::vector<YBTableName> consumer_table_names;
  consumer_table_names.reserve(kNumTables);
  for (int i = kNumTables - 1; i >= 0; i--) {
    auto t = ASSERT_RESULT(CreateTable(
        &consumer_cluster_, kNamespaceName, Format("test_schema_$0", i),
        "test_table_1", boost::none /* tablegroup */, kNTabletsPerTable));
    consumer_table_names.push_back(t);

    std::shared_ptr<client::YBTable> consumer_table;
    ASSERT_OK(consumer_client()->OpenTable(t, &consumer_table));
  }
  std::reverse(consumer_table_names.begin(), consumer_table_names.end());

  // Setup universe replication for the 3 tables.
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Write different numbers of records to the 3 producers, and verify that the
  // corresponding receivers receive the records.
  for (int i = 0; i < kNumTables; i++) {
    WriteWorkload(0, 2 * (i + 1), &producer_cluster_, producer_table_names[i]);
    ASSERT_OK(VerifyWrittenRecords(producer_table_names[i], consumer_table_names[i]));
  }

  ASSERT_OK(DeleteUniverseReplication(kUniverseId));
}

} // namespace enterprise
} // namespace yb
