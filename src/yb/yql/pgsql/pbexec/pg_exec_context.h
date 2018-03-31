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
//
// This class represents the context to execute a single statment. It contains the statement code
// (parse tree) and the environment (parameters and session context) with which the code is to be
// executed.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PBEXEC_PG_EXEC_CONTEXT_H_
#define YB_YQL_PGSQL_PBEXEC_PG_EXEC_CONTEXT_H_

#include "yb/yql/pgsql/ptree/pg_process_context.h"
#include "yb/yql/pgsql/proto/pg_proto.h"
#include "yb/yql/pgsql/util/pg_env.h"
#include "yb/yql/pgsql/util/pg_session.h"

namespace yb {
namespace pgsql {

class PgExecContext : public PgProcessContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<PgExecContext> SharedPtr;
  typedef std::shared_ptr<const PgExecContext> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  PgExecContext(const PgEnv::SharedPtr& pg_env,
                const PgSession::SharedPtr& client_session,
                const PgProto::SharedPtr& proto,
                string *exec_status,
                string *exec_output);
  virtual ~PgExecContext();

  //------------------------------------------------------------------------------------------------
  // Access API to PgSession parameters.
  string session_database() const {
    return pg_session_->current_database();
  }

  //------------------------------------------------------------------------------------------------
  // Access function for pg_env.
  const PgEnv::SharedPtr& pg_env() const {
    return pg_env_;
  }

  // Get a table creator from YB client.
  client::YBTableCreator* NewTableCreator() {
    return pg_env_->NewTableCreator();

  }
  CHECKED_STATUS DeleteTable(const client::YBTableName& name) {
    return pg_env_->DeleteTable(name);
  }

  // Create a new database with the given name.
  CHECKED_STATUS CreateDatabase(const std::string& db_name);

  // Delete database with the given name.
  CHECKED_STATUS DeleteDatabase(const std::string& db_name);

  // Create a new schema with the given name.
  CHECKED_STATUS CreateSchema(const std::string& schema_name) {
    CHECK(pg_env_) << "Execution env is not defined";
    return pg_env_->CreateSchema(schema_name);
  }

  // Delete schema with the given name.
  CHECKED_STATUS DeleteSchema(const std::string& schema_name) {
    return pg_env_->DeleteSchema(schema_name);
  }

  //------------------------------------------------------------------------------------------------
  // Returns the "code" to be executed. The code is either a PgProto or a DDL statement tree node.
  const PgProto::SharedPtr& pg_proto() const {
    return pg_proto_;
  }

  const TreeNode* ddl_stmt() const {
    return pg_proto_->ddl_stmt();
  }

  void set_dboutput(const string& msg) {
    *dboutput_ = msg;
  }

  void set_dbstatus(const string& msg) {
    *dbstatus_ = msg;
  }

  // Apply YBClient read/write operation.
  CHECKED_STATUS Apply(std::shared_ptr<client::YBPgsqlOp> op);

 private:
  // SQL environment and session.
  PgEnv::SharedPtr pg_env_;
  PgSession::SharedPtr pg_session_;

  // Code to be executed.
  PgProto::SharedPtr pg_proto_;

  // Read/write operation to execute.
  std::shared_ptr<client::YBPgsqlOp> op_;

  // Comment to be returned to client.
  string *dbstatus_;

  // Output from the database.
  string *dboutput_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PBEXEC_PG_EXEC_CONTEXT_H_
