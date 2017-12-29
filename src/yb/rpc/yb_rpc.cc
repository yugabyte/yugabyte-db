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

DECLARE_bool(rpc_dump_all_traces);
// Maximum size of RPC should be larger than size of consensus batch
// At each layer, we embed the "message" from the previous layer.
// In order to send three strings of 64, the request from cql/redis will be larger
// than that because we will have overheads from that layer.
// Hence, we have a limit of 254MB at the consensus layer.
// The rpc layer adds its own headers, so we limit the rpc message size to 255MB.
DEFINE_int32(rpc_max_message_size, 255_MB,
             "The maximum size of a message of any RPC that the server will accept.");

using std::placeholders::_1;
DECLARE_int32(rpc_slow_query_threshold_ms);

namespace yb {
namespace rpc {

namespace {

// One byte after YugaByte is reserved for future use. It could control type of connection.
const char kConnectionHeaderBytes[] = "YB\1";
const size_t kConnectionHeaderSize = sizeof(kConnectionHeaderBytes) - 1;

class ConnectionHeader : public OutboundData {
 public:
  static OutboundDataPtr Instance() {
    static OutboundDataPtr result(new ConnectionHeader());
    return result;
  }

  void Transferred(const Status&, Connection*) override {}

  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override {
    return false;
  }

  void Serialize(std::deque<RefCntBuffer> *output) const override {
    output->push_back(buffer_);
  }

  std::string ToString() const override {
    return "ConnectionHeader";
  }

  virtual ~ConnectionHeader() {}
 private:
  ConnectionHeader() : buffer_(kConnectionHeaderBytes, kConnectionHeaderSize) {}

  RefCntBuffer buffer_;
};

} // namespace

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::MessageLite;
using google::protobuf::io::CodedOutputStream;

namespace {

constexpr size_t kBigPacket = 128_KB;

} // namespace

YBConnectionContext::YBConnectionContext() {}

YBConnectionContext::~YBConnectionContext() {}

size_t YBConnectionContext::BufferLimit() {
  return FLAGS_rpc_max_message_size;
}

Status YBConnectionContext::ProcessCalls(const ConnectionPtr& connection,
                                         Slice slice,
                                         size_t* consumed) {
  auto pos = slice.data();
  const auto end = slice.end();

  if (state_ == RpcConnectionPB::NEGOTIATING) {
    if (slice.size() < kConnectionHeaderSize) {
      *consumed = 0;
      return Status::OK();
    }
    if (!slice.starts_with(kConnectionHeaderBytes, kConnectionHeaderSize)) {
      return STATUS_FORMAT(NetworkError,
                           "Invalid connection header: $0",
                           slice.ToDebugHexString());
    }
    state_ = RpcConnectionPB::OPEN;
    pos += kConnectionHeaderSize;
  }

  while (end - pos >= kMsgLengthPrefixLength) {
    const size_t data_length = NetworkByteOrder::Load32(pos);
    const size_t total_length = data_length + kMsgLengthPrefixLength;
    if (total_length > FLAGS_rpc_max_message_size) {
      return STATUS(NetworkError,
          strings::Substitute("The frame had a length of $0, but we only support "
                              "messages up to $1 bytes long.",
                              total_length,
                              FLAGS_rpc_max_message_size));
    }
    auto stop = pos + total_length;
    if (stop > end) {
      break;
    }
    pos += kMsgLengthPrefixLength;
    const auto status = HandleCall(connection, Slice(pos, stop - pos));
    if (!status.ok()) {
      return status;
    }

    pos = stop;
  }
  *consumed = pos - slice.data();
  return Status::OK();
}

size_t YBConnectionContext::MaxReceive(Slice existing_data) {
  if (existing_data.size() >= kMsgLengthPrefixLength) {
    const size_t data_length = NetworkByteOrder::Load32(existing_data.data());
    return std::max(kBigPacket, data_length + kMsgLengthPrefixLength);
  }
  return kBigPacket;
}


Status YBConnectionContext::HandleCall(const ConnectionPtr& connection, Slice call_data) {
  const auto direction = connection->direction();
  switch (direction) {
    case ConnectionDirection::CLIENT:
      return connection->HandleCallResponse(call_data);
    case ConnectionDirection::SERVER:
      return HandleInboundCall(connection, call_data);
  }
  LOG(FATAL) << "Invalid direction: " << direction;
}

Status YBConnectionContext::HandleInboundCall(const ConnectionPtr& connection, Slice call_data) {
  auto reactor = connection->reactor();
  DCHECK(reactor->IsCurrentThread());

  auto call = std::make_shared<YBInboundCall>(connection, call_processed_listener());

  Status s = call->ParseFrom(call_data);
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

uint64_t YBConnectionContext::ExtractCallId(InboundCall* call) {
  return down_cast<YBInboundCall*>(call)->call_id();
}

void YBConnectionContext::Connected(const ConnectionPtr& connection) {
  auto direction = connection->direction();
  state_ = direction == ConnectionDirection::SERVER ? RpcConnectionPB::NEGOTIATING
                                                    : RpcConnectionPB::OPEN;
}

void YBConnectionContext::AssignConnection(const ConnectionPtr& connection) {
  if (connection->direction() == ConnectionDirection::CLIENT) {
    connection->QueueOutboundData(ConnectionHeader::Instance());
  }
}

YBInboundCall::YBInboundCall(ConnectionPtr conn, CallProcessedListener call_processed_listener)
    : InboundCall(std::move(conn), std::move(call_processed_listener)) {}

YBInboundCall::YBInboundCall(const RemoteMethod& remote_method)
    : YBInboundCall(nullptr /* conn */, nullptr /*call_processed_listener  */) {
  remote_method_ = remote_method;
}

YBInboundCall::~YBInboundCall() {}

MonoTime YBInboundCall::GetClientDeadline() const {
  if (!header_.has_timeout_millis() || header_.timeout_millis() == 0) {
    return MonoTime::Max();
  }
  MonoTime deadline = timing_.time_received;
  deadline.AddDelta(MonoDelta::FromMilliseconds(header_.timeout_millis()));
  return deadline;
}

Status YBInboundCall::ParseFrom(Slice source) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "YBInboundCall", this);
  TRACE_EVENT0("rpc", "YBInboundCall::ParseFrom");

  request_data_.assign(source.data(), source.end());
  source = Slice(request_data_.data(), request_data_.size());
  RETURN_NOT_OK(serialization::ParseYBMessage(source, &header_, &serialized_request_));

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
  if (sidecars_.size() >= CallResponse::kMaxSidecarSlices) {
    return STATUS(ServiceUnavailable, "All available sidecars already used");
  }
  *idx = static_cast<int>(sidecars_.size());
  sidecars_.push_back(std::move(car));
  return Status::OK();
}

void YBInboundCall::ResetRpcSidecars() {
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
      yb::ToString(remote_address()),
      yb::ToString(local_address()),
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

void YBInboundCall::Serialize(std::deque<RefCntBuffer>* output) const {
  TRACE_EVENT0("rpc", "YBInboundCall::Serialize");
  CHECK_GT(response_buf_.size(), 0);
  output->push_back(response_buf_);
  for (auto& car : sidecars_) {
    output->push_back(car);
  }
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

} // namespace rpc
} // namespace yb
