//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/exec_context.h"

namespace yb {
namespace sql {

ExecContext::ExecContext(const char *sql_stmt,
                         size_t stmt_len,
                         ParseTree::UniPtr parse_tree,
                         SqlEnv *sql_env)
    : ProcessContext(sql_stmt, stmt_len, move(parse_tree)),
      sql_env_(sql_env) {
}

ExecContext::~ExecContext() {
}

CHECKED_STATUS ExecContext::ApplyWrite(std::shared_ptr<client::YBqlWriteOp> yb_op,
                                       const TreeNode *tnode) {
  Status s = sql_env_->ApplyWrite(yb_op);
  if (!s.ok()) {
    return Error(tnode->loc(), s.ToString().c_str(), ErrorCode::SQL_STATEMENT_INVALID);
  }
  return s;
}

CHECKED_STATUS ExecContext::ApplyRead(std::shared_ptr<client::YBqlReadOp> yb_op,
                                      const TreeNode *tnode) {
  Status s = sql_env_->ApplyRead(yb_op);
  // NOTE: Unlike other SQL storages, when query is empty, YB server does not use NOTFOUND status.
  if (!s.ok()) {
    return Error(tnode->loc(), s.ToString().c_str(), ErrorCode::SQL_STATEMENT_INVALID);
  }
  return s;
}

}  // namespace sql
}  // namespace yb
