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

#include "yb/rpc/yb_rpc.h"

#include <google/protobuf/io/coded_stream.h>

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/serialization.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/memory/memory.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"

using std::string;

using google::protobuf::io::CodedInputStream;
using namespace yb::size_literals;
using namespace std::literals;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_uint64(rpc_max_message_size);

DEFINE_bool(enable_rpc_keepalive, true, "Whether to enable RPC keepalive mechanism");

DEFINE_uint64(min_sidecar_buffer_size, 16_KB, "Minimal buffer to allocate for sidecar");

DEFINE_test_flag(uint64, yb_inbound_big_calls_parse_delay_ms, false,
                 "Test flag for simulating slow parsing of inbound calls larger than "
                 "rpc_throttle_threshold_bytes");

using std::placeholders::_1;
DECLARE_uint64(rpc_connection_timeout_ms);
DECLARE_int32(rpc_slow_query_threshold_ms);
DECLARE_int64(rpc_throttle_threshold_bytes);

namespace yb {
namespace rpc {

constexpr const auto kHeartbeatsPerTimeoutPeriod = 3;

namespace {

// One byte after YugaByte is reserved for future use. It could control type of connection.
const char kConnectionHeaderBytes[] = "YB\1";
const size_t kConnectionHeaderSize = sizeof(kConnectionHeaderBytes) - 1;

OutboundDataPtr ConnectionHeaderInstance() {
  static OutboundDataPtr result = std::make_shared<StringOutboundData>(
      kConnectionHeaderBytes, kConnectionHeaderSize, "ConnectionHeader");
  return result;
}

const char kEmptyMsgLengthPrefix[kMsgLengthPrefixLength] = {0};

class HeartbeatOutboundData : public StringOutboundData {
 public:
  bool IsHeartbeat() const override { return true; }

  static std::shared_ptr<HeartbeatOutboundData> Instance() {
    static std::shared_ptr<HeartbeatOutboundData> instance(new HeartbeatOutboundData());
    return instance;
  }

