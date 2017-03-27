//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/exec_context.h"
#include "yb/client/callbacks.h"

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

void ExecContext::ApplyWriteAsync(
    std::shared_ptr<client::YBqlWriteOp> yb_op, const TreeNode *tnode,
    StatementExecutedCallback cb) {
  sql_env_->ApplyWriteAsync(
      yb_op,
      Bind(&ExecContext::ApplyAsyncDone, Unretained(this), yb_op, Unretained(tnode), cb));
}

void ExecContext::ApplyReadAsync(
    std::shared_ptr<client::YBqlReadOp> yb_op, const TreeNode *tnode,
    StatementExecutedCallback cb) {
  sql_env_->ApplyReadAsync(
      yb_op,
      Bind(&ExecContext::ApplyAsyncDone, Unretained(this), yb_op, Unretained(tnode), cb));
}

void ExecContext::ApplyAsyncDone(
    std::shared_ptr<client::YBqlOp> yb_op, const TreeNode *tnode,
    StatementExecutedCallback cb, const Status &s) {
  ExecutedResult::SharedPtr result;
  const Status ss = ProcessResponseStatus(yb_op, tnode, s, &result);
  cb.Run(ss, result);
}

CHECKED_STATUS ExecContext::ProcessResponseStatus(
    std::shared_ptr<client::YBqlOp> yb_op, const TreeNode *tnode, const Status &s,
    ExecutedResult::SharedPtr* result) {
  CHECK(yb_op != nullptr) << " yb_op was found to be nullptr";
  CHECK(tnode != nullptr) << " tnode was found to be nullptr";
  if (!s.ok()) {
    // YBOperation returns not-found error when the tablet is not found.
    return Error(tnode->loc(),
                 s.ToString().c_str(),
                 s.IsNotFound() ? ErrorCode::TABLET_NOT_FOUND : ErrorCode::SQL_STATEMENT_INVALID);
  }
  const YQLResponsePB &resp = yb_op->response();
  CHECK(resp.has_status()) << "YQLResponsePB status missing";
  switch (resp.status()) {
    case YQLResponsePB::YQL_STATUS_OK:
      // Read the rows result if present.
      if (!yb_op->rows_data().empty()) {
        result->reset(new RowsResult(yb_op.get()));
      }
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
