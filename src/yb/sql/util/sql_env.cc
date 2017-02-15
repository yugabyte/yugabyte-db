//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SqlEnv represents the environment where SQL statements are being processed.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/sql_env.h"
#include "yb/master/catalog_manager.h"

namespace yb {
namespace sql {

using std::shared_ptr;

using client::YBClient;
using client::YBOperation;
using client::YBSession;
using client::YBTable;
using client::YBTableCache;
using client::YBTableCreator;
using client::YBqlReadOp;
using client::YBqlWriteOp;

SqlEnv::SqlEnv(shared_ptr<YBClient> client,
               shared_ptr<YBTableCache> cache,
               shared_ptr<YBSession> write_session,
               shared_ptr<YBSession> read_session)
    : client_(client),
      table_cache_(cache),
      write_session_(write_session),
      read_session_(read_session),
      current_keyspace_(yb::master::kDefaultNamespaceName) {
}

YBTableCreator *SqlEnv::NewTableCreator() {
  return client_->NewTableCreator();
}

CHECKED_STATUS SqlEnv::DeleteTable(const string& name) {
  return client_->DeleteTable(name);
}

CHECKED_STATUS SqlEnv::ApplyWrite(std::shared_ptr<YBqlWriteOp> yb_op) {
  // The previous result must have been cleared.
  DCHECK(rows_result_ == nullptr);

  // Execute the write.
  RETURN_NOT_OK(write_session_->Apply(yb_op));
  RETURN_NOT_OK(write_session_->Flush());

  // Read the processing result.
  if (!yb_op->rows_data().empty()) {
    rows_result_.reset(new RowsResult(yb_op.get()));
  }

  return Status::OK();
}

CHECKED_STATUS SqlEnv::ApplyRead(std::shared_ptr<YBqlReadOp> yb_op) {
  // The previous result must have been cleared.
  DCHECK(rows_result_ == nullptr);

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
  Status s = table_cache_->GetTable(table_name, &yb_table, refresh_metadata);

  if (s.IsNotFound()) {
    return nullptr;
  }
  CHECK(s.ok()) << "Server returns unexpected error. " << s.ToString();
  return yb_table;
}

void SqlEnv::Reset() {
  rows_result_ = nullptr;
}

CHECKED_STATUS SqlEnv::UseKeyspace(const std::string& keyspace_name) {
  // Check if a keyspace with the specified name exists.
  bool exists = false;
  RETURN_NOT_OK(client_->NamespaceExists(keyspace_name, &exists));

  if (!exists) {
    return STATUS(NotFound, "Cannot use unknown keyspace");
  }

  // Set the current keyspace name.

  // TODO(Oleg): Keyspace should be cached in CQL layer. We keep it here only as a workaround.
  current_keyspace_ = keyspace_name;

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
