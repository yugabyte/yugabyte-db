//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This class represents the execution context which contains both the SQL code (parse tree) and
// the environment (session context) within which the code is to be executed.
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
  ExecContext(const char *sql_stmt, size_t stmt_len, SqlEnv *sql_env);
  virtual ~ExecContext();

  // Get a table creator from YB client.
  client::YBTableCreator* NewTableCreator() {
    return sql_env_->NewTableCreator();
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

  SqlEnv* sql_env() const {
    return sql_env_;
  }

  // Apply YBClient write operator.
  void ApplyWriteAsync(
      std::shared_ptr<client::YBqlWriteOp> yb_op, const TreeNode* tnode,
      StatementExecutedCallback cb);

  // Apply YBClient read operator.
  void ApplyReadAsync(
      std::shared_ptr<client::YBqlReadOp> yb_op, const TreeNode* tnode,
      StatementExecutedCallback cb);

 private:
  void ApplyAsyncDone(
      std::shared_ptr<client::YBqlOp> yb_op, const TreeNode* tnode,
      StatementExecutedCallback cb, const Status& s);
  // Check and return read/write response status and result if any.
  CHECKED_STATUS ProcessResponseStatus(
      std::shared_ptr<client::YBqlOp> yb_op, const TreeNode* tnode, const Status& s,
      ExecutedResult::SharedPtr* result);

  // SQL environment.
  SqlEnv *sql_env_;

  // Async callback for ApplyRead/Write.
  Callback<void(const Status&)> async_callback_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXEC_CONTEXT_H_
