//
// Copyright (c) YugaByte, Inc.
//
#include "yb/rpc/yb_rpc.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/auth_store.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/serialization.h"

#include "yb/util/debug/trace_event.h"

using google::protobuf::MessageLite;
using strings::Substitute;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);

namespace yb {
namespace rpc {

YBInboundTransfer::YBInboundTransfer() {
  buf_.resize(total_length_);
}

string YBInboundTransfer::StatusAsString() const {
  return strings::Substitute("$0/$1 bytes received", cur_offset_, total_length_);
}

Status YBInboundTransfer::ReceiveBuffer(Socket& socket) {
  if (cur_offset_ < kMsgLengthPrefixLength) {
    // receive int32 length prefix
    int32_t rem = kMsgLengthPrefixLength - cur_offset_;
    int32_t nread;
    Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
    RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
    if (nread == 0) {
      return Status::OK();
    }
    DCHECK_GE(nread, 0);
    cur_offset_ += nread;
    if (cur_offset_ < kMsgLengthPrefixLength) {
      // If we still don't have the full length prefix, we can't continue
      // reading yet.
      return Status::OK();
    }
    // Since we only read 'rem' bytes above, we should now have exactly
    // the length prefix in our buffer and no more.
    DCHECK_EQ(cur_offset_, kMsgLengthPrefixLength);

    // The length prefix doesn't include its own 4 bytes, so we have to
    // add that back in.
    total_length_ = NetworkByteOrder::Load32(&buf_[0]) + kMsgLengthPrefixLength;
    if (total_length_ > FLAGS_rpc_max_message_size) {
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, but we only support "
              "messages up to %d bytes long.", total_length_,
          FLAGS_rpc_max_message_size));
    }
    if (total_length_ <= kMsgLengthPrefixLength) {
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, which is invalid",
          total_length_));
    }
    buf_.resize(total_length_);

    // Fall through to receive the message body, which is likely to be already
    // available on the socket.
  }

  // receive message body
  int32_t nread;
  int32_t rem = total_length_ - cur_offset_;
  Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
  RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
  cur_offset_ += nread;

  return Status::OK();
}

YBConnection::YBConnection(ReactorThread* reactor_thread,
                           Sockaddr remote,
                           int socket,
                           Direction direction)
    : Connection(reactor_thread, remote, socket, direction),
      sasl_client_(kSaslAppName, socket),
      sasl_server_(kSaslAppName, socket) {}

void YBConnection::RunNegotiation(const MonoTime& deadline) {
  Negotiation::YBNegotiation(this, deadline);
}

void YBConnection::CreateInboundTransfer() {
  return inbound_.reset(new YBInboundTransfer());
}

AbstractInboundTransfer *YBConnection::inbound() const {
  return inbound_.get();
}

Status YBConnection::InitSaslClient() {
  RETURN_NOT_OK(sasl_client().Init(kSaslProtoName));
  RETURN_NOT_OK(sasl_client().EnableAnonymous());
  RETURN_NOT_OK(sasl_client().EnablePlain(user_credentials().real_user(),
                                          user_credentials().password()));
  return Status::OK();
}

Status YBConnection::InitSaslServer() {
  // TODO: Do necessary configuration plumbing to enable user authentication.
  // Right now we just enable PLAIN with a "dummy" auth store, which allows everyone in.
  RETURN_NOT_OK(sasl_server().Init(kSaslProtoName));
  gscoped_ptr<AuthStore> auth_store(new DummyAuthStore());
  RETURN_NOT_OK(sasl_server().EnablePlain(auth_store.Pass()));
  return Status::OK();
}

void YBConnection::HandleFinishedTransfer() {
  if (direction_ == CLIENT) {
    HandleCallResponse(inbound_.PassAs<AbstractInboundTransfer>());
  } else if (direction_ == SERVER) {
    HandleIncomingCall(inbound_.PassAs<AbstractInboundTransfer>());
  } else {
    LOG(FATAL) << "Invalid direction: " << direction_;
  }
}

