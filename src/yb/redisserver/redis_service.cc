// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_service.h"

#include "yb/redisserver/redis.pb.h"
#include "yb/redisserver/redis_server.h"
#include "yb/rpc/rpc_context.h"
#include "yb/util/bytes_formatter.h"

METRIC_DEFINE_histogram(server,
                        handler_latency_yb_redisserver_RedisServerService_Any,
                        "yb.redisserver.RedisServerService.AnyMethod RPC Time",
                        yb::MetricUnit::kMicroseconds,
                        "Microseconds spent handling "
                            "yb.redisserver.RedisServerService.AnyMethod() "
                            "RPC requests",
                        60000000LU, 2);
namespace yb {
namespace redisserver {

using yb::rpc::InboundCall;
using yb::rpc::RedisInboundCall;
using yb::rpc::RpcContext;
using yb::rpc::RedisClientCommand;

RedisServiceImpl::RedisServiceImpl(RedisServer* server)
    : RedisServerServiceIf(server->metric_entity()) {
  metrics_[0].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_Any.Instantiate(
          server->metric_entity());
}

void RedisServiceImpl::Handle(InboundCall* inbound_call) {
  auto* call = down_cast<RedisInboundCall*>(CHECK_NOTNULL(inbound_call));

  DVLOG(4) << "Asked to handle a call " << call->ToString();

  google::protobuf::Message* empty_response = new EmptyResponsePB();
  RpcContext* context = new RpcContext(call, nullptr, empty_response, metrics_[0]);
  DVLOG(4) << "call->serialized_request() is " << call->serialized_request();
  const RedisClientCommand& c = call->GetClientCommand();

  // Process the request.
  DVLOG(4) << "Processing request from client ";
  const int size = c.cmd_args.size();
  for (int i = 0; i < size; i++) {
    LOG(INFO) << i + 1 << " / " << size << " : " << c.cmd_args[i].ToDebugString(8);
  }

  // Send the result.
  DVLOG(4) << "Responding to call " << call->ToString();
  context->RespondSuccess();
  DVLOG(4) << "Done Responding.";
}

}  // namespace redisserver
}  // namespace yb
