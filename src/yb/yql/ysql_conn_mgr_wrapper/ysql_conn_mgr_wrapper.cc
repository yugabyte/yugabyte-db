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
#include "yb/util/flags/flags_callback.h"
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
DECLARE_int32(ysql_conn_mgr_max_query_size);
DECLARE_int32(ysql_conn_mgr_wait_timeout_ms);
DECLARE_int32(ysql_conn_mgr_max_pools);
DECLARE_uint32(TEST_ysql_conn_mgr_auth_delay_ms);

// TODO(janand) : GH #17837  Find the optimum value for `ysql_conn_mgr_idle_time`.
DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_idle_time, 180,
    "Specifies the maximum idle (secs) time allowed for database connections created by "
    "the Ysql Connection Manager. If a database connection remains idle without serving a "
    "client connection for a duration equal to or exceeding the value provided, "
    "it will be automatically closed by the Ysql Connection Manager.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_num_workers, 0,
  "Number of worker threads used by Ysql Connection Manager. If set as 0 (default value), "
  "the number of worker threads will be half of the number of CPU cores.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_max_conns_per_db, 0,
    "Maximum number of concurrent database connections Ysql Connection Manager can create per "
    "pool.");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_internal_conn_user, "yugabyte",
    "Username to be used by Ysql Connection Manager while creating database connections.");

DEPRECATE_FLAG(string, ysql_conn_mgr_username, "04_2026");
DEPRECATE_FLAG(string, ysql_conn_mgr_password, "04_2026");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_internal_conn_db, "yugabyte",
    "Database to which Ysql Connection Manager will make connections to "
    "inorder to execute internal queries.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_dowarmup, false,
  "Enable precreation of server connections in Ysql Connection Manager. If set false, "
  "the server connections are created lazily (on-demand) in Ysql Connection Manager.");

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

DEFINE_RUNTIME_CONN_MGR_FLAG(uint32, max_prepared_statements, 500,
    "Soft limit on prepared statements per server connection. When the limit is exceeded, the"
    "least recently used statements are closed on the backend. This is enforced periodically at "
    "connection detach points, so the actual count may temporarily exceed this value. Set to 0 "
    "to disable LRU eviction.");

DEFINE_RUNTIME_CONN_MGR_FLAG(string, log_settings, "",
    "Comma-separated list of log settings for Ysql Connection Manger, which may include "
    "'log_debug', 'log_config', 'log_session', 'log_query', and 'log_stats'. Only the "
    "log settings present in this string will be enabled. Omitted settings will remain disabled.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_use_auth_backend, true,
    "Enable the use of the auth-backend for authentication of logical connections. "
    "When false, the older auth-passthrough implementation is used."
    );

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_auth_msg_timeout, 15000,
    "Maximum time (in milliseconds) to wait for each startup & auth message from client. "
    "If the client does not send a startup or auth packet/response within this timeout, the "
    "connection is closed. Note that this is a per-message timeout, not a total timeout for the "
    "entire auth process. This timeout applies individually to the TLS handshake, startup packet, "
    "and each auth packet received from the client.");

DEFINE_NON_RUNTIME_uint64(ysql_conn_mgr_log_max_size, 0,
    "Max ysql connection manager log size(in bytes) after which the log file gets rolled over");

