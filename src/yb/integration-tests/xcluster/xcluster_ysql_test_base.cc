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

#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

#include "yb/client/client.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/server/server_base.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/thread.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

#include "yb/integration-tests/xcluster/xcluster_test_utils.h"

DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_string(pgsql_proxy_bind_address);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_int32(replication_factor);
DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_bool(cdc_enable_implicit_checkpointing);
DECLARE_bool(xcluster_enable_ddl_replication);

DECLARE_bool(TEST_create_table_with_empty_pgschema_name);
DECLARE_bool(TEST_use_custom_varz);
DECLARE_uint64(TEST_pg_auth_key);

namespace yb {

using OK = Status::OK;
using client::YBTableName;

void XClusterYsqlTestBase::SetUp() {
  TEST_SETUP_SUPER(XClusterTestBase);
  YB_SKIP_TEST_IN_TSAN();

  LOG(INFO) << "DB-scoped replication will use automatic mode: " << UseAutomaticMode();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_enable_ddl_replication) = UseAutomaticMode();
}

Status XClusterYsqlTestBase::Initialize(
    uint32_t replication_factor, uint32_t num_masters) {
  MiniClusterOptions opts;
  opts.num_tablet_servers = replication_factor;
  opts.num_masters = num_masters;

  RETURN_NOT_OK(InitClusters(opts));

  return Status::OK();
}

void XClusterYsqlTestBase::InitFlags(const MiniClusterOptions& opts) {
  // In this test, the tservers in each cluster share the same postgres proxy. As each tserver
  // initializes, it will overwrite the auth key for the "postgres" user. Force an identical key
  // so that all tservers can authenticate as "postgres".
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pg_auth_key) = RandomUniformInt<uint64_t>();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = static_cast<int>(opts.num_tablet_servers);
  // Disable tablet split for regular tests, see xcluster-tablet-split-itest for those tests.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = false;

  // Init postgres.
  master::SetDefaultInitialSysCatalogSnapshotFlags();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_hide_pg_catalog_table_creation_logs) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_auto_run_initdb) = true;

  // Init CDC flags.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_implicit_checkpointing) = true;
}

Status XClusterYsqlTestBase::InitClusters(const MiniClusterOptions& opts) {
  InitFlags(opts);

  auto producer_opts = opts;
  producer_opts.cluster_id = "producer";
  producer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(producer_opts);

  // Randomly select the tserver index that will serve the postgres proxy.
  const size_t pg_ts_idx = RandomUniformInt<size_t>(0, opts.num_tablet_servers - 1);
  const std::string pg_addr = server::TEST_RpcAddress(pg_ts_idx + 1, server::Private::kTrue);

  // The 'pgsql_proxy_bind_address' flag must be set before starting each cluster.  Each
  // tserver will store this address when it starts.
  const uint16_t producer_pg_port = producer_cluster_.mini_cluster_->AllocateFreePort();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_bind_address) =
      Format("$0:$1", pg_addr, producer_pg_port);
  // In order for yb-controller to access this and other gflags to find Postgres or the
  // masters, we must make /varz reflect the startup value of these gflags.  Otherwise, both
  // clusters will have the same information and yb-controller will only talk to one of them.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_use_custom_varz) = true;

  RETURN_NOT_OK(PreProducerCreate());
  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    RETURN_NOT_OK(producer_cluster()->StartAsync());
  }
  RETURN_NOT_OK(PostProducerCreate());

  auto consumer_opts = opts;
  consumer_opts.cluster_id = "consumer";
  consumer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(consumer_opts);

  // Use a new pg proxy port for the consumer cluster.
  const uint16_t consumer_pg_port = consumer_cluster_.mini_cluster_->AllocateFreePort();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_bind_address) =
      Format("$0:$1", pg_addr, consumer_pg_port);

  RETURN_NOT_OK(PreConsumerCreate());
  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    RETURN_NOT_OK(consumer_cluster()->StartAsync());
  }
  RETURN_NOT_OK(PostConsumerCreate());

  RETURN_NOT_OK(
      RunOnBothClusters([](MiniCluster* cluster) { return cluster->WaitForAllTabletServers(); }));

  // Verify the that the selected tablets have their rpc servers bound to the expected pg addr.
  CHECK_EQ(producer_cluster_.mini_cluster_->mini_tablet_server(pg_ts_idx)->bound_rpc_addr().
           address().to_string(), pg_addr);
  CHECK_EQ(consumer_cluster_.mini_cluster_->mini_tablet_server(pg_ts_idx)->bound_rpc_addr().
           address().to_string(), pg_addr);

  producer_cluster_.client_ = VERIFY_RESULT(producer_cluster()->CreateClient());
  consumer_cluster_.client_ = VERIFY_RESULT(consumer_cluster()->CreateClient());
  producer_cluster_.pg_ts_idx_ = pg_ts_idx;
  consumer_cluster_.pg_ts_idx_ = pg_ts_idx;

  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    SetPGCallbacks(&producer_cluster_, producer_pg_port);
    RETURN_NOT_OK(StartPostgres(&producer_cluster_));
  }

  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    SetPGCallbacks(&consumer_cluster_, consumer_pg_port);
    RETURN_NOT_OK(StartPostgres(&consumer_cluster_));
  }

  return Status::OK();
}

