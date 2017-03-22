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
using client::YBError;
using client::YBOperation;
using client::YBSession;
using client::YBTable;
using client::YBTableCache;
using client::YBTableCreator;
using client::YBTableName;
using client::YBqlReadOp;
using client::YBqlWriteOp;

SqlEnv::SqlEnv(shared_ptr<YBClient> client, shared_ptr<YBTableCache> cache)
    : client_(client),
      table_cache_(cache),
      write_session_(client_->NewSession(false /* read_only */)),
      read_session_(client->NewSession(true /* read_only */)),
      sql_session_(nullptr) {

  write_session_->SetTimeoutMillis(kSessionTimeoutMs);
  CHECK_OK(write_session_->SetFlushMode(YBSession::MANUAL_FLUSH));

  read_session_->SetTimeoutMillis(kSessionTimeoutMs);
  CHECK_OK(read_session_->SetFlushMode(YBSession::MANUAL_FLUSH));
}

YBTableCreator *SqlEnv::NewTableCreator() {
  return client_->NewTableCreator();
}

CHECKED_STATUS SqlEnv::DeleteTable(const YBTableName& name) {
  return client_->DeleteTable(name);
}

CHECKED_STATUS SqlEnv::ProcessOpStatus(const YBOperation* op,
                                       const Status& s,
                                       YBSession* session) const {
  // When any error occurs during the dispatching of YBOperation, YBSession saves the error and
  // returns IOError. When it happens, retrieves the error that corresponds to this op and return.
  // Ignore overflow since there is nothing we can do but we will still search for the error for
  // the given op.
  if (s.IsIOError()) {
    vector<client::YBError*> errors;
    bool overflowed;
    session->GetPendingErrors(&errors, &overflowed);
    for (const auto* error : errors) {
      if (&error->failed_op() == op) {
        return error->status();
      }
    }
  }
  return s;
}

CHECKED_STATUS SqlEnv::ApplyWrite(std::shared_ptr<YBqlWriteOp> yb_op) {
  // Execute the write.
  RETURN_NOT_OK(write_session_->Apply(yb_op));
  Status s = write_session_->Flush();
  return ProcessOpStatus(yb_op.get(), s, write_session_.get());
}

CHECKED_STATUS SqlEnv::ApplyRead(std::shared_ptr<YBqlReadOp> yb_op) {
  // Execute the read.
  RETURN_NOT_OK(read_session_->Apply(yb_op));
  Status s = read_session_->Flush();
  return ProcessOpStatus(yb_op.get(), s, read_session_.get());
}

shared_ptr<YBTable> SqlEnv::GetTableDesc(const YBTableName& table_name, bool refresh_cache,
                                         bool* cache_used) {
  shared_ptr<YBTable> yb_table;
  Status s = table_cache_->GetTable(table_name, &yb_table, refresh_cache, cache_used);

  if (s.IsNotFound()) {
    return nullptr;
  }
  CHECK(s.ok()) << "Server returns unexpected error. " << s.ToString();
  return yb_table;
}

CHECKED_STATUS SqlEnv::UseKeyspace(const std::string& keyspace_name) {
  // Check if a keyspace with the specified name exists.
  bool exists = false;
  RETURN_NOT_OK(client_->NamespaceExists(keyspace_name, &exists));

  if (!exists) {
    return STATUS(NotFound, "Cannot use unknown keyspace");
  }

  // Set the current keyspace name.
  CHECK(sql_session_ != nullptr) << "SQL session is not set";
  sql_session_->set_current_keyspace(keyspace_name);

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
