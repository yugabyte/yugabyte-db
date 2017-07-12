// Copyright (c) YugaByte, Inc.

#include "yb/redisserver/redis_service.h"

#include <thread>

#include <boost/algorithm/string/case_conv.hpp>

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client_builder-internal.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_parser.h"
#include "yb/redisserver/redis_rpc.h"
#include "yb/redisserver/redis_server.h"

#include "yb/rpc/rpc_context.h"
#include "yb/tserver/tablet_server.h"
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

#define REDIS_COMMANDS \
    ((get, Get, 2, READ)) \
    ((hget, HGet, 3, READ)) \
    ((strlen, StrLen, 2, READ)) \
    ((exists, Exists, 2, READ)) \
    ((getrange, GetRange, 4, READ)) \
    ((set, Set, -3, WRITE)) \
    ((hset, HSet, 4, WRITE)) \
    ((getset, GetSet, 3, WRITE)) \
    ((append, Append, 3, WRITE)) \
    ((del, Del, 2, WRITE)) \
    ((setrange, SetRange, 4, WRITE)) \
    ((incr, Incr, 2, WRITE)) \
    ((echo, Echo, 2, ECHO)) \
    /**/

#define DO_DEFINE_HISTOGRAM(name, cname, arity, type) \
  DEFINE_REDIS_histogram(name, BOOST_PP_STRINGIZE(cname));
#define DEFINE_HISTOGRAM(r, data, elem) DO_DEFINE_HISTOGRAM elem

BOOST_PP_SEQ_FOR_EACH(DEFINE_HISTOGRAM, ~, REDIS_COMMANDS)

using yb::client::YBRedisReadOp;
using yb::client::YBRedisWriteOp;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::client::YBStatusCallback;
using yb::client::YBTableName;
using yb::RedisResponsePB;

namespace yb {
namespace redisserver {

namespace {

template<class Op>
class Callback: public YBStatusCallback {
 public:
  typedef boost::container::small_vector<std::shared_ptr<Op>,
                                         RedisClientBatch::static_capacity> Ops;
  // typedef std::vector<std::shared_ptr<Op>> Ops;

  Callback(std::shared_ptr<client::YBSession> session,
           std::shared_ptr<RedisInboundCall> redis_call,
           size_t idx,
           rpc::RpcMethodMetrics metrics,
           rpc::RpcMethodMetrics metrics_internal)
      : session_(std::move(session)),
        redis_call_(std::move(redis_call)),
        idx_(idx),
        metrics_(std::move(metrics)),
        metrics_internal_(std::move(metrics_internal)),
        start_(MonoTime::FineNow()) {}

  void AddOp(std::shared_ptr<Op> op) {
    ops_.push_back(std::move(op));
  }

  void MoveOps(Ops* source) {
    ops_ = std::move(*source);
  }

  void Run(const Status& status) override {
    MonoTime now = MonoTime::Now(MonoTime::FINE);
    metrics_internal_.handler_latency->Increment(now.GetDeltaSince(start_).ToMicroseconds());
    VLOG(3) << "Received status from call " << status.ToString(true);

    if (status.ok()) {
      for (size_t i = 0; i != ops_.size(); ++i) {
        redis_call_->RespondSuccess(idx_ + i, ops_[i]->response(), metrics_);
      }
    } else {
      client::CollectedErrors errors;
      bool overflowed;
      if (session_.get() != nullptr) {
        session_->GetPendingErrors(&errors, &overflowed);
        for (const auto& error : errors) {
          LOG(WARNING) << "Explicit error while inserting: " << error->status().ToString();
        }
      }

      for (size_t i = 0; i != ops_.size(); ++i) {
        redis_call_->RespondFailure(idx_ + i, status);
      }
    }

    delete this;
  }
 private:
  std::shared_ptr<client::YBSession> session_;
  std::shared_ptr<RedisInboundCall> redis_call_;
  size_t idx_;
  Ops ops_;
  rpc::RpcMethodMetrics metrics_;
  rpc::RpcMethodMetrics metrics_internal_;
  MonoTime start_;
};

struct BatchContext {
  std::shared_ptr<RedisInboundCall> call;
  rpc::RpcMethodMetrics metrics_get_internal;
  rpc::RpcMethodMetrics metrics_set_internal;