Status XClusterYsqlTestBase::InitProducerClusterOnly(const MiniClusterOptions& opts) {
  InitFlags(opts);

  auto producer_opts = opts;
  producer_opts.cluster_id = "producer";
  producer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(producer_opts);

  // Randomly select the tserver index that will serve the postgres proxy.
  const size_t pg_ts_idx = RandomUniformInt<size_t>(0, opts.num_tablet_servers - 1);
  const std::string pg_addr = server::TEST_RpcAddress(pg_ts_idx + 1, server::Private::kTrue);
  // The 'pgsql_proxy_bind_address' flag must be set before starting the producer cluster. Each
  // tserver will store this address when it starts.
  const uint16_t producer_pg_port = producer_cluster_.mini_cluster_->AllocateFreePort();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_bind_address) =
      Format("$0:$1", pg_addr, producer_pg_port);

  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    RETURN_NOT_OK(producer_cluster()->StartAsync());
  }

  RETURN_NOT_OK(producer_cluster()->WaitForAllTabletServers());

  // Verify the that the selected tablets have their rpc servers bound to the expected pg addr.
  CHECK_EQ(
      producer_cluster_.mini_cluster_->mini_tablet_server(pg_ts_idx)
          ->bound_rpc_addr()
          .address()
          .to_string(),
      pg_addr);

  producer_cluster_.client_ = VERIFY_RESULT(producer_cluster()->CreateClient());
  producer_cluster_.pg_ts_idx_ = pg_ts_idx;

  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    SetPGCallbacks(&producer_cluster_, producer_pg_port);
    RETURN_NOT_OK(StartPostgres(&producer_cluster_));
  }

  return Status::OK();
}

Status XClusterYsqlTestBase::InitPostgres(
    Cluster* cluster, const size_t pg_ts_idx, uint16_t pg_port) {
  RETURN_NOT_OK(WaitForInitDb(cluster->mini_cluster_.get()));

  tserver::MiniTabletServer* const pg_ts = cluster->mini_cluster_->mini_tablet_server(pg_ts_idx);
  CHECK(pg_ts);

  yb::pgwrapper::PgProcessConf pg_process_conf =
      VERIFY_RESULT(yb::pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
          yb::ToString(Endpoint(pg_ts->bound_rpc_addr().address(), pg_port)),
          pg_ts->options()->fs_opts.data_paths.front() + "/pg_data"));
  pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
  pg_process_conf.force_disable_log_file = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_webserver_port) =
      cluster->mini_cluster_->AllocateFreePort();

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses << ":"
            << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
            << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
  cluster->pg_supervisor_ =
      std::make_unique<pgwrapper::PgSupervisor>(pg_process_conf, pg_ts->server());
  RETURN_NOT_OK(cluster->pg_supervisor_->StartAndMaybePause());

  cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
  return OK();
}

Status XClusterYsqlTestBase::StartPostgres(Cluster* cluster) {
  auto* pg_ts = cluster->mini_cluster_->mini_tablet_server(cluster->pg_ts_idx_);
  return pg_ts->StartPgIfConfigured();
}

void XClusterYsqlTestBase::SetPGCallbacks(Cluster* cluster, uint16_t pg_port) {
  tserver::MiniTabletServer* const pg_ts =
      cluster->mini_cluster_->mini_tablet_server(cluster->pg_ts_idx_);
  CHECK(pg_ts);
  pg_ts->SetPgServerHandlers(
      // start_pg
      [this, cluster, pg_port] { return InitPostgres(cluster, cluster->pg_ts_idx_, pg_port); },
      // shutdown_pg
      [cluster] {
        cluster->pg_supervisor_->Stop();
        cluster->pg_supervisor_.reset();
      },
      [cluster] { return cluster->CreatePGConnSettings(); });
}

std::string XClusterYsqlTestBase::GetCompleteTableName(const YBTableName& table) {
  // Append schema name before table name, if schema is available.
  return table.has_pgschema_name() ? Format("$0.$1", table.pgschema_name(), table.table_name())
                                   : table.table_name();
}

Result<NamespaceId> XClusterYsqlTestBase::GetNamespaceId(YBClient* client) {
  return XClusterTestUtils::GetNamespaceId(*client, namespace_name);
}

Result<YBTableName> XClusterYsqlTestBase::CreateYsqlTable(
    Cluster* cluster, const std::string& namespace_name, const std::string& schema_name,
    const std::string& table_name, const std::optional<std::string>& tablegroup_name,
    uint32_t num_tablets, bool colocated, const ColocationId colocation_id,
    const bool ranged_partitioned) {
  auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
  std::string colocation_id_string = "";
  if (colocation_id > 0) {
    colocation_id_string = Format("colocation_id = $0", colocation_id);
  }
  if (!schema_name.empty()) {
    RETURN_NOT_OK(conn.Execute(Format("CREATE SCHEMA IF NOT EXISTS $0;", schema_name)));
  }
  std::string full_table_name =
      schema_name.empty() ? table_name : Format("$0.$1", schema_name, table_name);
  std::string query = Format(
      "CREATE TABLE $0($1 int, PRIMARY KEY ($1$2)) ", full_table_name, kKeyColumnName,
      ranged_partitioned ? " ASC" : "");
  // One cannot use tablegroup together with split into tablets.
  if (tablegroup_name.has_value()) {
    std::string with_clause =
        colocation_id_string.empty() ? "" : Format("WITH ($0) ", colocation_id_string);
    std::string tablegroup_clause = Format("TABLEGROUP $0", tablegroup_name.value());
    query += Format("$0$1", with_clause, tablegroup_clause);
  } else {
    std::string colocated_option = colocated ? "true" : "false";
    std::string colocated_clause = Format("colocation = $0", colocated_option);
    std::string with_clause = colocation_id_string.empty()
                                  ? colocated_clause
                                  : Format("$0, $1", colocation_id_string, colocated_clause);
    query += Format("WITH ($0)", with_clause);
    if (!colocated) {
      if (ranged_partitioned) {
        if (num_tablets > 1) {
          // Split at every 500 interval.
          query += " SPLIT AT VALUES(";
          for (size_t i = 0; i < num_tablets - 1; ++i) {
            query +=
                Format("($0)$1", i * kRangePartitionInterval, (i == num_tablets - 2) ? ")" : ", ");
          }
        }
      } else {
        query += Format(" SPLIT INTO $0 TABLETS", num_tablets);
      }
    }
  }
  LOG_WITH_FUNC(INFO) << "Executing: " << query;
  RETURN_NOT_OK(conn.Execute(query));

  // Only check the schema name if it is set AND we created the table with a valid pgschema_name.
  bool verify_schema_name =
      !schema_name.empty() && !FLAGS_TEST_create_table_with_empty_pgschema_name;
  return GetYsqlTable(
      cluster, namespace_name, schema_name, table_name, /*verify_table_name=*/true,
      verify_schema_name);
}