 private:
  HeartbeatOutboundData() :
      StringOutboundData(kEmptyMsgLengthPrefix, kMsgLengthPrefixLength, "Heartbeat") {}
};

} // namespace

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::MessageLite;
using google::protobuf::io::CodedOutputStream;

YBConnectionContext::YBConnectionContext(
    size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker,
    const MemTrackerPtr& call_tracker)
    : parser_(buffer_tracker, kMsgLengthPrefixLength, 0 /* size_offset */,
              FLAGS_rpc_max_message_size, IncludeHeader::kFalse, rpc::SkipEmptyMessages::kTrue,
              this),
      read_buffer_(receive_buffer_size, buffer_tracker),
      call_tracker_(call_tracker) {}

void YBConnectionContext::SetEventLoop(ev::loop_ref* loop) {
  loop_ = loop;
}

void YBConnectionContext::Shutdown(const Status& status) {
  timer_.Shutdown();
  loop_ = nullptr;
}

YBConnectionContext::~YBConnectionContext() {}

namespace {

CoarseMonoClock::Duration Timeout() {
  return FLAGS_rpc_connection_timeout_ms * 1ms;
}

CoarseMonoClock::Duration HeartbeatPeriod() {
  return Timeout() / kHeartbeatsPerTimeoutPeriod;
}

} // namespace

uint64_t YBConnectionContext::ExtractCallId(InboundCall* call) {
  return down_cast<YBInboundCall*>(call)->call_id();
}

Result<ProcessCallsResult> YBInboundConnectionContext::ProcessCalls(
    const ConnectionPtr& connection, const IoVecs& data, ReadBufferFull read_buffer_full) {
  if (state_ == RpcConnectionPB::NEGOTIATING) {
    // We assume that header is fully contained in the first block.
    if (data[0].iov_len < kConnectionHeaderSize) {
      return ProcessCallsResult{ 0, Slice() };
    }

    Slice slice(static_cast<const char*>(data[0].iov_base), data[0].iov_len);
    if (!slice.starts_with(kConnectionHeaderBytes, kConnectionHeaderSize)) {
      return STATUS_FORMAT(NetworkError,
                           "Invalid connection header: $0",
                           slice.ToDebugHexString());
    }
    state_ = RpcConnectionPB::OPEN;
    IoVecs data_copy(data);
    data_copy[0].iov_len -= kConnectionHeaderSize;
    data_copy[0].iov_base = const_cast<uint8_t*>(slice.data() + kConnectionHeaderSize);
    auto result = VERIFY_RESULT(
        parser().Parse(connection, data_copy, ReadBufferFull::kFalse, &call_tracker()));
    result.consumed += kConnectionHeaderSize;
    return result;
  }

  return parser().Parse(connection, data, read_buffer_full, &call_tracker());
}

namespace {

Status ThrottleRpcStatus(const MemTrackerPtr& throttle_tracker, const YBInboundCall& call) {
  if (ShouldThrottleRpc(throttle_tracker, call.request_data().size(), "Rejecting RPC call: ")) {
    return STATUS_FORMAT(ServiceUnavailable, "Call rejected due to memory pressure: $0", call);
  } else {
    return Status::OK();
  }
}

} // namespace

Status YBInboundConnectionContext::HandleCall(
    const ConnectionPtr& connection, CallData* call_data) {
  auto reactor = connection->reactor();
  DCHECK(reactor->IsCurrentThread());

  auto call = InboundCall::Create<YBInboundCall>(connection, this);

  Status s = call->ParseFrom(call_tracker(), call_data);
  if (!s.ok()) {
    return s;
  }

  s = Store(call.get());
  if (!s.ok()) {
    return s;
  }

  auto throttle_status = ThrottleRpcStatus(call_tracker(), *call);
  if (!throttle_status.ok()) {
    call->RespondFailure(ErrorStatusPB::ERROR_APPLICATION, throttle_status);
    return Status::OK();
  }

  reactor->messenger().Handle(call, Queue::kTrue);

  return Status::OK();
}

Status YBInboundConnectionContext::Connected(const ConnectionPtr& connection) {
  DCHECK_EQ(connection->direction(), Connection::Direction::SERVER);

  state_ = RpcConnectionPB::NEGOTIATING;

  connection_ = connection;
  last_write_time_ = connection->reactor()->cur_time();
  if (FLAGS_enable_rpc_keepalive) {
    timer_.Init(*loop_);
    timer_.SetCallback<
        YBInboundConnectionContext, &YBInboundConnectionContext::HandleTimeout>(this);
    timer_.Start(HeartbeatPeriod());
  }
  return Status::OK();
}

void YBInboundConnectionContext::UpdateLastWrite(const ConnectionPtr& connection) {
  last_write_time_ = connection->reactor()->cur_time();
  VLOG(4) << connection->ToString() << ": " << "Updated last_write_time_="
          << AsString(last_write_time_);
}

void YBInboundConnectionContext::HandleTimeout(ev::timer& watcher, int revents) {  // NOLINT
  const auto connection = connection_.lock();
  if (connection) {
    if (EV_ERROR & revents) {
      LOG(WARNING) << connection->ToString() << ": " << "Got an error in handle timeout";
      return;
    }

    const auto now = connection->reactor()->cur_time();

    const auto deadline =
        std::max(last_heartbeat_sending_time_, last_write_time_) + HeartbeatPeriod();
    if (now >= deadline) {
      if (last_write_time_ >= last_heartbeat_sending_time_) {
        // last_write_time_ < last_heartbeat_sending_time_ means that last heartbeat we've queued
        // for sending is still in queue due to RPC/networking issues, so no need to queue
        // another one.
        VLOG(4) << connection->ToString() << ": " << "Sending heartbeat, now: " << AsString(now)
                << ", deadline: " << ToStringRelativeToNow(deadline, now)
                << ", last_write_time_: " << AsString(last_write_time_)
                << ", last_heartbeat_sending_time_: " << AsString(last_heartbeat_sending_time_);
        auto queuing_status = connection->QueueOutboundData(HeartbeatOutboundData::Instance());
        if (queuing_status.ok()) {
          last_heartbeat_sending_time_ = now;
        } else {
          // Do not DFATAL here. This happens during shutdown and should not result in frequent
          // log messages.
          LOG(WARNING) << "Could not queue an inbound connection heartbeat message: "
                       << queuing_status;
          // We will try again at the next timer event.
        }
      }
      timer_.Start(HeartbeatPeriod());
    } else {
      timer_.Start(deadline - now);
    }
  }
}

YBInboundCall::YBInboundCall(ConnectionPtr conn, CallProcessedListener* call_processed_listener)
    : InboundCall(std::move(conn), nullptr /* rpc_metrics */, call_processed_listener) {}

YBInboundCall::YBInboundCall(RpcMetrics* rpc_metrics, const RemoteMethod& remote_method)
    : InboundCall(nullptr /* conn */, rpc_metrics, nullptr /* call_processed_listener */) {
  header_.remote_method = remote_method.serialized_body();
}

YBInboundCall::~YBInboundCall() {}

CoarseTimePoint YBInboundCall::GetClientDeadline() const {
  if (header_.timeout_ms == 0) {
    return CoarseTimePoint::max();
  }
  return ToCoarse(timing_.time_received) + header_.timeout_ms * 1ms;
}

Status YBInboundCall::ParseFrom(const MemTrackerPtr& mem_tracker, CallData* call_data) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "YBInboundCall", this);
  TRACE_EVENT0("rpc", "YBInboundCall::ParseFrom");

