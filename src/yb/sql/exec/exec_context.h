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

  CHECKED_STATUS DeleteTable(const string& name) {
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

  // Apply YBClient write operator.
  CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBqlWriteOp> yb_op,
                            const TreeNode *tnode);

  // Apply YBClient read operator.
  CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBqlReadOp> yb_op,
                           const TreeNode *tnode);

 private:
  SqlEnv *sql_env_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXEC_CONTEXT_H_
