// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_service.h"

#include <thread>

#include "yb/client/client.h"
#include "yb/gutil/strings/join.h"
#include "yb/cqlserver/cql_server.h"
#include "yb/rpc/rpc_context.h"
#include "yb/util/bytes_formatter.h"

DECLARE_int32(cql_service_num_threads);

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

  // Setup client.
  SetUpYBClient(yb_tier_master_addresses);

  // Setup processors.
  processors_.reserve(FLAGS_cql_service_num_threads);
  for (CQLProcessor::UniPtr& processor : processors_) {
    processor.reset(new CQLProcessor(client_));
  }
}

void CQLServiceImpl::SetUpYBClient(const string& yb_tier_master_addresses) {
  YBClientBuilder client_builder;
  client_builder.default_rpc_timeout(MonoDelta::FromSeconds(kRpcTimeoutSec));
  client_builder.add_master_server_addr(yb_tier_master_addresses);
  CHECK_OK(client_builder.Build(&client_));
}

void CQLServiceImpl::Handle(InboundCall* inbound_call) {
  // Collect the call.
  CQLInboundCall* cql_call = down_cast<CQLInboundCall*>(CHECK_NOTNULL(inbound_call));
  DVLOG(4) << "Handling " << cql_call->ToString();

  // Process the call.
  CQLProcessor *processor = GetProcessor();
  CHECK(processor != nullptr);
  unique_ptr<CQLResponse> response;
  processor->ProcessCall(cql_call->serialized_request(), &response);

  // Reply to client.
  SendResponse(cql_call, response.get());
  DVLOG(4) << cql_call->ToString() << " responded.";
}

CQLProcessor *CQLServiceImpl::GetProcessor() {
  // Must guard the processors_ pool as each processor can handle one and only one call at a time.
  std::lock_guard<std::mutex> guard(process_mutex_);

  CQLProcessor *cql_processor = nullptr;
  for (CQLProcessor::UniPtr& processor : processors_) {
    if (!processor->is_used()) {
      cql_processor = processor.get();
    }
  }

  // Create a new processor if needed.
  if (cql_processor == nullptr) {
    const int size = processors_.size();
    cql_processor = new CQLProcessor(client_);
    processors_.reserve(std::max<int>(size * 2, size + 10));
    processors_.emplace_back(cql_processor);
  }

  // Make this processor used and return.
  cql_processor->used();
  return cql_processor;
}

void CQLServiceImpl::SendResponse(CQLInboundCall* cql_call, CQLResponse *response) {
  CHECK(response != nullptr);

  // Serialize the response to return to the CQL client. In case of error, an error response
  // should still be present.
  response->Serialize(&cql_call->response_msg_buf());
  RpcContext *context = new RpcContext(cql_call, metrics_);
  context->RespondSuccess();
}

}  // namespace cqlserver
}  // namespace yb
