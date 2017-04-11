//
// Copyright (c) YugaByte, Inc.
//
#include "yb/rpc/redis_rpc.h"

#include "yb/common/redis_protocol.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/redis_encoding.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/serialization.h"

#include "yb/util/split.h"

#include "yb/util/debug/trace_event.h"

using google::protobuf::MessageLite;
using strings::Substitute;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);

namespace yb {
namespace rpc {

RedisInboundTransfer::~RedisInboundTransfer() {
}

RedisInboundTransfer::RedisInboundTransfer() {
  buf_.resize(kProtoIOBufLen);
  ASAN_POISON_MEMORY_REGION(buf_.data(), kProtoIOBufLen);
}

string RedisInboundTransfer::StatusAsString() const {
  return strings::Substitute("$0 : $1 bytes received", this, cur_offset_);
}

bool RedisInboundTransfer::FindEndOfLine() {
  searching_pos_ = max(searching_pos_, parsing_pos_);
  const char* newline = static_cast<const char*>(memchr(buf_.c_str() + searching_pos_, '\n',
      cur_offset_ - searching_pos_));

  // Nothing to do without a \r\n.
  if (newline == nullptr) {
    // Update searching_pos_ to cur_offset_ so that we don't search the searched bytes again.
    searching_pos_ = cur_offset_;
    return false;
  }

  return true;
}

Status RedisInboundTransfer::ParseNumber(int64_t* parse_result) {
  // NOLINTNEXTLINE
  static_assert(sizeof(long long) == sizeof(int64_t),
      "Expecting long long to be a 64-bit integer");
  char* end_ptr = nullptr;
  *parse_result = std::strtoll(buf_.c_str() + parsing_pos_, &end_ptr, 0);
  // If the length is well-formed, it should extend all the way until newline.
  SCHECK_EQ(
      '\r', end_ptr[0], Corruption, "Redis protocol error: expecting a number followed by newline");
  SCHECK_EQ(
      '\n', end_ptr[1], Corruption, "Redis protocol error: expecting a number followed by newline");
  parsing_pos_ = (end_ptr - buf_.c_str()) + 2;
  return Status::OK();
}

Status RedisInboundTransfer::CheckInlineBuffer() {
  if (done_) return Status::OK();

  if (!FindEndOfLine()) {
    return Status::OK();
  }

  client_command_.cmd_args.clear();
  const char* newline = static_cast<const char*>(memchr(buf_.c_str() + searching_pos_, '\r',
      cur_offset_ - searching_pos_));
  const size_t query_len = newline - (buf_.c_str() + parsing_pos_);
  // Split the input buffer up to the \r\n.
  Slice aux(&buf_[parsing_pos_], query_len);
  // TODO: fail gracefully without killing the server.
  RETURN_NOT_OK(util::SplitArgs(aux, &client_command_.cmd_args));
  parsing_pos_ = query_len + 2;
  done_ = true;
  return Status::OK();
}

Status RedisInboundTransfer::CheckMultiBulkBuffer() {
  if (done_) return Status::OK();

  if (client_command_.num_multi_bulk_args_left == 0) {
    // Multi bulk length cannot be read without a \r\n.
    parsing_pos_ = 0;
    client_command_.cmd_args.clear();
    client_command_.current_multi_bulk_arg_len = -1;

    DVLOG(4) << "Looking at : "
             << Slice(buf_.c_str() + parsing_pos_, cur_offset_ - parsing_pos_).ToDebugString(8);
    if (!FindEndOfLine()) return Status::OK();

    SCHECK_EQ(
        '*', buf_[parsing_pos_], Corruption,
        StringPrintf("Expected to see '*' instead of %c", buf_[parsing_pos_]));
    parsing_pos_++;
    int64_t num_args = 0;
    RETURN_NOT_OK(ParseNumber(&num_args));
    // TODO: create a single macro that checks if a value is in a certain range and de-duplicate
    // the following.
    SCHECK_GT(
        num_args, 0, Corruption,
        Substitute(
            "Number of lines in multibulk out of expected range (0, 1024 * 1024] : $0",
            num_args));
    SCHECK_LE(
        num_args, 1024 * 1024, Corruption,
        Substitute(
            "Number of lines in multibulk out of expected range (0, 1024 * 1024] : $0",
            num_args));
    client_command_.num_multi_bulk_args_left = num_args;
  }

  while (client_command_.num_multi_bulk_args_left > 0) {
    if (client_command_.current_multi_bulk_arg_len == -1) {  // Read bulk length if unknown.
      if (!FindEndOfLine()) return Status::OK();

      SCHECK_EQ(
          buf_[parsing_pos_], '$', Corruption,
          StringPrintf("Protocol error: expected '$', got '%c'", buf_[parsing_pos_]));
      parsing_pos_++;
      int64_t parsed_len = 0;
      RETURN_NOT_OK(ParseNumber(&parsed_len));
      SCHECK_GE(
          parsed_len, 0, Corruption,
          Substitute(
              "Protocol error: invalid bulk length not in the range [0, 512 * 1024 * 1024] : $0",
              parsed_len));
      SCHECK_LE(
          parsed_len, 512 * 1024 * 1024, Corruption,
          Substitute(
              "Protocol error: invalid bulk length not in the range [0, 512 * 1024 * 1024] : $0",
              parsed_len));
      client_command_.current_multi_bulk_arg_len = parsed_len;
    }

    // Read bulk argument.
    if (cur_offset_ < parsing_pos_ + client_command_.current_multi_bulk_arg_len + 2) {
      // Not enough data (+2 == trailing \r\n).
      return Status::OK();
    }

    client_command_.cmd_args.push_back(
        Slice(buf_.data() + parsing_pos_, client_command_.current_multi_bulk_arg_len));
    parsing_pos_ += client_command_.current_multi_bulk_arg_len + 2;
    client_command_.num_multi_bulk_args_left--;
    client_command_.current_multi_bulk_arg_len = -1;
  }

  // We're done consuming the client's command when num_multi_bulk_args_left == 0.
  done_ = true;
  return Status::OK();
}

RedisInboundTransfer* RedisInboundTransfer::ExcessData() const {
  CHECK_GE(cur_offset_, parsing_pos_) << "Parsing position cannot be past current offset.";
  if (cur_offset_ == parsing_pos_) return nullptr;

  // Copy excess data from buf_. Starting at pos_ up to cur_offset_.
  const int excess_bytes_len = cur_offset_ - parsing_pos_;
  RedisInboundTransfer *excess = new RedisInboundTransfer();
  // Right now, all the buffers are created with the same size. When we handle large sized
  // requests in RedisInboundTransfer, make sure that we have a large enough buffer.
  assert(excess->buf_.size() > excess_bytes_len);
  ASAN_UNPOISON_MEMORY_REGION(excess->buf_.data(), excess_bytes_len);
  memcpy(static_cast<void *>(excess->buf_.data()),
      static_cast<const void *>(buf_.data() + parsing_pos_),
      excess_bytes_len);
  excess->cur_offset_ = excess_bytes_len;

  // TODO: what's a better way to report errors here?
  CHECK_OK(excess->CheckReadCompletely());

  return excess;
}

Status RedisInboundTransfer::CheckReadCompletely() {
  /* Determine request type when unknown. */
  if (buf_[0] == '*') {
    RETURN_NOT_OK(CheckMultiBulkBuffer());
  } else {
    RETURN_NOT_OK(CheckInlineBuffer());
  }
  return Status::OK();
}

Status RedisInboundTransfer::ReceiveBuffer(Socket& socket) {
  // Try to read into the buffer whatever is available.
  const int32_t buf_space_left = kProtoIOBufLen - cur_offset_;
  int32_t bytes_read = 0;

  ASAN_UNPOISON_MEMORY_REGION(&buf_[cur_offset_], buf_space_left);
  Status status = socket.Recv(&buf_[cur_offset_], buf_space_left, &bytes_read);
  DCHECK_GE(bytes_read, 0);
  ASAN_POISON_MEMORY_REGION(&buf_[cur_offset_] + bytes_read, buf_space_left - bytes_read);

  RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
  if (bytes_read == 0) {
    return Status::OK();
  }
  cur_offset_ += bytes_read;

  // Check if we have read the whole command.
  RETURN_NOT_OK(CheckReadCompletely());
  return Status::OK();
}

class RedisResponseTransferCallbacks : public ResponseTransferCallbacks {
 public:
  RedisResponseTransferCallbacks(OutboundDataPtr outbound_data, RedisConnection* conn)
      : outbound_data_(std::move(outbound_data)), conn_(conn) {}

