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
#include "yb/master/ysql/ysql_initdb_major_upgrade_handler.h"

#include <signal.h>
#include <unistd.h>

#include "yb/server/server_base_options.h"

#include "yb/util/env.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"
#include "yb/util/pg_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(enable_ysql);
DECLARE_bool(enable_ysql_conn_mgr);
DECLARE_string(pgsql_proxy_bind_address);
DECLARE_string(rpc_bind_addresses);
DECLARE_bool(ysql_enable_auth);

DEFINE_RUNTIME_uint32(ysql_upgrade_postgres_port, 5434,
  "Port used to start the postgres process for ysql upgrade");

DEFINE_RUNTIME_string(ysql_major_upgrade_user, "yugabyte_upgrade",
    "The ysql user to use for ysql major upgrade operations when both:"
    " authentication is enabled, "
    " no yb-tserver process running on the yb-master nodes. "
    "This user should have superuser privileges and the password must be placed in the `.pgpass` "
    "file on all yb-master nodes.");

DEFINE_RUNTIME_bool(ysql_upgrade_import_stats, true,
    "Import relation and attribute stats as part of the YSQL Major upgrade");

DEFINE_test_flag(bool, kill_master_before_ysql_pg_upgrade, false,
    "SIGKILL the yb-master immediately before running the pg_upgrade binary");

DEFINE_test_flag(bool, fail_ysql_pg_upgrade, false,
    "Fail PerformPgUpgrade immediately before running the pg_upgrade binary");

using yb::pgwrapper::PgWrapper;

namespace yb::master {

namespace {

Result<std::string> GetPgUpgradeBindAddress() {
  const auto& bind_address =
      FLAGS_pgsql_proxy_bind_address.empty() ? FLAGS_rpc_bind_addresses
                                             : FLAGS_pgsql_proxy_bind_address;
  SCHECK(
      !bind_address.empty(), IllegalState,
      "No bind address found. Either pgsql_proxy_bind_address or rpc_bind_addresses must be set.");
  return bind_address;
}

}  // namespace

Status CleanupStalePgUpgradeSocketDir() {
  if (!FLAGS_enable_ysql) {
    return Status::OK();
  }

  auto host_port = VERIFY_RESULT(HostPort::FromString(
      VERIFY_RESULT(GetPgUpgradeBindAddress()), pgwrapper::PgProcessConf::kDefaultPort));
  host_port.set_port(FLAGS_ysql_upgrade_postgres_port);
  const auto socket_dir = PgDeriveSocketDir(host_port);

  auto* env = Env::Default();
  if (!env->DirExists(socket_dir)) {
    return Status::OK();
  }

  RETURN_NOT_OK(PgWrapper::CleanupLockFileAndKillHungPg(PgDeriveSocketLockFile(host_port)));

  LOG(INFO) << "Removing stale pg_upgrade socket directory " << socket_dir;
  return env->DeleteRecursively(socket_dir);
}

Status PerformYsqlPgUpgrade(
    pgwrapper::PgWrapperContext& server, const server::ServerBaseOptions& options,
    FsManager& fs_manager, HostPort closest_ts_hp, bool local_ts,
    const std::function<Result<std::optional<HostPort>>()>& get_pg_socket_dir) {
  const auto pg_upgrade_data_dir =
      JoinPathSegments(options.fs_opts.data_paths.front(), "pg_upgrade_data");

  RETURN_NOT_OK(PgWrapper::CleanupPgData(pg_upgrade_data_dir));

  auto bind_address = VERIFY_RESULT(GetPgUpgradeBindAddress());

  // Run local initdb to prepare the node for starting postgres.
  auto pg_conf = VERIFY_RESULT(pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
      bind_address, pg_upgrade_data_dir));

  pg_conf.master_addresses = options.master_addresses_flag;
  pg_conf.pg_port = FLAGS_ysql_upgrade_postgres_port;
  pg_conf.run_in_binary_upgrade = true;
  RETURN_NOT_OK(pg_conf.SetSslConf(options, fs_manager));

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_conf.listen_addresses << ", port "
            << pg_conf.pg_port;

