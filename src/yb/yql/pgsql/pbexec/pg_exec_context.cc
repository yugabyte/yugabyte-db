//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/pbexec/pg_exec_context.h"
#include "yb/client/callbacks.h"

namespace yb {
namespace pgsql {

PgExecContext::PgExecContext(const PgEnv::SharedPtr& pg_env,
                             const PgSession::SharedPtr& client_session,
                             const PgProto::SharedPtr& pg_proto,
                             string *exec_status,
                             string *exec_output)
    : PgProcessContext(pg_proto->stmt().c_str(), pg_proto->stmt().size()),
      pg_env_(pg_env),
      pg_session_(client_session),
      pg_proto_(pg_proto),
      dbstatus_(exec_status),
      dboutput_(exec_output) {
}

PgExecContext::~PgExecContext() {
}

CHECKED_STATUS PgExecContext::Apply(std::shared_ptr<client::YBPgsqlOp> op) {
  return pg_session_->Apply(op);
}

CHECKED_STATUS PgExecContext::CreateDatabase(const std::string& db_name) {
  CHECK(pg_env_) << "Execution env is not defined";
  RETURN_NOT_OK(pg_env_->CreateDatabase(db_name));
  pg_session_->set_current_database(db_name);
  return Status::OK();
}

// Delete database with the given name.
CHECKED_STATUS PgExecContext::DeleteDatabase(const std::string& db_name) {
  CHECK(pg_env_) << "Execution env is not defined";
  RETURN_NOT_OK(pg_env_->DeleteDatabase(db_name));
  pg_session_->set_current_database("");
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