  std::shared_ptr<YBSession> session;
  rpc::RpcMethodMetrics metrics;
  Callback<YBRedisWriteOp>::Ops writes;

  void Flush(size_t idx) {
    if (!session) {
      return;
    }
    if (!writes.empty()) {
      auto callback = new Callback<YBRedisWriteOp>(session,
                                                   call,
                                                   idx - writes.size(),
                                                   metrics,
                                                   metrics_set_internal);
      callback->MoveOps(&writes);
      writes.clear();
      session->FlushAsync(callback);
    }
    session.reset();
  }

  void Apply(size_t idx, std::shared_ptr<YBRedisWriteOp> op) {
    CHECK_OK(session->Apply(op));
    writes.push_back(std::move(op));
  }

  void Apply(size_t idx, std::shared_ptr<YBRedisReadOp> op) {
    auto callback = new Callback<YBRedisReadOp>(session,
                                                call,
                                                idx,
                                                metrics,
                                                metrics_get_internal);
    callback->AddOp(op);
    session->ReadAsync(op, callback);
  }
};

// Information about RedisCommand(s) that we support.
//
// Based on "struct redisCommand" from redis/src/server.h
//
// The remaining fields in "struct redisCommand" from redis' implementation are
// currently unused. They will be added and when we start using them.
struct RedisCommandInfo {
  string name;
  std::function<void(const RedisCommandInfo&,
                     size_t,
                     BatchContext*)> functor;
  int arity;
  yb::rpc::RpcMethodMetrics metrics;
};

} // namespace

class RedisServiceImpl::Impl {
 public:
  Impl(RedisServer* server, string yb_tier_master_address);

  void Handle(yb::rpc::InboundCallPtr call_ptr);

 private:
  void SetupMethod(const RedisCommandInfo& info) {
    command_name_to_info_map_[info.name] = info;
  }

  void EchoCommand(
      const RedisCommandInfo& info,
      size_t idx,
      std::string (*parse)(const RedisClientCommand&),
      BatchContext* context);

  template<class Op>
  void Command(
      const RedisCommandInfo& info,
      size_t idx,
      Status(*parse)(Op*, const RedisClientCommand&),
      BatchContext* context);

  constexpr static int kRpcTimeoutSec = 5;

  void PopulateHandlers();
  // Fetches the appropriate handler for the command, nullptr if none exists.
  const RedisCommandInfo* FetchHandler(const RedisClientCommand& cmd_args);
  CHECKED_STATUS SetUpYBClient();
  void RespondWithFailure(std::shared_ptr<RedisInboundCall> call,
                          size_t idx,
                          const std::string& error);

  std::unordered_map<std::string, RedisCommandInfo> command_name_to_info_map_;
  yb::rpc::RpcMethodMetrics metrics_error_;
  yb::rpc::RpcMethodMetrics metrics_get_internal_;
  yb::rpc::RpcMethodMetrics metrics_set_internal_;

  std::string yb_tier_master_addresses_;
  std::mutex yb_mutex_;  // Mutex that protects the creation of client_ and table_.
  std::atomic<bool> yb_client_initialized_;
  std::shared_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBTable> table_;

