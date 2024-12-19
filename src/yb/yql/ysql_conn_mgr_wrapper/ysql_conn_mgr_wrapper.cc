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
#include "yb/util/flag_validators.h"
#include "yb/util/net/net_util.h"
#include "yb/util/string_trim.h"
#include "yb/util/path_util.h"
#include "yb/util/pg_util.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

DECLARE_bool(enable_ysql_conn_mgr_stats);
DECLARE_int32(ysql_max_connections);
DECLARE_string(ysql_conn_mgr_warmup_db);
DECLARE_string(TEST_ysql_conn_mgr_dowarmup_all_pools_mode);
DECLARE_bool(ysql_conn_mgr_superuser_sticky);
DECLARE_bool(ysql_conn_mgr_version_matching);
DECLARE_bool(ysql_conn_mgr_version_matching_connect_higher_version);

// TODO(janand) : GH #17837  Find the optimum value for `ysql_conn_mgr_idle_time`.
DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_idle_time, 60,
    "Specifies the maximum idle (secs) time allowed for database connections created by "
    "the Ysql Connection Manager. If a database connection remains idle without serving a "
    "client connection for a duration equal to or exceeding the value provided, "
    "it will be automatically closed by the Ysql Connection Manager.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_max_client_connections, 10000,
    "Total number of concurrent client connections that the Ysql Connection Manager allows.");

DEFINE_validator(ysql_conn_mgr_max_client_connections, FLAG_GT_VALUE_VALIDATOR(1));

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

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_stats_interval, 1,
  "Interval (in secs) at which the stats for Ysql Connection Manager will be updated.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_min_conns_per_db, 1,
    "Minimum number of physical connections, that will be present in pool. "
    "This limit is not considered while closing a broken physical connection.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_use_unix_conn, true,
    "Enable unix socket connections between Ysql Connection Manager and pg_backend. "
    "For pg_backend to accept unix socket connection by Ysql Connection Manager add "
    "'local all yugabyte trust' in hba.conf (set ysql_hba_conf_csv as 'local all yugabyte trust')."
    );

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_server_lifetime, 3600,
    "Specifies the maximum duration (in seconds) that a backend PostgreSQL connection "
    "managed by Ysql Connection Manager can remain open after its creation. Once this time "
    "is reached, the connection is automatically closed, regardless of activity, ensuring that "
    "fresh backend connections are regularly maintained.");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_log_settings, "",
    "Comma-separated list of log settings for Ysql Connection Manger, which may include "
    "'log_debug', 'log_config', 'log_session', 'log_query', and 'log_stats'. Only the "
    "log settings present in this string will be enabled. Omitted settings will remain disabled.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_use_auth_backend, true,
    "Enable the use of the auth-backend for authentication of logical connections. "
    "When false, the older auth-passthrough implementation is used."
    );

DEFINE_NON_RUNTIME_uint64(ysql_conn_mgr_log_max_size, 0,
    "Max ysql connection manager log size(in bytes) after which the log file gets rolled over");

DEFINE_NON_RUNTIME_uint64(ysql_conn_mgr_log_rotate_interval, 0,
    "Duration(in secs) after which ysql connection manager log will get rolled over");

namespace {

bool ValidateLogSettings(const char* flag_name, const std::string& value) {
  const std::unordered_set<std::string> valid_settings = {
    "log_debug", "log_config", "log_session", "log_query", "log_stats"
  };

  std::stringstream ss(value);
  std::string setting;
  std::unordered_set<std::string> seen_settings;

  while (std::getline(ss, setting, ',')) {
    setting = yb::util::TrimStr(setting);

    if (setting.empty()) {
      continue;
    }
    if (valid_settings.find(setting) == valid_settings.end()) {
      LOG_FLAG_VALIDATION_ERROR(flag_name, value)
          << "Invalid log setting '" << setting << "'. Valid options are: "
          << "'log_debug', 'log_config', 'log_session', 'log_query', and 'log_stats'.";
      return false;
    }
  }
  return true;
}

} // namespace

DEFINE_validator(ysql_conn_mgr_log_settings, &ValidateLogSettings);

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

  if (FLAGS_TEST_ysql_conn_mgr_dowarmup_all_pools_mode != "none") {
    LOG(INFO) << "Warmup of server connections is enabled in ysql connection manager";
    if (FLAGS_TEST_ysql_conn_mgr_dowarmup_all_pools_mode == "random") {
      LOG(INFO) << "Random allotment of server connections is enabled in ysql connection manager";
    } else if (FLAGS_TEST_ysql_conn_mgr_dowarmup_all_pools_mode == "round_robin") {
      LOG(INFO) << "Round robin allotment of server connections is enabled in ysql connection "
                << "manager";
    }
    if (FLAGS_ysql_conn_mgr_min_conns_per_db < 3)
      FLAGS_ysql_conn_mgr_min_conns_per_db = 3;
  } else {
    LOG(INFO) << "Warmup of server connections is disabled in ysql connection manager";
  }

  if (FLAGS_ysql_conn_mgr_superuser_sticky)
    LOG(INFO) << "Superuser connections will be made sticky in ysql connection manager";

  std::vector<std::string> argv{
      ysql_conn_mgr_executable, conf_.CreateYsqlConnMgrConfigAndGetPath()};
  proc_.emplace(ysql_conn_mgr_executable, argv);
  proc_->SetEnv("YB_YSQLCONNMGR_PDEATHSIG", Format("$0", SIGINT));

  if (getenv("YB_YSQL_CONN_MGR_USER") == NULL) {
    proc_->SetEnv("YB_YSQL_CONN_MGR_USER", FLAGS_ysql_conn_mgr_username);
  }

  proc_->SetEnv("YB_YSQL_CONN_MGR_PASSWORD", conf_.yb_tserver_key_);

  proc_->SetEnv("YB_YSQL_CONN_MGR_DOWARMUP", FLAGS_ysql_conn_mgr_dowarmup ? "true" : "false");

  proc_->SetEnv("YB_YSQL_CONN_MGR_DOWARMUP_ALL_POOLS_MODE",
                FLAGS_TEST_ysql_conn_mgr_dowarmup_all_pools_mode);

  proc_->SetEnv(
      "YB_YSQL_CONN_MGR_VERSION_MATCHING", FLAGS_ysql_conn_mgr_version_matching ? "true" : "false");

  proc_->SetEnv(
      "YB_YSQL_CONN_MGR_VERSION_MATCHING_CONNECT_HIGHER_VERSION",
      FLAGS_ysql_conn_mgr_version_matching_connect_higher_version ? "true" : "false");

  unsetenv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME);
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
