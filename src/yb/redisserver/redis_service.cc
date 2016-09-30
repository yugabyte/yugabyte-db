// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_service.h"

#include <thread>

#include "yb/common/redis_protocol.pb.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
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
using yb::RedisResponsePB;
using std::shared_ptr;
using std::vector;
using strings::Substitute;

void RedisServiceImpl::PopulateHandlers() {
  CHECK_EQ(kMethodCount, sizeof(kRedisCommandTable) / sizeof(RedisCommandInfo))
      << "Expect to see " << kMethodCount << " elements in kRedisCommandTable";
  for (int i = 0; i < kMethodCount; i++) {
    command_name_to_info_map_[RedisServiceImpl::kRedisCommandTable[i].name] =
        &(RedisServiceImpl::kRedisCommandTable[i]);
  }
}

RedisServiceImpl::RedisCommandFunctionPtr RedisServiceImpl::FetchHandler(
    const std::vector<Slice>& cmd_args) {
  CHECK_GE(cmd_args.size(), 1) << "Need to have at least the command name in the argument vector.";
  string cmd_name = cmd_args[0].ToString();
  ToLowerCase(cmd_name, &cmd_name);
  auto iter = command_name_to_info_map_.find(cmd_name);
  if (iter == command_name_to_info_map_.end()) {
    // TODO(Amit): Convert this to a more aggressive error, once we have implemented the desired
    // subset of commands.
    LOG(ERROR) << "Command " << cmd_name << " not yet supported.";
    return &RedisServiceImpl::DummyCommand;
  }
  // Verify that the command has the required number of arguments.
  //
  // TODO(Amit): Fail gracefully, i.e. close the connection or send a error message without
  // killing the server itself.
  const RedisCommandInfo cmd_info = *iter->second;
  if (cmd_info.arity < 0) {  // -X means that the command needs >= X arguments.
    CHECK_GE(cmd_args.size(), -1 * cmd_info.arity) << "Requested command " << cmd_name
                                                   << " does not have enough arguments.";
  } else {
    CHECK_EQ(cmd_args.size(), cmd_info.arity) << "Requested command " << cmd_name
                                              << " has wrong number of arguments.";
  }
  return cmd_info.function_ptr;
}

RedisServiceImpl::RedisServiceImpl(RedisServer* server)
    : RedisServerServiceIf(server->metric_entity()) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  metrics_[0].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_Any.Instantiate(
          server->metric_entity());
  PopulateHandlers();
}

void RedisServiceImpl::Handle(InboundCall* inbound_call) {
  auto* call = down_cast<RedisInboundCall*>(CHECK_NOTNULL(inbound_call));

  DVLOG(4) << "Asked to handle a call " << call->ToString();

  rpc::RedisClientCommand& c = call->GetClientCommand();

  auto handler = FetchHandler(c.cmd_args);
  (this->*handler)(call, &c);
}

void RedisServiceImpl::EchoCommand(InboundCall* call, RedisClientCommand* c) {
  RedisResponsePB* echo_response = new RedisResponsePB();
  echo_response->set_string_response(c->cmd_args[1].ToString());
  VLOG(4) << "Responding to Echo with " << c->cmd_args[1].ToString();
  RpcContext* context = new RpcContext(call, nullptr, echo_response, metrics_[0]);
  context->RespondSuccess();
  VLOG(4) << "Done Responding to Echo.";
}

void RedisServiceImpl::GetCommand(InboundCall* call, RedisClientCommand* c) {
  DummyCommand(call, c);  // Not yet Implemented.
}

void RedisServiceImpl::SetCommand(InboundCall* call, RedisClientCommand* c) {
  DummyCommand(call, c);  // Not yet Implemented.
}

void RedisServiceImpl::DummyCommand(InboundCall* call, RedisClientCommand* c) {
  // process the request
  LOG(INFO) << " Processing request from client ";
  int size = c->cmd_args.size();
  for (int i = 0; i < size; i++) {
    LOG(INFO) << i + 1 << " / " << size << " : " << c->cmd_args[i].ToDebugString(8);
  }

  // Send the result.
  DVLOG(4) << "Responding to call " << call->ToString();
  RedisResponsePB* ok_response = new RedisResponsePB();
  ok_response->set_string_response("OK");
  RpcContext* context = new RpcContext(call, nullptr, ok_response, metrics_[0]);
  context->RespondSuccess();
  DVLOG(4) << "Done Responding.";
}

}  // namespace redisserver
}  // namespace yb
