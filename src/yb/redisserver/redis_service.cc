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

METRIC_DEFINE_histogram(
    server, handler_latency_yb_redisserver_RedisServerService_get,
    "yb.redisserver.RedisServerService.AnyMethod RPC Time", yb::MetricUnit::kMicroseconds,
    "Microseconds spent handling "
    "yb.redisserver.RedisServerService.GetCommand() "
    "RPC requests",
    60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_redisserver_RedisServerService_set,
    "yb.redisserver.RedisServerService.AnyMethod RPC Time", yb::MetricUnit::kMicroseconds,
    "Microseconds spent handling "
    "yb.redisserver.RedisServerService.SetCommand() "
    "RPC requests",
    60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_redisserver_RedisServerService_echo,
    "yb.redisserver.RedisServerService.AnyMethod RPC Time", yb::MetricUnit::kMicroseconds,
    "Microseconds spent handling "
    "yb.redisserver.RedisServerService.EchoCommand() "
    "RPC requests",
    60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_redisserver_RedisServerService_error,
    "yb.redisserver.RedisServerService.AnyMethod RPC Time", yb::MetricUnit::kMicroseconds,
    "Microseconds spent handling "
    "yb.redisserver.RedisServerService.ErrorUnsupportedMethod() "
    "RPC requests",
    60000000LU, 2);
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
  int idx = 0;
  SETUP_METRICS_FOR_METHOD(get, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(set, idx);
  idx++;
  SETUP_METRICS_FOR_METHOD(echo, idx);
  idx++;
  CHECK_EQ(kMethodCount, idx);

  // Set up metrics for erroneous calls.
  metrics_["error"] = yb::rpc::RpcMethodMetrics();
  metrics_["error"].handler_latency =
      METRIC_handler_latency_yb_redisserver_RedisServerService_error.Instantiate(
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

void RedisServiceImpl::ValidateAndHandle(
    const RedisServiceImpl::RedisCommandInfo* cmd_info, InboundCall* call, RedisClientCommand* c) {
  if (cmd_info == nullptr) {
    RespondWithFailure("Unsupported call.", call, c);
  } else if (cmd_info->arity < 0 && c->cmd_args.size() < -1 * cmd_info->arity) {
    // -X means that the command needs >= X arguments.
    LOG(ERROR) << "Requested command " << c->cmd_args[0] << " does not have enough arguments.";
    RespondWithFailure("Too few arguments.", call, c);
  } else if (cmd_info->arity > 0 && c->cmd_args.size() != cmd_info->arity) {
    // X (> 0) means that the command needs exactly X arguments.
    LOG(ERROR) << "Requested command " << c->cmd_args[0] << " has wrong number of arguments.";
    RespondWithFailure("Wrong number of arguments.", call, c);
  } else {
    // Handle the call.
    (this->*cmd_info->function_ptr)(call, c);
  }
}

RedisServiceImpl::RedisServiceImpl(RedisServer* server, string yb_tier_master_addresses)
    : RedisServerServiceIf(server->metric_entity()),
      yb_tier_master_addresses_(yb_tier_master_addresses),
      yb_client_initialized_(false),
      server_(server) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  PopulateHandlers();
}

void RedisServiceImpl::SetUpYBClient(string yb_tier_master_addresses) {
  std::lock_guard<std::mutex> guard(yb_mutex_);
  if (!yb_client_initialized_.load()) {
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
    yb_client_initialized_.store(true);
  }
}

void RedisServiceImpl::Handle(InboundCall* inbound_call) {
  auto* call = down_cast<RedisInboundCall*>(CHECK_NOTNULL(inbound_call));

  DVLOG(4) << "Asked to handle a call " << call->ToString();
  if (!yb_client_initialized_.load()) SetUpYBClient(yb_tier_master_addresses_);

  rpc::RedisClientCommand& c = call->GetClientCommand();

  auto cmd_info = FetchHandler(c.cmd_args);
  ValidateAndHandle(cmd_info, call, &c);
}

void RedisServiceImpl::EchoCommand(InboundCall* call, RedisClientCommand* c) {
  RedisResponsePB* echo_response = new RedisResponsePB();
  echo_response->set_string_response(c->cmd_args[1].ToString());
  VLOG(4) << "Responding to Echo with " << c->cmd_args[1].ToString();
  RpcContext* context = new RpcContext(call, nullptr, echo_response, metrics_["echo"]);
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
      get_op, new GetCommandCb(read_only_session, call, get_op, metrics_["get"]));
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

constexpr int64_t kMaxTTlSec = std::numeric_limits<int64_t >::max() / 1000000;

void RedisServiceImpl::SetCommand(InboundCall* call, RedisClientCommand* c) {
  auto tmp_session = client_->NewSession();

  // If args don't match, then we assume there's one EX field set.
  // TODO: Cover more cases for various options when supported.
  if (c->cmd_args.size() != 3 && c->cmd_args.size() != 5) {
    LOG(ERROR) << "Requested command " << c->cmd_args[0] << " has wrong number of arguments.";
    RespondWithFailure("Wrong number of arguments.", call, c);
  }
  if (c->cmd_args.size() == 5) {
    if (c->cmd_args[3].ToString() != "EX") {
      LOG(ERROR) << "Requested command is expected to have 'EX' as 4th argument, found: "
                 << c->cmd_args[3].ToString() << ".";
      RespondWithFailure("Invalid arguments.", call, c);
    }
    const string ttl = c->cmd_args[4].ToString();
    try {
      const int64_t val = std::stoll(ttl);
      if (val <= 0 || val > kMaxTTlSec) {
        throw new std::out_of_range("TTL value should be positive and less than " + kMaxTTlSec);
      }
    } catch (std::invalid_argument e) {
      LOG(ERROR) << "Requested command " << c->cmd_args[0] << " has non-numeric TTL: " << ttl;
      RespondWithFailure("Unable to parse TTL as a valid number: " + ttl, call, c);
    } catch (std::out_of_range e) {
      LOG(ERROR) << "Requested command " << c->cmd_args[0] << " has out of range TTL: " << ttl;
      RespondWithFailure("TTL is out of bounds: " + ttl, call, c);
    }
  }

  CHECK_OK(tmp_session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
  CHECK_OK(tmp_session->Apply(
      RedisWriteOpForSetKV(table_.get(), c->cmd_args)));
  // Callback will delete itself, after processing the callback.
  tmp_session->FlushAsync(new SetCommandCb(tmp_session, call, metrics_["set"]));
}

void RedisServiceImpl::RespondWithFailure(
    const string& error, InboundCall* call, RedisClientCommand* c) {
  // process the request
  DVLOG(4) << " Processing request from client ";
  int size = c->cmd_args.size();
  for (int i = 0; i < size; i++) {
    DVLOG(4) << i + 1 << " / " << size << " : " << c->cmd_args[i].ToDebugString(8);
  }

  // Send the result.
  DVLOG(4) << "Responding to call " << call->ToString() << " with failure " << error;
  RpcContext* context = new RpcContext(call, nullptr, nullptr, metrics_[0]);
  context->RespondFailure(STATUS(
      RuntimeError, StringPrintf("%s : %s", error.c_str(), c->cmd_args[0].ToString().c_str())));
}

}  // namespace redisserver
}  // namespace yb
