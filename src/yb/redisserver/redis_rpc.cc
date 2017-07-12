//
// Copyright (c) YugaByte, Inc.
//
#include "yb/redisserver/redis_rpc.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/redisserver/redis_encoding.h"
#include "yb/redisserver/redis_parser.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/logging.h"
#include "yb/util/size_literals.h"

#include "yb/util/debug/trace_event.h"

#include "yb/util/memory/memory.h"

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);
DEFINE_uint64(redis_max_concurrent_commands, 1,
              "Max number of redis commands received from single connection, "
              "that could be processed concurrently");
DEFINE_uint64(redis_max_batch, 1, "Max number of redis commands that forms batch");


using namespace std::literals; // NOLINT
using namespace std::placeholders;

namespace yb {
namespace redisserver {

RedisConnectionContext::RedisConnectionContext()
    : ConnectionContextWithQueue(FLAGS_redis_max_concurrent_commands) {}

RedisConnectionContext::~RedisConnectionContext() {}

void RedisConnectionContext::RunNegotiation(rpc::ConnectionPtr connection,
                                            const MonoTime& deadline) {
  CHECK_EQ(connection->direction(), rpc::ConnectionDirection::SERVER);
  connection->CompleteNegotiation(Status::OK());
}

Status RedisConnectionContext::ProcessCalls(const rpc::ConnectionPtr& connection,
                                            Slice slice,
                                            size_t* consumed) {
  if (!parser_) {
    parser_.reset(new RedisParser(slice));
  } else {
    parser_->Update(slice);
  }
  RedisParser& parser = *parser_;
  *consumed = 0;
  const uint8_t* begin_of_batch = slice.data();
  const uint8_t* end_of_batch = begin_of_batch;
  // Try to parse all received commands to a single RedisInboundCall.
  for(;;) {
    const uint8_t* end_of_command = nullptr;
    RETURN_NOT_OK(parser.NextCommand(&end_of_command));
    if (end_of_command == nullptr) {
      break;
    }
    end_of_batch = end_of_command;
    if (++commands_in_batch_ >= FLAGS_redis_max_batch) {
      RETURN_NOT_OK(HandleInboundCall(connection,
                                      commands_in_batch_,
                                      Slice(begin_of_batch, end_of_batch)));
      begin_of_batch = end_of_batch;
      commands_in_batch_ = 0;
    }
  }
  // Create call for rest of commands.
  // Do not form new call if we are in a middle of command.
  // It means that soon we should receive remaining data for this command and could wait.
  if (commands_in_batch_ > 0 && end_of_batch == slice.end()) {
    RETURN_NOT_OK(HandleInboundCall(connection,
                                    commands_in_batch_,
                                    Slice(begin_of_batch, end_of_batch)));
    begin_of_batch = end_of_batch;
    commands_in_batch_ = 0;
  }
  *consumed = begin_of_batch - slice.data();
  parser.Consume(*consumed);
  return Status::OK();
}

Status RedisConnectionContext::HandleInboundCall(const rpc::ConnectionPtr& connection,
                                                 size_t commands_in_batch,
                                                 Slice source) {
  auto reactor = connection->reactor();
  DCHECK(reactor->IsCurrentThread());

  auto call = std::make_shared<RedisInboundCall>(connection, call_processed_listener());

  Status s = call->ParseFrom(commands_in_batch, source);
  if (!s.ok()) {
    return s;
  }

  Enqueue(std::move(call));

  return Status::OK();
}

size_t RedisConnectionContext::BufferLimit() {
  return kMaxBufferSize;
}

RedisInboundCall::RedisInboundCall(rpc::ConnectionPtr conn,
                                   CallProcessedListener call_processed_listener)
    : QueueableInboundCall(std::move(conn), std::move(call_processed_listener)) {
}

Status RedisInboundCall::ParseFrom(size_t commands, Slice source) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "RedisInboundCall", this);
  TRACE_EVENT0("rpc", "RedisInboundCall::ParseFrom");

  request_data_.assign(source.data(), source.end());
  serialized_request_ = source = Slice(request_data_.data(), request_data_.size());

