//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// QLEnv represents the environment where SQL statements are being processed.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/util/ql_env.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/yb_op.h"

#include "yb/master/catalog_manager.h"
#include "yb/rpc/messenger.h"
#include "yb/util/trace.h"

using namespace std::literals;

namespace yb {
namespace ql {

using std::string;
using std::shared_ptr;
using std::weak_ptr;

using client::YBClient;
using client::YBError;
using client::YBOperation;
using client::YBSession;
using client::YBStatusMemberCallback;
using client::YBTable;
using client::YBMetaDataCache;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableName;
using client::YBqlReadOp;
using client::YBqlWriteOp;

namespace {

const auto kSessionTimeout = 60s;

}

// Runs the callback (cb) and returns if the status s is not OK.
#define CB_RETURN_NOT_OK(cb, s)    \
  do {                             \
    ::yb::Status _s = (s);         \
    if (PREDICT_FALSE(!_s.ok())) { \
      (cb)->Run(_s);               \
      return;                      \
    }                              \
  } while (0)

QLEnv::QLEnv(
    weak_ptr<rpc::Messenger> messenger, shared_ptr<YBClient> client,
    shared_ptr<YBMetaDataCache> cache, cqlserver::CQLRpcServerEnv* cql_rpcserver_env)
    : client_(client),
      metadata_cache_(cache),
      session_(client->NewSession()),
      messenger_(messenger),
      flush_done_cb_(this, &QLEnv::FlushAsyncDone),
      cql_rpcserver_env_(cql_rpcserver_env) {
  session_->SetTimeout(kSessionTimeout);
  CHECK_OK(session_->SetFlushMode(YBSession::MANUAL_FLUSH));
}

QLEnv::~QLEnv() {}

YBTableCreator *QLEnv::NewTableCreator() {
  return client_->NewTableCreator();
}

YBTableAlterer *QLEnv::NewTableAlterer(const YBTableName& table_name) {
  return client_->NewTableAlterer(table_name);
}

CHECKED_STATUS QLEnv::TruncateTable(const string& table_id) {
  return client_->TruncateTable(table_id);
}

CHECKED_STATUS QLEnv::DeleteTable(const YBTableName& name) {
  return client_->DeleteTable(name);
}

CHECKED_STATUS QLEnv::DeleteIndexTable(const YBTableName& name, YBTableName* indexed_table_name) {
  return client_->DeleteIndexTable(name, indexed_table_name);
}

void QLEnv::SetCurrentCall(rpc::InboundCallPtr cql_call) {
  DCHECK(cql_call == nullptr || current_call_ == nullptr)
      << this << " Tried updating current call. Current call is " << current_call_;
  current_call_ = std::move(cql_call);
}

CHECKED_STATUS QLEnv::Apply(std::shared_ptr<client::YBqlOp> op) {
  has_session_operations_ = true;

  // Apply the write.
  TRACE("Apply");
  return session_->Apply(std::move(op));
}

bool QLEnv::FlushAsync(Callback<void(const Status &)>* cb) {
  if (!has_session_operations_) {
    return false;
  }
  DCHECK(requested_callback_ == nullptr);
  requested_callback_ = cb;
  TRACE("Flush Async");
  session_->FlushAsync(&flush_done_cb_);
  return true;
}

Status QLEnv::GetOpError(const client::YBqlOp* op) const {
  const auto itr = op_errors_.find(op);
  return itr != op_errors_.end() ? itr->second : Status::OK();
}

void QLEnv::AbortOps() {
  session_->Abort();
}

void QLEnv::FlushAsyncDone(const Status &s) {
  // When any error occurs during the dispatching of YBOperation, YBSession saves the error and
  // returns IOError. When it happens, retrieves the errors and discard the IOError.
  DCHECK(has_session_operations_);
  if (PREDICT_FALSE(!s.ok())) {
    if (s.IsIOError()) {
      for (const auto& error : session_->GetPendingErrors()) {
        op_errors_[static_cast<const client::YBqlOp*>(&error->failed_op())] = error->status();
      }
    } else {
      flush_status_ = s;
    }
  }
  has_session_operations_ = false;

  TRACE("Flush Async Done");
  if (current_call_ == nullptr) {
    // For unit tests. Run the callback in the current (reactor) thread and allow wait for the case
    // when a statement needs to be reprepared and we need to fetch table metadata synchronously.
    bool allowed = ThreadRestrictions::SetWaitAllowed(true);
    ResumeCQLCall();
    ThreadRestrictions::SetWaitAllowed(allowed);
    return;
  }

  // Production/cqlserver usecase: enqueue the callback to run in the server's handler thread.
  resume_execution_ = Bind(&QLEnv::ResumeCQLCall, Unretained(this));
  current_cql_call()->SetResumeFrom(&resume_execution_);

  auto messenger = messenger_.lock();
  DCHECK(messenger != nullptr) << "weak_ptr's messenger is null";
  messenger->QueueInboundCall(current_call_);
}

void QLEnv::ResumeCQLCall() {
  TRACE("Resuming CQL Call");
  requested_callback_->Run(flush_status_);
}

shared_ptr<YBTable> QLEnv::GetTableDesc(const YBTableName& table_name, bool* cache_used) {
  // Hide tables in system_redis keyspace.
  if (table_name.is_redis_namespace()) {
    return nullptr;
  }
  shared_ptr<YBTable> yb_table;
  Status s = metadata_cache_->GetTable(table_name, &yb_table, cache_used);

  if (!s.ok()) {
    VLOG(3) << "GetTableDesc: Server returns an error: " << s.ToString();
    return nullptr;
  }

  return yb_table;
}

shared_ptr<QLType> QLEnv::GetUDType(const std::string &keyspace_name,
                                      const std::string &type_name,
                                      bool *cache_used) {
  shared_ptr<QLType> ql_type = std::make_shared<QLType>(keyspace_name, type_name);
  Status s = metadata_cache_->GetUDType(keyspace_name, type_name, &ql_type, cache_used);

  if (!s.ok()) {
    VLOG(3) << "GetTypeDesc: Server returned an error: " << s.ToString();
    return nullptr;
  }

  return ql_type;
}

void QLEnv::RemoveCachedTableDesc(const YBTableName& table_name) {
  metadata_cache_->RemoveCachedTable(table_name);
}

void QLEnv::RemoveCachedUDType(const std::string& keyspace_name, const std::string& type_name) {
  metadata_cache_->RemoveCachedUDType(keyspace_name, type_name);
}

void QLEnv::Reset() {
  has_session_operations_ = false;
  requested_callback_ = nullptr;
  flush_status_ = Status::OK();
  op_errors_.clear();
}

Status QLEnv::CreateKeyspace(const std::string& keyspace_name) {
  return client_->CreateNamespace(keyspace_name);
}

Status QLEnv::DeleteKeyspace(const string& keyspace_name) {
  RETURN_NOT_OK(client_->DeleteNamespace(keyspace_name));

  // Reset the current keyspace name if it's dropped.
  if (CurrentKeyspace() == keyspace_name) {
    if (current_call_ != nullptr) {
      current_cql_call()->ql_session()->set_current_keyspace(kUndefinedKeyspace);
    } else {
      current_keyspace_.reset(new string(kUndefinedKeyspace));
    }
  }

  return Status::OK();
}

Status QLEnv::UseKeyspace(const string& keyspace_name) {
  // Check if a keyspace with the specified name exists.
  bool exists = false;
  RETURN_NOT_OK(client_->NamespaceExists(keyspace_name, &exists));

  if (!exists) {
    return STATUS(NotFound, "Cannot use unknown keyspace");
  }

  // Set the current keyspace name.
  if (current_call_ != nullptr) {
    current_cql_call()->ql_session()->set_current_keyspace(keyspace_name);
  } else {
    current_keyspace_.reset(new string(keyspace_name));
  }

  return Status::OK();
}

Status QLEnv::CreateUDType(const std::string &keyspace_name,
                           const std::string &type_name,
                           const std::vector<std::string> &field_names,
                           const std::vector<std::shared_ptr<QLType>> &field_types) {
  return client_->CreateUDType(keyspace_name, type_name, field_names, field_types);
}

Status QLEnv::DeleteUDType(const std::string &keyspace_name, const std::string &type_name) {
  return client_->DeleteUDType(keyspace_name, type_name);
}

}  // namespace ql
}  // namespace yb
