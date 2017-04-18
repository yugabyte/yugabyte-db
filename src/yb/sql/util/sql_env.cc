//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SqlEnv represents the environment where SQL statements are being processed.
//--------------------------------------------------------------------------------------------------

#include <yb/util/trace.h>
#include "yb/sql/util/sql_env.h"
#include "yb/client/callbacks.h"
#include "yb/master/catalog_manager.h"

namespace yb {
namespace sql {

using std::shared_ptr;
using std::weak_ptr;

using client::YBClient;
using client::YBError;
using client::YBOperation;
using client::YBSession;
using client::YBStatusMemberCallback;
using client::YBTable;
using client::YBTableCache;
using client::YBTableCreator;
using client::YBTableName;
using client::YBqlReadOp;
using client::YBqlWriteOp;

// Runs the callback (cb) and returns if the status s is not OK.
#define CB_RETURN_NOT_OK(cb, s)    \
  do {                             \
    ::yb::Status _s = (s);         \
    if (PREDICT_FALSE(!_s.ok())) { \
      (cb).Run(_s);                \
      return;                      \
    }                              \
  } while (0)

SqlEnv::SqlEnv(
    weak_ptr<rpc::Messenger> messenger, shared_ptr<YBClient> client, shared_ptr<YBTableCache> cache)
    : client_(client),
      table_cache_(cache),
      write_session_(client_->NewSession(false /* read_only */)),
      read_session_(client->NewSession(true /* read_only */)),
      messenger_(messenger),
      flush_done_cb_(this, &SqlEnv::FlushAsyncDone) {
  write_session_->SetTimeoutMillis(kSessionTimeoutMs);
  CHECK_OK(write_session_->SetFlushMode(YBSession::MANUAL_FLUSH));

  read_session_->SetTimeoutMillis(kSessionTimeoutMs);
  CHECK_OK(read_session_->SetFlushMode(YBSession::MANUAL_FLUSH));
}

SqlEnv::~SqlEnv() {}

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
    ElementDeleter d(&errors);
    session->GetPendingErrors(&errors, &overflowed);
    for (const auto* error : errors) {
      if (&error->failed_op() == op) {
        return error->status();
      }
    }
  }
  return s;
}

void SqlEnv::SetCurrentCall(rpc::InboundCallPtr cql_call) {
  DCHECK(cql_call == nullptr || current_call_ == nullptr)
      << this << " Tried updating current call. Current call is " << current_call_;
  current_call_ = std::move(cql_call);
}

void SqlEnv::ApplyWriteAsync(
    std::shared_ptr<YBqlWriteOp> yb_op, Callback<void(const Status &)> cb) {
  // The previous result must have been cleared.
  DCHECK(current_read_op_ == nullptr);
  DCHECK(current_write_op_ == nullptr);

  // Execute the write.
  CB_RETURN_NOT_OK(cb, write_session_->Apply(yb_op));
  current_write_op_ = yb_op;
  requested_callback_ = cb;
  TRACE("Flushing Write");
  write_session_->FlushAsync(&flush_done_cb_);
}

void SqlEnv::ApplyReadAsync(
    std::shared_ptr<YBqlReadOp> yb_op, Callback<void(const Status &)> cb) {
  // The previous result must have been cleared.
  DCHECK(current_read_op_ == nullptr);
  DCHECK(current_write_op_ == nullptr);

  // Execute the read.
  CB_RETURN_NOT_OK(cb, read_session_->Apply(yb_op));
  current_read_op_ = yb_op;
  requested_callback_ = cb;
  TRACE("Flushing Read");
  read_session_->FlushAsync(&flush_done_cb_);
}

void SqlEnv::FlushAsyncDone(const Status &s) {
  TRACE("Flush Async Done");
  if (current_call_ == nullptr) {
    // For unit tests. Run the callback in the current (reactor) thread and allow wait for the case
    // when a statement needs to be reprepared and we need to fetch table metadata synchronously.
    bool allowed = ThreadRestrictions::SetWaitAllowed(true);
    ResumeCQLCall(s);
    ThreadRestrictions::SetWaitAllowed(allowed);
    return;
  }

  // Production/cqlserver usecase. Enqueue the callback to run in the server's handler thread.
  resume_execution_ = Bind(&SqlEnv::ResumeCQLCall, Unretained(this), s);
  current_cql_call()->SetResumeFrom(&resume_execution_);

  auto messenger = messenger_.lock();
  DCHECK(messenger != nullptr) << "weak_ptr's messenger is null";
  messenger->QueueInboundCall(current_call_);
}

void SqlEnv::ResumeCQLCall(const Status &s) {
  TRACE("Resuming CQL Call");
  if (current_write_op_.get() != nullptr) {
    DCHECK(current_read_op_ == nullptr);
    requested_callback_.Run(ProcessWriteResult(s));
  } else {
    DCHECK(current_write_op_ == nullptr);
    requested_callback_.Run(ProcessReadResult(s));
  }
}

Status SqlEnv::ProcessWriteResult(const Status &s) {
  auto yb_op = current_write_op_;
  current_write_op_ = nullptr;
  RETURN_NOT_OK(ProcessOpStatus(yb_op.get(), s, write_session_.get()));
  return Status::OK();
}

Status SqlEnv::ProcessReadResult(const Status &s) {
  auto yb_op = current_read_op_;
  current_read_op_ = nullptr;
  RETURN_NOT_OK(ProcessOpStatus(yb_op.get(), s, read_session_.get()));
  return Status::OK();
}

shared_ptr<YBTable> SqlEnv::GetTableDesc(const YBTableName& table_name, bool refresh_cache,
                                         bool is_system, bool* cache_used) {
  shared_ptr<YBTable> yb_table;
  Status s = table_cache_->GetTable(table_name, &yb_table, refresh_cache, is_system, cache_used);

  if (!s.ok()) {
    VLOG(3) << "GetTableDesc: Server returns an error: " << s.ToString();
    return nullptr;
  }

  return yb_table;
}

void SqlEnv::Reset() {
  current_read_op_ = nullptr;
  current_write_op_ = nullptr;
}

CHECKED_STATUS SqlEnv::DeleteKeyspace(const std::string& keyspace_name) {
  RETURN_NOT_OK(client_->DeleteNamespace(keyspace_name));

  // Reset the current keyspace name if it's dropped.
  if (CurrentKeyspace() == keyspace_name) {
    if (current_call_ != nullptr) {
      current_cql_call()->GetSqlSession()->reset_current_keyspace();
    } else {
      current_keyspace_.reset(new string(yb::master::kDefaultNamespaceName));
    }
  }

  return Status::OK();
}

CHECKED_STATUS SqlEnv::UseKeyspace(const std::string& keyspace_name) {
  // Check if a keyspace with the specified name exists.
  bool exists = false;
  RETURN_NOT_OK(client_->NamespaceExists(keyspace_name, &exists));

  if (!exists) {
    return STATUS(NotFound, "Cannot use unknown keyspace");
  }

  // Set the current keyspace name.
  if (current_call_ != nullptr) {
    current_cql_call()->GetSqlSession()->set_current_keyspace(keyspace_name);
  } else {
    current_keyspace_.reset(new string(keyspace_name));
  }

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
