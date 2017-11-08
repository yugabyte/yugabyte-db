//
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
#include "yb/yql/redis/redisserver/redis_rpc.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/yql/redis/redisserver/redis_encoding.h"
#include "yb/yql/redis/redisserver/redis_parser.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/logging.h"
#include "yb/util/size_literals.h"

#include "yb/util/debug/trace_event.h"

#include "yb/util/memory/memory.h"

using yb::operator"" _KB;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);
DEFINE_uint64(redis_max_concurrent_commands, 1,
              "Max number of redis commands received from single connection, "
              "that could be processed concurrently");
DEFINE_uint64(redis_max_batch, 500, "Max number of redis commands that forms batch");
DEFINE_int32(rpcz_max_redis_query_dump_size, 4_KB,
             "The maximum size of the Redis query string in the RPCZ dump.");


using namespace std::literals; // NOLINT
using namespace std::placeholders;

namespace yb {
namespace redisserver {

RedisConnectionContext::RedisConnectionContext()
    : ConnectionContextWithQueue(FLAGS_redis_max_concurrent_commands) {}

RedisConnectionContext::~RedisConnectionContext() {}

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
    DCHECK_NE(0, client_batch_[i].size());
    if (client_batch_[i].empty()) { // Should not be there
      return STATUS(Corruption, "Empty command");
    }
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

  parsed_.store(true, std::memory_order_release);
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
  MonoTime now = MonoTime::Now();
  auto total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), /* include_time_deltas */ true);
  }
}

string RedisInboundCall::ToString() const {
  return Format("Redis Call from $0", connection()->remote());
}

bool RedisInboundCall::DumpPB(const rpc::DumpRunningRpcsRequestPB& req,
                              rpc::RpcCallInProgressPB* resp) {
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now().GetDeltaSince(timing_.time_received)
      .ToMicroseconds());

  if (!parsed_.load(std::memory_order_acquire)) {
    return true;
  }

  // RedisClientBatch client_batch_
  rpc::RedisCallDetailsPB* redis_details = resp->mutable_redis_details();
  for (RedisClientCommand command : client_batch_) {
    string query = "";
    for (Slice arg : command) {
      query += " " + arg.ToDebugString(FLAGS_rpcz_max_redis_query_dump_size);
    }
    redis_details->add_call_details()->set_redis_string(query);
  }

  return true;
}

template <class Collection, class Out>
Out DoSerializeResponses(const Collection& responses, Out out) {
  // TODO(Amit): As and when we implement get/set and its h* equivalents, we would have to
  // handle arrays, hashes etc. For now, we only support the string response.

  for (const auto& redis_response : responses) {
    if (redis_response.code() == RedisResponsePB_RedisStatusCode_SERVER_ERROR) {
      out = SerializeError("Request was unable to be processed from server.", out);
    } else if (redis_response.code() == RedisResponsePB_RedisStatusCode_NOT_FOUND) {
      out = SerializeEncoded(kNilResponse, out);
    } else if (redis_response.code() != RedisResponsePB_RedisStatusCode_OK) {
      // We send a nil response for all non-ok statuses as of now.
      // TODO: Follow redis error messages.
      out = SerializeError("Error: Something wrong", out);
    } else if (redis_response.has_string_response()) {
      out = SerializeBulkString(redis_response.string_response(), out);
    } else if (redis_response.has_int_response()) {
      out = SerializeInteger(redis_response.int_response(), out);
    } else if (redis_response.has_array_response()) {
      if (redis_response.array_response().has_encoded() &&
          redis_response.array_response().encoded()) {
        out = SerializeEncodedArray(redis_response.array_response().elements(), out);
      } else {
        out = SerializeArray(redis_response.array_response().elements(), out);
      }
    } else {
      out = SerializeEncoded(kOkResponse, out);
    }
  }
  return out;
}

template <class Collection>
RefCntBuffer SerializeResponses(const Collection& responses) {
  constexpr size_t kZero = 0;
  size_t size = DoSerializeResponses(responses, kZero);
  RefCntBuffer result(size);
  uint8_t* end = DoSerializeResponses(responses, result.udata());
  DCHECK_EQ(result.uend(), end);
  return result;
}

void RedisInboundCall::Serialize(std::deque<RefCntBuffer>* output) const {
  output->push_back(SerializeResponses(responses_));
}

void RedisInboundCall::RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code,
                                      const Status& status) {
  for (size_t i = 0; i != client_batch_.size(); ++i) {
    RespondFailure(i, status);
  }
}

// We wait until all responses are ready for batch embedded in this call.
void RedisInboundCall::Respond(size_t idx, bool is_success, RedisResponsePB* resp) {
  // Did we set response for command at this index already?
  VLOG(2) << "Responding to '" << client_batch_[idx][0] << "' with " << resp->ShortDebugString();
  if (ready_[idx].fetch_add(1, std::memory_order_relaxed) == 0) {
    if (!is_success) {
      had_failures_.store(true, std::memory_order_release);
    }
    responses_[idx].Swap(resp);
    // Did we get all responses and ready to send data.
    size_t responded = ready_count_.fetch_add(1, std::memory_order_release) + 1;
    if (responded == client_batch_.size()) {
      RecordHandlingCompleted(nullptr);
      QueueResponse(!had_failures_.load(std::memory_order_acquire));
    }
  }
}

void RedisInboundCall::RespondSuccess(size_t idx,
                                      const rpc::RpcMethodMetrics& metrics,
                                      RedisResponsePB* resp) {
  Respond(idx, true, resp);
  metrics.handler_latency->Increment((MonoTime::Now() - timing_.time_handled).ToMicroseconds());
}

void RedisInboundCall::RespondFailure(size_t idx, const Status& status) {
  RedisResponsePB resp;
  Slice message = status.message();
  resp.set_code(RedisResponsePB_RedisStatusCode_SERVER_ERROR);
  resp.set_error_message(message.data(), message.size());
  Respond(idx, false, &resp);
}

} // namespace redisserver
} // namespace yb
