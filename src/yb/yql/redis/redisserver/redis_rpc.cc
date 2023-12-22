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

#include "yb/client/client_fwd.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/format.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"

#include "yb/yql/redis/redisserver/redis_encoding.h"
#include "yb/yql/redis/redisserver/redis_parser.h"
#include "yb/util/flags.h"

using std::string;

using namespace std::literals;
using namespace std::placeholders;
using namespace yb::size_literals;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);
DEFINE_UNKNOWN_uint64(redis_max_concurrent_commands, 1,
              "Max number of redis commands received from single connection, "
              "that could be processed concurrently");
DEFINE_UNKNOWN_uint64(redis_max_batch, 500, "Max number of redis commands that forms batch");
DEFINE_UNKNOWN_int32(rpcz_max_redis_query_dump_size, 4_KB,
             "The maximum size of the Redis query string in the RPCZ dump.");
DEFINE_UNKNOWN_uint64(redis_max_read_buffer_size, 128_MB,
              "Max read buffer size for Redis connections.");

DEFINE_UNKNOWN_uint64(redis_max_queued_bytes, 128_MB,
              "Max number of bytes in queued redis commands.");

DEFINE_UNKNOWN_int32(
    redis_connection_soft_limit_grace_period_sec, 60,
    "The duration for which the outbound data needs to exceeed the softlimit "
    "before the connection gets closed down.");

namespace yb {
namespace redisserver {

RedisConnectionContext::RedisConnectionContext(
    rpc::GrowableBufferAllocator* allocator,
    const MemTrackerPtr& call_tracker)
    : ConnectionContextWithQueue(
          FLAGS_redis_max_concurrent_commands, FLAGS_redis_max_queued_bytes),
      read_buffer_(allocator, FLAGS_redis_max_read_buffer_size),
      call_mem_tracker_(call_tracker) {}

RedisConnectionContext::~RedisConnectionContext() {}

Result<rpc::ProcessCallsResult> RedisConnectionContext::ProcessCalls(
    const rpc::ConnectionPtr& connection, const IoVecs& data,
    rpc::ReadBufferFull read_buffer_full) {
  if (!can_enqueue()) {
    return rpc::ProcessCallsResult{
      .consumed = 0,
      .buffer = Slice(),
    };
  }

  if (!parser_) {
    parser_.reset(new RedisParser(data));
  } else {
    parser_->Update(data);
  }
  RedisParser& parser = *parser_;
  size_t begin_of_batch = 0;
  // Try to parse all received commands to a single RedisInboundCall.
  for (;;) {
    auto end_of_command = VERIFY_RESULT(parser.NextCommand());
    if (end_of_command == 0) {
      break;
    }
    end_of_batch_ = end_of_command;
    if (++commands_in_batch_ >= FLAGS_redis_max_batch) {
      rpc::CallData call_data(end_of_batch_ - begin_of_batch);
      IoVecsToBuffer(data, begin_of_batch, end_of_batch_, call_data.data());
      RETURN_NOT_OK(HandleInboundCall(connection, commands_in_batch_, &call_data));
      begin_of_batch = end_of_batch_;
      commands_in_batch_ = 0;
    }
  }
  // Create call for rest of commands.
  // Do not form new call if we are in a middle of command.
  // It means that soon we should receive remaining data for this command and could wait.
  if (commands_in_batch_ > 0 && (end_of_batch_ == IoVecsFullSize(data) || read_buffer_full)) {
    rpc::CallData call_data(end_of_batch_ - begin_of_batch);
    IoVecsToBuffer(data, begin_of_batch, end_of_batch_, call_data.data());
    RETURN_NOT_OK(HandleInboundCall(connection, commands_in_batch_, &call_data));
    begin_of_batch = end_of_batch_;
    commands_in_batch_ = 0;
  }
  parser.Consume(begin_of_batch);
  end_of_batch_ -= begin_of_batch;
  return rpc::ProcessCallsResult{
    .consumed = begin_of_batch,
    .buffer = Slice(),
  };
}

Status RedisConnectionContext::HandleInboundCall(const rpc::ConnectionPtr& connection,
                                                 size_t commands_in_batch,
                                                 rpc::CallData* data) {
  auto call = rpc::InboundCall::Create<RedisInboundCall>(connection, data->size(), this);

  Status s = call->ParseFrom(call_mem_tracker_, commands_in_batch, data);
  if (!s.ok()) {
    return s;
  }

  Enqueue(std::move(call));

  return Status::OK();
}

Status RedisConnectionContext::ReportPendingWriteBytes(size_t pending_bytes) {
  static constexpr size_t kHardLimit = 32_MB;
  static constexpr size_t kSoftLimit = 8_MB;
  auto mode = ClientMode();
  DVLOG(3) << "Connection in mode " << ToString(mode) << " has " << pending_bytes
           << " bytes in the queue.";
  if (mode == RedisClientMode::kNormal) {
    return Status::OK();
  }

  // We use the same buffering logic for subscribers and monitoring clients.
  // Close a client if:
  // 1) it exceeds the hard limit of 32MB. or
  // 2) it has been over the soft limit of 8MB for longer than 1min.
  if (pending_bytes > kHardLimit) {
    LOG(INFO) << "Connection in mode " << ToString(mode) << " has reached the HardLimit. "
              << pending_bytes << " bytes in the queue.";
    return STATUS(NetworkError, "Slow Redis Client: HardLimit exceeded.");
  } else if (pending_bytes > kSoftLimit) {
    auto now = CoarseMonoClock::Now();
    static const CoarseDuration kGracePeriod =
        std::chrono::seconds(FLAGS_redis_connection_soft_limit_grace_period_sec);
    if (soft_limit_exceeded_since_ == CoarseTimePoint::max()) {
      DVLOG(1) << "Connection in mode " << ToString(mode) << " has reached the Softlimit now. "
               << pending_bytes << " bytes in the queue.";
      soft_limit_exceeded_since_ = now;
    } else if (now > soft_limit_exceeded_since_ + kGracePeriod) {
      LOG(INFO) << "Connection in mode " << ToString(mode) << " has reached the Softlimit > "
                << yb::ToString(kGracePeriod) << " ago. " << pending_bytes
                << " bytes in the queue.";
      return STATUS(NetworkError, "Slow Redis Client: Softlimit exceeded.");
    } else {
      DVLOG(1) << "Connection in mode " << ToString(mode)
               << " has reached the Softlimit less than  " << yb::ToString(kGracePeriod) << " ago. "
               << pending_bytes;
    }
  } else {
    if (soft_limit_exceeded_since_ != CoarseTimePoint::max()) {
      DVLOG(1) << "Connection in mode " << ToString(mode) << " has dropped below the Softlimit. "
               << pending_bytes << " bytes in the queue.";
      soft_limit_exceeded_since_ = CoarseTimePoint::max();
    }
  }
  return Status::OK();
}

void RedisConnectionContext::Shutdown(const Status& status) {
  if (cleanup_hook_) {
    cleanup_hook_();
  }
  rpc::ConnectionContextWithQueue::Shutdown(status);
}

RedisInboundCall::RedisInboundCall(rpc::ConnectionPtr conn,
                                   size_t weight_in_bytes,
                                   CallProcessedListener* call_processed_listener)
    : QueueableInboundCall(std::move(conn), weight_in_bytes, call_processed_listener) {}

RedisInboundCall::~RedisInboundCall() {
  Status status =
      STATUS(ServiceUnavailable, "Shutdown connection", "" /* msg2 */, Errno(ESHUTDOWN));
  if (quit_.load(std::memory_order_acquire)) {
    rpc::ConnectionPtr conn = connection();
    rpc::Reactor* reactor = conn->reactor();
    auto scheduling_status = reactor->ScheduleReactorTask(MakeFunctorReactorTask(
        std::bind(&rpc::Reactor::DestroyConnection, reactor, conn.get(), status), conn,
        SOURCE_LOCATION()));
    LOG_IF(DFATAL, !scheduling_status.ok()) << "Failed to schedule destroy";
  }
}

Status RedisInboundCall::ParseFrom(
    const MemTrackerPtr& mem_tracker, size_t commands, rpc::CallData* data) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "RedisInboundCall", this);
  TRACE_EVENT0("rpc", "RedisInboundCall::ParseFrom");

