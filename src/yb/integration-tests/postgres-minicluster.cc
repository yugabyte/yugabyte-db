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

#include "yb/client/client.h"

#include "yb/integration-tests/postgres-minicluster.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_int32(pgsql_proxy_webserver_port);

namespace yb {

Status PostgresMiniCluster::InitPostgres() {
  auto pg_ts_idx = RandomUniformInt<size_t>(0, mini_cluster_->num_tablet_servers() - 1);
  auto pg_ts = mini_cluster_->mini_tablet_server(pg_ts_idx);
  auto pg_port = pg_ts->server()->pgsql_proxy_bind_address().port();
  return InitPostgres(pg_ts_idx, pg_port);
}

Status PostgresMiniCluster::InitPostgres(size_t pg_ts_idx, uint16_t pg_port) {
  pg_ts_idx_ = pg_ts_idx;
  pg_port_ = pg_port;

  auto pg_ts = mini_cluster_->mini_tablet_server(pg_ts_idx_);
  pgwrapper::PgProcessConf pg_process_conf =
      VERIFY_RESULT(pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
          AsString(Endpoint(pg_ts->bound_rpc_addr().address(), pg_port)),
          pg_ts->options()->fs_opts.data_paths.front() + "/pg_data"));
  pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
  pg_process_conf.force_disable_log_file = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_webserver_port) = mini_cluster_->AllocateFreePort();

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses << ":"
            << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
            << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
  pg_supervisor_ = std::make_unique<pgwrapper::PgSupervisor>(pg_process_conf, pg_ts->server());
  RETURN_NOT_OK(pg_supervisor_->StartAndMaybePause());

  pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
  pg_ts->SetPgServerHandlers(
      [this] { return InitPostgres(pg_ts_idx_, pg_port_); }, [this] { ShutdownPostgres(); },
      [this] { return CreatePgConnSettings(); });
  return Status::OK();
}

void PostgresMiniCluster::ShutdownPostgres() { pg_supervisor_->Stop(); }

Result<pgwrapper::PGConn> PostgresMiniCluster::Connect() {
  return ConnectToDB(std::string() /* dbname */);
}

Result<pgwrapper::PGConn> PostgresMiniCluster::ConnectToDB(
    const std::string& dbname, bool simple_query_protocol) {
  auto settings = CreatePgConnSettings();
  settings.dbname = dbname;
  return pgwrapper::PGConnBuilder(
             {.host = pg_host_port_.host(), .port = pg_host_port_.port(), .dbname = dbname})
      .Connect(simple_query_protocol);
}

Result<pgwrapper::PGConn> PostgresMiniCluster::ConnectToDBWithReplication(
    const std::string& dbname) {
  auto settings = CreatePgConnSettings();
  settings.dbname = dbname;
  settings.replication = "database";
  return pgwrapper::PGConnBuilder(std::move(settings)).Connect(/*simple_query_protocol=*/true);
}

pgwrapper::PGConnSettings PostgresMiniCluster::CreatePgConnSettings() {
  pgwrapper::PGConnSettings settings;
  settings.host = pg_host_port_.host();
  settings.port = pg_host_port_.port();
  return settings;
}

}  // namespace yb
