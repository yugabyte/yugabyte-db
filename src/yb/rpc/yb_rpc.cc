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

#include "yb/gutil/endian.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/serialization.h"

#include "yb/util/flag_tags.h"
#include "yb/util/size_literals.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/memory/memory.h"

using google::protobuf::io::CodedInputStream;
using yb::operator"" _MB;
using namespace std::literals;

DECLARE_bool(rpc_dump_all_traces);
// Maximum size of RPC should be larger than size of consensus batch
// At each layer, we embed the "message" from the previous layer.
// In order to send three strings of 64, the request from cql/redis will be larger
// than that because we will have overheads from that layer.
// Hence, we have a limit of 254MB at the consensus layer.
// The rpc layer adds its own headers, so we limit the rpc message size to 255MB.
DEFINE_int32(rpc_max_message_size, 255_MB,
             "The maximum size of a message of any RPC that the server will accept.");

DEFINE_bool(enable_rpc_keepalive, true, "Whether to enable RPC keepalive mechanism");

using std::placeholders::_1;
DECLARE_int32(rpc_slow_query_threshold_ms);
DECLARE_uint64(rpc_connection_timeout_ms);

namespace yb {
namespace rpc {

constexpr const auto kHeartbeatsPerTimeoutPeriod = 3;

namespace {

// One byte after YugaByte is reserved for future use. It could control type of connection.
const char kConnectionHeaderBytes[] = "YB\1";
const size_t kConnectionHeaderSize = sizeof(kConnectionHeaderBytes) - 1;

OutboundDataPtr ConnectionHeaderInstance() {
  static OutboundDataPtr result(
      new StringOutboundData(kConnectionHeaderBytes, kConnectionHeaderSize, "ConnectionHeader"));
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

Result<ProcessDataResult> YBInboundConnectionContext::ProcessCalls(
    const ConnectionPtr& connection, const IoVecs& data, ReadBufferFull read_buffer_full) {
  if (state_ == RpcConnectionPB::NEGOTIATING) {
    // We assume that header is fully contained in the first block.
    if (data[0].iov_len < kConnectionHeaderSize) {
      return ProcessDataResult{ 0, Slice() };
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
    auto result = VERIFY_RESULT(parser().Parse(connection, data_copy, ReadBufferFull::kFalse));
    result.consumed += kConnectionHeaderSize;
    return result;
  }

  return parser().Parse(connection, data, read_buffer_full);
}

Status YBInboundConnectionContext::HandleCall(
    const ConnectionPtr& connection, CallData* call_data) {
  auto reactor = connection->reactor();
  DCHECK(reactor->IsCurrentThread());

  auto call = InboundCall::Create<YBInboundCall>(connection, call_processed_listener());

  Status s = call->ParseFrom(call_tracker(), call_data);
  if (!s.ok()) {
    return s;
  }

  s = Store(call.get());
  if (!s.ok()) {
    return s;
  }

  reactor->messenger()->QueueInboundCall(call);

  return Status::OK();
}

void YBInboundConnectionContext::Connected(const ConnectionPtr& connection) {
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
      VLOG(4) << connection->ToString() << ": " << "Sending heartbeat, now: " << AsString(now)
              << ", deadline: " << AsString(deadline)
              << ", last_write_time_: " << AsString(last_write_time_)
              << ", last_heartbeat_sending_time_: " << AsString(last_heartbeat_sending_time_);
      connection->QueueOutboundData(HeartbeatOutboundData::Instance());
      last_heartbeat_sending_time_ = now;
      timer_.Start(HeartbeatPeriod());
    } else {
      timer_.Start(deadline - now);
    }
  }
}

YBInboundCall::YBInboundCall(ConnectionPtr conn, CallProcessedListener call_processed_listener)
    : InboundCall(std::move(conn), nullptr /* rpc_metrics */, std::move(call_processed_listener)) {}

YBInboundCall::YBInboundCall(RpcMetrics* rpc_metrics, const RemoteMethod& remote_method)
    : InboundCall(nullptr /* conn */, rpc_metrics, nullptr /* call_processed_listener */) {
  remote_method_ = remote_method;
}

YBInboundCall::~YBInboundCall() {}

CoarseTimePoint YBInboundCall::GetClientDeadline() const {
  if (!header_.has_timeout_millis() || header_.timeout_millis() == 0) {
    return CoarseTimePoint::max();
  }
  return ToCoarse(timing_.time_received) + header_.timeout_millis() * 1ms;
}

Status YBInboundCall::ParseFrom(const MemTrackerPtr& mem_tracker, CallData* call_data) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "YBInboundCall", this);
  TRACE_EVENT0("rpc", "YBInboundCall::ParseFrom");