DEFINE_NON_RUNTIME_uint64(ysql_conn_mgr_log_rotate_interval, 0,
    "Duration(in secs) after which ysql connection manager log will get rolled over");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_readahead_buffer_size, 8192,
    "Set size of per-connection buffer used for io readahead operations in "
    "Ysql Connection Manager");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_tcp_keepalive, 15,
    "TCP keepalive time in Ysql Connection Manager. Set to zero, to disable keepalive");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_tcp_keepalive_keep_interval, 75,
    "TCP keepalive interval in Ysql Connection Manager. This is applicable if "
    "'ysql_conn_mgr_tcp_keepalive' is enabled.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_tcp_keepalive_probes, 9,
    "TCP keepalive probes in Ysql Connection Manager. This is applicable if "
    "'ysql_conn_mgr_tcp_keepalive' is enabled.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_tcp_keepalive_usr_timeout, 0,
    "TCP user timeout in Ysql Connection Manager. This is applicable if "
    "'ysql_conn_mgr_tcp_keepalive' is enabled.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_control_connection_pool_size, 0,
    "Maximum number of concurrent control connections in Ysql Connection Manager. "
    "If the value is zero, the default value is 0.1 * ysql_max_connections");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_pool_timeout, 0,
    "Server pool wait timeout(in ms) in Ysql Connection Manager. Time to wait in "
    "milliseconds for an available server. Disconnect client on timeout reach. "
    "If the value is set to zero, the client waits for the server connection indefinitely");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_optimized_extended_query_protocol, true,
    "Enable optimized extended query protocol in Ysql Connection Manager. "
    "If set to false, extended query protocol handling is fully correct but unoptimized.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_enable_prep_stmt_close, true,
    "When enabled, the YSQL Connection Manager forwards Close messages to the backend, which "
    "drops the prepared statement only if its cached plan is invalid or the connection is sticky; "
    "valid plans on non-sticky connections are retained for reuse across logical connections. "
    "When disabled, Close messages are handled as a no-op by the connection manager itself "
    "and never reach the backend, which can cause errors. "
    "Requires ysql_conn_mgr_optimized_extended_query_protocol to be enabled.");

DEPRECATE_FLAG(bool, ysql_conn_mgr_deallocate_if_invalid_prep_stmt, "04_2026");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_enable_multi_route_pool, true,
    "Enable the use of the dynamic multi-route pooling. "
    "When false, the older static pool sizes are used."
    );

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_optimized_session_parameters, true,
    "Optimize usage of session parameters in Ysql Connection Manager. "
    "If set to false, session parameters are replayed at transaction boundaries for each "
    "logical connection.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_jitter_time, 120,
    "Specifies the jitter duration in seconds upto which an idle physical connection may be kept "
    "open beyond its idle timeout duration. This is to avoid sudden bursts of connections closing "
    "when dealing with connection burst sceanrios.");

DEPRECATE_FLAG(uint32, ysql_conn_mgr_max_phy_conn_percent, "12_2025");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_reserve_internal_conns, 15,
  "This flag specifies the number of physical connections to reserve from ysql_max_connections"
  "for internal operations that bypass the YSQL Connection Manager. The remaining connections"
  "are then available for the connection manager's pool. For example, if ysql_max_connections"
  "is 300 and this flag is set to its default of 15, the YSQL Connection Manager will have a"
  "physical connection limit of 285 (300 - 15).");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_dump_heap_snapshot_interval, 0,
    "Dump tcmalloc current heap snapshot of Ysql Connection Manager process. "
    "If set to greater than 0, tcmalloc current heap snapshot will be dumped to the conn mgr "
    "logs after every ysql_conn_mgr_dump_heap_snapshot_interval number of seconds.");

DEFINE_RUNTIME_CONN_MGR_FLAG(bool, enable_parse_queue_tracking, true,
    "Enables tracking of in-flight Parse operations in the YSQL Connection Manager. "
    "This is used so that prepared-statement state tracked on the Connection Manager can be "
    "reconciled with the backend when errors disrupt the expected packet sequence. When "
    "disabled, the Connection Manager's view of prepared statements can drift out of sync with "
    "the backend, which may surface as errors such as 'prepared statement does not exist'.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_tcmalloc_sample_period, 1024 * 1024,
    "Sets the interval at which TCMalloc should sample allocations for connection manager. "
    "Sampling is disabled if this is set to 0. This flag will only be in effect if "
    "ysql_conn_mgr_dump_heap_snapshot_interval is set to greater than 0 i.e if dumping heap "
    "snapshots is enabled. Otherwise we keep the sample period same as what google tcmalloc "
    "has set for connection manager process");

