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
// This file contains the CQLServiceImpl class that implements the CQL server to handle requests
// from Cassandra clients using the CQL native protocol.

#ifndef YB_CQLSERVER_CQL_SERVICE_H_
#define YB_CQLSERVER_CQL_SERVICE_H_

#include <vector>

#include "yb/cqlserver/cql_message.h"
#include "yb/cqlserver/cql_processor.h"
#include "yb/cqlserver/cql_rpcserver_env.h"
#include "yb/cqlserver/cql_statement.h"
#include "yb/cqlserver/cql_service.service.h"
#include "yb/cqlserver/cql_server_options.h"
#include "yb/ql/statement.h"

#include "yb/util/string_case.h"

#include "yb/client/async_initializer.h"
#include "yb/client/client.h"

namespace yb {

namespace client {
class YBClient;
class YBTable;
class YBSession;
}  // namespace client

namespace cqlserver {

extern const char* const kRoleColumnNameSaltedHash;

class CQLMetrics;
class CQLProcessor;
class CQLServer;

class CQLServiceImpl : public CQLServerServiceIf {
 public:
  // Constructor.
  CQLServiceImpl(CQLServer* server, const CQLServerOptions& opts);

  void Shutdown() override;

  // Processing all incoming request from RPC and sending response back.
  void Handle(yb::rpc::InboundCallPtr call) override;

  // Return CQL processor at pos as available.
  void ReturnProcessor(const CQLProcessorListPos& pos);

  // Allocate a prepared statement. If the statement already exists, return it instead.
  std::shared_ptr<CQLStatement> AllocatePreparedStatement(
      const CQLMessage::QueryId& id, const std::string& keyspace, const std::string& ql_stmt);

  // Look up a prepared statement by its id. Nullptr will be returned if the statement is not found.
  std::shared_ptr<const CQLStatement> GetPreparedStatement(const CQLMessage::QueryId& id);

  std::shared_ptr<ql::Statement> GetAuthPreparedStatement() const { return auth_prepared_stmt_; }

  // Delete the prepared statement from the cache.
  void DeletePreparedStatement(const std::shared_ptr<const CQLStatement>& stmt);

  // Return the memory tracker for prepared statements.
  std::shared_ptr<MemTracker> prepared_stmts_mem_tracker() const {
    return prepared_stmts_mem_tracker_;
  }

  // Return the YBClient to communicate with either master or tserver.
  const std::shared_ptr<client::YBClient>& client() const;

  // Return the YBClientCache.
  const std::shared_ptr<client::YBMetaDataCache>& metadata_cache() const;

  // Return the CQL metrics.
  std::shared_ptr<CQLMetrics> cql_metrics() const { return cql_metrics_; }

  // Return the messenger.
  std::weak_ptr<rpc::Messenger> messenger() { return messenger_; }

  // Return the CQL RPC environment.
  CQLRpcServerEnv* cql_rpc_env() { return cql_rpcserver_env_.get(); }

 private:
  constexpr static int kRpcTimeoutSec = 5;

  // Either gets an available processor or creates a new one.
  CQLProcessor *GetProcessor();

  // Insert a prepared statement at the front of the LRU list. "prepared_stmts_mutex_" needs to be
  // locked before this call.
  void InsertLruPreparedStatementUnlocked(const std::shared_ptr<CQLStatement>& stmt);

  // Move a prepared statement to the front of the LRU list. "prepared_stmts_mutex_" needs to be
  // locked before this call.
  void MoveLruPreparedStatementUnlocked(const std::shared_ptr<CQLStatement>& stmt);

  // Delete a prepared statement from the cache and the LRU list. "prepared_stmts_mutex_" needs to
  // be locked before this call.
  void DeletePreparedStatementUnlocked(const std::shared_ptr<const CQLStatement> stmt);

  // Delete the least recently used prepared statement from the cache to free up memory.
  void DeleteLruPreparedStatement();

  // CQLServer of this service.
  CQLServer* const server_;

  // YBClient is to communicate with either master or tserver.
  yb::client::AsyncClientInitialiser async_client_init_;

  // A cache to reduce opening tables or (user-defined) types again and again.
  mutable std::shared_ptr<client::YBMetaDataCache> metadata_cache_;
  mutable std::atomic<bool> is_metadata_initialized_ = { false };
  mutable std::mutex metadata_init_mutex_;

  // List of CQL processors (in-use and available). In-use ones are at the beginning and available
  // ones at the end.
  CQLProcessorList processors_;

  // Next available CQL processor.
  CQLProcessorListPos next_available_processor_;

  // Mutex that protects access to processors_.
  std::mutex processors_mutex_;

  // Prepared statements cache.
  CQLStatementMap prepared_stmts_map_;

  // Prepared statements LRU list (least recently used one at the end).
  CQLStatementList prepared_stmts_list_;

  // Mutex that protects the prepared statements and the LRU list.
  std::mutex prepared_stmts_mutex_;

  std::shared_ptr<ql::Statement> auth_prepared_stmt_;

  // Tracker to measure and limit memory usage of prepared statements.
  std::shared_ptr<MemTracker> prepared_stmts_mem_tracker_;

  // Metrics to be collected and reported.
  yb::rpc::RpcMethodMetrics metrics_;

  std::shared_ptr<CQLMetrics> cql_metrics_;
  // Used to requeue the cql_inbound call to handle the response callback(s).
  std::weak_ptr<rpc::Messenger> messenger_;

  // RPC environment for CQL proxy.
  std::unique_ptr<CQLRpcServerEnv> cql_rpcserver_env_;
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_CQLSERVER_CQL_SERVICE_H_
