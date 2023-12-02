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

#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_initialization.h"
#include "yb/server/server_base.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/thread.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_int32(pggate_rpc_timeout_secs);
DECLARE_string(pgsql_proxy_bind_address);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_int32(replication_factor);
DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);

DECLARE_bool(TEST_create_table_with_empty_pgschema_name);
DECLARE_uint64(TEST_pg_auth_key);

namespace yb {

using OK = Status::OK;
using client::YBTableName;

void XClusterYsqlTestBase::SetUp() {
  YB_SKIP_TEST_IN_TSAN();
  XClusterTestBase::SetUp();
}

Status XClusterYsqlTestBase::Initialize(uint32_t replication_factor, uint32_t num_masters) {
  // In this test, the tservers in each cluster share the same postgres proxy. As each tserver
  // initializes, it will overwrite the auth key for the "postgres" user. Force an identical key
  // so that all tservers can authenticate as "postgres".
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pg_auth_key) = RandomUniformInt<uint64_t>();

  MiniClusterOptions opts;
  opts.num_tablet_servers = replication_factor;
  opts.num_masters = num_masters;

  RETURN_NOT_OK(InitClusters(opts));

  return Status::OK();
}

Status XClusterYsqlTestBase::InitClusters(const MiniClusterOptions& opts) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = static_cast<int>(opts.num_tablet_servers);
  // Disable tablet split for regular tests, see xcluster-tablet-split-itest for those tests.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = false;

  // Init postgres.
  master::SetDefaultInitialSysCatalogSnapshotFlags();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_hide_pg_catalog_table_creation_logs) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_auto_run_initdb) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pggate_rpc_timeout_secs) = 120;

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

  auto consumer_opts = opts;
  consumer_opts.cluster_id = "consumer";
  consumer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(consumer_opts);

  // Use a new pg proxy port for the consumer cluster.
  const uint16_t consumer_pg_port = consumer_cluster_.mini_cluster_->AllocateFreePort();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_bind_address) =
      Format("$0:$1", pg_addr, consumer_pg_port);

  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    RETURN_NOT_OK(consumer_cluster()->StartAsync());
  }

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
    RETURN_NOT_OK(InitPostgres(&producer_cluster_, pg_ts_idx, producer_pg_port));
  }

  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    RETURN_NOT_OK(InitPostgres(&consumer_cluster_, pg_ts_idx, consumer_pg_port));
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
          pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
          pg_ts->server()->GetSharedMemoryFd()));
  pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
  pg_process_conf.force_disable_log_file = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_webserver_port) =
      cluster->mini_cluster_->AllocateFreePort();

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses << ":"
            << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
            << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
  cluster->pg_supervisor_ =
      std::make_unique<pgwrapper::PgSupervisor>(pg_process_conf, nullptr /* tserver */);
  RETURN_NOT_OK(cluster->pg_supervisor_->Start());

  cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
  return OK();
}

std::string XClusterYsqlTestBase::GetCompleteTableName(const YBTableName& table) {
  // Append schema name before table name, if schema is available.
  return table.has_pgschema_name() ? Format("$0.$1", table.pgschema_name(), table.table_name())
                                   : table.table_name();
}

Result<std::string> XClusterYsqlTestBase::GetNamespaceId(YBClient* client) {
  master::GetNamespaceInfoResponsePB resp;

  RETURN_NOT_OK(
      client->GetNamespaceInfo({} /* namespace_id */, namespace_name, YQL_DATABASE_PGSQL, &resp));

  return resp.namespace_().id();
}