DEFINE_RUNTIME_CONN_MGR_FLAG(uint32, tcmalloc_gc_interval, 300,
    "Interval in seconds between tcmalloc page-heap GC checks in Ysql Connection Manager. "
    "Each tick invokes yb::MemTracker::GcTcmallocIfNeeded(); if the pageheap free-list "
    "overhead exceeds tcmalloc_max_free_bytes_percentage of currently-allocated bytes, the "
    "excess is returned to the OS. Set to 0 to disable periodic GC.");

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

DEFINE_validator(ysql_conn_mgr_enable_prep_stmt_close,
    FLAG_REQUIRES_FLAG_VALIDATOR(ysql_conn_mgr_optimized_extended_query_protocol));

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
      ysql_conn_mgr_executable, VERIFY_RESULT(conf_.CreateYsqlConnMgrConfigAndGetPath())};
  proc_.emplace(ysql_conn_mgr_executable, argv);
  proc_->SetEnv("YB_YSQLCONNMGR_PDEATHSIG", Format("$0", SIGINT));

  if (getenv("YB_YSQL_CONN_MGR_USER") == NULL) {
    proc_->SetEnv("YB_YSQL_CONN_MGR_USER", FLAGS_ysql_conn_mgr_internal_conn_user);
  }

  if (getenv("YB_YSQL_CONN_MGR_DB") == NULL) {
    proc_->SetEnv("YB_YSQL_CONN_MGR_DB", FLAGS_ysql_conn_mgr_internal_conn_db);
  }

  proc_->SetEnv("YB_YSQL_CONN_MGR_PASSWORD", UInt64ToString(conf_.yb_tserver_key()));

  proc_->SetEnv("YB_YSQL_CONN_MGR_DOWARMUP", FLAGS_ysql_conn_mgr_dowarmup ? "true" : "false");

  proc_->SetEnv("YB_YSQL_CONN_MGR_DOWARMUP_ALL_POOLS_MODE",
                FLAGS_TEST_ysql_conn_mgr_dowarmup_all_pools_mode);

  proc_->SetEnv(
      "YB_YSQL_CONN_MGR_MAX_QUERY_SIZE", std::to_string(FLAGS_ysql_conn_mgr_max_query_size));

  proc_->SetEnv(
      "YB_YSQL_CONN_MGR_WAIT_TIMEOUT_MS", std::to_string(FLAGS_ysql_conn_mgr_wait_timeout_ms));

  proc_->SetEnv(
      "YB_YSQL_CONN_MGR_MAX_POOLS", std::to_string(FLAGS_ysql_conn_mgr_max_pools));

  unsetenv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME);
  if (FLAGS_enable_ysql_conn_mgr_stats) {
    if (stat_shm_key_ <= 0) return STATUS(InternalError, "Invalid stats shared memory key.");

    LOG(INFO) << Format(
        "Using shared memory segment with key $0 for collecting Ysql Connection Manager stats",
        stat_shm_key_);

    proc_->SetEnv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME, std::to_string(stat_shm_key_));
  }

  unsetenv("YB_YSQL_CONN_MGR_DUMP_HEAP_SNAPSHOT_INTERVAL");
  if (FLAGS_ysql_conn_mgr_dump_heap_snapshot_interval > 0) {
    proc_->SetEnv("YB_YSQL_CONN_MGR_DUMP_HEAP_SNAPSHOT_INTERVAL",
      std::to_string(FLAGS_ysql_conn_mgr_dump_heap_snapshot_interval));
    LOG(INFO) << "TCMalloc heap snapshot is dumped to the conn mgr logs after every "
              << FLAGS_ysql_conn_mgr_dump_heap_snapshot_interval << " seconds";
  }
  unsetenv("YB_YSQL_CONN_MGR_TCMALLOC_SAMPLE_PERIOD");
  if (FLAGS_ysql_conn_mgr_dump_heap_snapshot_interval > 0 &&
      FLAGS_ysql_conn_mgr_tcmalloc_sample_period > 0) {
    proc_->SetEnv("YB_YSQL_CONN_MGR_TCMALLOC_SAMPLE_PERIOD",
      std::to_string(FLAGS_ysql_conn_mgr_tcmalloc_sample_period));
    LOG(INFO) << "TCMalloc sample period is set to: "
              << FLAGS_ysql_conn_mgr_tcmalloc_sample_period << " bytes";
  }

  proc_->SetEnv(YSQL_CONN_MGR_WARMUP_DB, FLAGS_ysql_conn_mgr_warmup_db);