  pgwrapper::PgSupervisor pg_supervisor(pg_conf, &server);
  auto se = ScopeExit([&pg_supervisor]() { pg_supervisor.Stop(); });
  RETURN_NOT_OK(pg_supervisor.Start());

  PgWrapper::PgUpgradeParams pg_upgrade_params;
  pg_upgrade_params.ysql_user_name = "yugabyte";
  pg_upgrade_params.data_dir = pg_conf.data_dir;
  pg_upgrade_params.new_version_socket_dir =
      PgDeriveSocketDir(HostPort(pg_conf.listen_addresses, pg_conf.pg_port));

  // Ensure the temporary socket directory used by pg_upgrade is cleaned up
  // when this function exits, even if it fails or returns early. This prevents
  // stale socket directories from being left behind and confusing subsequent checks.
  auto socket_dir_cleanup = ScopeExit([&pg_upgrade_params] {
    LOG(INFO) << "Cleaning up temporary socket directory: "
               << pg_upgrade_params.new_version_socket_dir;
    auto status = Env::Default()->DeleteRecursively(pg_upgrade_params.new_version_socket_dir);
    if (!status.ok()) {
      LOG(WARNING) << "Failed to clean up socket directory "
                   << pg_upgrade_params.new_version_socket_dir << ": " << status;
    }
  });
  pg_upgrade_params.new_version_pg_port = pg_conf.pg_port;
  pg_upgrade_params.no_statistics = !FLAGS_ysql_upgrade_import_stats;

  if (local_ts) {
    auto pg_socket_dir_opt = VERIFY_RESULT(get_pg_socket_dir());
    if (pg_socket_dir_opt) {
      closest_ts_hp = std::move(*pg_socket_dir_opt);
      pg_upgrade_params.old_version_socket_dir = closest_ts_hp.host();
    } else {
      // Tserver is running a version which does not support the RPC to get Pg local address. Do
      // best effort to guess the most likely address based on our own flags.

      // When pgsql_proxy_bind_address is set, it is used as the socket dir. The tserver does not
      // expose its pgsql_proxy_bind_address, but we expect master and tserver flags to be the same,
      // so we can use our flag value to infer the tservers socket dir.
      if (!FLAGS_pgsql_proxy_bind_address.empty()) {
        closest_ts_hp.set_host(pg_conf.listen_addresses);
      }
      if (FLAGS_enable_ysql_conn_mgr) {
        closest_ts_hp.set_port(pgwrapper::PgProcessConf::kDefaultPortWithConnMgr);
      }
      pg_upgrade_params.old_version_socket_dir = PgDeriveSocketDir(closest_ts_hp);
    }
  } else {
    // Remote tserver.
    pg_upgrade_params.old_version_pg_address = closest_ts_hp.host();

    if (FLAGS_ysql_enable_auth || pg_conf.enable_tls) {
      pg_upgrade_params.ysql_user_name = FLAGS_ysql_major_upgrade_user;
      LOG(INFO) << "Running ysql major upgrade on a authentication enabled universe which does not "
                   "have a yb-tserver on the same node as the yb-master. Upgrade will be performed "
                   "using yb-tserver hosted on "
                << closest_ts_hp << " by user " << pg_upgrade_params.ysql_user_name;
    }
  }
  pg_upgrade_params.old_version_pg_port = closest_ts_hp.port();

  if (FLAGS_TEST_kill_master_before_ysql_pg_upgrade) {
    LOG(INFO) << "TEST: Killing yb-master before running pg_upgrade";
    kill(getpid(), SIGKILL);
  }

  SCHECK(!FLAGS_TEST_fail_ysql_pg_upgrade, InternalError, "TEST: Injected pg_upgrade failure");

  RETURN_NOT_OK(PgWrapper::RunPgUpgrade(pg_upgrade_params));

  pg_supervisor.Stop();

  // We only want to clean up the upgrade data if it was successful. Otherwise,
  // we want to keep the data directory for debugging purposes.
  RETURN_NOT_OK(PgWrapper::CleanupPgData(pg_upgrade_data_dir));

  return Status::OK();
}

}  // namespace yb::master
