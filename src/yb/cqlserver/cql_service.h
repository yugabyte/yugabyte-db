// Copyright (c) YugaByte, Inc.
//
// This file contains the CQLServiceImpl class that implements the CQL server to handle requests
// from Cassandra clients using the CQL native protocol.

#ifndef YB_CQLSERVER_CQL_SERVICE_H_
#define YB_CQLSERVER_CQL_SERVICE_H_

#include <vector>

#include "yb/cqlserver/cql_message.h"
#include "yb/cqlserver/cql_service.service.h"
#include "yb/util/string_case.h"

namespace yb {

namespace client {
class YBClient;
class YBTable;
class YBSession;
}  // namespace client

namespace rpc {
class CQLInboundCall;
} // namespace rpc

namespace cqlserver {

class CQLMetrics;
class CQLProcessor;
class CQLServer;
class CQLStatement;

// A map of CQL query id to the prepared statement for caching the prepared statments. Shared_ptr
// is used so that a prepared statement can be aged out and removed from the cache here without
// deleting one that is being executed by another client in another thread.
using CQLStatementMap = std::unordered_map<CQLMessage::QueryId, std::shared_ptr<CQLStatement>>;

// A LRU list of CQL statements and position in the list.
using CQLStatementList = std::list<std::shared_ptr<CQLStatement>>;
using CQLStatementListPos = CQLStatementList::iterator;

class CQLServiceImpl : public CQLServerServiceIf {
 public:
  // Constructor.
  CQLServiceImpl(
      CQLServer* server, std::shared_ptr<rpc::Messenger> messenger,
      const std::string& yb_tier_master_address);

  // Processing all incoming request from RPC and sending response back.
  void Handle(yb::rpc::InboundCall* call) override;

  // Either gets an available processor or creates a new one.
  CQLProcessor *GetProcessor();

  // Reply to a call request.
  void SendResponse(rpc::InboundCallPtr cql_call, CQLResponse *response);

  // Allocate a prepared statement. If the statement already exists, return it instead.
  std::shared_ptr<CQLStatement> AllocatePreparedStatement(
      const CQLMessage::QueryId& id, const std::string& keyspace, const std::string& sql_stmt);

  // Look up a prepared statement by its id. Nullptr will be returned if the statement is not found.
  std::shared_ptr<CQLStatement> GetPreparedStatement(const CQLMessage::QueryId& id);

  // Return the memory tracker for prepared statements.
  std::shared_ptr<MemTracker> prepared_stmts_mem_tracker() const {
    return prepared_stmts_mem_tracker_;
  }

  // Return the YBClient to communicate with either master or tserver.
  std::shared_ptr<client::YBClient> client() const { return client_; }

  // Return the YBClientCache.
  std::shared_ptr<client::YBTableCache> table_cache() const { return table_cache_; }

  // Return the CQL metrics.
  std::shared_ptr<CQLMetrics> cql_metrics() const { return cql_metrics_; }

  // Return the messenger.
  std::weak_ptr<rpc::Messenger> messenger() { return messenger_; }

 private:
  constexpr static int kRpcTimeoutSec = 5;

  // Setup YBClient.
  void SetUpYBClient(
      const std::string& yb_master_address, const scoped_refptr<MetricEntity>& metric_entity);

  // Insert a prepared statement at the front of the LRU list. "prepared_stmts_mutex_" needs to be
  // locked before this call.
  void InsertLruPreparedStatementUnlocked(const std::shared_ptr<CQLStatement>& stmt);

  // Move a prepared statement to the front of the LRU list. "prepared_stmts_mutex_" needs to be
  // locked before this call.
  void MoveLruPreparedStatementUnlocked(const std::shared_ptr<CQLStatement>& stmt);

  // Delete the least recently used prepared statement from the cache to free up memory.
  void DeleteLruPreparedStatement();


  // YBClient is to communicate with either master or tserver.
  std::shared_ptr<client::YBClient> client_;
  // A cache to reduce opening tables again and again.
  std::shared_ptr<client::YBTableCache> table_cache_;

  // Processors.
  vector<std::unique_ptr<CQLProcessor>> processors_;

  // Mutex that protects the creation of client_ and processor_.
  std::mutex process_mutex_;

  // Prepared statements cache.
  CQLStatementMap prepared_stmts_map_;

  // Prepared statements LRU list (least recently used one at the end).
  CQLStatementList prepared_stmts_list_;

  // Mutex that protects the prepared statements and the LRU list.
  std::mutex prepared_stmts_mutex_;

  // Tracker to measure and limit memory usage of prepared statements.
  std::shared_ptr<MemTracker> prepared_stmts_mem_tracker_;

  // Metrics to be collected and reported.
  yb::rpc::RpcMethodMetrics metrics_;

  std::shared_ptr<CQLMetrics> cql_metrics_;
  // Used to requeue the cql_inbound call to handle the response callback(s).
  std::weak_ptr<rpc::Messenger> messenger_;
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_CQLSERVER_CQL_SERVICE_H_
