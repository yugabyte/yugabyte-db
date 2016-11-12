// Copyright (c) YugaByte, Inc.
//
// This file contains the CQLServiceImpl class that implements the CQL server to handle requests
// from Cassandra clients using the CQL native protocol.

#ifndef YB_CQLSERVER_CQL_SERVICE_H
#define YB_CQLSERVER_CQL_SERVICE_H

#include <vector>

#include "yb/cqlserver/cql_message.h"
#include "yb/cqlserver/cql_service.service.h"
#include "yb/rpc/transfer.h"
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
  CQLServiceImpl(CQLServer* server, const std::string& yb_tier_master_address);

  void Handle(yb::rpc::InboundCall* call) override;

 private:
  constexpr static int kRpcTimeoutSec = 5;

  void SetUpYBClient(const std::string& yb_master_address);

  yb::rpc::RpcMethodMetrics metrics_;

  std::shared_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBSession> session_;
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_CQLSERVER_CQL_SERVICE_H
