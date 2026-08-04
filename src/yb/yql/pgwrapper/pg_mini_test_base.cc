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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/client/yb_table_name.h"

#include "yb/gutil/casts.h"

#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/metrics.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(enable_wait_queues);
DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_bool(ysql_disable_index_backfill);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_string(pgsql_proxy_bind_address);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(ysql_num_tablets);
DECLARE_int32(ysql_num_shards_per_tserver);
DECLARE_string(ysql_pg_conf_csv);

namespace yb::pgwrapper {

void PgMiniTestBase::DoTearDown() {
  if (pg_supervisor_) {
    pg_supervisor_->Stop();
  }
  YBMiniClusterTestBase::DoTearDown();
}

void PgMiniTestBase::SetUp() {
  HybridTime::TEST_SetPrettyToString(true);

  EnableYSQLFlags();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 120000 * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_hide_pg_catalog_table_creation_logs) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_disable_index_backfill) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_num_shards_per_tserver) = 1;

  // Make sure ysql_num_tablets are not specified to rely on other shard-related flags.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_num_tablets) = -1;

  master::SetDefaultInitialSysCatalogSnapshotFlags();
  MiniClusterTestWithClient::SetUp();

  MiniClusterOptions mini_cluster_opt = MiniClusterOptions{
      .num_masters = NumMasters(),
      .num_tablet_servers = NumTabletServers(),
      .num_drives = 1,
      .master_env = env_.get()};
  OverrideMiniClusterOptions(&mini_cluster_opt);
  cluster_ = std::make_unique<MiniCluster>(mini_cluster_opt);

  const auto pg_addr = server::TEST_RpcAddress(kPgTsIndex + 1, server::Private::kTrue);
  auto pg_port = cluster_->AllocateFreePort();
  auto host_port = HostPort(pg_addr, pg_port);
  // The 'pgsql_proxy_bind_address' flag must be set before starting the cluster. Each
  // tserver will store this address when it starts. Setting the 'pgsql_proxy_bind_address' flag
  // is needed for tserver local PG connections.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_bind_address) = host_port.ToString();

  cluster_->SetPgTServerSelected(kPgTsIndex, host_port);

  ASSERT_OK(cluster_->Start(ExtraTServerOptions()));

  ASSERT_OK(SetupPGCallbacksAndStartPG(pg_port, kPgTsIndex));
  DontVerifyClusterBeforeNextTearDown();

  ASSERT_OK(MiniClusterTestWithClient<MiniCluster>::CreateClient());
}

Result<TableId> PgMiniTestBase::GetTableIDFromTableName(const std::string& table_name) {
  // Get YBClient handler and tablet ID. Using this we can get the number of tablets before starting
  // the test and before the test ends. With this we can ensure that tablet splitting has occurred.
  const auto tables = VERIFY_RESULT(client_->ListTables());
  for (const auto& table : tables) {
    if (table.has_table() && table.table_name() == table_name) {
      return table.table_id();
    }
  }
  return STATUS_FORMAT(NotFound, "Didn't find table with name: $0.", table_name);
}

Result<master::CatalogManagerIf*> PgMiniTestBase::catalog_manager() const {
  return &CHECK_NOTNULL(VERIFY_RESULT(cluster_->GetLeaderMiniMaster()))->catalog_manager();
}

Result<master::CatalogManager*> PgMiniTestBase::catalog_manager_impl() const {
  return &CHECK_NOTNULL(VERIFY_RESULT(cluster_->GetLeaderMiniMaster()))->catalog_manager_impl();
}

void PgMiniTestBase::EnableYSQLFlags() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_auto_run_initdb) = true;
}

Result<PgProcessConf> PgMiniTestBase::CreatePgProcessConf(uint16_t port, size_t ts_idx) {
  auto* pg_ts = cluster_->mini_tablet_server(ts_idx);
  PgProcessConf pg_process_conf = VERIFY_RESULT(PgProcessConf::CreateValidateAndRunInitDb(
      AsString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
      pg_ts->options()->fs_opts.data_paths.front() + "/pg_data"));

  pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
  pg_process_conf.force_disable_log_file = true;
  pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
  cluster_->SetYsqlHostport(pg_host_port());

  return pg_process_conf;
}