Result<YBTableName> XClusterYsqlTestBase::CreateYsqlTable(
    uint32_t idx, uint32_t num_tablets, Cluster* cluster,
    const std::optional<std::string>& tablegroup_name, bool colocated,
    const bool ranged_partitioned) {
  // Generate colocation_id based on index so that we have the same colocation_id for
  // producer/consumer.
  const int colocation_id = (tablegroup_name.has_value() || colocated) ? (idx + 1) * 111111 : 0;
  return CreateYsqlTable(
      cluster, namespace_name, "" /* schema_name */, Format("test_table_$0", idx), tablegroup_name,
      num_tablets, colocated, colocation_id, ranged_partitioned);
}

Result<std::string> XClusterYsqlTestBase::GetUniverseId(Cluster* cluster) {
  return VERIFY_RESULT(GetClusterConfig(*cluster)).cluster_uuid();
}

Result<master::SysClusterConfigEntryPB> XClusterYsqlTestBase::GetClusterConfig(Cluster& cluster) {
  master::GetMasterClusterConfigRequestPB req;
  master::GetMasterClusterConfigResponsePB resp;

  master::MasterClusterProxy master_proxy(
      &cluster.client_->proxy_cache(),
      VERIFY_RESULT(cluster.mini_cluster_->GetLeaderMasterBoundRpcAddr()));

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy.GetMasterClusterConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return resp.cluster_config();
}

Result<std::pair<NamespaceId, NamespaceId>> XClusterYsqlTestBase::CreateDatabaseOnBothClusters(
    const NamespaceName& db_name) {
  RETURN_NOT_OK(RunOnBothClusters([this, &db_name](Cluster* cluster) -> Status {
    RETURN_NOT_OK(CreateDatabase(cluster, db_name));
    auto table_name = VERIFY_RESULT(CreateYsqlTable(
        cluster, db_name, "" /* schema_name */, "initial_table",
        /*tablegroup_name=*/std::nullopt, /*num_tablets=*/1));
    std::shared_ptr<client::YBTable> table;
    RETURN_NOT_OK(cluster->client_->OpenTable(table_name, &table));
    cluster->tables_.emplace_back(std::move(table));
    return Status::OK();
  }));
  return std::make_pair(
      VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*producer_client(), db_name)),
      VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*consumer_client(), db_name)));
}

Result<YBTableName> XClusterYsqlTestBase::GetYsqlTable(
    Cluster* cluster,
    const std::string& namespace_name,
    const std::string& schema_name,
    const std::string& table_name,
    bool verify_table_name,
    bool verify_schema_name,
    bool exclude_system_tables) {
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

  if (!verify_schema_name) {
    // There are could be multiple tables with the same name.
    // So reverse tables to find the latest one.
    // Since we are dealing with postgres tables, their id reflects postgres table ids, that
    // are assigned in increasing order.
    std::reverse(resp.mutable_tables()->begin(), resp.mutable_tables()->end());
  }

  // Now need to find the table and return it.
  for (const auto& table : resp.tables()) {
    // If !verify_table_name, just return the first table.
    if (!verify_table_name ||
        (table.name() == table_name && table.namespace_().name() == namespace_name)) {
      // In case of a match, further check for match in schema_name.
      if (!verify_schema_name || (table.pgschema_name() == schema_name)) {
        YBTableName yb_table;
        yb_table.set_table_id(table.id());
        yb_table.set_table_name(table_name);
        yb_table.set_namespace_id(table.namespace_().id());
        yb_table.set_namespace_name(namespace_name);
        if (!table.pgschema_name().empty()) {
          yb_table.set_pgschema_name(table.pgschema_name());
        }
        return yb_table;
      }
    }
  }
  return STATUS(
      NotFound,
      strings::Substitute("Unable to find table $0 in namespace $1", table_name, namespace_name));
}