  ~RedisResponseTransferCallbacks() {
    conn_->FinishedHandlingACall();
  }

 protected:
  OutboundData* outbound_data() override {
    return outbound_data_.get();
  }

 private:
  OutboundDataPtr outbound_data_;
  RedisConnection* conn_;
};

RedisConnection::RedisConnection(ReactorThread* reactor_thread,
                                 Sockaddr remote,
                                 int socket,
                                 Direction direction)
    : Connection(reactor_thread, remote, socket, direction), processing_call_(false) {}

void RedisConnection::RunNegotiation(const MonoTime& deadline) {
  Negotiation::RedisNegotiation(this, deadline);
}

void RedisConnection::CreateInboundTransfer() {
  return inbound_.reset(new RedisInboundTransfer());
}

AbstractInboundTransfer *RedisConnection::inbound() const {
  return inbound_.get();
}

TransferCallbacks* RedisConnection::GetResponseTransferCallback(OutboundDataPtr outbound_data) {
  return new RedisResponseTransferCallbacks(std::move(outbound_data), this);
}

void RedisConnection::HandleFinishedTransfer() {
  if (processing_call_) {
    DVLOG(4) << "Already handling a call from the client. Need to wait. " << ToString();
    return;
  }

  DCHECK_EQ(direction_, SERVER) << "Invalid direction for Redis: " << direction_;
  RedisInboundTransfer* next_transfer = inbound_->ExcessData();
  HandleIncomingCall(inbound_.PassAs<AbstractInboundTransfer>());
  inbound_.reset(next_transfer);
}

void RedisConnection::HandleIncomingCall(gscoped_ptr<AbstractInboundTransfer> transfer) {
  DCHECK(reactor_thread_->IsCurrentThread());

  InboundCallPtr call(new RedisInboundCall(this));

  Status s = call->ParseFrom(transfer.Pass());
  if (!s.ok()) {
    LOG(WARNING) << ToString() << ": received bad data: " << s.ToString();
    reactor_thread_->DestroyConnection(this, s);
    return;
  }

  processing_call_ = true;
  reactor_thread_->reactor()->messenger()->QueueInboundCall(std::move(call));
}

void RedisConnection::FinishedHandlingACall() {
  // If the next client call has already been received by the server. Check if it is
  // ready to be handled.
  processing_call_ = false;
  if (inbound_ && inbound_->TransferFinished()) {
    HandleFinishedTransfer();
  }
}

RedisInboundCall::RedisInboundCall(RedisConnection* conn) : conn_(conn) {}

Status RedisInboundCall::ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "RedisInboundCall", this);
  TRACE_EVENT0("rpc", "RedisInboundCall::ParseFrom");

