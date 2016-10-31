// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_service.h"

#include <thread>

#include "yb/client/client.h"
#include "yb/client/redis_helpers.h"
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

using yb::client::RedisConstants;
using yb::client::YBRedisWriteOp;
using yb::client::RedisReadOpForGetKey;
using yb::client::RedisWriteOpForSetKV;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
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

RedisServiceImpl::RedisServiceImpl(RedisServer* server, string yb_tier_master_addresses)
    : RedisServerServiceIf(server->metric_entity()), server_(server) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  metrics_[0].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_Any.Instantiate(
          server->metric_entity());
  PopulateHandlers();
  SetUpYBClient(yb_tier_master_addresses);
}

void RedisServiceImpl::SetUpYBClient(string yb_tier_master_addresses) {
  YBClientBuilder client_builder;
  client_builder.default_rpc_timeout(MonoDelta::FromSeconds(kRpcTimeoutSec));
  client_builder.add_master_server_addr(yb_tier_master_addresses);
  CHECK_OK(client_builder.Build(&client_));

  const string table_name(RedisConstants::kRedisTableName);
  // Ensure that the table has already been created.
  {
    YBSchema existing_schema;
    CHECK(client_->GetTableSchema(table_name, &existing_schema).ok()) << "Table '" << table_name
                                                                      << "' does not exist yet";
  }
  CHECK_OK(client_->OpenTable(table_name, &table_));

  session_ = client_->NewSession();
  CHECK_OK(session_->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
  read_only_session_ = client_->NewSession(true);
  CHECK_OK(read_only_session_->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
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
  VLOG(1) << "Processing Get.";
  auto get_op = RedisReadOpForGetKey(table_.get(), c->cmd_args[1].ToString());
  CHECK_OK(read_only_session_->ReadSync(get_op));
  RedisResponsePB* ok_response = new RedisResponsePB(get_op->response());
  RpcContext* context = new RpcContext(call, nullptr, ok_response, metrics_[0]);
  context->RespondSuccess();
}

void RedisServiceImpl::SetCommand(InboundCall* call, RedisClientCommand* c) {
  VLOG(1) << "Processing Set.";
  // TODO: Using a synchronous call, as we do here, is going to quickly block up all
  // our threads. Switch this to FlushAsync as soon as it is ready.
  auto set_op = RedisWriteOpForSetKV(table_.get(),
                                     c->cmd_args[1].ToString(),
                                     c->cmd_args[2].ToString());
  CHECK_OK(session_->Apply(set_op));
  Status status = session_->Flush();
  LOG(INFO) << "Received status from Flush " << status.ToString(true);
  RedisResponsePB* ok_response = new RedisResponsePB();
  RpcContext* context = new RpcContext(call, nullptr, ok_response, metrics_[0]);

  if (status.ok()) {
    ok_response->set_string_response("OK");
    context->RespondSuccess();
  } else {
    context->RespondFailure(status);
  }
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
