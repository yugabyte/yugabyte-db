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
  RETURN_NOT_OK(ProcessResponseStatus(*yb_op, tnode, s));
  // Read the rows result. Rows result may be present for conditional DML.
  if (!yb_op->rows_data().empty()) {
    result_.reset(new RowsResult(yb_op.get()));
  }
  return Status::OK();
}

CHECKED_STATUS ExecContext::ApplyRead(std::shared_ptr<client::YBqlReadOp> yb_op,
                                      const TreeNode *tnode) {
  // NOTE: Unlike other SQL storages, when query is empty, YB server does not use NOTFOUND status.
  Status s = sql_env_->ApplyRead(yb_op);
  RETURN_NOT_OK(ProcessResponseStatus(*yb_op, tnode, s));
  // Read the rows result.
  CHECK(!yb_op->rows_data().empty()) << "Rows result absent";
  result_.reset(new RowsResult(yb_op.get()));
  return Status::OK();
}

CHECKED_STATUS ExecContext::ProcessResponseStatus(const client::YBqlOp& yb_op,
                                                  const TreeNode *tnode,
                                                  const Status& s) {
  if (!s.ok()) {
    // YBOperation returns not-found error when the tablet is not found.
    return Error(tnode->loc(),
                 s.ToString().c_str(),
                 s.IsNotFound() ? ErrorCode::TABLET_NOT_FOUND : ErrorCode::SQL_STATEMENT_INVALID);
  }

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