  trace_->AddChildTrace(transfer->trace());
  RETURN_NOT_OK(serialization::ParseRedisMessage(transfer->data(), &serialized_request_));

  RemoteMethodPB remote_method_pb;
  remote_method_pb.set_service_name("yb.redisserver.RedisServerService");
  remote_method_pb.set_method_name("anyMethod");
  remote_method_.FromPB(remote_method_pb);

  // Retain the buffer that we have a view into.
  transfer_.swap(transfer);
  return Status::OK();
}

RedisClientCommand& RedisInboundCall::GetClientCommand() {
  return down_cast<RedisInboundTransfer*>(transfer_.get())->client_command();
}

Status RedisInboundCall::SerializeResponseBuffer(const MessageLite& response,
                                                 bool is_success) {
  if (!is_success) {
    const ErrorStatusPB& error_status = static_cast<const ErrorStatusPB&>(response);
    response_msg_buf_.append(EncodeAsError(error_status.message()));
    return Status::OK();
  }

  const RedisResponsePB& redis_response = static_cast<const RedisResponsePB&>(response);

  if (redis_response.code() == RedisResponsePB_RedisStatusCode_SERVER_ERROR) {
    response_msg_buf_.append(EncodeAsError("Request was unable to be processed from server."));
    return Status::OK();
  }

  // TODO(Amit): As and when we implement get/set and its h* equivalents, we would have to
  // handle arrays, hashes etc. For now, we only support the string response.

  if (redis_response.code() != RedisResponsePB_RedisStatusCode_OK) {
    // We send a nil response for all non-ok statuses as of now.
    // TODO: Follow redis error messages.
    response_msg_buf_.append(kNilResponse);
  } else {
    if (redis_response.has_string_response()) {
      response_msg_buf_.append(EncodeAsSimpleString(redis_response.string_response()));
    } else if (redis_response.has_int_response()) {
      response_msg_buf_.append(EncodeAsInteger(redis_response.int_response()));
    } else {
      response_msg_buf_.append(EncodeAsSimpleString("OK"));
    }
  }
  return Status::OK();
}

string RedisInboundCall::ToString() const {
  return Substitute("Redis Call $0 from $1",
      remote_method_.ToString(),
      conn_->remote().ToString());
}

void RedisInboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                              RpcCallInProgressPB* resp) {
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now(MonoTime::FINE).GetDeltaSince(timing_.time_received)
      .ToMicroseconds());
}

MonoTime RedisInboundCall::GetClientDeadline() const {
  return MonoTime::Max();  // No timeout specified in the protocol for Redis.
}

void RedisInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  int total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces || total_time > FLAGS_rpc_slow_query_threshold_ms)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

void RedisInboundCall::QueueResponseToConnection() {
  conn_->QueueOutboundData(InboundCallPtr(this));
}

scoped_refptr<Connection> RedisInboundCall::get_connection() {
  return conn_;
}

const scoped_refptr<Connection> RedisInboundCall::get_connection() const {
  return conn_;
}

void RedisInboundCall::SerializeResponseTo(vector<Slice>* slices) const {
  TRACE_EVENT0("rpc", "RedisInboundCall::SerializeResponseTo");
  CHECK_GT(response_msg_buf_.size(), 0);
  slices->push_back(Slice(response_msg_buf_));
}

} // namespace rpc
} // namespace yb
