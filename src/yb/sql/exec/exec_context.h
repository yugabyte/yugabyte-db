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

#ifndef YB_SQL_EXEC_EXEC_CONTEXT_H_
#define YB_SQL_EXEC_EXEC_CONTEXT_H_

#include "yb/sql/ptree/process_context.h"
#include "yb/sql/util/sql_env.h"
#include "yb/sql/util/statement_result.h"

namespace yb {
namespace sql {

class ExecContext : public ProcessContextBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ExecContext> UniPtr;
  typedef std::unique_ptr<const ExecContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  ExecContext(const char *sql_stmt,
              size_t stmt_len,
              const ParseTree *parse_tree,
              const StatementParameters *params,
              SqlEnv *sql_env);
  virtual ~ExecContext();

  // Get a table creator from YB client.
  client::YBTableCreator* NewTableCreator() {
    return sql_env_->NewTableCreator();
  }

  // Get a table alterer from YB client.
  client::YBTableAlterer* NewTableAlterer(const client::YBTableName& table_name) {
    return sql_env_->NewTableAlterer(table_name);
  }

  CHECKED_STATUS DeleteTable(const client::YBTableName& name) {
    return sql_env_->DeleteTable(name);
  }

  // Keyspace related methods.

  // Create a new keyspace with the given name.
  CHECKED_STATUS CreateKeyspace(const std::string& keyspace_name) {
    return sql_env_->CreateKeyspace(keyspace_name);
  }

  // Delete keyspace with the given name.
  CHECKED_STATUS DeleteKeyspace(const std::string& keyspace_name) {
    return sql_env_->DeleteKeyspace(keyspace_name);
  }

  // Use keyspace with the given name.
  CHECKED_STATUS UseKeyspace(const std::string& keyspace_name) {
    return sql_env_->UseKeyspace(keyspace_name);
  }

  std::string CurrentKeyspace() const {
    return sql_env_->CurrentKeyspace();
  }

  // (User-defined) Type related methods.

  // Create (user-defined) type with the given arguments
  CHECKED_STATUS CreateUDType(const std::string &keyspace_name,
                              const std::string &type_name,
                              const std::vector<std::string> &field_names,
                              const std::vector<std::shared_ptr<YQLType>> &field_types) {
    return sql_env_->CreateUDType(keyspace_name, type_name, field_names, field_types);
  }

  // Delete a (user-defined) type by name.
  CHECKED_STATUS DeleteUDType(const std::string &keyspace_name, const std::string &type_name) {
    return sql_env_->DeleteUDType(keyspace_name, type_name);
  }

  // Access function for sql_env.
  SqlEnv* sql_env() const {
    return sql_env_;
  }

  // Access function for parse_tree.
  const ParseTree* parse_tree() const {
    return parse_tree_;
  }

  // Returns the tree node of the statement being executed.
  const TreeNode* tnode() const {
    return parse_tree_->root().get();
  }

  // Access function for params.
  const StatementParameters* params() const {
    return params_;
  }

  // Access function for op.
  const std::shared_ptr<client::YBqlOp>& op() const {
    return op_;
  }

  // Access function for start_time.
  const MonoTime& start_time() const {
    return start_time_;
  }

  // Apply YBClient read/write operation.
  CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBqlWriteOp> op) {
    op_ = op;
    return sql_env_->ApplyWrite(op);
  }
  CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBqlReadOp> op) {
    op_ = op;
    return sql_env_->ApplyRead(op);
  }

  // Variants of ProcessContextBase::Error() that report location of statement tnode as the error
  // location.
  using ProcessContextBase::Error;
  CHECKED_STATUS Error(ErrorCode error_code);
  CHECKED_STATUS Error(const char *m, ErrorCode error_code);
  CHECKED_STATUS Error(const Status& s, ErrorCode error_code);

 private:
  // Statement parse tree to execute.
  const ParseTree *parse_tree_;

  // Statement parameters to execute with.
  const StatementParameters *params_;

  // Read/write operation to execute.
  std::shared_ptr<client::YBqlOp> op_;

  // Execution start time.
  const MonoTime start_time_;

  // SQL environment.
  SqlEnv *sql_env_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXEC_CONTEXT_H_