Result<bool> XClusterYsqlTestBase::IsTableDeleted(Cluster& cluster, const YBTableName& table_name) {
  master::ListTablesRequestPB req;
  master::ListTablesResponsePB resp;

  req.set_name_filter(table_name.table_name());
  req.mutable_namespace_()->set_name(table_name.namespace_name());
  req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  req.set_include_not_running(true);

  master::MasterDdlProxy master_proxy(
      &cluster.client_->proxy_cache(),
      VERIFY_RESULT(cluster.mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy.ListTables(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  for (const auto& table : resp.tables()) {
    if (table.pgschema_name() == table_name.pgschema_name() &&
        table.state() != master::SysTablesEntryPB::DELETED) {
      LOG(INFO) << "Found table in incorrect state: " << table.ShortDebugString();
      return false;
    }
  }
  return true;
}

Status XClusterYsqlTestBase::WaitForTableToFullyDelete(
    Cluster& cluster, const client::YBTableName& table_name, MonoDelta timeout) {
  return LoggedWaitFor(
      [&]() -> Result<bool> { return IsTableDeleted(cluster, producer_table_->name()); }, timeout,
      "Wait for table to transition to deleted.");
}

Status XClusterYsqlTestBase::DropYsqlTable(
    Cluster* cluster, const std::string& namespace_name, const std::string& schema_name,
    const std::string& table_name, bool is_index) {
  auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
  auto full_table_name =
      schema_name.empty() ? table_name : Format("$0.$1", schema_name, table_name);
  std::string query = Format("DROP $0 $1", is_index ? "INDEX" : "TABLE", full_table_name);

  return conn.Execute(query);
}

Status XClusterYsqlTestBase::DropYsqlTable(Cluster& cluster, const client::YBTable& table) {
  const auto table_name = table.name();
  return DropYsqlTable(
      &cluster, table_name.namespace_name(), table_name.pgschema_name(), table_name.table_name(),
      table_name.relation_type() == master::INDEX_TABLE_RELATION);
}

Result<pgwrapper::PGResultPtr> XClusterYsqlTestBase::ScanToStrings(
    const YBTableName& table_name, XClusterTestBase::Cluster* cluster) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(table_name.namespace_name()));
  const std::string table_name_str = GetCompleteTableName(table_name);
  auto result = VERIFY_RESULT(
      conn.FetchFormat("SELECT * FROM $0 ORDER BY $1", table_name_str, kKeyColumnName));
  return result;
}

Result<int> XClusterYsqlTestBase::GetRowCount(
    const YBTableName& table_name, Cluster* cluster, bool read_latest) {
  auto conn = VERIFY_RESULT(
      cluster->ConnectToDB(table_name.namespace_name(), true /*simple_query_protocol*/));
  if (read_latest) {
    RETURN_NOT_OK(conn.FetchRow<std::string>(
        "UPDATE pg_settings SET setting = 'tablet' WHERE name = 'yb_xcluster_consistency_level'"));
  }
  std::string table_name_str = GetCompleteTableName(table_name);
  auto results = VERIFY_RESULT(conn.FetchFormat("SELECT * FROM $0", table_name_str));
  return PQntuples(results.get());
}

Status XClusterYsqlTestBase::WaitForRowCount(
    const YBTableName& table_name, uint32_t row_count, Cluster* cluster, bool allow_greater) {
  uint32_t last_row_count = 0;

  return LoggedWaitFor(
      [&]() -> Result<bool> {
        auto result = GetRowCount(table_name, cluster);
        if (!result) {
          LOG(INFO) << result.status().ToString();
          return false;
        }
        last_row_count = result.get();
        LOG(INFO) << "Row Count: " << last_row_count;
        if (allow_greater) {
          return last_row_count >= row_count;
        }
        return last_row_count == row_count;
      },
      MonoDelta::FromSeconds(kWaitForRowCountTimeout),
      Format(
          "Wait for $0 row_count to reach $2 $3", table_name, allow_greater ? "atleast" : "",
          row_count));
}

Status XClusterYsqlTestBase::ValidateRows(
    const YBTableName& table_name, int row_count, Cluster* cluster) {
  auto results = VERIFY_RESULT(ScanToStrings(table_name, cluster));
  auto actual_row_count = PQntuples(results.get());
  SCHECK_EQ(
      row_count, actual_row_count, Corruption,
      Format("Expected $0 rows but got $1 rows", row_count, actual_row_count));

  int result;
  for (int i = 0; i < row_count; ++i) {
    result = VERIFY_RESULT(pgwrapper::GetValue<int32_t>(results.get(), i, 0));
    SCHECK(i == result, Corruption, Format("Expected row value $0 but got $1", i, result));
  }

  return OK();
}

Status XClusterYsqlTestBase::VerifyWrittenRecords(
    std::shared_ptr<client::YBTable> producer_table,
    std::shared_ptr<client::YBTable> consumer_table) {
  if (!producer_table) {
    producer_table = producer_table_;
  }
  if (!consumer_table) {
    consumer_table = consumer_table_;
  }
  return VerifyWrittenRecords(producer_table->name(), consumer_table->name());
}

Status XClusterYsqlTestBase::VerifyWrittenRecords(
    ExpectNoRecords expect_no_records, CheckColumnCounts check_col_counts) {
  return VerifyWrittenRecords(
      producer_table_->name(), consumer_table_->name(), expect_no_records, check_col_counts);
}

Status XClusterYsqlTestBase::VerifyWrittenRecords(
    const YBTableName& producer_table_name, const YBTableName& consumer_table_name,
    ExpectNoRecords expect_no_records, CheckColumnCounts check_col_counts) {
  int prod_row_count, cons_row_count = 0, prod_col_count = 0, cons_col_count = 0;
  const Status s = LoggedWaitFor(
      [this, producer_table_name, consumer_table_name, &prod_row_count, &cons_row_count,
       &prod_col_count, &cons_col_count, expect_no_records, check_col_counts]() -> Result<bool> {
        auto producer_results =
            VERIFY_RESULT(ScanToStrings(producer_table_name, &producer_cluster_));
        prod_row_count = PQntuples(producer_results.get());
        LOG_WITH_FUNC(INFO) << "prod_row_count: " << prod_row_count;
        if (expect_no_records ? (prod_row_count != 0) : (prod_row_count == 0)) {
          return false;
        }
        auto consumer_results =
            VERIFY_RESULT(ScanToStrings(consumer_table_name, &consumer_cluster_));
        cons_row_count = PQntuples(consumer_results.get());
        LOG_WITH_FUNC(INFO) << "cons_row_count: " << cons_row_count;
        if (prod_row_count != cons_row_count) {
          return false;
        }
        prod_col_count = PQnfields(producer_results.get());
        cons_col_count = PQnfields(consumer_results.get());
        SCHECK(
            !check_col_counts || prod_col_count == cons_col_count, Corruption,
            Format("Expected $0 columns but got $1 columns", prod_col_count, cons_col_count));
        int col_count = std::min(prod_col_count, cons_col_count);
        for (int row = 0; row < prod_row_count; ++row) {
          for (int col = 0; col < col_count; ++col) {
            auto prod_val = EXPECT_RESULT(pgwrapper::ToString(producer_results.get(), row, col));
            auto cons_val = EXPECT_RESULT(pgwrapper::ToString(consumer_results.get(), row, col));
            if (prod_val != cons_val) {
              LOG(INFO) << Format(
                  "Mismatch at row $0, col $1: Producer: $2, Consumer: $3", row, col, prod_val,
                  cons_val);
              return false;
            }
          }
        }
        return true;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Verify written records");
  LOG(INFO) << "Row counts, Producer: " << prod_row_count << ", Consumer: " << cons_row_count
            << ". Column counts, Producer: " << prod_col_count << ", Consumer: " << cons_col_count;
  return s;
}

Status XClusterYsqlTestBase::VerifyWrittenRecords(
    const std::vector<TableName>& table_names, const NamespaceName& database_name,
    const std::string& schema_name) {
  auto db_name = database_name.empty() ? namespace_name : database_name;
  for (const auto& table_name : table_names) {
    auto producer_table = VERIFY_RESULT(GetProducerTable(
        VERIFY_RESULT(GetYsqlTable(&producer_cluster_, db_name, schema_name, table_name))));
    auto consumer_table = VERIFY_RESULT(GetConsumerTable(
        VERIFY_RESULT(GetYsqlTable(&consumer_cluster_, db_name, schema_name, table_name))));
    RETURN_NOT_OK_PREPEND(
        VerifyWrittenRecords(producer_table, consumer_table),
        Format("Failed to verify written records for table $0", table_name));
  }
  return Status::OK();
}

Result<std::shared_ptr<client::YBTable>> XClusterYsqlTestBase::GetProducerTable(
    const client::YBTableName& producer_table_name) {
  std::shared_ptr<client::YBTable> producer_table;
  RETURN_NOT_OK(producer_client()->OpenTable(producer_table_name, &producer_table));
  return producer_table;
}

Result<std::shared_ptr<client::YBTable>> XClusterYsqlTestBase::GetConsumerTable(
    const client::YBTableName& producer_table_name) {
  auto consumer_table_name = VERIFY_RESULT(GetYsqlTable(
      &consumer_cluster_, producer_table_name.namespace_name(), producer_table_name.pgschema_name(),
      producer_table_name.table_name()));
  std::shared_ptr<client::YBTable> consumer_table;
  RETURN_NOT_OK(consumer_client()->OpenTable(consumer_table_name, &consumer_table));
  return consumer_table;
}

Result<std::vector<xrepl::StreamId>> XClusterYsqlTestBase::BootstrapCluster(
    const std::vector<std::shared_ptr<client::YBTable>>& tables,
    XClusterTestBase::Cluster* cluster) {
  cdc::BootstrapProducerRequestPB req;
  cdc::BootstrapProducerResponsePB resp;

  for (const auto& table : tables) {
    req.add_table_ids(table->id());
  }
  rpc::RpcController rpc;
  auto producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &cluster->client_->proxy_cache(),
      HostPort::FromBoundEndpoint(cluster->mini_cluster_->mini_tablet_server(0)->bound_rpc_addr()));
  RETURN_NOT_OK(producer_cdc_proxy->BootstrapProducer(req, &resp, &rpc));
  CHECK(!resp.has_error()) << resp.error().DebugString();

  CHECK_EQ(resp.cdc_bootstrap_ids().size(), tables.size());

  std::vector<xrepl::StreamId> bootstrap_ids;
  int table_idx = 0;
  for (const auto& bootstrap_id : resp.cdc_bootstrap_ids()) {
    LOG(INFO) << "Got bootstrap id " << bootstrap_id << " for table "
              << tables[table_idx++]->name().table_name();
    bootstrap_ids.emplace_back(VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_id)));
  }

  return bootstrap_ids;
}