  Slice source(call_data->data(), call_data->size());
  RETURN_NOT_OK(ParseYBMessage(source, &header_, &serialized_request_));
  DVLOG(4) << "Parsed YBInboundCall header: " << header_.call_id;

  consumption_ = ScopedTrackedConsumption(mem_tracker, call_data->size());
  request_data_ = std::move(*call_data);

  // Adopt the service/method info from the header as soon as it's available.
  if (PREDICT_FALSE(header_.remote_method.empty())) {
    return STATUS(Corruption, "Non-connection context request header must specify remote_method");
  }

  return Status::OK();
}

size_t YBInboundCall::CopyToLastSidecarBuffer(const Slice& car) {
  if (sidecar_buffers_.empty()) {
    return 0;
  }
  auto& last_buffer =  sidecar_buffers_.back();
  auto len = std::min(last_buffer.size() - filled_bytes_in_last_sidecar_buffer_, car.size());
  memcpy(last_buffer.data() + filled_bytes_in_last_sidecar_buffer_, car.data(), len);
  filled_bytes_in_last_sidecar_buffer_ += len;

  return len;
}

size_t YBInboundCall::AddRpcSidecar(Slice car) {
  sidecar_offsets_.Add(narrow_cast<uint32_t>(total_sidecars_size_));
  total_sidecars_size_ += car.size();
  // Copy start of sidecar to existing buffer if present.
  car.remove_prefix(CopyToLastSidecarBuffer(car));

  // If sidecar did not fit into last buffer, then we should allocate a new one.
  if (!car.empty()) {
    DCHECK(sidecar_buffers_.empty() ||
           filled_bytes_in_last_sidecar_buffer_ == sidecar_buffers_.back().size());

    // Allocate new sidecar buffer and copy remaining part of sidecar to it.
    AllocateSidecarBuffer(std::max<size_t>(car.size(), FLAGS_min_sidecar_buffer_size));
    memcpy(sidecar_buffers_.back().data(), car.data(), car.size());
    filled_bytes_in_last_sidecar_buffer_ = car.size();
  }

  return num_sidecars_++;
}

void YBInboundCall::ResetRpcSidecars() {
  if (consumption_) {
    for (const auto& buffer : sidecar_buffers_) {
      consumption_.Add(-buffer.size());
    }
  }
  num_sidecars_ = 0;
  filled_bytes_in_last_sidecar_buffer_ = 0;
  total_sidecars_size_ = 0;
  sidecar_buffers_.clear();
  sidecar_offsets_.Clear();
}

void YBInboundCall::ReserveSidecarSpace(size_t space) {
  if (num_sidecars_ != 0) {
    LOG(DFATAL) << "Attempt to ReserveSidecarSpace when there are already sidecars present";
    return;
  }

  AllocateSidecarBuffer(space);
}

void YBInboundCall::AllocateSidecarBuffer(size_t size) {
  sidecar_buffers_.push_back(RefCntBuffer(size));
  if (consumption_) {
    consumption_.Add(size);
  }
}

Status YBInboundCall::SerializeResponseBuffer(AnyMessageConstPtr response, bool is_success) {
  auto body_size = response.SerializedSize();

  ResponseHeader resp_hdr;
  resp_hdr.set_call_id(header_.call_id);
  resp_hdr.set_is_error(!is_success);
  for (auto& offset : sidecar_offsets_) {
    offset += body_size;
  }
  *resp_hdr.mutable_sidecar_offsets() = std::move(sidecar_offsets_);

  response_buf_ = VERIFY_RESULT(SerializeRequest(
      body_size, total_sidecars_size_, resp_hdr, response));
  response_data_memory_usage_ = response_buf_.size();

  return Status::OK();
}