  client_batch_.resize(commands);
  responses_.resize(commands);
  ready_.reserve(commands);
  for (size_t i = 0; i != commands; ++i)
    ready_.emplace_back(0);
  Status status;
  RedisParser parser(source);
  const uint8_t* end_of_command = nullptr;
  for (size_t i = 0; i != commands; ++i) {
    parser.SetArgs(&client_batch_[i]);
    RETURN_NOT_OK(parser.NextCommand(&end_of_command));
    if (!end_of_command) {
      break;
    }
  }
  if (end_of_command != source.end()) {
    return STATUS_SUBSTITUTE(Corruption,
                             "Parsed size $0 does not match source size $1",
                             end_of_command - source.data(),
                             source.size());
  }

  return Status::OK();
}

const std::string& RedisInboundCall::service_name() const {
  static std::string result = "yb.redisserver.RedisServerService"s;
  return result;
}

const std::string& RedisInboundCall::method_name() const {
  static std::string result = "anyMethod"s;
  return result;
}

MonoTime RedisInboundCall::GetClientDeadline() const {
  return MonoTime::Max();  // No timeout specified in the protocol for Redis.
}

void RedisInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  auto total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), /* include_time_deltas */ true);
  }
}

string RedisInboundCall::ToString() const {
  return Format("Redis Call from $0", connection()->remote());
}

void RedisInboundCall::DumpPB(const rpc::DumpRunningRpcsRequestPB& req,
                              rpc::RpcCallInProgressPB* resp) {
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now(MonoTime::FINE).GetDeltaSince(timing_.time_received)
      .ToMicroseconds());
}

util::RefCntBuffer SerializeResponseBuffer(const RedisResponsePB& redis_response) {
  if (redis_response.code() == RedisResponsePB_RedisStatusCode_SERVER_ERROR) {
    return util::RefCntBuffer(EncodeAsError("Request was unable to be processed from server."));
  }

  // TODO(Amit): As and when we implement get/set and its h* equivalents, we would have to
  // handle arrays, hashes etc. For now, we only support the string response.

  if (redis_response.code() != RedisResponsePB_RedisStatusCode_OK) {
    // We send a nil response for all non-ok statuses as of now.
    // TODO: Follow redis error messages.
    return util::RefCntBuffer(kNilResponse);
  }

  if (redis_response.has_string_response()) {
    return util::RefCntBuffer(EncodeAsBulkString(redis_response.string_response()));
  }

  if (redis_response.has_int_response()) {
    return util::RefCntBuffer(EncodeAsInteger(redis_response.int_response()));
  }

  if (redis_response.has_array_response()) {
    const auto& resp_array = redis_response.array_response().elements();
    vector<string> responses;
    for (const auto& elt : resp_array) {
      responses.push_back(elt == "" ? kNilResponse : EncodeAsBulkString(elt));
    }
    return util::RefCntBuffer(EncodeAsArrays(responses));
  }

  static util::RefCntBuffer ok_response(EncodeAsSimpleString("OK"));
  return ok_response;
}

void RedisInboundCall::Serialize(std::deque<util::RefCntBuffer>* output) const {
  for (const auto& buf : responses_) {
    output->push_back(buf);
  }
}

void RedisInboundCall::RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code,
                                      const Status& status) {
  auto buffer = util::RefCntBuffer(EncodeAsError(status.message().ToBuffer()));
  for (size_t i = 0; i != client_batch_.size(); ++i)
    Respond(i, buffer, false);
}

// We wait until all responses are ready for batch embedded in this call.
void RedisInboundCall::Respond(size_t idx, const util::RefCntBuffer& buffer, bool is_success) {
  // Did we set response for command at this index already?
  if (ready_[idx].fetch_add(1, std::memory_order_relaxed) == 0) {
    if (!is_success) {
      had_failures_.store(true, std::memory_order_release);
    }
    responses_[idx] = buffer;
    // Did we get all responses and ready to send data.
    size_t responded = ready_count_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (responded == client_batch_.size()) {
      RecordHandlingCompleted(nullptr);
      QueueResponse(had_failures_.load(std::memory_order_acquire));
    }
  }
}

void RedisInboundCall::RespondSuccess(size_t idx,
                                      const RedisResponsePB& resp,
                                      const rpc::RpcMethodMetrics& metrics) {
  Respond(idx, SerializeResponseBuffer(resp), true);
  metrics.handler_latency->Increment((MonoTime::FineNow() - timing_.time_handled).ToMicroseconds());
}

void RedisInboundCall::RespondFailure(size_t idx, const Status& status) {
  Respond(idx, util::RefCntBuffer(EncodeAsError(status.message().ToBuffer())), false);
}

} // namespace redisserver
} // namespace yb
