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
    RETURN_NOT_OK(sql_env_->UseKeyspace(keyspace_name));
    result_.reset(new SetKeyspaceResult(keyspace_name));
    return Status::OK();
  }

  std::string CurrentKeyspace() const {
    return sql_env_->CurrentKeyspace();
  }

  // Apply YBClient write operator.
  CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBqlWriteOp> yb_op,
                            const TreeNode *tnode);

  // Apply YBClient read operator.
  CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBqlReadOp> yb_op,
                           const TreeNode *tnode);

  // Clears only the paging state of the row result.
  void ClearPagingState() {
    if (result_ != nullptr && result_->type() == ExecuteResult::Type::ROWS) {
      static_cast<RowsResult*>(result_.get())->clear_paging_state();
    }
  }

  // Returns the execution result and release the ownership from this context.
  ExecuteResult::UniPtr AcquireExecuteResult() {
    return std::move(result_);
  }

 private:
  // Check and return read/write response status.
  CHECKED_STATUS ProcessResponseStatus(const client::YBqlOp& yb_op,
                                       const TreeNode *tnode,
                                       const Status& s);

  // SQL environment.
  SqlEnv *sql_env_;

  // Execution result.
  ExecuteResult::UniPtr result_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXEC_CONTEXT_H_