#ifdef THREAD_SANITIZER
  // Disable thread leak detection for the Ysql Connection Manager (Odyssey) process.
  // Worker threads may not be joined before exit(0) on SIGINT/SIGTERM shutdown.
  // This mirrors the same approach used for the PostgreSQL process in pg_wrapper.cc.
  {
    static const std::string kTSANOptionsEnvName = "TSAN_OPTIONS";
    const char* tsan_options = getenv(kTSANOptionsEnvName.c_str());
    proc_->SetEnv(
        kTSANOptionsEnvName, std::string(tsan_options ? tsan_options : "") +
                                 " report_thread_leaks=0");
  }
#endif

  RETURN_NOT_OK(proc_->Start());

  LOG(INFO) << "Ysql Connection Manager process running as pid " << proc_->pid();


  return Status::OK();
}

Status YsqlConnMgrWrapper::ReloadConfig() {
  if (!proc_) {
    return STATUS(IllegalState, "Process has not been started");
  }
  return proc_->KillNoCheckIfRunning(SIGHUP);
}

Status YsqlConnMgrWrapper::UpdateAndReloadConfig() {
  RETURN_NOT_OK(conf_.CreateYsqlConnMgrConfigAndGetPath());
  return ReloadConfig();
}

YsqlConnMgrSupervisor::YsqlConnMgrSupervisor(const YsqlConnMgrConf& conf, key_t stat_shm_key)
    : conf_(conf), stat_shm_key_(stat_shm_key) {}

std::shared_ptr<ProcessWrapper> YsqlConnMgrSupervisor::CreateProcessWrapper() {
  return std::make_shared<YsqlConnMgrWrapper>(conf_, stat_shm_key_);
}

void YsqlConnMgrSupervisor::UpdateAndReloadConfig() {
  std::lock_guard lock(mtx_);
  if (process_wrapper_) {
    const Status status = process_wrapper_->UpdateAndReloadConfig();
    if (!status.ok()) {
      LOG(ERROR) << "Failed to update and reload config: " << status;
    }
  }
}

Status YsqlConnMgrSupervisor::RegisterReloadConfigCallback(const void* flag_ptr) {
  flag_callbacks_.emplace_back(VERIFY_RESULT(RegisterFlagUpdateCallback(
      flag_ptr, "ReloadConnMgrConfig",
      std::bind(&YsqlConnMgrSupervisor::UpdateAndReloadConfig, this))));
  return Status::OK();
}

Status YsqlConnMgrSupervisor::RegisterFlagChangeNotifications() {
  DeregisterFlagChangeNotifications();

  std::vector<google::CommandLineFlagInfo> flags;
  google::GetAllFlags(&flags);
  for (const auto& flag : flags) {
    std::unordered_set<FlagTag> tags;
    GetFlagTags(flag.name, &tags);
    if (tags.contains(FlagTag::kConnMgr)) {
      RETURN_NOT_OK(RegisterReloadConfigCallback(flag.flag_ptr));
    }
  }

  return Status::OK();
}

void YsqlConnMgrSupervisor::DeregisterFlagChangeNotifications() {
  for (auto& callback : flag_callbacks_) {
    callback.Deregister();
  }
  flag_callbacks_.clear();
}

Status YsqlConnMgrSupervisor::PrepareForStart() { return RegisterFlagChangeNotifications(); }

void YsqlConnMgrSupervisor::PrepareForStop() { DeregisterFlagChangeNotifications(); }

Status YsqlConnMgrSupervisor::StartIfNotStarted() {
  if (started_) {
    return Status::OK();
  }
  started_ = true;
  return ProcessSupervisor::Restart();
}

}  // namespace ysql_conn_mgr_wrapper
}  // namespace yb
