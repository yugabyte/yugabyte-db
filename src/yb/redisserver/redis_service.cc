// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_service.h"

#include <thread>

#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client_builder-internal.h"
#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_parser.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/redisserver/redis_server.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/redis_rpc.h"
#include "yb/util/bytes_formatter.h"

#define DEFINE_REDIS_histogram_EX(name_identifier, label_str, desc_str) \
  METRIC_DEFINE_histogram( \
      server, BOOST_PP_CAT(handler_latency_yb_redisserver_RedisServerService_, name_identifier), \
      (label_str), yb::MetricUnit::kMicroseconds, \
      "Microseconds spent handling " desc_str " RPC requests", \
      60000000LU, 2)

#define DEFINE_REDIS_histogram(name_identifier, capitalized_name_str) \
  DEFINE_REDIS_histogram_EX( \
      name_identifier, \
      "yb.redisserver.RedisServerService." BOOST_STRINGIZE(name_identifier) " RPC Time", \
      "yb.redisserver.RedisServerService." capitalized_name_str "Command()")

DEFINE_REDIS_histogram(get, "Get");
DEFINE_REDIS_histogram(hget, "HGet");
DEFINE_REDIS_histogram(strlen, "StrLen");
DEFINE_REDIS_histogram(exists, "Exists");
DEFINE_REDIS_histogram(getrange, "GetRange");
DEFINE_REDIS_histogram(set, "SetCommand");
DEFINE_REDIS_histogram(hset, "HSet");
DEFINE_REDIS_histogram(getset, "GetSet");
DEFINE_REDIS_histogram(append, "Append");
DEFINE_REDIS_histogram(del, "Del");
DEFINE_REDIS_histogram(setrange, "SetRange");
DEFINE_REDIS_histogram(incr, "Incr");
DEFINE_REDIS_histogram(echo, "Echo");
DEFINE_REDIS_histogram_EX(error,
                          "yb.redisserver.RedisServerService.AnyMethod RPC Time",
                          "yb.redisserver.RedisServerService.ErrorUnsupportedMethod()");
DEFINE_REDIS_histogram_EX(get_internal,
                          "yb.redisserver.RedisServerService.Get RPC Time",
                          "in yb.client.Get");
DEFINE_REDIS_histogram_EX(set_internal,
                          "yb.redisserver.RedisServerService.Set RPC Time",
                          "in yb.client.Set");

DEFINE_int32(redis_service_yb_client_timeout_millis, 60000,
             "Timeout in milliseconds for RPC calls from Redis service to master/tserver");

