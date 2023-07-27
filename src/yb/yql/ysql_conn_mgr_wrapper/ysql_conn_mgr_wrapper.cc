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

DECLARE_bool(enable_ysql);
DECLARE_bool(start_pgsql_proxy);

DEFINE_NON_RUNTIME_bool(enable_ysql_conn_mgr, false,
    "Enable Ysql Connection Manager for the cluster. Tablet Server will start a "
    "Ysql Connection Manager process as a child process.");

// TODO(janand) : GH #17837  Find the optimum value for `ysql_conn_mgr_idle_time`.
DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_idle_time, 3600,
    "Specifies the maximum idle (secs) time allowed for database connections created by "
    "the Ysql Connection Manager. If a database connection remains idle without serving a "
    "client connection for a duration equal to or exceeding the value provided, "
    "it will be automatically closed by the Ysql Connection Manager.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_max_client_connections, 10000,
    "Total number of concurrent client connections that the Ysql Connection Manager allows.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_num_workers, 0,
  "Number of worker threads used by Ysql Connection Manager. If set as 0 (default value), "
  "the number of worker threads will be half of the number of CPU cores.");

DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_pool_size, 70,
    "Total number of concurrent database connections Ysql Connection Manager can create. "
    "Apart from database connections for the global pool, "
    "a small number of database connections will also be created as control connections, "
    "which will be proportional to ysql_conn_mgr_pool_size.");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_username, "yugabyte",
    "Username to be used by Ysql Connection Manager while creating database connections.");

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_password, "yugabyte",
    "Password to be used by Ysql Connection Manager while creating database connections.");

DEFINE_NON_RUNTIME_bool(ysql_conn_mgr_dowarmup, true,
  "Enable precreation of server connections in Ysql Connection Manager. If set false, "
  "the server connections are created lazily (on-demand) in Ysql Connection Manager.");

namespace {

bool ValidateEnableYsqlConnMgr(const char* flagname, bool value) {
  if (!FLAGS_start_pgsql_proxy && !FLAGS_enable_ysql && value) {
    LOG(ERROR) << "Cannot start Ysql Connection Manager (YSQL is not enabled)";
    return false;
  }
  return true;
}

bool ValidatePoolSize(const char* flagname, uint32_t value) {
  if (value < 2) {
    LOG(ERROR) << flagname << "(" << value << ") can not be less than 2";
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
DEFINE_validator(ysql_conn_mgr_pool_size, &ValidatePoolSize);
DEFINE_validator(ysql_conn_mgr_max_client_connections, &ValidateMaxClientConn);

namespace yb {
namespace ysql_conn_mgr_wrapper {

YsqlConnMgrWrapper::YsqlConnMgrWrapper(const YsqlConnMgrConf& conf) : conf_(std::move(conf)) {}

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

  if (getenv("YB_YSQL_CONN_MGR_PASSWORD") == NULL) {
    proc_->SetEnv("YB_YSQL_CONN_MGR_PASSWORD", FLAGS_ysql_conn_mgr_password);
  }

  proc_->SetEnv("YB_YSQL_CONN_MGR_DOWARMUP", FLAGS_ysql_conn_mgr_dowarmup ? "true" : "false");

  RETURN_NOT_OK(proc_->Start());

  LOG(INFO) << "Ysql Connection Manager process running as pid " << proc_->pid();
  return Status::OK();
}

YsqlConnMgrSupervisor::YsqlConnMgrSupervisor(const YsqlConnMgrConf& conf)
    : conf_(std::move(conf)) {}

std::shared_ptr<ProcessWrapper> YsqlConnMgrSupervisor::CreateProcessWrapper() {
  return std::make_shared<YsqlConnMgrWrapper>(conf_);
}

}  // namespace ysql_conn_mgr_wrapper
}  // namespace yb