string YBInboundCall::ToString() const {
  std::lock_guard<simple_spinlock> lock(mutex_);
  if (!cached_to_string_.empty()) {
    return cached_to_string_;
  }
  cached_to_string_ = Format("Call $0 $1 => $2 (request call id $3)",
                           cleared_ ? kUnknownRemoteMethod : header_.RemoteMethodAsString(),
                           remote_address(), local_address(), header_.call_id);
  return cached_to_string_;
}

bool YBInboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                           RpcCallInProgressPB* resp) {
  header_.ToPB(resp->mutable_header());
  auto my_trace = trace();
  if (req.include_traces() && my_trace) {
    resp->set_trace_buffer(my_trace->DumpToString(true));
  }
  resp->set_elapsed_millis(MonoTime::Now().GetDeltaSince(timing_.time_received)
      .ToMilliseconds());
  return true;
}

void YBInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now();
  auto total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (header_.timeout_ms > 0) {
    double log_threshold = header_.timeout_ms * 0.75f;
    if (total_time > log_threshold) {
      // TODO: consider pushing this onto another thread since it may be slow.
      // The traces may also be too large to fit in a log message.
      LOG(WARNING) << ToString() << " took " << total_time << "ms (client timeout "
                   << header_.timeout_ms << "ms).";
      auto my_trace = trace();
      if (my_trace) {
        LOG(WARNING) << "Trace:\n" << my_trace->DumpToString(1, true);
      }
      return;
    }
  }

  auto my_trace = trace();
  if (PREDICT_FALSE(
          (my_trace && my_trace->must_print()) ||
          FLAGS_rpc_dump_all_traces ||
          total_time > FLAGS_rpc_slow_query_threshold_ms)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    if (my_trace) {
      my_trace->Dump(&LOG(INFO), true);
    }
  }
}

void YBInboundCall::DoSerialize(boost::container::small_vector_base<RefCntBuffer>* output) {
  TRACE_EVENT0("rpc", "YBInboundCall::Serialize");
  CHECK_GT(response_buf_.size(), 0);
  output->push_back(std::move(response_buf_));
  if (!sidecar_buffers_.empty()) {
    sidecar_buffers_.back().Shrink(filled_bytes_in_last_sidecar_buffer_);
    for (auto& car : sidecar_buffers_) {
      output->push_back(std::move(car));
    }
    sidecar_buffers_.clear();
  }
}

Status YBInboundCall::ParseParam(RpcCallParams* params) {
  RETURN_NOT_OK(ThrottleRpcStatus(consumption_.mem_tracker(), *this));

  auto consumption = params->ParseRequest(serialized_request());
  if (!consumption.ok()) {
    auto status = consumption.status().CloneAndPrepend(
        Format("Invalid parameter for call $0", header_.RemoteMethodAsString()));
    LOG(WARNING) << status;
    return status;
  }
  consumption_.Add(*consumption);

  if (PREDICT_FALSE(FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms > 0 &&
          implicit_cast<ssize_t>(request_data_.size()) > FLAGS_rpc_throttle_threshold_bytes)) {
    LOG(INFO) << Format("Sleeping for $0ms due to FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms",
                        FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms);
    std::this_thread::sleep_for(FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms * 1ms);
  }

  return Status::OK();
}

void YBInboundCall::RespondSuccess(AnyMessageConstPtr response) {
  TRACE_EVENT0("rpc", "InboundCall::RespondSuccess");
  Respond(response, true);
}

void YBInboundCall::RespondFailure(ErrorStatusPB::RpcErrorCodePB error_code,
                                   const Status& status) {
  TRACE_EVENT0("rpc", "InboundCall::RespondFailure");
  ErrorStatusPB err;
  err.set_message(status.ToString());
  err.set_code(error_code);

  Respond(AnyMessageConstPtr(&err), false);
}

void YBInboundCall::RespondApplicationError(int error_ext_id, const std::string& message,
                                            const MessageLite& app_error_pb) {
  ErrorStatusPB err;
  ApplicationErrorToPB(error_ext_id, message, app_error_pb, &err);
  Respond(AnyMessageConstPtr(&err), false);
}