namespace yb {
namespace redisserver {

#define SETUP_METRICS_FOR_METHOD(method, idx)                                          \
  do {                                                                                 \
    CHECK_EQ(#method, RedisServiceImpl::kRedisCommandTable[idx].name)                  \
        << "Expected command " << #method << " at index " << idx;                      \
    command_name_to_info_map_[#method] = &(RedisServiceImpl::kRedisCommandTable[idx]); \
    metrics_[#method] = yb::rpc::RpcMethodMetrics();                                   \
    metrics_[#method].handler_latency =                                                \
        METRIC_handler_latency_yb_redisserver_RedisServerService_##method.Instantiate( \
            server_->metric_entity());                                                 \
  } while (0)

using yb::client::YBRedisReadOp;
using yb::client::YBRedisWriteOp;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::client::YBStatusCallback;
using yb::client::YBTableName;
using yb::rpc::InboundCall;
using yb::rpc::InboundCallPtr;
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
  int idx = 0;
  SETUP_METRICS_FOR_METHOD(get, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(hget, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(strlen, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(exists, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(getrange, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(set, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(hset, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(getset, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(append, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(del, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(setrange, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(incr, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(echo, idx);
  idx++;
  CHECK_EQ(kMethodCount, idx);

  // Set up metrics for erroneous calls.
  metrics_["error"] = yb::rpc::RpcMethodMetrics();
  metrics_["error"].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_error.Instantiate(
          server_->metric_entity());
  metrics_["get_internal"] = yb::rpc::RpcMethodMetrics();
  metrics_["get_internal"].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_get_internal.Instantiate(
          server_->metric_entity());
  metrics_["set_internal"] = yb::rpc::RpcMethodMetrics();
  metrics_["set_internal"].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_set_internal.Instantiate(
          server_->metric_entity());
}

const RedisServiceImpl::RedisCommandInfo* RedisServiceImpl::FetchHandler(
    const std::vector<Slice>& cmd_args) {
  CHECK_GE(cmd_args.size(), 1) << "Need to have at least the command name in the argument vector.";
  string cmd_name = cmd_args[0].ToString();
  ToLowerCase(cmd_name, &cmd_name);
  auto iter = command_name_to_info_map_.find(cmd_name);
  if (iter == command_name_to_info_map_.end()) {
    LOG(ERROR) << "Command " << cmd_name << " not yet supported.";
    return nullptr;
  }
  return iter->second;
}

void RedisServiceImpl::ValidateAndHandle(const RedisServiceImpl::RedisCommandInfo* cmd_info,
                                         InboundCallPtr call,
                                         RedisClientCommand* c) {
  // Ensure that we have the required YBClient(s) initialized.
  if (!yb_client_initialized_.load()) {
    auto status = SetUpYBClient(yb_tier_master_addresses_);
    if (!status.ok()) {
      RespondWithFailure(StrCat("Could not open .redis table. ", status.ToString()),
                         std::move(call),
                         c);
      return;
    }
  }

  // Handle the current redis command.
  if (cmd_info == nullptr) {
    RespondWithFailure("Unsupported call.", std::move(call), c);
  } else if (cmd_info->arity < 0 && c->cmd_args.size() < -1 * cmd_info->arity) {
    // -X means that the command needs >= X arguments.
    LOG(ERROR) << "Requested command " << c->cmd_args[0] << " does not have enough arguments."
               << " At least " << -cmd_info->arity << " expected, but " << c->cmd_args.size()
               << " found.";
    RespondWithFailure("Too few arguments.", std::move(call), c);
  } else if (cmd_info->arity > 0 && c->cmd_args.size() != cmd_info->arity) {
    // X (> 0) means that the command needs exactly X arguments.
    LOG(ERROR) << "Requested command " << c->cmd_args[0] << " has wrong number of arguments.";
    RespondWithFailure("Wrong number of arguments.", std::move(call), c);
  } else {
    // Handle the call.
    (this->*cmd_info->function_ptr)(std::move(call), c);
  }
}

void RedisServiceImpl::ConfigureSession(client::YBSession *session) {
  session->SetTimeoutMillis(FLAGS_redis_service_yb_client_timeout_millis);
}

RedisServiceImpl::RedisServiceImpl(RedisServer* server, string yb_tier_master_addresses)
    : RedisServerServiceIf(server->metric_entity()),
      yb_tier_master_addresses_(yb_tier_master_addresses),
      yb_client_initialized_(false),
      server_(server) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  PopulateHandlers();
}

Status RedisServiceImpl::SetUpYBClient(string yb_tier_master_addresses) {
  std::lock_guard<std::mutex> guard(yb_mutex_);
  if (!yb_client_initialized_.load()) {
    YBClientBuilder client_builder;
    client_builder.set_client_name("redis_ybclient");
    client_builder.default_rpc_timeout(MonoDelta::FromSeconds(kRpcTimeoutSec));
    client_builder.add_master_server_addr(yb_tier_master_addresses);
    client_builder.set_metric_entity(server_->metric_entity());
    RETURN_NOT_OK(client_builder.Build(&client_));

    const YBTableName table_name(kRedisTableName);
    RETURN_NOT_OK(client_->OpenTable(table_name, &table_));
    yb_client_initialized_.store(true);
  }
  return Status::OK();
}

void RedisServiceImpl::Handle(InboundCall* inbound_call) {
  InboundCallPtr call_ptr(inbound_call);
  auto* call = down_cast<RedisInboundCall*>(CHECK_NOTNULL(inbound_call));

  DVLOG(4) << "Asked to handle a call " << call->ToString();
  rpc::RedisClientCommand& c = call->GetClientCommand();

  auto cmd_info = FetchHandler(c.cmd_args);
  ValidateAndHandle(cmd_info, std::move(call_ptr), &c);
}

void RedisServiceImpl::EchoCommand(InboundCallPtr call, RedisClientCommand* c) {
  RedisResponsePB* echo_response = new RedisResponsePB();
  echo_response->set_string_response(c->cmd_args[1].ToString());
  VLOG(4) << "Responding to Echo with " << c->cmd_args[1].ToString();
  RpcContext* context = new RpcContext(call.get(), nullptr, echo_response, metrics_["echo"]);
  context->RespondSuccess();
  VLOG(4) << "Done Responding to Echo.";
}

class ReadCommandCb : public YBStatusCallback {
 public:
  ReadCommandCb(shared_ptr<client::YBSession> session,
                rpc::InboundCallPtr call,
                shared_ptr<YBRedisReadOp> read_op,
                rpc::RpcMethodMetrics metrics,
                rpc::RpcMethodMetrics metrics_internal)
      : session_(session),
        redis_call_(std::move(call)),
        read_op_(read_op),
        metrics_(metrics),
        metrics_internal_(metrics_internal),
        start_(MonoTime::Now(MonoTime::FINE)) {}

  void Run(const Status& status) override;

 private:
  shared_ptr<client::YBSession> session_;
  rpc::InboundCallPtr redis_call_;
  shared_ptr<YBRedisReadOp> read_op_;
  rpc::RpcMethodMetrics metrics_;
  rpc::RpcMethodMetrics metrics_internal_;
  MonoTime start_;
};

void ReadCommandCb::Run(const Status& status) {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  metrics_internal_.handler_latency->Increment(now.GetDeltaSince(start_).ToMicroseconds());
  VLOG(3) << "Received status from call " << status.ToString(true);

  if (status.ok()) {
    RedisResponsePB* ok_response = new RedisResponsePB(read_op_->response());
    RpcContext* context = new RpcContext(redis_call_.get(), nullptr, ok_response, metrics_);
    context->RespondSuccess();
  } else {
    RpcContext* context = new RpcContext(redis_call_.get(), nullptr, nullptr, metrics_);
    context->RespondFailure(status);
  }

  delete this;
}

void RedisServiceImpl::ReadCommand(
    yb::rpc::InboundCallPtr call,
    yb::rpc::RedisClientCommand* c,
    const std::string& command_name,
    Status(*parse)(YBRedisReadOp*, const std::vector<Slice>&)) {
  VLOG(1) << "Processing " << command_name << ".";
  auto read_only_session = client_->NewSession(true);
  ConfigureSession(read_only_session.get());
  CHECK_OK(read_only_session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));

  shared_ptr<YBRedisReadOp> read_op(table_->NewRedisRead());
  Status s = parse(read_op.get(), c->cmd_args);
  if (!s.ok()) {
    RespondWithFailure(s.message().ToString(), call, c);
    return;
  }
  auto command = new ReadCommandCb(read_only_session,
                                   std::move(call),
                                   read_op,
                                   metrics_[command_name],
                                   metrics_["get_internal"]);

  read_only_session->ReadAsync(read_op, command);
}

void RedisServiceImpl::GetCommand(InboundCallPtr call, RedisClientCommand* c) {
  ReadCommand(std::move(call), c, "get", &ParseGet);
}

void RedisServiceImpl::HGetCommand(InboundCallPtr call, RedisClientCommand* c) {
  ReadCommand(std::move(call), c, "hget", &ParseHGet);
}

void RedisServiceImpl::StrLenCommand(InboundCallPtr call, RedisClientCommand* c) {
  ReadCommand(std::move(call), c, "strlen", &ParseStrLen);
}

void RedisServiceImpl::ExistsCommand(InboundCallPtr call, RedisClientCommand* c) {
  ReadCommand(std::move(call), c, "exists", &ParseExists);
}

void RedisServiceImpl::GetRangeCommand(InboundCallPtr call, RedisClientCommand* c) {
  ReadCommand(std::move(call), c, "exists", &ParseGetRange);
}

class WriteCommandCb : public YBStatusCallback {
 public:
  WriteCommandCb(shared_ptr<client::YBSession> session,
                 rpc::InboundCallPtr call,
                 shared_ptr<YBRedisWriteOp> write_op,
                 rpc::RpcMethodMetrics metrics,
                 rpc::RpcMethodMetrics metrics_internal)
      : session_(session),
        redis_call_(std::move(call)),
        write_op_(write_op),
        metrics_(metrics),
        metrics_internal_(metrics_internal),
        start_(MonoTime::Now(MonoTime::FINE)) {}

  void Run(const Status& status) override;

 private:
  shared_ptr<client::YBSession> session_;
  rpc::InboundCallPtr redis_call_;
  shared_ptr<YBRedisWriteOp> write_op_;
  rpc::RpcMethodMetrics metrics_;
  rpc::RpcMethodMetrics metrics_internal_;
  MonoTime start_;
};

void WriteCommandCb::Run(const Status& status) {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  metrics_internal_.handler_latency->Increment(now.GetDeltaSince(start_).ToMicroseconds());
  VLOG(3) << "Received status from call " << status.ToString(true);

  if (status.ok()) {
    RedisResponsePB* ok_response = new RedisResponsePB(write_op_->response());
    RpcContext* context = new RpcContext(redis_call_.get(), nullptr, ok_response, metrics_);
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

    RpcContext* context = new RpcContext(redis_call_.get(), nullptr, nullptr, metrics_);
    context->RespondFailure(status);
  }

  delete this;
}

void RedisServiceImpl::WriteCommand(
    InboundCallPtr call,
    RedisClientCommand* c,
    const string& command_name,
    Status(*parse)(YBRedisWriteOp*, const std::vector<Slice>&)) {
  VLOG(1) << "Processing " << command_name << ".";

  auto tmp_session = client_->NewSession();
  ConfigureSession(tmp_session.get());
  CHECK_OK(tmp_session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
  shared_ptr<YBRedisWriteOp> write_op(table_->NewRedisWrite());
  Status s = parse(write_op.get(), c->cmd_args);
  if (!s.ok()) {
    RespondWithFailure(s.message().ToString(), std::move(call), c);
    return;
  }
  CHECK_OK(tmp_session->Apply(write_op));
  // Callback will delete itself, after processing the callback.
  auto command = new WriteCommandCb(tmp_session,
                                    std::move(call),
                                    write_op,
                                    metrics_[command_name],
                                    metrics_["set_internal"]);
  tmp_session->FlushAsync(command);
}

void RedisServiceImpl::SetCommand(InboundCallPtr call, RedisClientCommand* c) {
  WriteCommand(std::move(call), c, "set", &ParseSet);
}

void RedisServiceImpl::HSetCommand(InboundCallPtr call, RedisClientCommand* c) {
  WriteCommand(std::move(call), c, "hset", &ParseHSet);
}

void RedisServiceImpl::GetSetCommand(InboundCallPtr call, RedisClientCommand* c) {
  WriteCommand(std::move(call), c, "getset", &ParseGetSet);
}

void RedisServiceImpl::AppendCommand(InboundCallPtr call, RedisClientCommand* c) {
  WriteCommand(std::move(call), c, "append", &ParseAppend);
}

void RedisServiceImpl::DelCommand(InboundCallPtr call, RedisClientCommand* c) {
  WriteCommand(std::move(call), c, "del", &ParseDel);
}

void RedisServiceImpl::SetRangeCommand(InboundCallPtr call, RedisClientCommand* c) {
  WriteCommand(std::move(call), c, "setrange", &ParseSetRange);
}

void RedisServiceImpl::IncrCommand(InboundCallPtr call, RedisClientCommand* c) {
  WriteCommand(std::move(call), c, "incr", &ParseIncr);
}

void RedisServiceImpl::RespondWithFailure(
    const string& error, InboundCallPtr call, RedisClientCommand* c) {
  // process the request
  DVLOG(4) << " Processing request from client ";
  int size = c->cmd_args.size();
  for (int i = 0; i < size; i++) {
    DVLOG(4) << i + 1 << " / " << size << " : " << c->cmd_args[i].ToDebugString(8);
  }

  // Send the result.
  DVLOG(4) << "Responding to call " << call->ToString() << " with failure " << error;
  string cmd = c->cmd_args[0].ToString();
  RpcContext* context = new RpcContext(call.get(), nullptr, nullptr, metrics_["error"]);
  context->RespondFailure(STATUS(
      RuntimeError, StringPrintf("%s : %s", error.c_str(),  cmd.c_str())));
}

}  // namespace redisserver
}  // namespace yb