void YBConnection::HandleIncomingCall(gscoped_ptr<AbstractInboundTransfer> transfer) {
  DCHECK(reactor_thread_->IsCurrentThread());

  YBInboundCall * call;
  InboundCallPtr call_ptr(call = new YBInboundCall(this));

  Status s = call->ParseFrom(transfer.Pass());
  if (!s.ok()) {
    LOG(WARNING) << ToString() << ": received bad data: " << s.ToString();
    reactor_thread_->DestroyConnection(this, s);
    return;
  }

  // call_id exists only for YB. Not for Redis.
  if (!InsertIfNotPresent(&calls_being_handled_, call->call_id(), call_ptr)) {
    LOG(WARNING) << ToString() << ": received call ID " << call->call_id()
                 << " but was already processing this ID! Ignoring";
    reactor_thread_->DestroyConnection(
        this, STATUS(RuntimeError, "Received duplicate call id",
                                   Substitute("$0", call->call_id())));
    return;
  }

  reactor_thread_->reactor()->messenger()->QueueInboundCall(std::move(call_ptr));
}

YBInboundCall::YBInboundCall(YBConnection* conn) : conn_(conn) {}

MonoTime YBInboundCall::GetClientDeadline() const {
  if (!header_.has_timeout_millis() || header_.timeout_millis() == 0) {
    return MonoTime::Max();
  }
  MonoTime deadline = timing_.time_received;
  deadline.AddDelta(MonoDelta::FromMilliseconds(header_.timeout_millis()));
  return deadline;
}

Status YBInboundCall::ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "YBInboundCall", this);
  TRACE_EVENT0("rpc", "YBInboundCall::ParseFrom");

  trace_->AddChildTrace(transfer->trace());
  RETURN_NOT_OK(serialization::ParseYBMessage(transfer->data(), &header_, &serialized_request_));

  // Adopt the service/method info from the header as soon as it's available.
  if (PREDICT_FALSE(!header_.has_remote_method())) {
    return STATUS(Corruption, "Non-connection context request header must specify remote_method");
  }
  if (PREDICT_FALSE(!header_.remote_method().IsInitialized())) {
    return STATUS(Corruption, "remote_method in request header is not initialized",
        header_.remote_method().InitializationErrorString());
  }

  remote_method_.FromPB(header_.remote_method());

  // Retain the buffer that we have a view into.
  transfer_.swap(transfer);
  return Status::OK();
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
  return Substitute("Call $0 from $1 (request call id $2)",
      remote_method_.ToString(),
      conn_->remote().ToString(),
      header_.call_id());
}

void YBInboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                           RpcCallInProgressPB* resp) {
  resp->mutable_header()->CopyFrom(header_);
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now(MonoTime::FINE).GetDeltaSince(timing_.time_received)
      .ToMicroseconds());
}

void YBInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  auto total_time = now.GetDeltaSince(timing_.time_received);

  if (header_.has_timeout_millis() && header_.timeout_millis() > 0) {
    int64_t log_threshold = header_.timeout_millis() * 750LL;
    if (total_time.ToMicroseconds() > log_threshold) {
      // TODO: consider pushing this onto another thread since it may be slow.
      // The traces may also be too large to fit in a log message.
      LOG(WARNING) << ToString() << " took " << total_time.ToMicroseconds() << "us (client timeout "
                   << header_.timeout_millis() * 1000 << " us).";
      std::string s = trace_->DumpToString(true);
      if (!s.empty()) {
        LOG(WARNING) << "Trace:\n" << s;
      }
      return;
    }
  }

  if (PREDICT_FALSE(
          FLAGS_rpc_dump_all_traces ||
          total_time.ToMilliseconds() > FLAGS_rpc_slow_query_threshold_ms)) {
    LOG(INFO) << ToString() << " took " << total_time.ToMicroseconds() << "us. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

void YBInboundCall::QueueResponseToConnection() {
  conn_->QueueOutboundData(InboundCallPtr(this));
}

ConnectionPtr YBInboundCall::get_connection() const {
  return conn_;
}

void YBInboundCall::Serialize(std::deque<util::RefCntBuffer>* output) const {
  TRACE_EVENT0("rpc", "YBInboundCall::Serialize");
  CHECK_GT(response_buf_.size(), 0);
  output->push_back(response_buf_);
  for (auto& car : sidecars_) {
    output->push_back(car);
  }
}

void YBInboundCall::NotifyTransferred(const Status& status) {
  // Remove the call from the map.
  InboundCallPtr call_from_map = EraseKeyReturnValuePtr(
      &conn_->calls_being_handled_, call_id());
  DCHECK_EQ(call_from_map.get(), this);
}

} // namespace rpc
} // namespace yb