void XClusterYsqlTestBase::BumpUpSchemaVersionsWithAlters(
    const std::vector<std::shared_ptr<client::YBTable>>& tables) {
  // Perform some ALTERs to bump up the schema versions and cause schema version mismatch
  // between producer and consumer before setting up replication.
  {
    for (const auto& table : tables) {
      auto tbl = table->name();
      for (size_t i = 0; i < 4; i++) {
        auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
        ASSERT_OK(p_conn.ExecuteFormat("ALTER TABLE $0 OWNER TO yugabyte;", tbl.table_name()));
        if (i % 2 == 0) {
          auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
          ASSERT_OK(c_conn.ExecuteFormat("ALTER TABLE $0 OWNER TO yugabyte;", tbl.table_name()));
        }
      }
    }
  }
}

Status XClusterYsqlTestBase::InsertRowsInProducer(
    uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table,
    std::optional<bool> use_transaction) {
  if (!producer_table) {
    producer_table = producer_table_;
  }
  return WriteWorkload(
      start, end, &producer_cluster_, producer_table->name(), /* delete_op */ false,
      use_transaction);
}

Status XClusterYsqlTestBase::DeleteRowsInProducer(
    uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table,
    std::optional<bool> use_transaction) {
  if (!producer_table) {
    producer_table = producer_table_;
  }
  return WriteWorkload(
      start, end, &producer_cluster_, producer_table->name(), /* delete_op */ true,
      use_transaction);
}

Status XClusterYsqlTestBase::InsertGenerateSeriesOnProducer(
    uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table) {
  if (!producer_table) {
    producer_table = producer_table_;
  }
  return WriteGenerateSeries(start, end, &producer_cluster_, producer_table->name());
}