  Slice source(call_data->data(), call_data->size());
  RETURN_NOT_OK(serialization::ParseYBMessage(source, &header_, &serialized_request_));

  consumption_ = ScopedTrackedConsumption(mem_tracker, call_data->size());
  request_data_ = std::move(*call_data);

  // Adopt the service/method info from the header as soon as it's available.
  if (PREDICT_FALSE(!header_.has_remote_method())) {
    return STATUS(Corruption, "Non-connection context request header must specify remote_method");
  }
  if (PREDICT_FALSE(!header_.remote_method().IsInitialized())) {
    return STATUS(Corruption, "remote_method in request header is not initialized",
        header_.remote_method().InitializationErrorString());
  }
  remote_method_.FromPB(header_.remote_method());

  return Status::OK();
}

Status YBInboundCall::AddRpcSidecar(RefCntBuffer car, int* idx) {
  // Check that the number of sidecars does not exceed the number of payload
  // slices that are free.
  *idx = static_cast<int>(sidecars_.size());
  if(consumption_) {
    consumption_.Add(car.size());
  }
  sidecars_.push_back(std::move(car));

  return Status::OK();
}

int YBInboundCall::RpcSidecarsSize() const {
  return sidecars_.size();
}

const RefCntBuffer& YBInboundCall::RpcSidecar(int idx) {
  return sidecars_[idx];
}

void YBInboundCall::ResetRpcSidecars() {
  if (consumption_) {
    for (const auto& sidecar : sidecars_) {
      consumption_.Add(-sidecar.size());
    }
  }
  sidecars_.clear();
}

Status YBInboundCall::SerializeResponseBuffer(const google::protobuf::MessageLite& response,
                                              bool is_success) {
  using serialization::SerializeMessage;
  using serialization::SerializeHeader;

  uint32_t protobuf_msg_size = response.ByteSize();

  ResponseHeader resp_hdr;
  resp_hdr.set_call_id(header_.call_id());
  resp_hdr.set_is_error(!is_success);
  uint32_t absolute_sidecar_offset = protobuf_msg_size;
  for (auto& car : sidecars_) {
    resp_hdr.add_sidecar_offsets(absolute_sidecar_offset);
    absolute_sidecar_offset += car.size();
  }

  int additional_size = absolute_sidecar_offset - protobuf_msg_size;

  size_t message_size = 0;
  auto status = SerializeMessage(response,
                                 /* param_buf */ nullptr,
                                 additional_size,
                                 /* use_cached_size */ true,
                                 /* offset */ 0,
                                 &message_size);
  if (!status.ok()) {
    return status;
  }
  size_t header_size = 0;
  status = SerializeHeader(resp_hdr,
                           message_size + additional_size,
                           &response_buf_,
                           message_size,
                           &header_size);
  if (!status.ok()) {
    return status;
  }
  return SerializeMessage(response,
                          &response_buf_,
                          additional_size,
                          /* use_cached_size */ true,
                          header_size);
}

string YBInboundCall::ToString() const {
  return strings::Substitute("Call $0 $1 => $2 (request call id $3)",
      remote_method_.ToString(),
      AsString(remote_address()),
      AsString(local_address()),
      header_.call_id());
}

bool YBInboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                           RpcCallInProgressPB* resp) {
  resp->mutable_header()->CopyFrom(header_);
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now().GetDeltaSince(timing_.time_received)
      .ToMicroseconds());
  return true;
}

void YBInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now();
  int total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (header_.has_timeout_millis() && header_.timeout_millis() > 0) {
    double log_threshold = header_.timeout_millis() * 0.75f;
    if (total_time > log_threshold) {
      // TODO: consider pushing this onto another thread since it may be slow.
      // The traces may also be too large to fit in a log message.
      LOG(WARNING) << ToString() << " took " << total_time << "ms (client timeout "
                   << header_.timeout_millis() << "ms).";
      std::string s = trace_->DumpToString(true);
      if (!s.empty()) {
        LOG(WARNING) << "Trace:\n" << s;
      }
      return;
    }
  }

  if (PREDICT_FALSE(
          FLAGS_rpc_dump_all_traces ||
          total_time > FLAGS_rpc_slow_query_threshold_ms)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

void YBInboundCall::Serialize(boost::container::small_vector_base<RefCntBuffer>* output) {
  TRACE_EVENT0("rpc", "YBInboundCall::Serialize");
  CHECK_GT(response_buf_.size(), 0);
  output->push_back(std::move(response_buf_));
  for (auto& car : sidecars_) {
    output->push_back(std::move(car));
  }
  sidecars_.clear();
}

Status YBInboundCall::ParseParam(google::protobuf::Message *message) {
  Slice param(serialized_request());
  CodedInputStream in(param.data(), param.size());
  in.SetTotalBytesLimit(FLAGS_rpc_max_message_size, FLAGS_rpc_max_message_size*3/4);
  if (PREDICT_FALSE(!message->ParseFromCodedStream(&in))) {
    string err = Format("Invalid parameter for call $0: $1",
                        remote_method_.ToString(),
                        message->InitializationErrorString().c_str());
    LOG(WARNING) << err;
    return STATUS(InvalidArgument, err);
  }
  consumption_.Add(message->SpaceUsedLong());

  return Status::OK();
}

void YBInboundCall::RespondBadMethod() {
  auto err = Format("Call on service $0 received from $1 with an invalid method name: $2",
                    remote_method_.service_name(),
                    connection()->ToString(),
                    remote_method_.method_name());
  LOG(WARNING) << err;
  RespondFailure(ErrorStatusPB::ERROR_NO_SUCH_METHOD, STATUS(InvalidArgument, err));
}

void YBInboundCall::RespondSuccess(const MessageLite& response) {
  TRACE_EVENT0("rpc", "InboundCall::RespondSuccess");
  Respond(response, true);
}

void YBInboundCall::RespondFailure(ErrorStatusPB::RpcErrorCodePB error_code,
                                   const Status& status) {
  TRACE_EVENT0("rpc", "InboundCall::RespondFailure");
  ErrorStatusPB err;
  err.set_message(status.ToString());
  err.set_code(error_code);

  Respond(err, false);
}

void YBInboundCall::RespondApplicationError(int error_ext_id, const std::string& message,
                                            const MessageLite& app_error_pb) {
  ErrorStatusPB err;
  ApplicationErrorToPB(error_ext_id, message, app_error_pb, &err);
  Respond(err, false);
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

void YBInboundCall::Respond(const MessageLite& response, bool is_success) {
  TRACE_EVENT_FLOW_END0("rpc", "InboundCall", this);
  Status s = SerializeResponseBuffer(response, is_success);
  if (PREDICT_FALSE(!s.ok())) {
    // TODO: test error case, serialize error response instead
    LOG(DFATAL) << "Unable to serialize response: " << s.ToString();
  }

  TRACE_EVENT_ASYNC_END1("rpc", "InboundCall", this, "method", method_name());

  QueueResponse(is_success);
}

Status YBOutboundConnectionContext::HandleCall(
    const ConnectionPtr& connection, CallData* call_data) {
  return connection->HandleCallResponse(call_data);
}

void YBOutboundConnectionContext::Connected(const ConnectionPtr& connection) {
  DCHECK_EQ(connection->direction(), Connection::Direction::CLIENT);
  connection_ = connection;
  last_read_time_ = connection->reactor()->cur_time();
  if (FLAGS_enable_rpc_keepalive) {
    timer_.Init(*loop_);
    timer_.SetCallback<
        YBOutboundConnectionContext, &YBOutboundConnectionContext::HandleTimeout>(this);
    timer_.Start(Timeout());
  }
}

void YBOutboundConnectionContext::AssignConnection(const ConnectionPtr& connection) {
  connection->QueueOutboundData(ConnectionHeaderInstance());
}

Result<ProcessDataResult> YBOutboundConnectionContext::ProcessCalls(
    const ConnectionPtr& connection, const IoVecs& data, ReadBufferFull read_buffer_full) {
  return parser().Parse(connection, data, read_buffer_full);
}

void YBOutboundConnectionContext::UpdateLastRead(const ConnectionPtr& connection) {
  last_read_time_ = connection->reactor()->cur_time();
  VLOG(4) << connection->ToString() << ": " << "Updated last_read_time_="
          << AsString(last_read_time_);
}

void YBOutboundConnectionContext::HandleTimeout(ev::timer& watcher, int revents) {  // NOLINT
  const auto connection = connection_.lock();
  if (connection) {
    if (EV_ERROR & revents) {
      LOG(WARNING) << connection->ToString() << ": " << "Got an error in handle timeout";
      return;
    }

    const auto now = connection->reactor()->cur_time();
    const MonoDelta timeout = Timeout();

    auto deadline = last_read_time_ + timeout;
    if (now > deadline) {
      auto passed = now - last_read_time_;
      const auto status = STATUS_FORMAT(
          NetworkError, "Read timeout, passed: $0, timeout: $1", passed, timeout);
      VLOG(3) << connection->ToString() << ": " << status;
      connection->reactor()->DestroyConnection(connection.get(), status);
      return;
    }

    timer_.Start(deadline - now);
  }
}

} // namespace rpc
} // namespace yb