  RedisServer* server_;
};

std::string ParseEcho(const RedisClientCommand& command) {
  return command[1].ToBuffer();
}

#define REDIS_METRIC(name) \
    BOOST_PP_CAT(METRIC_handler_latency_yb_redisserver_RedisServerService_, name)

#define READ_COMMAND Command<YBRedisReadOp>
#define WRITE_COMMAND Command<YBRedisWriteOp>
#define ECHO_COMMAND EchoCommand

#define DO_POPULATE_HANDLER(name, cname, arity, type) \
  { \
    auto functor = [this](const RedisCommandInfo& info, \
                          size_t idx, \
                          BatchContext* context) { \
      BOOST_PP_CAT(type, _COMMAND)(info, idx, &BOOST_PP_CAT(Parse, cname), context); \
    }; \
    yb::rpc::RpcMethodMetrics metrics(REDIS_METRIC(name).Instantiate(metric_entity)); \
    SetupMethod({BOOST_PP_STRINGIZE(name), functor, arity, std::move(metrics)}); \
  } \
  /**/

#define POPULATE_HANDLER(z, data, elem) DO_POPULATE_HANDLER elem

void RedisServiceImpl::Impl::PopulateHandlers() {
  auto metric_entity = server_->metric_entity();
  BOOST_PP_SEQ_FOR_EACH(POPULATE_HANDLER, ~, REDIS_COMMANDS);

  // Set up metrics for erroneous calls.
  metrics_error_.handler_latency = REDIS_METRIC(error).Instantiate(metric_entity);
  metrics_get_internal_.handler_latency = REDIS_METRIC(get_internal).Instantiate(metric_entity);
  metrics_set_internal_.handler_latency = REDIS_METRIC(set_internal).Instantiate(metric_entity);
}

const RedisCommandInfo* RedisServiceImpl::Impl::FetchHandler(const RedisClientCommand& cmd_args) {
  if (cmd_args.size() < 1) {
    return nullptr;
  }
  std::string cmd_name = cmd_args[0].ToBuffer();
  boost::to_lower(cmd_name);
  auto iter = command_name_to_info_map_.find(cmd_name);
  if (iter == command_name_to_info_map_.end()) {
    LOG(ERROR) << "Command " << cmd_name << " not yet supported.";
    return nullptr;
  }
  return &iter->second;
}

RedisServiceImpl::Impl::Impl(RedisServer* server, string yb_tier_master_addresses)
    : yb_tier_master_addresses_(std::move(yb_tier_master_addresses)),
      yb_client_initialized_(false),
      server_(server) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  PopulateHandlers();
}

Status RedisServiceImpl::Impl::SetUpYBClient() {
  std::lock_guard<std::mutex> guard(yb_mutex_);
  if (!yb_client_initialized_.load()) {
    YBClientBuilder client_builder;
    client_builder.set_client_name("redis_ybclient");
    client_builder.default_rpc_timeout(MonoDelta::FromSeconds(kRpcTimeoutSec));
    client_builder.add_master_server_addr(yb_tier_master_addresses_);
    client_builder.set_metric_entity(server_->metric_entity());
    RETURN_NOT_OK(client_builder.Build(&client_));

    // Add proxy to call local tserver if available.
    if (server_->tserver() != nullptr && server_->tserver()->proxy() != nullptr) {
      client_->AddTabletServerProxy(
          server_->tserver()->permanent_uuid(), server_->tserver()->proxy());
    }

    const YBTableName table_name(kRedisKeyspaceName, kRedisTableName);
    RETURN_NOT_OK(client_->OpenTable(table_name, &table_));
    yb_client_initialized_.store(true);
  }
  return Status::OK();
}

void RedisServiceImpl::Impl::Handle(rpc::InboundCallPtr call_ptr) {
  auto call = std::static_pointer_cast<RedisInboundCall>(call_ptr);

  DVLOG(4) << "Asked to handle a call " << call->ToString();

  // Ensure that we have the required YBClient(s) initialized.
  if (!yb_client_initialized_.load(std::memory_order_acquire)) {
    auto status = SetUpYBClient();
    if (!status.ok()) {
      auto message = StrCat("Could not open .redis table. ", status.ToString());
      for (size_t idx = 0; idx != call->client_batch().size(); ++idx) {
        RespondWithFailure(call, idx, message);
      }
      return;
    }
  }

  // Call could contain several commands, i.e. batch.
  // We process them as follows:
  // Each read commands are processed individually.
  // Sequential write commands use single session and the same batcher.
  BatchContext context = { call, metrics_get_internal_, metrics_set_internal_ };
  const auto& batch = call->client_batch();
  for (size_t idx = 0; idx != batch.size(); ++idx) {
    const RedisClientCommand& c = batch[idx];

    auto cmd_info = FetchHandler(c);

    // Handle the current redis command.
    if (cmd_info == nullptr) {
      RespondWithFailure(call, idx, "Unsupported call.");
    } else if (cmd_info->arity < 0 && c.size() < static_cast<size_t>(-1 * cmd_info->arity)) {
      // -X means that the command needs >= X arguments.
      LOG(ERROR) << "Requested command " << c[0] << " does not have enough arguments."
                 << " At least " << -cmd_info->arity << " expected, but " << c.size()
                 << " found.";
      RespondWithFailure(call, idx, "Too few arguments.");
    } else if (cmd_info->arity > 0 && c.size() != cmd_info->arity) {
      // X (> 0) means that the command needs exactly X arguments.
      LOG(ERROR) << "Requested command " << c[0] << " has wrong number of arguments.";
      RespondWithFailure(call, idx, "Wrong number of arguments.");
    } else {
      // Handle the call.
      cmd_info->functor(*cmd_info, idx, &context);
    }
  }
  context.Flush(batch.size());
}

void RedisServiceImpl::Impl::EchoCommand(
    const RedisCommandInfo& info,
    size_t idx,
    std::string (*parse)(const RedisClientCommand&),
    BatchContext* context) {
  const auto& command = context->call->client_batch()[idx];
  RedisResponsePB echo_response;
  echo_response.set_string_response(parse(command));
  VLOG(4) << "Responding to Echo with " << command[1].ToBuffer();
  context->call->RespondSuccess(idx, echo_response, info.metrics);
  VLOG(4) << "Done Responding to Echo.";
}

template<class Op>
void RedisServiceImpl::Impl::Command(
    const RedisCommandInfo& info,
    size_t idx,
    Status(*parse)(Op*, const RedisClientCommand&),
    BatchContext* context) {
  const bool read = std::is_same<Op, YBRedisReadOp>::value;

  VLOG(1) << "Processing " << info.name << ".";
  auto* session = context->session.get();
  if (!session || session->is_read_only() != read) {
    context->Flush(idx);
    context->session = client_->NewSession(read);
    context->metrics = info.metrics;
    session = context->session.get();
    session->SetTimeoutMillis(FLAGS_redis_service_yb_client_timeout_millis);
    CHECK_OK(session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
  }

  auto op = std::make_shared<Op>(table_);
  const auto& command = context->call->client_batch()[idx];
  Status s = parse(op.get(), command);
  if (!s.ok()) {
    RespondWithFailure(context->call, idx, s.message().ToBuffer());
    return;
  }
  context->Apply(idx, std::move(op));
}

void RedisServiceImpl::Impl::RespondWithFailure(
    std::shared_ptr<RedisInboundCall> call,
    size_t idx,
    const std::string& error) {
  // process the request
  DVLOG(4) << " Processing request from client ";
  const auto& command = call->client_batch()[idx];
  size_t size = command.size();
  for (size_t i = 0; i < size; i++) {
    DVLOG(4) << i + 1 << " / " << size << " : " << command[i].ToDebugString(8);
  }

  // Send the result.
  DVLOG(4) << "Responding to call " << call->ToString() << " with failure " << error;
  std::string cmd = command[0].ToBuffer();
  call->RespondFailure(idx, STATUS_FORMAT(RuntimeError, "$0: $1", cmd, error));
}

RedisServiceImpl::RedisServiceImpl(RedisServer* server, string yb_tier_master_address)
    : RedisServerServiceIf(server->metric_entity()),
      impl_(new Impl(server, std::move(yb_tier_master_address))) {}

RedisServiceImpl::~RedisServiceImpl() {
}

void RedisServiceImpl::Handle(yb::rpc::InboundCallPtr call) {
  impl_->Handle(std::move(call));
}

}  // namespace redisserver
}  // namespace yb