Status XClusterYsqlTestBase::InsertTransactionalBatchOnProducer(
    uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table,
    bool commit_transaction) {
  if (!producer_table) {
    producer_table = producer_table_;
  }
  return WriteTransactionalWorkload(
      start, end, &producer_cluster_, producer_table->name(), commit_transaction);
}

Status XClusterYsqlTestBase::WriteGenerateSeries(
    uint32_t start, uint32_t end, Cluster* cluster, const client::YBTableName& table) {
  auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
  auto generate_string =
      Format("INSERT INTO $0 VALUES (generate_series($1, $2))", table.table_name(), start, end);
  return conn.ExecuteFormat(generate_string);
}

Status XClusterYsqlTestBase::WriteTransactionalWorkload(
    uint32_t start, uint32_t end, Cluster* cluster, const client::YBTableName& table,
    bool commit_transaction) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(table.namespace_name()));
  std::string table_name_str = GetCompleteTableName(table);
  RETURN_NOT_OK(conn.Execute("BEGIN"));
  for (uint32_t i = start; i < end; i++) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO $0($1) VALUES ($2) ON CONFLICT DO NOTHING", table_name_str, kKeyColumnName,
        i));
  }
  if (commit_transaction) {
    RETURN_NOT_OK(conn.Execute("COMMIT"));
  } else {
    RETURN_NOT_OK(conn.Execute("ABORT"));
  }
  return Status::OK();
}

Status XClusterYsqlTestBase::WriteWorkload(
    uint32_t start, uint32_t end, Cluster* cluster, const YBTableName& table, bool delete_op,
    std::optional<bool> use_transaction_opt) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(table.namespace_name()));
  std::string table_name_str = GetCompleteTableName(table);

  bool use_transaction = use_transaction_opt.value_or(end != start);
  LOG(INFO) << "Writing " << end - start << (delete_op ? " deletes" : " inserts")
            << " use_transaction: " << use_transaction;
  if (use_transaction) {
    RETURN_NOT_OK(conn.ExecuteFormat("BEGIN"));
  }
  for (uint32_t i = start; i < end; i++) {
    if (delete_op) {
      RETURN_NOT_OK(
          conn.ExecuteFormat("DELETE FROM $0 WHERE $1 = $2", table_name_str, kKeyColumnName, i));
    } else {
      // TODO(#6582) transactions currently don't work, so don't use ON CONFLICT DO NOTHING now.
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1) VALUES ($2)",  // ON CONFLICT DO NOTHING",
          table_name_str, kKeyColumnName, i));
    }
  }
  if (use_transaction) {
    RETURN_NOT_OK(conn.ExecuteFormat("COMMIT"));
  }

  return Status::OK();
}

void XClusterYsqlTestBase::TestReplicationWithSchemaChanges(
    TableId producer_table_id, bool bootstrap) {
  auto tbl = consumer_table_->name();

  BumpUpSchemaVersionsWithAlters({consumer_table_});

  {
    // Perform a DDL so that there are already some change metadata ops in the WAL to process
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(p_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN pre_setup TEXT", tbl.table_name()));

    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(c_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN pre_setup TEXT", tbl.table_name()));
  }

  // If bootstrap is requested, run bootstrap
  std::vector<xrepl::StreamId> bootstrap_ids = {};
  if (bootstrap) {
    bootstrap_ids = ASSERT_RESULT(
        BootstrapProducer(producer_cluster(), producer_client(), {producer_table_id}));
  }

  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      {producer_table_id}, bootstrap_ids, {LeaderOnly::kTrue, Transactional::kTrue}));

  ASSERT_OK(InsertGenerateSeriesOnProducer(0, 50, producer_table_));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(
      VerifyUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, &resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_id);
  ASSERT_OK(VerifyWrittenRecords());

  // Alter the table on the Consumer side by adding a column
  {
    std::string new_col = "new_col";
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT", tbl.table_name(), new_col));
  }

  // Verify single value inserts are replicated correctly and can be read
  ASSERT_OK(InsertRowsInProducer(51, 52));
  auto verify_written_records_without_column_counts = [this]() {
    // Don't check column counts as they are different now.
    return VerifyWrittenRecords(ExpectNoRecords::kFalse, CheckColumnCounts::kFalse);
  };

  ASSERT_OK(verify_written_records_without_column_counts());

  // 5. Verify batch inserts are replicated correctly and can be read
  ASSERT_OK(InsertGenerateSeriesOnProducer(52, 100));
  ASSERT_OK(verify_written_records_without_column_counts());

  // Verify transactional inserts are replicated correctly and can be read
  ASSERT_OK(InsertTransactionalBatchOnProducer(101, 150));
  ASSERT_OK(verify_written_records_without_column_counts());

  // Alter the table on the producer side by adding the same column and insert some rows
  // and verify
  {
    std::string new_col = "new_col";
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT", tbl.table_name(), new_col));
  }

  // Verify single value inserts are replicated correctly and can be read
  ASSERT_OK(InsertRowsInProducer(151, 152));
  ASSERT_OK(VerifyWrittenRecords());

  // Verify batch inserts are replicated correctly and can be read
  ASSERT_OK(InsertGenerateSeriesOnProducer(152, 200));
  ASSERT_OK(VerifyWrittenRecords());

  // Verify transactional inserts are replicated correctly and can be read
  ASSERT_OK(InsertTransactionalBatchOnProducer(201, 250));
  ASSERT_OK(VerifyWrittenRecords());

  {
    // Verify I/U/D
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(251,'Hello', 'World')", tbl.table_name()));
    ASSERT_OK(VerifyWrittenRecords());

    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET new_col = 'Hi' WHERE key = 251", tbl.table_name()));
    ASSERT_OK(VerifyWrittenRecords());

    ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE key = 251", tbl.table_name()));
    ASSERT_OK(VerifyWrittenRecords());
  }

  // Bump up schema version on Producer and verify.
  {
    auto alter_owner_str = Format("ALTER TABLE $0 OWNER TO yugabyte;", tbl.table_name());
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat(alter_owner_str));
    ASSERT_OK(InsertRowsInProducer(251, 300));
    ASSERT_OK(VerifyWrittenRecords());
  }

  // Alter table on consumer side to generate new schema version.
  {
    std::string new_col = "new_col_2";
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 TEXT", tbl.table_name(), new_col));
  }

  ASSERT_OK(consumer_cluster()->FlushTablets());

  ASSERT_OK(InsertRowsInProducer(301, 350));
  ASSERT_OK(verify_written_records_without_column_counts());
}

