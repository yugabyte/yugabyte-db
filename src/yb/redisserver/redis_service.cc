// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_service.h"

#include <thread>

#include "yb/client/callbacks.h"
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
using yb::client::YBRedisReadOp;
using yb::client::YBRedisWriteOp;
using yb::client::RedisReadOpForGetKey;
using yb::client::RedisWriteOpForSetKV;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::client::YBStatusCallback;
using yb::rpc::InboundCall;
using yb::rpc::RedisInboundCall;
using yb::rpc::RpcContext;
using yb::rpc::RedisClientCommand;
using yb::RedisResponsePB;
using std::shared_ptr;
using std::unique_ptr;
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

class GetCommandCb : public YBStatusCallback {
 public:
  GetCommandCb(
      shared_ptr<client::YBSession> session, InboundCall* call, shared_ptr<YBRedisReadOp> read_op,
      rpc::RpcMethodMetrics metrics)
      : session_(session), redis_call_(call), read_op_(read_op), metrics_(metrics) {}

  void Run(const Status& status) override;

 private:
  shared_ptr<client::YBSession> session_;
  unique_ptr<InboundCall> redis_call_;
  shared_ptr<YBRedisReadOp> read_op_;
  rpc::RpcMethodMetrics metrics_;
};

void GetCommandCb::Run(const Status& status) {
  VLOG(3) << "Received status from call " << status.ToString(true);

  if (status.ok()) {
    RedisResponsePB* ok_response = new RedisResponsePB(read_op_->response());
    RpcContext* context = new RpcContext(redis_call_.release(), nullptr, ok_response, metrics_);
    context->RespondSuccess();
  } else {
    RpcContext* context = new RpcContext(redis_call_.release(), nullptr, nullptr, metrics_);
    context->RespondFailure(status);
  }

  delete this;
}

void RedisServiceImpl::GetCommand(InboundCall* call, RedisClientCommand* c) {
  VLOG(1) << "Processing Get.";
  auto read_only_session = client_->NewSession(true);
  CHECK_OK(read_only_session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
  auto get_op = RedisReadOpForGetKey(table_.get(), c->cmd_args[1].ToString());
  read_only_session->ReadAsync(
      get_op, new GetCommandCb(read_only_session, call, get_op, metrics_[0]));
}

class SetCommandCb : public YBStatusCallback {
 public:
  SetCommandCb(
      shared_ptr<client::YBSession> session, InboundCall* call, rpc::RpcMethodMetrics metrics)
      : session_(session), redis_call_(call), metrics_(metrics) {}

  void Run(const Status& status) override;

 private:
  shared_ptr<client::YBSession> session_;
  unique_ptr<InboundCall> redis_call_;
  rpc::RpcMethodMetrics metrics_;
};

void SetCommandCb::Run(const Status& status) {
  VLOG(3) << "Received status from call " << status.ToString(true);

  if (status.ok()) {
    RedisResponsePB* ok_response = new RedisResponsePB();
    RpcContext* context = new RpcContext(redis_call_.release(), nullptr, ok_response, metrics_);
    ok_response->set_string_response("OK");
    context->RespondSuccess();
  } else {
    vector<client::YBError*> errors;
    bool overflowed;
    ElementDeleter d(&errors);
    if (session_.get() != nullptr) {
      session_->GetPendingErrors(&errors, &overflowed);
      for (const auto error : errors) {
        LOG(WARNING) << "Explicit error while inserting: " << error->status().ToString();
      }
    }

    RpcContext* context = new RpcContext(redis_call_.release(), nullptr, nullptr, metrics_);
    context->RespondFailure(status);
  }

  delete this;
}

void RedisServiceImpl::SetCommand(InboundCall* call, RedisClientCommand* c) {
  auto tmp_session = client_->NewSession();
  CHECK_OK(tmp_session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
  CHECK_OK(tmp_session->Apply(
      RedisWriteOpForSetKV(table_.get(), c->cmd_args[1].ToString(), c->cmd_args[2].ToString())));
  // Callback will delete itself, after processing the callback.
  tmp_session->FlushAsync(new SetCommandCb(tmp_session, call, metrics_[0]));
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
