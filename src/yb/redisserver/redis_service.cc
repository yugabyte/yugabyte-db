// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_service.h"

#include "yb/redisserver/redis.pb.h"
#include "yb/redisserver/redis_server.h"
#include "yb/rpc/rpc_context.h"

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

RedisServiceImpl::RedisServiceImpl(RedisServer* server)
    : RedisServerServiceIf(server->metric_entity()) {
  metrics_[0].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_Any.Instantiate(
          server->metric_entity());
}

void RedisServiceImpl::Handle(::yb::rpc::InboundCall* call) {
  LOG(INFO) << "Asked to handle a call " << call->ToString();

  google::protobuf::Message* empty_response = new EmptyResponsePB();
  ::yb::rpc::RpcContext* context = new ::yb::rpc::RpcContext(call,
                                                             nullptr,
                                                             empty_response,
                                                             metrics_[0]);

  LOG(INFO) << "call->serialized_request() is " << call->serialized_request();
  LOG(INFO) << "Extracting the slice";
  Slice data = call->serialized_request();
  LOG(INFO) << "slice is " << data;

  // Send the result.
  LOG(INFO) << "Responding to call " << call->ToString();
  context->RespondSuccess();
  LOG(INFO) << "Done Responding.";
}

} // namespace redisserver
} // namespace yb
