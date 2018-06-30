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

#include "yb/util/logging.h"
#include "yb/util/subprocess.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"

using std::vector;
using std::string;

namespace yb {
namespace pgwrapper {

string GetPostgresInstallRoot() {
  return JoinPathSegments(yb::env_util::GetRootDir("postgres"), "postgres");
}

Status PgWrapper::Start() {
  auto postgres_executable = JoinPathSegments(
      GetPostgresInstallRoot(), "bin", "postgres");
  vector<string> argv {
    postgres_executable,
    "-D",
    conf_.data_dir,
    "--port",
    std::to_string(conf_.pg_port)
  };

  if (!Env::Default()->FileExists(postgres_executable)) {
    return STATUS_FORMAT(IOError, "PostgreSQL executable not found: $0", postgres_executable);
  }

  pg_proc_.emplace(postgres_executable, argv);
  pg_proc_->ShareParentStderr();
  pg_proc_->ShareParentStdout();
  SetCommonEnv(&pg_proc_.get());
  RETURN_NOT_OK(pg_proc_->Start());
  LOG(INFO) << "PostgreSQL server running as pid " << pg_proc_->pid();
  return Status::OK();
}

Status PgWrapper::InitDB() {
  string initdb_program_path = JoinPathSegments(GetPostgresInstallRoot(), "bin", "initdb");
  if (!Env::Default()->FileExists(initdb_program_path)) {
    return STATUS_FORMAT(IOError, "initdb not found at: $0", initdb_program_path);
  }

  vector<string> initdb_args { initdb_program_path, "-D", conf_.data_dir };
  Subprocess initdb_subprocess(initdb_program_path, initdb_args);
  SetCommonEnv(&initdb_subprocess);
  int exit_code = 0;
  RETURN_NOT_OK(initdb_subprocess.Start());
  RETURN_NOT_OK(initdb_subprocess.Wait(&exit_code));
  if (exit_code != 0) {
    return STATUS_FORMAT(RuntimeError, "$0 failed with exit code $1",
                         initdb_program_path,
                         exit_code);
  }
  LOG(INFO) << "initdb completed successfully. Database initialized at " << conf_.data_dir;
  return Status::OK();
}

void PgWrapper::SetCommonEnv(Subprocess* proc) {
  // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
  proc->SetEnv("YB_PG_FALLBACK_SYSTEM_USER_NAME", "postgres");
}

}  // namespace pgwrapper
}  // namespace yb
