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

#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_wrapper.h"

#include <boost/algorithm/string.hpp>

#include "yb/util/env_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"
#include "yb/util/pg_util.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

DECLARE_bool(enable_ysql);
DECLARE_bool(start_pgsql_proxy);
DECLARE_bool(enable_ysql_conn_mgr);
DECLARE_bool(enable_ysql_conn_mgr_stats);
DECLARE_int32(ysql_max_connections);
DECLARE_string(ysql_conn_mgr_warmup_db);

// TODO(janand) : GH #17837  Find the optimum value for `ysql_conn_mgr_idle_time`.
DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_idle_time, 60,
    "Specifies the maximum idle (secs) time allowed for database connections created by "
    "the Ysql Connection Manager. If a database connection remains idle without serving a "
    "client connection for a duration equal to or exceeding the value provided, "
    "it will be automatically closed by the Ysql Connection Manager.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_max_client_connections, 10000,
    "Total number of concurrent client connections that the Ysql Connection Manager allows.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_num_workers, 0,
  "Number of worker threads used by Ysql Connection Manager. If set as 0 (default value), "
  "the number of worker threads will be half of the number of CPU cores.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_max_conns_per_db, 0,
    "Maximum number of concurrent database connections Ysql Connection Manager can create per "
    "pool.");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_username, "yugabyte",
    "Username to be used by Ysql Connection Manager while creating database connections.");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_password, "yugabyte",
    "Password to be used by Ysql Connection Manager while creating database connections.");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_internal_conn_db, "yugabyte",
    "Database to which Ysql Connection Manager will make connections to "
    "inorder to execute internal queries.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_dowarmup, false,
  "Enable precreation of server connections in Ysql Connection Manager. If set false, "
  "the server connections are created lazily (on-demand) in Ysql Connection Manager.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_stats_interval, 10,
  "Interval (in secs) at which the stats for Ysql Connection Manager will be updated.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_min_conns_per_db, 1,
    "Minimum number of physical connections, that will be present in pool. "
    "This limit is not considered while closing a broken physical connection.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_use_unix_conn, true,
    "Enable unix socket connections between Ysql Connection Manager and pg_backend. "
    "For pg_backend to accept unix socket connection by Ysql Connection Manager add "
    "'local all yugabyte trust' in hba.conf (set ysql_hba_conf_csv as 'local all yugabyte trust')."
    );

namespace {

bool ValidateEnableYsqlConnMgr(const char* flagname, bool value) {
  if (!FLAGS_start_pgsql_proxy && !FLAGS_enable_ysql && value) {
    LOG(ERROR) << "Cannot start Ysql Connection Manager (YSQL is not enabled)";
    return false;
  }
  return true;
}

bool ValidateMaxClientConn(const char* flagname, uint32_t value) {
  if (value < 1) {
    LOG(ERROR) << flagname << "(" << value << ") can not be less than 1";
    return false;
  }
  return true;
}

} // namespace

DEFINE_validator(enable_ysql_conn_mgr, &ValidateEnableYsqlConnMgr);
DEFINE_validator(ysql_conn_mgr_max_client_connections, &ValidateMaxClientConn);

namespace yb {
namespace ysql_conn_mgr_wrapper {

YsqlConnMgrWrapper::YsqlConnMgrWrapper(const YsqlConnMgrConf& conf, key_t stat_shm_key)
    : conf_(std::move(conf)), stat_shm_key_(std::move(stat_shm_key)) {}

std::string YsqlConnMgrWrapper::GetYsqlConnMgrExecutablePath() {
  return JoinPathSegments(yb::env_util::GetRootDir("bin"), "bin", "odyssey");
}

Status YsqlConnMgrWrapper::PreflightCheck() {
  return CheckExecutableValid(GetYsqlConnMgrExecutablePath());
}

Status YsqlConnMgrWrapper::Start() {
  auto ysql_conn_mgr_executable = GetYsqlConnMgrExecutablePath();
  RETURN_NOT_OK(CheckExecutableValid(ysql_conn_mgr_executable));

  std::vector<std::string> argv{
      ysql_conn_mgr_executable, conf_.CreateYsqlConnMgrConfigAndGetPath()};
  proc_.emplace(ysql_conn_mgr_executable, argv);
  proc_->SetEnv("YB_YSQLCONNMGR_PDEATHSIG", Format("$0", SIGINT));

  if (getenv("YB_YSQL_CONN_MGR_USER") == NULL) {
    proc_->SetEnv("YB_YSQL_CONN_MGR_USER", FLAGS_ysql_conn_mgr_username);
  }

  proc_->SetEnv("YB_YSQL_CONN_MGR_PASSWORD", conf_.yb_tserver_key_);

  proc_->SetEnv("YB_YSQL_CONN_MGR_DOWARMUP", FLAGS_ysql_conn_mgr_dowarmup ? "true" : "false");

  if (FLAGS_enable_ysql_conn_mgr_stats) {
    if (stat_shm_key_ <= 0) return STATUS(InternalError, "Invalid stats shared memory key.");

    LOG(INFO) << "Using shared memory segment with key " << stat_shm_key_
              << "for collecting Ysql Connection Manager stats";

    proc_->SetEnv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME, std::to_string(stat_shm_key_));
  }

  proc_->SetEnv(YSQL_CONN_MGR_WARMUP_DB, FLAGS_ysql_conn_mgr_warmup_db);
  RETURN_NOT_OK(proc_->Start());

  LOG(INFO) << "Ysql Connection Manager process running as pid " << proc_->pid();


  return Status::OK();
}

YsqlConnMgrSupervisor::YsqlConnMgrSupervisor(const YsqlConnMgrConf& conf, key_t stat_shm_key)
    : conf_(std::move(conf)), stat_shm_key_(std::move(stat_shm_key)) {}

std::shared_ptr<ProcessWrapper> YsqlConnMgrSupervisor::CreateProcessWrapper() {
  return std::make_shared<YsqlConnMgrWrapper>(conf_, stat_shm_key_);
}

}  // namespace ysql_conn_mgr_wrapper
}  // namespace yb
