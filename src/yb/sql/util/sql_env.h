//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// SqlEnv defines the interface for the environment where SQL engine is running.
//
// If we support different types of servers underneath SQL engine (which we don't), this class
// should be an abstract interface and let the server (such as proxy server) defines the content.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_SQL_ENV_H_
#define YB_SQL_UTIL_SQL_ENV_H_

#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/sql/sql_session.h"
#include "yb/rpc/messenger.h"

#include "yb/cqlserver/cql_rpc.h"
#include "yb/cqlserver/cql_rpcserver_env.h"

namespace yb {

namespace sql {

class SqlEnv {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<SqlEnv> UniPtr;
  typedef std::unique_ptr<const SqlEnv> UniPtrConst;
  static const int kSessionTimeoutMs = 60000;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  SqlEnv(
      std::weak_ptr<rpc::Messenger> messenger, std::shared_ptr<client::YBClient> client,
      std::shared_ptr<client::YBMetaDataCache> cache,
      cqlserver::CQLRpcServerEnv* cql_rpcserver_env = nullptr);
  virtual ~SqlEnv();

  virtual client::YBTableCreator *NewTableCreator();

  virtual CHECKED_STATUS DeleteTable(const client::YBTableName& name);

  virtual void ApplyWriteAsync(
      std::shared_ptr<client::YBqlWriteOp> yb_op, Callback<void(const Status&)>* callback);

  virtual void ApplyReadAsync(
      std::shared_ptr<client::YBqlReadOp> yb_op, Callback<void(const Status&)>* callback);


  virtual std::shared_ptr<client::YBTable> GetTableDesc(
      const client::YBTableName& table_name, bool *cache_used);


  virtual void RemoveCachedTableDesc(const client::YBTableName& table_name);

  // Keyspace related methods.

  // Create a new keyspace with the given name.
  virtual CHECKED_STATUS CreateKeyspace(const std::string& keyspace_name) {
    return client_->CreateNamespace(keyspace_name);
  }

  // Delete keyspace with the given name.
  virtual CHECKED_STATUS DeleteKeyspace(const std::string& keyspace_name);

  // Use keyspace with the given name.
  virtual CHECKED_STATUS UseKeyspace(const std::string& keyspace_name);

  virtual std::string CurrentKeyspace() const {
    return (current_call_ != nullptr) ?
        current_cql_call()->sql_session()->current_keyspace() :
        current_keyspace_ != nullptr ? *current_keyspace_ : kUndefinedKeyspace;
  }

  // (User-defined) Type related methods.

  // Create (user-defined) type with the given arguments
  CHECKED_STATUS CreateUDType(const std::string &keyspace_name,
                              const std::string &type_name,
                              const std::vector<std::string> &field_names,
                              const std::vector<std::shared_ptr<YQLType>> &field_types) {
    return client_->CreateUDType(keyspace_name, type_name, field_names, field_types);
  }

  // Delete (user-defined) type by name.
  virtual CHECKED_STATUS DeleteUDType(const std::string &keyspace_name,
                                      const std::string &type_name);

  // Retrieve (user-defined) type by name.
  std::shared_ptr<YQLType> GetUDType(const std::string &keyspace_name,
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
  void ResumeCQLCall(const Status& s);
  // Process YBOperation status.
  CHECKED_STATUS ProcessOpStatus(const client::YBOperation* op,
                                 const Status& s,
                                 client::YBSession* session) const;
  CHECKED_STATUS ProcessWriteResult(const Status& s);
  CHECKED_STATUS ProcessReadResult(const Status& s);

  cqlserver::CQLInboundCall* current_cql_call() const {
    return static_cast<cqlserver::CQLInboundCall*>(current_call_.get());
  }

  // Persistent attributes.

  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;

  // YBMetaDataCache, a cache to avoid creating a new table or type for each call.
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> write_session_;

  // A specific session (within YBClient) to execute a statement.
  std::shared_ptr<client::YBSession> read_session_;

  // Messenger used to requeue the CQL call upon callback.
  std::weak_ptr<rpc::Messenger> messenger_;

  client::YBStatusMemberCallback<SqlEnv> flush_done_cb_;
  // Transient attributes.
  // The following attributes are reset implicitly for every execution.

  // The "current" write/read op whose response we might be waiting for.
  std::shared_ptr<client::YBqlWriteOp> current_write_op_;
  std::shared_ptr<client::YBqlReadOp> current_read_op_;
  rpc::InboundCallPtr current_call_ = nullptr;

  Callback<void(const Status&)>* requested_callback_;
  Callback<void(void)> resume_execution_;

  // The current keyspace. Used only in test environment when there is no current call.
  std::unique_ptr<std::string> current_keyspace_;

  cqlserver::CQLRpcServerEnv* cql_rpcserver_env_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_UTIL_SQL_ENV_H_
