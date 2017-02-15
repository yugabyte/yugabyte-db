// Copyright (c) YugaByte, Inc.
//
// This file contains the CQLServiceImpl class that implements the CQL server to handle requests
// from Cassandra clients using the CQL native protocol.

#ifndef YB_CQLSERVER_CQL_SERVICE_H_
#define YB_CQLSERVER_CQL_SERVICE_H_

#include <vector>

#include "yb/cqlserver/cql_message.h"
#include "yb/cqlserver/cql_processor.h"
#include "yb/cqlserver/cql_service.service.h"
#include "yb/rpc/transfer.h"
#include "yb/rpc/inbound_call.h"
#include "yb/util/string_case.h"

namespace yb {

namespace client {
class YBClient;
class YBTable;
class YBSession;
}  // namespace client

namespace cqlserver {

class CQLServer;

class CQLServiceImpl : public CQLServerServiceIf {
 public:
  // Constructor.
  CQLServiceImpl(CQLServer* server, const std::string& yb_tier_master_address);

  // Processing all incoming request from RPC and sending response back.
  void Handle(yb::rpc::InboundCall* call) override;

  // Either gets an available processor or creates a new one.
  CQLProcessor *GetProcessor();

  // Reply to a call request.
  void SendResponse(rpc::CQLInboundCall* cql_call, CQLResponse *response);

 private:
  constexpr static int kRpcTimeoutSec = 5;

  // Setup YBClient.
  void SetUpYBClient(
      const std::string& yb_master_address, const scoped_refptr<MetricEntity>& metric_entity);

  // YBClient is to communicate with either master or tserver.
  std::shared_ptr<client::YBClient> client_;
  // A cache to reduce opening tables again and again.
  std::shared_ptr<client::YBTableCache> table_cache_;

  // Processors.
  vector<CQLProcessor::UniPtr> processors_;

  // Mutex that protects the creation of client_ and processor_.
  std::mutex process_mutex_;

  // Metrics to be collected and reported.
  yb::rpc::RpcMethodMetrics metrics_;

  std::shared_ptr<CQLMetrics> cql_metrics_;
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_CQLSERVER_CQL_SERVICE_H_
