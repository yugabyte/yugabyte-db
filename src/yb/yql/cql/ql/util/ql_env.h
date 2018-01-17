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
// QLEnv defines the interface for the environment where SQL engine is running.
//
// If we support different types of servers underneath SQL engine (which we don't), this class
// should be an abstract interface and let the server (such as proxy server) defines the content.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_UTIL_QL_ENV_H_
#define YB_YQL_CQL_QL_UTIL_QL_ENV_H_

#include "yb/client/callbacks.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"

#include "yb/gutil/callback.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/yql/cql/cqlserver/cql_rpc.h"
#include "yb/yql/cql/cqlserver/cql_rpcserver_env.h"
#include "yb/yql/cql/ql/ql_session.h"

#include "yb/util/enums.h"

namespace yb {
namespace ql {

class QLEnv {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<QLEnv> UniPtr;
  typedef std::unique_ptr<const QLEnv> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  QLEnv(
      std::weak_ptr<rpc::Messenger> messenger, std::shared_ptr<client::YBClient> client,
      std::shared_ptr<client::YBMetaDataCache> cache,
      cqlserver::CQLRpcServerEnv* cql_rpcserver_env = nullptr);
  virtual ~QLEnv();

  virtual client::YBTableCreator *NewTableCreator();

  virtual client::YBTableAlterer *NewTableAlterer(const client::YBTableName& table_name);

  virtual CHECKED_STATUS TruncateTable(const std::string& table_id);

  virtual CHECKED_STATUS DeleteTable(const client::YBTableName& name);

  virtual CHECKED_STATUS DeleteIndexTable(const client::YBTableName& name,
                                          client::YBTableName* indexed_table_name);

  // Read/write related methods.

  // Apply a read/write operation. The operation is batched and needs to be flushed with FlushAsync.
  // Mix of read/write operations in a batch is not supported currently.
  virtual CHECKED_STATUS Apply(std::shared_ptr<client::YBqlOp> op);

  // Flush batched operations. Returns false when there is no batched operation.
  virtual bool FlushAsync(Callback<void(const Status &)>* cb);

  // Get the status of an individual read/write op after it has been flushed and completed.
  virtual Status GetOpError(const client::YBqlOp* op) const;

  // Abort the batched ops.
  virtual void AbortOps();

  // Start a distributed transaction.
  void StartTransaction(IsolationLevel isolation_level);

  // Commit the current distributed transaction.
  void CommitTransaction(client::CommitCallback callback);

  virtual std::shared_ptr<client::YBTable> GetTableDesc(
      const client::YBTableName& table_name, bool *cache_used);

  virtual void RemoveCachedTableDesc(const client::YBTableName& table_name);

  // Keyspace related methods.

  // Create a new keyspace with the given name.
  virtual CHECKED_STATUS CreateKeyspace(const std::string& keyspace_name);

  // Delete keyspace with the given name.
  virtual CHECKED_STATUS DeleteKeyspace(const std::string& keyspace_name);

  // Use keyspace with the given name.
  virtual CHECKED_STATUS UseKeyspace(const std::string& keyspace_name);

  virtual std::string CurrentKeyspace() const {
    return (current_call_ != nullptr) ?
        current_cql_call()->ql_session()->current_keyspace() :
        current_keyspace_ != nullptr ? *current_keyspace_ : kUndefinedKeyspace;
  }

  // (User-defined) Type related methods.

  // Create (user-defined) type with the given arguments
  CHECKED_STATUS CreateUDType(const std::string &keyspace_name,
                              const std::string &type_name,
                              const std::vector<std::string> &field_names,
                              const std::vector<std::shared_ptr<QLType>> &field_types);

  // Delete (user-defined) type by name.
  virtual CHECKED_STATUS DeleteUDType(const std::string &keyspace_name,
                                      const std::string &type_name);

  // Retrieve (user-defined) type by name.
  std::shared_ptr<QLType> GetUDType(const std::string &keyspace_name,
                                     const std::string &type_name,
                                     bool *cache_used);

  virtual void RemoveCachedUDType(const std::string& keyspace_name, const std::string& type_name);

  // Reset all env states or variables before executing the next statement.
  void Reset();

  void SetCurrentCall(rpc::InboundCallPtr call);

  cqlserver::CQLRpcServerEnv* cql_rpcserver_env() { return cql_rpcserver_env_; }

 private:
  // Helpers to process the asynchronously received response from ybclient.
  void FlushAsyncDone(const Status &s);
  void ResumeCQLCall();

  cqlserver::CQLInboundCall* current_cql_call() const {
    return static_cast<cqlserver::CQLInboundCall*>(current_call_.get());
  }

  // Persistent attributes.

  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;

  // YBMetaDataCache, a cache to avoid creating a new table or type for each call.
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_;

  // YBSession to apply operations.
  std::shared_ptr<client::YBSession> session_;

  // Transaction manager to create distributed transactions.
  std::unique_ptr<client::TransactionManager> transaction_manager_;

  // Current distributed transaction if present.
  std::shared_ptr<client::YBTransaction> transaction_;

  bool has_session_operations_ = false;

  // Messenger used to requeue the CQL call upon callback.
  std::weak_ptr<rpc::Messenger> messenger_;

  // Transient attributes.
  // The following attributes are reset implicitly for every execution.

  // The "current" call whose response we might be waiting for.
  rpc::InboundCallPtr current_call_ = nullptr;

  // Last flush error if any.
  Status flush_status_;

  // Errors of read/write operations that failed.
  std::unordered_map<const client::YBqlOp*, Status> op_errors_;

  Callback<void(const Status&)>* requested_callback_ = nullptr;
  Callback<void(void)> resume_execution_;

  // The current keyspace. Used only in test environment when there is no current call.
  std::unique_ptr<std::string> current_keyspace_;

  cqlserver::CQLRpcServerEnv* cql_rpcserver_env_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_UTIL_QL_ENV_H_
