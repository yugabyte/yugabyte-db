// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_service.h"

#include <thread>

#include "yb/client/client.h"
#include "yb/gutil/strings/join.h"
#include "yb/cqlserver/cql_server.h"
#include "yb/rpc/rpc_context.h"
#include "yb/util/bytes_formatter.h"

METRIC_DEFINE_histogram(server,
                        handler_latency_yb_cqlserver_CQLServerService_Any,
                        "yb.cqlserver.CQLServerService.AnyMethod RPC Time",
                        yb::MetricUnit::kMicroseconds,
                        "Microseconds spent handling "
                        "yb.cqlserver.CQLServerService.AnyMethod() "
                        "RPC requests",
                        60000000LU, 2);
namespace yb {
namespace cqlserver {

using std::string;
using std::unique_ptr;
using std::vector;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::rpc::InboundCall;
using yb::rpc::CQLInboundCall;
using yb::rpc::RpcContext;

CQLServiceImpl::CQLServiceImpl(CQLServer* server, const string& yb_tier_master_addresses)
    : CQLServerServiceIf(server->metric_entity()) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  metrics_.handler_latency =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_Any.Instantiate(
          server->metric_entity());

  // TODO(Robert): connect to YB
  // SetUpYBClient(yb_tier_master_addresses);
}

void CQLServiceImpl::SetUpYBClient(const string& yb_tier_master_addresses) {
  YBClientBuilder client_builder;
  client_builder.default_rpc_timeout(MonoDelta::FromSeconds(kRpcTimeoutSec));
  client_builder.add_master_server_addr(yb_tier_master_addresses);
  CHECK_OK(client_builder.Build(&client_));

  session_ = client_->NewSession();
  CHECK_OK(session_->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
}

void CQLServiceImpl::Handle(InboundCall* inbound_call) {
  auto* cql_call = down_cast<CQLInboundCall*>(CHECK_NOTNULL(inbound_call));

  DVLOG(4) << "Handling " << cql_call->ToString();

  // Parse the CQL request and execute it
  unique_ptr<CQLRequest> request;
  unique_ptr<CQLResponse> response;
  if (CQLRequest::ParseRequest(cql_call->serialized_request(), &request, &response)) {
    response.reset(request->Execute());
  }

  // Serialize the response to return to the CQL client. In case of error, an error response
  // should still be present.
  DCHECK(response.get() != nullptr);
  response->Serialize(&cql_call->response_msg_buf());
  RpcContext* context = new RpcContext(cql_call, metrics_);
  context->RespondSuccess();

  DVLOG(4) << cql_call->ToString() << " responded.";
}

}  // namespace cqlserver
}  // namespace yb
