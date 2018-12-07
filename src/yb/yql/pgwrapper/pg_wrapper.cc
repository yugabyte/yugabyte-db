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

#include "yb/yql/pgwrapper/pg_wrapper.h"

#include <vector>
#include <string>
#include <random>
#include <fstream>

#include <boost/scope_exit.hpp>
#include <gflags/gflags.h>

#include "yb/util/logging.h"
#include "yb/util/subprocess.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/errno.h"

DEFINE_string(pg_proxy_bind_address, "", "Address for the PostgreSQL proxy to bind to");

using std::vector;
using std::string;

using namespace std::literals;

namespace yb {
namespace pgwrapper {

string GetPostgresInstallRoot() {
  return JoinPathSegments(yb::env_util::GetRootDir("postgres"), "postgres");
}

Result<PgProcessConf> PgProcessConf::CreateValidateAndRunInitDb(
    const std::string& bind_addresses,
    const std::string& data_dir) {
  PgProcessConf conf;
  if (!bind_addresses.empty()) {
    auto pg_host_port = VERIFY_RESULT(HostPort::FromString(
        bind_addresses, PgProcessConf::kDefaultPort));
    conf.listen_addresses = pg_host_port.host();
    conf.pg_port = pg_host_port.port();
  }
  conf.data_dir = data_dir;
  PgWrapper pg_wrapper(conf);
  RETURN_NOT_OK(pg_wrapper.PreflightCheck());
  RETURN_NOT_OK(pg_wrapper.InitDbLocalOnlyIfNeeded());
  return conf;
}

// ------------------------------------------------------------------------------------------------
// PgWrapper: managing one instance of a PostgreSQL child process
// ------------------------------------------------------------------------------------------------

PgWrapper::PgWrapper(PgProcessConf conf)
    : conf_(std::move(conf)) {
}

Status PgWrapper::PreflightCheck() {
  RETURN_NOT_OK(CheckExecutableValid(GetPostgresExecutablePath()));
  RETURN_NOT_OK(CheckExecutableValid(GetInitDbExecutablePath()));
  return Status::OK();
}

Status PgWrapper::Start() {
  auto postgres_executable = GetPostgresExecutablePath();
  RETURN_NOT_OK(CheckExecutableValid(postgres_executable));
  vector<string> argv {
    postgres_executable,
    "-D",
    conf_.data_dir,
    "-p",
    std::to_string(conf_.pg_port),
    "-h",
    conf_.listen_addresses
  };

  pg_proc_.emplace(postgres_executable, argv);
  pg_proc_->ShareParentStderr();
  pg_proc_->ShareParentStdout();
  SetCommonEnv(&pg_proc_.get(), /* yb_enabled */ true);
  RETURN_NOT_OK(pg_proc_->Start());
  LOG(INFO) << "PostgreSQL server running as pid " << pg_proc_->pid();
  return Status::OK();
}

Status PgWrapper::InitDb(bool yb_enabled) {
  const string initdb_program_path = GetInitDbExecutablePath();
  RETURN_NOT_OK(CheckExecutableValid(initdb_program_path));
  if (!Env::Default()->FileExists(initdb_program_path)) {
    return STATUS_FORMAT(IOError, "initdb not found at: $0", initdb_program_path);
  }

  vector<string> initdb_args { initdb_program_path, "-D", conf_.data_dir, "-U", "postgres" };
  Subprocess initdb_subprocess(initdb_program_path, initdb_args);
  SetCommonEnv(&initdb_subprocess, yb_enabled);
  int exit_code = 0;
  RETURN_NOT_OK(initdb_subprocess.Start());
  RETURN_NOT_OK(initdb_subprocess.Wait(&exit_code));
  if (exit_code != 0) {
    return STATUS_FORMAT(RuntimeError, "$0 failed with exit code $1",
                         initdb_program_path,
                         exit_code);
  }

  {
    string hba_conf_path = JoinPathSegments(conf_.data_dir, "pg_hba.conf");
    std::ofstream hba_conf_file;

    hba_conf_file.open(hba_conf_path, std::ios_base::app);
    hba_conf_file << std::endl;
    hba_conf_file << "host all all 0.0.0.0/0 trust" << std::endl;
    hba_conf_file << "host all all ::0/0 trust" << std::endl;

    if (!hba_conf_file) {
      return STATUS(IOError, "Could not append additional lines to file " + hba_conf_path,
                    ErrnoToString(errno), errno);
    }
  }

  LOG(INFO) << "initdb completed successfully. Database initialized at " << conf_.data_dir;
  return Status::OK();
}

Status PgWrapper::InitDbLocalOnlyIfNeeded() {
  if (Env::Default()->FileExists(conf_.data_dir)) {
    LOG(INFO) << "Data directory " << conf_.data_dir << " already exists, skipping initdb";
    return Status::OK();
  }
  // Do not communicate with the YugaByte cluster at all. This function is only concerned with
  // setting up the local PostgreSQL data directory on this tablet server.
  return InitDb(/* yb_enabled */ false);
}

Result<int> PgWrapper::Wait() {
  if (!pg_proc_) {
    return STATUS(IllegalState,
                  "PostgreSQL child process has not been started, cannot wait for it to exit");
  }
  return pg_proc_->Wait();
}

Status PgWrapper::InitDbForYSQL(const string& master_addresses, const string& tmp_dir_base) {
  LOG(INFO) << "Running initdb to initialize YSQL cluster with master addresses "
            << master_addresses;
  PgProcessConf conf;
  conf.master_addresses = master_addresses;
  conf.pg_port = 0;  // We should not use this port.
  std::mt19937 rng{std::random_device()()};
  conf.data_dir = Format("$0/tmp_pg_data_$1", tmp_dir_base, rng());
  BOOST_SCOPE_EXIT(&conf) {
    Status del_status = Env::Default()->DeleteRecursively(conf.data_dir);
    if (!del_status.ok()) {
      LOG(WARNING) << "Failed to delete directory " << conf.data_dir;
    }
  } BOOST_SCOPE_EXIT_END;
  PgWrapper pg_wrapper(conf);
  return pg_wrapper.InitDb(/* yb_enabled */ true);
}

string PgWrapper::GetPostgresExecutablePath() {
  return JoinPathSegments(GetPostgresInstallRoot(), "bin", "postgres");
}

string PgWrapper::GetInitDbExecutablePath() {
  return JoinPathSegments(GetPostgresInstallRoot(), "bin", "initdb");
}

Status PgWrapper::CheckExecutableValid(const std::string& executable_path) {
  if (VERIFY_RESULT(Env::Default()->IsExecutableFile(executable_path))) {
    return Status::OK();
  }
  return STATUS_FORMAT(NotFound, "Not an executable file: $0", executable_path);
}

void PgWrapper::SetCommonEnv(Subprocess* proc, bool yb_enabled) {
  // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
  proc->SetEnv("YB_PG_FALLBACK_SYSTEM_USER_NAME", "postgres");
  proc->SetEnv("YB_PG_ALLOW_RUNNING_AS_ANY_USER", "1");
  if (yb_enabled) {
    proc->SetEnv("YB_ENABLED_IN_POSTGRES", "1");
    proc->SetEnv("FLAGS_pggate_master_addresses", conf_.master_addresses);

    // Pass non-default flags to the child process using FLAGS_... environment variables.
    std::vector<google::CommandLineFlagInfo> flag_infos;
    google::GetAllFlags(&flag_infos);
    for (const auto& flag_info : flag_infos) {
      string env_var_name = "FLAGS_" + flag_info.name;
      // We already set FLAGS_pggate_master_addresses explicitly above, based on
      // conf_.master_addresses and not based on FLAGS_pggate_masster_addresses, so skip it here.
      if (env_var_name != "FLAGS_pggate_master_addresses" && !flag_info.is_default) {
        proc->SetEnv(env_var_name, flag_info.current_value);
      }
    }
  }
}

// ------------------------------------------------------------------------------------------------
// PgSupervisor: monitoring a PostgreSQL child process and restarting if needed
// ------------------------------------------------------------------------------------------------

PgSupervisor::PgSupervisor(PgProcessConf conf)
    : conf_(std::move(conf)) {
}

Status PgSupervisor::Start() {
  std::lock_guard<std::mutex> lock(mtx_);
  RETURN_NOT_OK(ExpectStateUnlocked(PgProcessState::kNotStarted));
  LOG(INFO) << "Starting PostgreSQL server";
  RETURN_NOT_OK(StartServerUnlocked());

  Status status = Thread::Create(
      "pg_supervisor", "pg_supervisor", &PgSupervisor::RunThread, this, &supervisor_thread_);
  if (!status.ok()) {
    supervisor_thread_.reset();
    return status;
  }

  state_ = PgProcessState::kRunning;

  return Status::OK();
}

PgProcessState PgSupervisor::GetState() {
  std::lock_guard<std::mutex> lock(mtx_);
  return state_;
}

CHECKED_STATUS PgSupervisor::ExpectStateUnlocked(PgProcessState expected_state) {
  if (state_ != expected_state) {
    return STATUS_FORMAT(
        IllegalState, "Expected PostgreSQL server state to be $0, got $1", expected_state, state_);
  }
  return Status::OK();
}

CHECKED_STATUS PgSupervisor::StartServerUnlocked() {
  if (pg_wrapper_) {
    return STATUS(IllegalState, "Expecting pg_wrapper_ to not be set");
  }
  pg_wrapper_.emplace(conf_);
  auto start_status = pg_wrapper_->Start();
  if (!start_status.ok()) {
    pg_wrapper_.reset();
    return start_status;
  }
  return Status::OK();
}

void PgSupervisor::RunThread() {
  while (true) {
    Result<int> wait_result = pg_wrapper_->Wait();
    if (wait_result.ok()) {
      int ret_code = *wait_result;
      if (ret_code == 0) {
        LOG(INFO) << "PostgreSQL server exited normally";
      } else {
        LOG(WARNING) << "PostgreSQL server exited with code " << ret_code;
      }
      pg_wrapper_.reset();
    } else {
      // TODO: a better way to handle this error.
      LOG(WARNING) << "Failed when waiting for PostgreSQL server to exit: "
                   << wait_result.status() << ", waiting a bit";
      std::this_thread::sleep_for(1s);
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(mtx_);
      if (state_ == PgProcessState::kStopping) {
        break;
      }
      LOG(INFO) << "Restarting PostgreSQL server";
      Status start_status = StartServerUnlocked();
      if (!start_status.ok()) {
        // TODO: a better way to handle this error.
        LOG(WARNING) << "Failed trying to start PostgreSQL server: "
                     << start_status << ", waiting a bit";
        std::this_thread::sleep_for(1s);
      }
    }
  }
}

}  // namespace pgwrapper
}  // namespace yb