  consumption_ = ScopedTrackedConsumption(mem_tracker, data->size());

  request_data_ = std::move(*data);
  serialized_request_ = Slice(request_data_.data(), request_data_.size());

  client_batch_.resize(commands);
  responses_.resize(commands);
  ready_.resize(commands, 0);
  RedisParser parser(IoVecs(1, iovec{request_data_.data(), request_data_.size()}));
  size_t end_of_command = 0;
  for (size_t i = 0; i != commands; ++i) {
    parser.SetArgs(&client_batch_[i]);
    end_of_command = VERIFY_RESULT(parser.NextCommand());
    DCHECK_NE(0, client_batch_[i].size());
    if (client_batch_[i].empty()) { // Should not be there.
      return STATUS(Corruption, "Empty command");
    }
    if (!end_of_command) {
      break;
    }
  }
  if (end_of_command != request_data_.size()) {
    return STATUS_FORMAT(Corruption,
                         "Parsed size $0 does not match source size $1",
                         end_of_command, request_data_.size());
  }

  parsed_.store(true, std::memory_order_release);
  return Status::OK();
}

namespace {

const rpc::RemoteMethod remote_method("yb.redisserver.RedisServerService", "anyMethod");

}

Slice RedisInboundCall::static_serialized_remote_method() {
  return remote_method.serialized_body();
}

Slice RedisInboundCall::serialized_remote_method() const {
  return remote_method.serialized_body();
}