void YBInboundCall::ApplicationErrorToPB(int error_ext_id, const std::string& message,
                                         const google::protobuf::MessageLite& app_error_pb,
                                         ErrorStatusPB* err) {
  err->set_message(message);
  const FieldDescriptor* app_error_field =
      err->GetReflection()->FindKnownExtensionByNumber(error_ext_id);
  if (app_error_field != nullptr) {
    err->GetReflection()->MutableMessage(err, app_error_field)->CheckTypeAndMergeFrom(app_error_pb);
  } else {
    LOG(DFATAL) << "Unable to find application error extension ID " << error_ext_id
                << " (message=" << message << ")";
  }
}

void YBInboundCall::Respond(AnyMessageConstPtr response, bool is_success) {
  TRACE_EVENT_FLOW_END0("rpc", "InboundCall", this);
  Status s = SerializeResponseBuffer(response, is_success);
  if (PREDICT_FALSE(!s.ok())) {
    if (is_success) {
      RespondFailure(ErrorStatusPB::ERROR_APPLICATION, s);
    } else {
      LOG(DFATAL) << "Failed to serialize failure: " << s;
    }
    return;
  }

  TRACE_EVENT_ASYNC_END1("rpc", "InboundCall", this, "method", method_name().ToBuffer());

  QueueResponse(is_success);
}

Slice YBInboundCall::method_name() const {
  auto parsed_remote_method = ParseRemoteMethod(header_.remote_method);
  return parsed_remote_method.ok() ? parsed_remote_method->method : Slice();
}

Status YBOutboundConnectionContext::HandleCall(
    const ConnectionPtr& connection, CallData* call_data) {
  return connection->HandleCallResponse(call_data);
}

Status YBOutboundConnectionContext::Connected(const ConnectionPtr& connection) {
  DCHECK_EQ(connection->direction(), Connection::Direction::CLIENT);
  connection_ = connection;
  last_read_time_ = connection->reactor()->cur_time();
  if (FLAGS_enable_rpc_keepalive) {
    timer_.Init(*loop_);
    timer_.SetCallback<
        YBOutboundConnectionContext, &YBOutboundConnectionContext::HandleTimeout>(this);
    timer_.Start(Timeout());
  }
  return Status::OK();
}

Status YBOutboundConnectionContext::AssignConnection(const ConnectionPtr& connection) {
  return connection->QueueOutboundData(ConnectionHeaderInstance());
}

Result<ProcessCallsResult> YBOutboundConnectionContext::ProcessCalls(
    const ConnectionPtr& connection, const IoVecs& data, ReadBufferFull read_buffer_full) {
  return parser().Parse(connection, data, read_buffer_full, nullptr /* tracker_for_throttle */);
}

void YBOutboundConnectionContext::UpdateLastRead(const ConnectionPtr& connection) {
  last_read_time_ = connection->reactor()->cur_time();
  VLOG(4) << Format("$0: Updated last_read_time_=$1", connection, last_read_time_);
}

void YBOutboundConnectionContext::HandleTimeout(ev::timer& watcher, int revents) {  // NOLINT
  const auto connection = connection_.lock();
  if (connection) {
    VLOG(5) << Format("$0: YBOutboundConnectionContext::HandleTimeout", connection);
    if (EV_ERROR & revents) {
      LOG(WARNING) << connection->ToString() << ": " << "Got an error in handle timeout";
      return;
    }

    const auto now = connection->reactor()->cur_time();
    const MonoDelta timeout = Timeout();

    auto deadline = last_read_time_ + timeout;
    VLOG(5) << Format(
        "$0: YBOutboundConnectionContext::HandleTimeout last_read_time_: $1, timeout: $2",
        connection, last_read_time_, timeout);
    if (now > deadline) {
      auto passed = now - last_read_time_;
      const auto status = STATUS_FORMAT(
          NetworkError, "Rpc timeout, passed: $0, timeout: $1, now: $2, last_read_time_: $3",
          passed, timeout, now, last_read_time_);
      LOG(WARNING) << connection->ToString() << ": " << status;
      connection->reactor()->DestroyConnection(connection.get(), status);
      return;
    }

    timer_.Start(deadline - now);
  }
}

} // namespace rpc
} // namespace yb
