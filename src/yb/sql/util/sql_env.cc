//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SqlEnv represents the environment where SQL statements are being processed.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/sql_env.h"

namespace yb {
namespace sql {

using std::shared_ptr;

using client::YBClient;
using client::YBOperation;
using client::YBSession;
using client::YBTable;
using client::YBTableCreator;
using client::YBSqlReadOp;
using client::YBSqlWriteOp;

SqlEnv::SqlEnv(shared_ptr<YBClient> client,
               shared_ptr<YBSession> write_session,
               shared_ptr<YBSession> read_session)
    : client_(client),
      write_session_(write_session),
      read_session_(read_session) {
}

YBTableCreator *SqlEnv::NewTableCreator() {
  return client_->NewTableCreator();
}

CHECKED_STATUS SqlEnv::DeleteTable(const string& name) {
  return client_->DeleteTable(name);
}


  CHECKED_STATUS SqlEnv::ApplyWrite(std::shared_ptr<YBSqlWriteOp> yb_op) {
  // Clear the previous result.
  rows_result_ = nullptr;

  // Execute the write.
  RETURN_NOT_OK(write_session_->Apply(yb_op));
  RETURN_NOT_OK(write_session_->Flush());

  // Read the processing result.
  if (!yb_op->rows_data().empty()) {
    rows_result_.reset(new RowsResult(yb_op.get()));
  }

  return Status::OK();
}

CHECKED_STATUS SqlEnv::ApplyRead(std::shared_ptr<YBSqlReadOp> yb_op) {
  // Clear the previous result.
  rows_result_ = nullptr;

  if (yb_op.get() != nullptr) {
    // Execute the read.
    RETURN_NOT_OK(read_session_->Apply(yb_op));
    RETURN_NOT_OK(read_session_->Flush());

    // Read the processing result.
    rows_result_.reset(new RowsResult(yb_op.get()));
  }

  return Status::OK();
}

shared_ptr<YBTable> SqlEnv::GetTableDesc(const char *table_name, bool refresh_metadata) {
  // TODO(neil) Once we decide where to cache the descriptor, the refresh_metadata should be used
  // to decide whether or not the cached version should be used.
  // At the moment, we read the table descriptor every time we need it.
  shared_ptr<YBTable> yb_table;
  Status s = client_->OpenTable(table_name, &yb_table);
  if (s.IsNotFound()) {
    return nullptr;
  }
  CHECK(s.ok()) << "Server returns unexpected error. " << s.ToString();
  return yb_table;
}

} // namespace sql
} // namespace yb
