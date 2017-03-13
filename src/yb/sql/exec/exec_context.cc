//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/exec_context.h"

namespace yb {
namespace sql {

ExecContext::ExecContext(const char *sql_stmt,
                         size_t stmt_len,
                         SqlEnv *sql_env)
    : ProcessContextBase(sql_stmt, stmt_len),
      sql_env_(sql_env) {
}

ExecContext::~ExecContext() {
}

CHECKED_STATUS ExecContext::ApplyWrite(std::shared_ptr<client::YBqlWriteOp> yb_op,
                                       const TreeNode *tnode) {
  Status s = sql_env_->ApplyWrite(yb_op);
  if (!s.ok()) {
    // YBOperation returns not-found error when the tablet is not found.
    return Error(tnode->loc(), s.ToString().c_str(),
                 s.IsNotFound() ? ErrorCode::TABLET_NOT_FOUND : ErrorCode::SQL_STATEMENT_INVALID);
  }
  return ProcessResponseStatus(*yb_op.get(), tnode);
}

CHECKED_STATUS ExecContext::ApplyRead(std::shared_ptr<client::YBqlReadOp> yb_op,
                                      const TreeNode *tnode) {
  Status s = sql_env_->ApplyRead(yb_op);
  // NOTE: Unlike other SQL storages, when query is empty, YB server does not use NOTFOUND status.
  if (!s.ok()) {
    // YBOperation returns not-found error when the tablet is not found.
    return Error(tnode->loc(), s.ToString().c_str(),
                 s.IsNotFound() ? ErrorCode::TABLET_NOT_FOUND : ErrorCode::SQL_STATEMENT_INVALID);
  }
  return ProcessResponseStatus(*yb_op.get(), tnode);
}

CHECKED_STATUS ExecContext::ProcessResponseStatus(const client::YBqlOp& yb_op,
                                                  const TreeNode *tnode) {
  const YQLResponsePB& resp = yb_op.response();
  CHECK(resp.has_status()) << "YQLResponsePB status missing";
  switch (resp.status()) {
    case YQLResponsePB::YQL_STATUS_OK:
      return Status::OK();
    case YQLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH:
      return Error(tnode->loc(), resp.error_message().c_str(), ErrorCode::WRONG_METADATA_VERSION);
    case YQLResponsePB::YQL_STATUS_RUNTIME_ERROR:
      return Error(tnode->loc(), resp.error_message().c_str(), ErrorCode::EXEC_ERROR);
    // default: fall-through to below
  }
  LOG(FATAL) << "Unknown status: " << resp.DebugString();
}


}  // namespace sql
}  // namespace yb