Result<YBTableName> XClusterYsqlTestBase::CreateYsqlTable(
    Cluster* cluster,
    const std::string& namespace_name,
    const std::string& schema_name,
    const std::string& table_name,
    const boost::optional<std::string>& tablegroup_name,
    uint32_t num_tablets,
    bool colocated,
    const ColocationId colocation_id,
    const bool ranged_partitioned) {
  auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
  std::string colocation_id_string = "";
  if (colocation_id > 0) {
    colocation_id_string = Format("colocation_id = $0", colocation_id);
  }
  if (!schema_name.empty()) {
    EXPECT_OK(conn.Execute(Format("CREATE SCHEMA IF NOT EXISTS $0;", schema_name)));
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
    std::string colocated_clause = Format("colocation = $0", colocated);
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
  EXPECT_OK(conn.Execute(query));

  // Only check the schema name if it is set AND we created the table with a valid pgschema_name.
  bool verify_schema_name =
      !schema_name.empty() && !FLAGS_TEST_create_table_with_empty_pgschema_name;
  return GetYsqlTable(
      cluster, namespace_name, schema_name, table_name, true /* verify_table_name */,
      verify_schema_name);
}

Result<YBTableName> XClusterYsqlTestBase::CreateYsqlTable(
    uint32_t idx, uint32_t num_tablets, Cluster* cluster,
    const boost::optional<std::string>& tablegroup_name, bool colocated,
    const bool ranged_partitioned) {
  // Generate colocation_id based on index so that we have the same colocation_id for
  // producer/consumer.
  const int colocation_id = (tablegroup_name.has_value() || colocated) ? (idx + 1) * 111111 : 0;
  return CreateYsqlTable(
      cluster, namespace_name, "" /* schema_name */, Format("test_table_$0", idx), tablegroup_name,
      num_tablets, colocated, colocation_id, ranged_partitioned);
}

Result<std::string> XClusterYsqlTestBase::GetUniverseId(Cluster* cluster) {
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

  // Now need to find the table and return it.
  for (const auto& table : resp.tables()) {
    // If !verify_table_name, just return the first table.
    if (!verify_table_name ||
        (table.name() == table_name && table.namespace_().name() == namespace_name)) {
      // In case of a match, further check for match in schema_name.
      if (!verify_schema_name || (!table.has_pgschema_name() && schema_name.empty()) ||
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
  return STATUS(
      IllegalState,
      strings::Substitute("Unable to find table $0 in namespace $1", table_name, namespace_name));
}

Status XClusterYsqlTestBase::DropYsqlTable(
    Cluster* cluster,
    const std::string& namespace_name,
    const std::string& schema_name,
    const std::string& table_name) {
  auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
  std::string full_table_name =
      schema_name.empty() ? table_name : Format("$0.$1", schema_name, table_name);
  std::string query = Format("DROP TABLE $0", full_table_name, kKeyColumnName);

  return conn.Execute(query);
}

void XClusterYsqlTestBase::WriteWorkload(
    const YBTableName& table, uint32_t start, uint32_t end, Cluster* cluster) {
  auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
  std::string table_name_str = GetCompleteTableName(table);

  LOG(INFO) << "Writing " << end - start << " inserts";

  // Use a transaction if more than 1 row is to be inserted.
  const bool use_tran = end - start > 1;
  if (use_tran) {
    EXPECT_OK(conn.ExecuteFormat("BEGIN"));
  }

  for (uint32_t i = start; i < end; i++) {
    EXPECT_OK(
        conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2)", table_name_str, kKeyColumnName, i));
  }

  if (use_tran) {
    EXPECT_OK(conn.ExecuteFormat("COMMIT"));
  }
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
    auto setting_res = VERIFY_RESULT(
        conn.FetchRowAsString("UPDATE pg_settings SET setting = 'tablet' WHERE name = "
                              "'yb_xcluster_consistency_level'"));
    SCHECK_EQ(
        setting_res, "tablet", IllegalState,
        "Failed to set yb_xcluster_consistency_level to tablet.");
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
    const YBTableName& producer_table_name, const YBTableName& consumer_table_name) {
  int prod_count = 0, cons_count = 0;
  const Status s = LoggedWaitFor(
      [this, producer_table_name, consumer_table_name, &prod_count, &cons_count]() -> Result<bool> {
        auto producer_results =
            VERIFY_RESULT(ScanToStrings(producer_table_name, &producer_cluster_));
        auto consumer_results =
            VERIFY_RESULT(ScanToStrings(consumer_table_name, &consumer_cluster_));
        prod_count = PQntuples(producer_results.get());
        cons_count = PQntuples(consumer_results.get());
        if (prod_count != cons_count) {
          return false;
        }
        for (int i = 0; i < prod_count; ++i) {
          auto prod_val = EXPECT_RESULT(pgwrapper::ToString(producer_results.get(), i, 0));
          auto cons_val = EXPECT_RESULT(pgwrapper::ToString(consumer_results.get(), i, 0));
          if (prod_val != cons_val) {
            return false;
          }
        }
        return true;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Verify written records");
  LOG(INFO) << "Row counts, Producer: " << prod_count << ", Consumer: " << cons_count;
  return s;
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
  CHECK(!resp.has_error());

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
    bool use_transaction) {
  if (!producer_table) {
    producer_table = producer_table_;
  }
  return WriteWorkload(
      start, end, &producer_cluster_, producer_table->name(), /* delete_op */ false,
      use_transaction);
}

Status XClusterYsqlTestBase::DeleteRowsInProducer(
    uint32_t start, uint32_t end, std::shared_ptr<client::YBTable> producer_table,
    bool use_transaction) {
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
    bool use_transaction) {
  auto conn = VERIFY_RESULT(cluster->ConnectToDB(table.namespace_name()));
  std::string table_name_str = GetCompleteTableName(table);

  LOG(INFO) << "Writing " << end - start << (delete_op ? " deletes" : " inserts")
            << " using transaction " << use_transaction;
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
  ASSERT_EQ(resp.entry().producer_id(), kReplicationGroupId);
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
  ASSERT_OK(VerifyWrittenRecords());

  // 5. Verify batch inserts are replicated correctly and can be read
  ASSERT_OK(InsertGenerateSeriesOnProducer(52, 100));
  ASSERT_OK(VerifyWrittenRecords());

  // Verify transactional inserts are replicated correctly and can be read
  ASSERT_OK(InsertTransactionalBatchOnProducer(101, 150));
  ASSERT_OK(VerifyWrittenRecords());

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
  ASSERT_OK(VerifyWrittenRecords());
}
}  // namespace yb