Status XClusterYsqlTestBase::SetUpWithParams(
    const std::vector<uint32_t>& num_consumer_tablets,
    const std::vector<uint32_t>& num_producer_tablets, uint32_t replication_factor,
    uint32_t num_masters, const bool ranged_partitioned) {
  SetupParams params{
      .num_consumer_tablets = num_consumer_tablets,
      .num_producer_tablets = num_producer_tablets,
      .replication_factor = replication_factor,
      .num_masters = num_masters,
      .ranged_partitioned = ranged_partitioned,
      .is_colocated = false,
  };

  return SetUpClusters(params);
}

Status XClusterYsqlTestBase::SetUpClusters() {
  static const SetupParams default_params;
  return SetUpClusters(default_params);
}

Status XClusterYsqlTestBase::SetUpClusters(const SetupParams& params) {
  SCHECK(
      !params.is_colocated || namespace_name != "yugabyte", IllegalState,
      "yugabyte is a non-colocated database. Set namespace_name to a different value");
  SCHECK(
      !params.use_different_database_oids || namespace_name != "yugabyte", IllegalState,
      "yugabyte is an existing database and cannot be caused to have different OIDs. Set "
      "namespace_name to a different value");

  RETURN_NOT_OK(Initialize(params.replication_factor, params.num_masters));

  if (params.start_yb_controller_servers) {
    {
      TEST_SetThreadPrefixScoped prefix_se("P");
      RETURN_NOT_OK(producer_cluster_.mini_cluster_->StartYbControllerServers());
    }
    {
      TEST_SetThreadPrefixScoped prefix_se("C");
      RETURN_NOT_OK(consumer_cluster_.mini_cluster_->StartYbControllerServers());
    }
  }

  SCHECK_EQ(
      params.num_consumer_tablets.size(), params.num_producer_tablets.size(), IllegalState,
      Format(
          "Num consumer tables: $0 num producer tables: $1 must be equal.",
          params.num_consumer_tablets.size(), params.num_producer_tablets.size()));

  if (params.use_different_database_oids) {
    // Create an extra database on the consumer before creating the
    // test database so the databases will have different OIDs and
    // thus namespace IDs between the consumer and producer universes.
    RETURN_NOT_OK(CreateDatabase(&consumer_cluster_, "gratuitous_db", false));
  }

  RETURN_NOT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    master::GetNamespaceInfoResponsePB resp;
    auto namespace_status = cluster->client_->GetNamespaceInfo(
        /*namespace_id=*/"", namespace_name, YQL_DATABASE_PGSQL, &resp);
    if (!namespace_status.ok()) {
      if (namespace_status.IsNotFound()) {
        RETURN_NOT_OK(CreateDatabase(cluster, namespace_name, params.is_colocated));
      } else {
        return namespace_status;
      }
    }

    const auto* num_tablets = &params.num_producer_tablets;
    if (cluster == &consumer_cluster_) {
      num_tablets = &params.num_consumer_tablets;
    }

    for (uint32_t i = 0; i < num_tablets->size(); i++) {
      auto table_name = VERIFY_RESULT(CreateYsqlTable(
          i, num_tablets->at(i), cluster, std::nullopt /* tablegroup */, false /* colocated */,
          params.ranged_partitioned));
      std::shared_ptr<client::YBTable> table;
      RETURN_NOT_OK(cluster->client_->OpenTable(table_name, &table));
      cluster->tables_.push_back(table);
    }
    return Status::OK();
  }));

  if (params.use_different_database_oids) {
    SCHECK_NE(
        VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*producer_client(), namespace_name)),
        VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*consumer_client(), namespace_name)),
        InternalError, "Unable to use different OIDs for the source and target databases");
  }

  return PostSetUp();
}

Status XClusterYsqlTestBase::CheckpointReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, bool require_no_bootstrap_needed) {
  auto bootstrap_required = VERIFY_RESULT(XClusterTestUtils::CheckpointReplicationGroup(
      *producer_client(), replication_group_id, namespace_name, MonoDelta::FromSeconds(kRpcTimeout),
      UseAutomaticMode()));
  SCHECK(
      !require_no_bootstrap_needed || !bootstrap_required, IllegalState,
      "Bootstrap should not be required");

  return Status::OK();
}

Result<bool> XClusterYsqlTestBase::IsXClusterBootstrapRequired(
    const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceId& source_namespace_id) {
  std::promise<Result<bool>> promise;
  auto future = promise.get_future();
  RETURN_NOT_OK(client::XClusterClient(*producer_client())
                    .IsBootstrapRequired(
                        CoarseMonoClock::now() + MonoDelta::FromSeconds(kRpcTimeout),
                        replication_group_id, source_namespace_id,
                        [&promise](Result<bool> res) { promise.set_value(res); }));
  return future.get();
}