Slice RedisInboundCall::method_name() const {
  return remote_method.method_name();
}

CoarseTimePoint RedisInboundCall::GetClientDeadline() const {
  return CoarseTimePoint::max();  // No timeout specified in the protocol for Redis.
}

void RedisInboundCall::GetCallDetails(rpc::RpcCallInProgressPB *call_in_progress_pb) const {
    rpc::RedisCallDetailsPB* redis_details = call_in_progress_pb->mutable_redis_details();
    for (const RedisClientCommand& command : client_batch_) {
        string query;
        for (const Slice& arg : command) {
            query += " " + arg.ToDebugString(FLAGS_rpcz_max_redis_query_dump_size);
        }
        redis_details->add_call_details()->set_redis_string(query);
    }
}

void RedisInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now();
  auto total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  auto trace_ = trace();
  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces
          || (trace_ && trace_->must_print())
          || total_time > FLAGS_rpc_slow_query_threshold_ms)) {
    LOG(WARNING) << ToString() << " took " << total_time << "ms. Details:";
    rpc::RpcCallInProgressPB call_in_progress_pb;
    GetCallDetails(&call_in_progress_pb);
    LOG(WARNING) << call_in_progress_pb.DebugString() << "Trace: ";
    if (trace_) {
      trace_->Dump(&LOG(WARNING), /* include_time_deltas */ true);
    }
  }
}

string RedisInboundCall::ToString() const {
  return Format("Redis Call from $0", connection()->remote());
}

bool RedisInboundCall::DumpPB(const rpc::DumpRunningRpcsRequestPB& req,
                              rpc::RpcCallInProgressPB* resp) {
  auto trace_ = trace();
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_elapsed_millis(MonoTime::Now().GetDeltaSince(timing_.time_received)
      .ToMilliseconds());

  if (!parsed_.load(std::memory_order_acquire)) {
    return true;
  }

  GetCallDetails(resp);
  return true;
}

template <class Collection, class Out>
Out DoSerializeResponses(const Collection& responses, Out out) {
  // TODO(Amit): As and when we implement get/set and its h* equivalents, we would have to
  // handle arrays, hashes etc. For now, we only support the string response.

  for (const auto& redis_response : responses) {
    string error_message = redis_response.error_message();
    if (error_message == "") {
      error_message = "Unknown error";
    }
    // Several types of error cases:
    //    1) Parsing error: The command is malformed (eg. too few arguments "SET a")
    //    2) Server error: Request to server failed due to reasons not related to the command
    //    3) Execution error: The command ran into problem during execution (eg. WrongType errors,
    //       HSET on a key that isn't a hash).

    if (redis_response.code() == RedisResponsePB_RedisStatusCode_PARSING_ERROR) {
      out = SerializeError(error_message, out);
    } else if (redis_response.code() == RedisResponsePB_RedisStatusCode_SERVER_ERROR) {
      out = SerializeError(error_message, out);
    } else if (redis_response.code() == RedisResponsePB_RedisStatusCode_NIL) {
      out = SerializeEncoded(kNilResponse, out);
    } else if (redis_response.code() != RedisResponsePB_RedisStatusCode_OK) {
      out = SerializeError(error_message, out);
    } else if (redis_response.has_string_response()) {
      out = SerializeBulkString(redis_response.string_response(), out);
    } else if (redis_response.has_status_response()) {
      out = SerializeSimpleString(redis_response.status_response(), out);
    } else if (redis_response.has_int_response()) {
      out = SerializeInteger(redis_response.int_response(), out);
    } else if (redis_response.has_array_response()) {
      if (redis_response.array_response().has_encoded() &&
          redis_response.array_response().encoded()) {
        out = SerializeEncodedArray(redis_response.array_response().elements(), out);
      } else {
        out = SerializeArray(redis_response.array_response().elements(), out);
      }
    } else if (redis_response.has_encoded_response()) {
      out = SerializeEncoded(redis_response.encoded_response(), out);
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

void RedisInboundCall::DoSerialize(rpc::ByteBlocks* output) {
  output->emplace_back(SerializeResponses(responses_));
}

RedisConnectionContext& RedisInboundCall::connection_context() const {
  return static_cast<RedisConnectionContext&>(connection()->context());
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
  if (base::subtle::NoBarrier_AtomicIncrement(&ready_[idx], 1) == 1) {
    if (!is_success) {
      had_failures_.store(true, std::memory_order_release);
    }
    responses_[idx].Swap(resp);
    // Did we get all responses and ready to send data.
    size_t responded = ready_count_.fetch_add(1, std::memory_order_release) + 1;
    if (responded == client_batch_.size()) {
      RecordHandlingCompleted();
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
  resp.set_code(RedisResponsePB_RedisStatusCode_PARSING_ERROR);
  resp.set_error_message(message.data(), message.size());
  Respond(idx, false, &resp);
}

} // namespace redisserver
} // namespace yb
