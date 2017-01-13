//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This class represents the execution context which contains both the SQL code (parse tree) and
// the environment (session context) within which the code is to be executed.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_EXEC_EXEC_CONTEXT_H_
#define YB_SQL_EXEC_EXEC_CONTEXT_H_

#include "yb/sql/util/sql_env.h"
#include "yb/sql/ptree/process_context.h"

namespace yb {
namespace sql {

class ExecContext : public ProcessContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ExecContext> UniPtr;
  typedef std::unique_ptr<const ExecContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  ExecContext(const char *sql_stmt,
              size_t stmt_len,
              ParseTree::UniPtr parse_tree,
              SqlEnv *sql_env);
  virtual ~ExecContext();

  // Get a table creator from YB client.
  client::YBTableCreator* NewTableCreator() {
    return sql_env_->NewTableCreator();
  }

  // Apply YBClient write operator.
  CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBSqlWriteOp> yb_op,
                            const TreeNode *tnode);

  // Apply YBClient read operator.
  CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBSqlReadOp> yb_op,
                           const TreeNode *tnode);

 private:
  SqlEnv *sql_env_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXEC_CONTEXT_H_
