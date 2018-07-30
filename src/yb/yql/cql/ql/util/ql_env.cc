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
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/master/catalog_manager.h"
#include "yb/rpc/messenger.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/trace.h"

namespace yb {
namespace ql {

using std::string;
using std::shared_ptr;
using std::weak_ptr;

using client::CommitCallback;
using client::TransactionManager;
using client::YBClient;
using client::YBError;
using client::YBOperation;
using client::YBSession;
using client::YBStatusMemberCallback;
using client::YBTable;
using client::YBTransaction;
using client::YBMetaDataCache;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableName;
using client::YBqlReadOp;
using client::YBqlWriteOp;

// Runs the callback (cb) and returns if the status s is not OK.
#define CB_RETURN_NOT_OK(cb, s)    \
  do {                             \
    ::yb::Status _s = (s);         \
    if (PREDICT_FALSE(!_s.ok())) { \
      (cb)->Run(_s);               \
      return;                      \
    }                              \
  } while (0)

QLEnv::QLEnv(weak_ptr<rpc::Messenger> messenger, shared_ptr<YBClient> client,
             shared_ptr<YBMetaDataCache> cache,
             const server::ClockPtr& clock,
             TransactionManagerProvider transaction_manager_provider)
    : client_(std::move(client)),
      metadata_cache_(std::move(cache)),
      session_(std::make_shared<YBSession>(client_, clock)),
      transaction_manager_provider_(std::move(transaction_manager_provider)),
      messenger_(messenger),
      resume_execution_(Bind(&QLEnv::ResumeCQLCall, Unretained(this))) {
  CHECK(clock);
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

void QLEnv::RescheduleCurrentCall(Callback<void(void)>* callback) {
  if (current_call_ == nullptr) {
    // Some unit tests are not executed in CQL proxy and have no call pointer. In those cases, just
    // execute the callback directly while disabling thread restrictions.
    const bool allowed = ThreadRestrictions::SetWaitAllowed(true);
    callback->Run();
    ThreadRestrictions::SetWaitAllowed(allowed);
    return;
  }

  current_cql_call()->SetResumeFrom(callback);
  auto messenger = messenger_.lock();
  DCHECK(messenger != nullptr) << "weak_ptr's messenger is null";
  messenger->QueueInboundCall(current_call_);
}

void QLEnv::StartTransaction(const IsolationLevel isolation_level) {
  if (transaction_manager_ == nullptr) {
    transaction_manager_ = transaction_manager_provider_();
  }
  if (transaction_ == nullptr) {
    transaction_ =  std::make_shared<YBTransaction>(transaction_manager_, isolation_level);
  } else {
    DCHECK(transaction_->IsRestartRequired());
    transaction_ = transaction_->CreateRestartedTransaction();
  }
  session_->SetTransaction(transaction_);
}

Status QLEnv::PrepareChildTransaction(ChildTransactionDataPB* data) {
  ChildTransactionDataPB result =
      VERIFY_RESULT(DCHECK_NOTNULL(transaction_.get())->PrepareChildFuture().get());
  *data = std::move(result);
  return Status::OK();
}

Status QLEnv::ApplyChildTransactionResult(const ChildTransactionResultPB& result) {
  return DCHECK_NOTNULL(transaction_.get())->ApplyChildResult(result);
}

void QLEnv::CommitTransaction(CommitCallback callback) {
  if (transaction_ == nullptr) {
    LOG(DFATAL) << "No transaction to commit";
    return;
  }
  // SetTransaction() must be called before the Commit() call instead of after because when the
  // commit callback is invoked, it will finish the current transaction, return the response and
  // make the CQLProcessor available for the next statement and its operations would be aborted by
  // SetTransaction().
  session_->SetTransaction(nullptr);
  shared_ptr<client::YBTransaction> transaction = std::move(transaction_);
  transaction->Commit(std::move(callback));
}

void QLEnv::SetReadPoint(const ReExecute reexecute) {
  session_->SetReadPoint(static_cast<client::Retry>(reexecute));
}

CHECKED_STATUS QLEnv::Apply(std::shared_ptr<client::YBqlOp> op) {
  // Apply the write.
  TRACE("Apply");
  return session_->Apply(std::move(op));
}

bool QLEnv::HasBufferedOperations() const {
  return session_->CountBufferedOperations() > 0;
}

bool QLEnv::FlushAsync(Callback<void(const Status &, bool)>* cb) {
  if (HasBufferedOperations()) {
    requested_callback_ = cb;
    TRACE("Flush Async");
    session_->FlushAsync([this](const Status& status) { FlushAsyncDone(status); });
    return true;
  }
  return false;
}

Status QLEnv::GetOpError(const client::YBqlOp* op) const {
  const auto itr = op_errors_.find(op);
  return itr != op_errors_.end() ? itr->second : Status::OK();
}

void QLEnv::FlushAsyncDone(const Status &s) {
  TRACE("Flush Async Done");
  flush_status_ = s;
  // When any error occurs during the dispatching of YBOperation, YBSession saves the error and
  // returns IOError. When it happens, retrieves the errors and discard the IOError.
  op_errors_.clear();
  if (PREDICT_FALSE(!s.ok())) {
    if (s.IsIOError()) {
      for (const auto& error : session_->GetPendingErrors()) {
        op_errors_[static_cast<const client::YBqlOp*>(&error->failed_op())] = error->status();
      }
      flush_status_ = Status::OK();
    }
  }
  // If the current thread is the RPC worker thread, call the callback directly. Otherwise,
  // reschedule the call to resume in the RPC worker thread.
  if (rpc::ThreadPool::IsCurrentThreadRpcWorker()) {
    requested_callback_->Run(flush_status_, false /* rescheduled_call */);
  } else {
    RescheduleCurrentCall(&resume_execution_);
  }
}

void QLEnv::ResumeCQLCall() {
  TRACE("Resuming CQL Call");
  requested_callback_->Run(flush_status_, true /* rescheduled_call */);
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

shared_ptr<YBTable> QLEnv::GetTableDesc(const TableId& table_id, bool* cache_used) {
  shared_ptr<YBTable> yb_table;
  Status s = metadata_cache_->GetTable(table_id, &yb_table, cache_used);

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

void QLEnv::RemoveCachedTableDesc(const TableId& table_id) {
  metadata_cache_->RemoveCachedTable(table_id);
}

void QLEnv::RemoveCachedUDType(const std::string& keyspace_name, const std::string& type_name) {
  metadata_cache_->RemoveCachedUDType(keyspace_name, type_name);
}

void QLEnv::Reset(const ReExecute reexecute) {
  session_->Abort();
  session_->SetTransaction(nullptr);
  requested_callback_ = nullptr;
  flush_status_ = Status::OK();
  op_errors_.clear();
  if (transaction_ != nullptr && !(transaction_->IsRestartRequired() && reexecute)) {
    shared_ptr<client::YBTransaction> transaction = std::move(transaction_);
    transaction->Abort();
  }
}

Status QLEnv::GrantPermission(const PermissionType& permission, const ResourceType& resource_type,
                              const std::string& canonical_resource, const char* resource_name,
                              const char* namespace_name, const std::string& role_name) {
  return client_->GrantPermission(permission, resource_type, canonical_resource, resource_name,
                                  namespace_name, role_name);
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
  Result<bool> exists = client_->NamespaceExists(keyspace_name);
  RETURN_NOT_OK(exists);

  if (!exists.get()) {
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

Status QLEnv::CreateRole(const std::string& role_name,
                         const std::string& salted_hash,
                         const bool login, const bool superuser) {
  return client_->CreateRole(role_name, salted_hash, login, superuser);
}

Status QLEnv::AlterRole(const std::string& role_name,
                        const boost::optional<std::string>& salted_hash,
                        const boost::optional<bool> login,
                        const boost::optional<bool> superuser) {
  return client_->AlterRole(role_name, salted_hash, login, superuser);
}

Status QLEnv::DeleteRole(const std::string& role_name) {
  return client_->DeleteRole(role_name);
}

Status QLEnv::GrantRole(const std::string& granted_role_name,
                        const std::string& recipient_role_name) {
  return client_->GrantRole(granted_role_name, recipient_role_name);
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
