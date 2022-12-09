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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/client/yb_table_name.h"

#include "yb/gutil/casts.h"

#include "yb/master/sys_catalog_initialization.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/metrics.h"
#include "yb/util/tsan_util.h"

DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_bool(ysql_disable_index_backfill);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(pggate_rpc_timeout_secs);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_int32(ysql_num_shards_per_tserver);

namespace yb {
namespace pgwrapper {

void PgMiniTestBase::DoTearDown() {
  if (pg_supervisor_) {
    pg_supervisor_->Stop();
  }
  YBMiniClusterTestBase::DoTearDown();
}

void PgMiniTestBase::SetUp() {
  HybridTime::TEST_SetPrettyToString(true);

  FLAGS_client_read_write_timeout_ms = 120000 * kTimeMultiplier;
  FLAGS_enable_ysql = true;
  FLAGS_hide_pg_catalog_table_creation_logs = true;
  FLAGS_master_auto_run_initdb = true;
  FLAGS_pggate_rpc_timeout_secs = 120;
  FLAGS_ysql_disable_index_backfill = true;
  FLAGS_ysql_num_shards_per_tserver = 1;


  master::SetDefaultInitialSysCatalogSnapshotFlags();
  YBMiniClusterTestBase::SetUp();

  MiniClusterOptions mini_cluster_opt = MiniClusterOptions {
      .num_masters = NumMasters(),
      .num_tablet_servers = NumTabletServers(),
      .num_drives = 1,
      .master_env = env_.get()
  };
  OverrideMiniClusterOptions(&mini_cluster_opt);
  cluster_ = std::make_unique<MiniCluster>(mini_cluster_opt);
  ASSERT_OK(cluster_->Start(ExtraTServerOptions()));

  ASSERT_OK(WaitForInitDb(cluster_.get()));

  auto port = cluster_->AllocateFreePort();
  auto pg_process_conf = ASSERT_RESULT(CreatePgProcessConf(port));
  FLAGS_pgsql_proxy_webserver_port = cluster_->AllocateFreePort();

  LOG(INFO) << "Starting PostgreSQL server listening on "
            << pg_process_conf.listen_addresses << ":" << pg_process_conf.pg_port << ", data: "
            << pg_process_conf.data_dir
            << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;

  BeforePgProcessStart();
  pg_supervisor_ = std::make_unique<PgSupervisor>(pg_process_conf, nullptr /* tserver */);
  ASSERT_OK(pg_supervisor_->Start());

  DontVerifyClusterBeforeNextTearDown();
}

Result<TableId> PgMiniTestBase::GetTableIDFromTableName(const std::string table_name) {
  // Get YBClient handler and tablet ID. Using this we can get the number of tablets before starting
  // the test and before the test ends. With this we can ensure that tablet splitting has occurred.
  auto client = VERIFY_RESULT(cluster_->CreateClient());
  const auto tables = VERIFY_RESULT(client->ListTables());
  for (const auto& table : tables) {
    if (table.has_table() && table.table_name() == table_name) {
      return table.table_id();
    }
  }
  return STATUS_FORMAT(NotFound, "Didn't find table with name: $0.", table_name);
}

Result<PgProcessConf> PgMiniTestBase::CreatePgProcessConf(uint16_t port) {
  auto pg_ts = RandomElement(cluster_->mini_tablet_servers());
  PgProcessConf pg_process_conf = VERIFY_RESULT(PgProcessConf::CreateValidateAndRunInitDb(
      AsString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
      pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
      pg_ts->server()->GetSharedMemoryFd()));

  pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
  pg_process_conf.force_disable_log_file = true;
  pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);

  return pg_process_conf;
}

Status PgMiniTestBase::RestartCluster() {
  pg_supervisor_->Stop();
  RETURN_NOT_OK(cluster_->RestartSync());
  pg_supervisor_ = std::make_unique<PgSupervisor>(
      VERIFY_RESULT(CreatePgProcessConf(pg_host_port_.port())), nullptr /* tserver */);
  return pg_supervisor_->Start();
}

void PgMiniTestBase::OverrideMiniClusterOptions(MiniClusterOptions* options) {}

const std::shared_ptr<tserver::MiniTabletServer> PgMiniTestBase::PickPgTabletServer(
    const MiniCluster::MiniTabletServers& servers) {
  return RandomElement(servers);
}

MetricWatcher::MetricWatcher(
  const server::RpcServerBase& server, const MetricPrototype& metric)
    : server_(server), metric_(metric) {
}

Result<size_t> MetricWatcher::Delta(const DeltaFunctor& functor) const {
  auto initial_values = VERIFY_RESULT(GetMetricCount());
  RETURN_NOT_OK(functor());
  return VERIFY_RESULT(GetMetricCount()) - initial_values;
}

Result<size_t> MetricWatcher::GetMetricCount() const {
  const auto& metric_map = server_.metric_entity()->UnsafeMetricsMapForTests();
  auto item = metric_map.find(&metric_);
  SCHECK(item != metric_map.end(), IllegalState, "Metric not found");
  const auto& metric = *item->second;
  switch(metric.prototype()->type()) {
    case MetricType::kHistogram: return down_cast<const Histogram&>(metric).TotalCount();
    case MetricType::kCounter: return down_cast<const Counter&>(metric).value();

    case MetricType::kGauge: break;
    case MetricType::kLag: break;
  }
  return STATUS_FORMAT(IllegalState, "Unsupported metric type $0", metric.prototype()->type());
}

std::vector<tserver::TabletServerOptions> PgMiniTestBase::ExtraTServerOptions() {
  return std::vector<tserver::TabletServerOptions>();
}

} // namespace pgwrapper
} // namespace yb