Status XClusterYsqlTestBase::AddNamespaceToXClusterReplication(
    const NamespaceId& source_namespace_id, const NamespaceId& target_namespace_id) {
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  auto target_master_address = consumer_cluster()->GetMasterAddresses();

  RETURN_NOT_OK(source_xcluster_client.AddNamespaceToXClusterReplication(
      kReplicationGroupId, target_master_address, source_namespace_id));
  RETURN_NOT_OK(LoggedWaitFor(
      [this, &target_master_address]() -> Result<bool> {
        auto result = VERIFY_RESULT(
            client::XClusterClient(*producer_client())
                .IsAlterXClusterReplicationDone(kReplicationGroupId, target_master_address));
        if (!result.status().ok()) {
          return result.status();
        }
        return result.done();
      },
      MonoDelta::FromSeconds(kRpcTimeout), "IsAlterXClusterReplicationDone"));

  return WaitForValidSafeTimeOnAllTServers(target_namespace_id);
}

Status XClusterYsqlTestBase::CreateReplicationFromCheckpoint(
    const std::string& target_master_addresses,
    const xcluster::ReplicationGroupId& replication_group_id,
    std::vector<NamespaceName> namespace_names) {
  RETURN_NOT_OK(SetupCertificates(replication_group_id));

  auto master_addr = target_master_addresses;
  if (master_addr.empty()) {
    master_addr = consumer_cluster()->GetMasterAddresses();
  }
  if (namespace_names.empty()) {
    namespace_names = {namespace_name};
  }

  RETURN_NOT_OK(XClusterTestUtils::CreateReplicationFromCheckpoint(
      *producer_client(), replication_group_id, master_addr, MonoDelta::FromSeconds(kRpcTimeout)));

  return WaitForSafeTimeToAdvanceToNow(namespace_names);
}

Status XClusterYsqlTestBase::DeleteOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id) {
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  const auto target_master_address = consumer_cluster()->GetMasterAddresses();

  return source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, target_master_address);
}

Status XClusterYsqlTestBase::VerifyDDLExtensionTablesCreation(
    const NamespaceName& db_name, bool only_source) {
  return RunOnBothClusters([&](Cluster* cluster) -> Status {
    if (cluster == &consumer_cluster_ && only_source) {
      return Status::OK();
    }
    for (const auto& table_name :
         {xcluster::kDDLQueueTableName, xcluster::kDDLReplicatedTableName}) {
      auto yb_table_name = VERIFY_RESULT(
          GetYsqlTable(cluster, db_name, xcluster::kDDLQueuePgSchemaName, table_name));
      std::shared_ptr<client::YBTable> table;
      RETURN_NOT_OK(cluster->client_->OpenTable(yb_table_name, &table));
      SCHECK_EQ(table->GetPartitionCount(), 1, IllegalState, "Expected 1 tablet");
    }
    return Status::OK();
  });
}

Status XClusterYsqlTestBase::VerifyDDLExtensionTablesDeletion(
    const NamespaceName& db_name, bool only_source) {
  return RunOnBothClusters([&](Cluster* cluster) -> Status {
    if (cluster == &consumer_cluster_ && only_source) {
      return Status::OK();
    }
    for (const auto& table_name :
         {xcluster::kDDLQueueTableName, xcluster::kDDLReplicatedTableName}) {
      auto yb_table_name_result =
          GetYsqlTable(cluster, db_name, xcluster::kDDLQueuePgSchemaName, table_name);
      SCHECK(!yb_table_name_result.ok(), IllegalState, "Table $0 should not exist", table_name);
      SCHECK(
          yb_table_name_result.status().IsNotFound(), IllegalState, "Table $0 should not exist: $1",
          table_name, yb_table_name_result.status());
    }
    return Status::OK();
  });
}

Status XClusterYsqlTestBase::EnablePITROnClusters() {
  return RunOnBothClusters([this](Cluster* cluster) -> Status {
    client::SnapshotTestUtil snapshot_util;
    snapshot_util.SetProxy(&cluster->client_->proxy_cache());
    snapshot_util.SetCluster(cluster->mini_cluster_.get());

    RETURN_NOT_OK(snapshot_util.CreateSchedule(
        nullptr, YQL_DATABASE_PGSQL, namespace_name, client::WaitSnapshot::kTrue,
        2s * kTimeMultiplier, 20h));
    return Status::OK();
  });
}

Status XClusterYsqlTestBase::PerformPITROnConsumerCluster(HybridTime time) {
  auto yb_admin_client = std::make_unique<tools::ClusterAdminClient>(
      consumer_cluster_.mini_cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(30));
  RETURN_NOT_OK(yb_admin_client->Init());
  auto j = VERIFY_RESULT(yb_admin_client->ListSnapshotSchedules(SnapshotScheduleId::Nil()));
  auto snapshot_schedule_id =
      VERIFY_RESULT(SnapshotScheduleIdFromString(j["schedules"].GetArray()[0]["id"].GetString()));
  auto sink = RegexWaiterLogSink(".*Marking restoration.*as complete in sys catalog");
  RETURN_NOT_OK(yb_admin_client->RestoreSnapshotSchedule(snapshot_schedule_id, time));
  RETURN_NOT_OK(sink.WaitFor(300s));
  LOG(INFO) << "PITR has been completed";
  return Status::OK();
}

Result<YBTableName> XClusterYsqlTestBase::CreateMaterializedView(
    Cluster& cluster, const YBTableName& table) {
  auto conn = EXPECT_RESULT(cluster.ConnectToDB(table.namespace_name()));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "CREATE MATERIALIZED VIEW $0_mv AS SELECT COUNT(*) FROM $0", table.table_name()));
  return GetYsqlTable(
      &cluster, table.namespace_name(), table.pgschema_name(), table.table_name() + "_mv");
}

}  // namespace yb