Status PgMiniTestBase::SetupPGCallbacksAndStartPG(uint16_t pg_port, size_t pg_ts_idx) {
  RETURN_NOT_OK(WaitForInitDb(cluster_.get()));

  auto pg_process_conf = VERIFY_RESULT(CreatePgProcessConf(pg_port, pg_ts_idx));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_webserver_port) = cluster_->AllocateFreePort();

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses << ":"
            << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
            << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;

  BeforePgProcessStart();

  auto pg_ts = cluster_->mini_tablet_server(pg_ts_idx);
  pg_ts->SetPgServerHandlers(
      [this] { return StartPostgres(); }, [this] { StopPostgres(); },
      [this] { return MakeConnSettings(); });
  return pg_ts->StartPgIfConfigured();
}

Status PgMiniTestBase::RecreatePgSupervisor() {
  pg_supervisor_ = std::make_unique<PgSupervisor>(
      VERIFY_RESULT(CreatePgProcessConf(pg_host_port_.port(), kPgTsIndex)),
      cluster_->mini_tablet_server(kPgTsIndex)->server());
  return Status::OK();
}

Status PgMiniTestBase::RestartCluster() {
  // Postgres will get stopped/started when the corresponding tserver is, so no need to
  // explicitly restart it here.
  return cluster_->RestartSync();
}

Status PgMiniTestBase::RestartMaster() {
  LOG(INFO) << "Restarting Master";
  auto mini_master_ = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
  RETURN_NOT_OK(mini_master_->Restart());
  return mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests();
}

void PgMiniTestBase::StopPostgres() {
  LOG(INFO) << "Stopping PostgreSQL server";
  pg_supervisor_->Stop();
  pg_supervisor_ = nullptr;
}

Status PgMiniTestBase::StartPostgres() {
  LOG(INFO) << "Starting PostgreSQL server";
  RETURN_NOT_OK(RecreatePgSupervisor());
  return pg_supervisor_->StartAndMaybePause();
}

Status PgMiniTestBase::RestartPostgres() {
  return cluster_->mini_tablet_server(kPgTsIndex)->server()->RestartPG();
}

void PgMiniTestBase::OverrideMiniClusterOptions(MiniClusterOptions* options) {}

std::vector<tserver::TabletServerOptions> PgMiniTestBase::ExtraTServerOptions() {
  return std::vector<tserver::TabletServerOptions>();
}

void PgMiniTestBase::FlushAndCompactTablets() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
  const auto compaction_start = MonoTime::Now();
  ASSERT_OK(cluster_->CompactTablets());
  const auto compaction_finish = MonoTime::Now();
  const double compaction_elapsed_time_sec = (compaction_finish - compaction_start).ToSeconds();
  LOG(INFO) << "Compaction duration: " << compaction_elapsed_time_sec << " s";
}

PGConnSettings PgMiniTestBase::MakeConnSettings(const std::string& dbname) const {
  return PGConnSettings {
    .host = pg_host_port_.host(),
    .port = pg_host_port_.port(),
    .dbname = dbname
  };
}

Result<PGConn> PgMiniTestBase::ConnectToDB(const std::string& dbname, size_t timeout) const {
  auto settings = MakeConnSettings(dbname);
  settings.connect_timeout = timeout;
  auto result = VERIFY_RESULT(PGConnBuilder(settings).Connect());
  RETURN_NOT_OK(SetupConnection(&result));
  return result;
}

Status PgMiniTestBase::SetupConnection(PGConn* conn) const {
  return Status::OK();
}

void PgMiniTestBase::EnableFailOnConflict() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = false;
  // Set the number of retries to 2 to speed up the test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = MaxQueryLayerRetriesConf(2);
}

} // namespace yb::pgwrapper
