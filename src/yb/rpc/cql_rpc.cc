//
// Copyright (c) YugaByte, Inc.
//
#include "yb/rpc/cql_rpc.h"

#include "yb/cqlserver/cql_message.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/redis_encoding.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/debug/trace_event.h"

using google::protobuf::MessageLite;
using strings::Substitute;

DECLARE_bool(rpc_dump_all_traces);

namespace yb {

using cqlserver::CQLMessage;

namespace rpc {

CQLInboundTransfer::CQLInboundTransfer() {
  buf_.resize(total_length_);
}

Status CQLInboundTransfer::ReceiveBuffer(Socket& socket) {

  if (cur_offset_ < CQLMessage::kMessageHeaderLength) {
    // receive the fixed header
    const int32_t rem = CQLMessage::kMessageHeaderLength - cur_offset_;
    int32_t nread = 0;
    const Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
    RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
    if (nread == 0) {
      return Status::OK();
    }
    DCHECK_GE(nread, 0);
    cur_offset_ += nread;
    if (cur_offset_ < CQLMessage::kMessageHeaderLength) {
      // If we still don't have the full header, we can't continue reading yet.
      return Status::OK();
    }
    // Since we only read 'rem' bytes above, we should now have exactly the header in our buffer
    // and no more.
    DCHECK_EQ(cur_offset_, CQLMessage::kMessageHeaderLength);

    // Extract the body length field in buf_[5..8] and update the total length of the frame.
    total_length_ = CQLMessage::kMessageHeaderLength +
        NetworkByteOrder::Load32(&buf_[CQLMessage::kHeaderPosLength]);
    if (total_length_ > CQLMessage::kMaxMessageLength) {
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, but we only support "
          "messages up to %d bytes long.", total_length_, CQLMessage::kMaxMessageLength));
    }
    if (total_length_ < CQLMessage::kMessageHeaderLength) {
      // total_length_ can become less than kMessageHeaderLength if arithmetic overflow occurs.
      return STATUS(NetworkError, StringPrintf("the frame had a length of %d, which is invalid",
          total_length_));
    }
    buf_.resize(total_length_);

    // Fall through to receive the message body, which is likely to be already available on the
    // socket.
  }

  // receive message body
  int32_t nread = 0;
  const int32_t rem = total_length_ - cur_offset_;
  if (rem > 0) {
    Status status = socket.Recv(&buf_[cur_offset_], rem, &nread);
    RETURN_ON_ERROR_OR_SOCKET_NOT_READY(status);
    cur_offset_ += nread;
  }

  return Status::OK();
}

string CQLInboundTransfer::StatusAsString() const {
  return strings::Substitute("$0: $1 bytes received", this, cur_offset_);
}

class CQLResponseTransferCallbacks : public ResponseTransferCallbacks {
 public:
  CQLResponseTransferCallbacks(InboundCallPtr call, CQLConnection* conn)
      : call_(std::move(call)), conn_(conn) {}

  ~CQLResponseTransferCallbacks() {
    conn_->FinishedHandlingACall();
  }

 protected:
  InboundCall* call() override {
    return call_.get();
  }

 private:
  InboundCallPtr call_;
  CQLConnection* conn_;
};

CQLConnection::CQLConnection(ReactorThread* reactor_thread,
                             Sockaddr remote,
                             int socket,
                             Direction direction)
    : Connection(reactor_thread, remote, socket, direction), sql_session_(new sql::SqlSession()) {}

void CQLConnection::RunNegotiation(const MonoTime& deadline) {
  Negotiation::CQLNegotiation(this, deadline);
}

void CQLConnection::CreateInboundTransfer() {
  return inbound_.reset(new CQLInboundTransfer());
}

AbstractInboundTransfer *CQLConnection::inbound() const {
  return inbound_.get();
}

TransferCallbacks* CQLConnection::GetResponseTransferCallback(InboundCallPtr call) {
  return new CQLResponseTransferCallbacks(std::move(call), this);
}

void CQLConnection::HandleFinishedTransfer() {
  CHECK(direction_ == SERVER) << "Invalid direction for CQL: " << direction_;
  HandleIncomingCall(inbound_.PassAs<AbstractInboundTransfer>());
}

void CQLConnection::HandleIncomingCall(gscoped_ptr<AbstractInboundTransfer> transfer) {
  DCHECK(reactor_thread_->IsCurrentThread());

  InboundCallPtr call(new CQLInboundCall(this));

  Status s = call->ParseFrom(transfer.Pass());
  if (!s.ok()) {
    LOG(WARNING) << ToString() << ": received bad data: " << s.ToString();
    // TODO: shutdown? probably, since any future stuff on this socket will be
    // "unsynchronized"
    return;
  }

  reactor_thread_->reactor()->messenger()->QueueInboundCall(std::move(call));
}

void CQLConnection::FinishedHandlingACall() {
  // If the next client call has already been received by the server. Check if it is
  // ready to be handled.
  if (inbound_ && inbound_->TransferFinished()) {
    HandleFinishedTransfer();
  }
}

CQLInboundCall::CQLInboundCall(CQLConnection* conn) : conn_(conn) {}

Status CQLInboundCall::ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "CQLInboundCall", this);
  TRACE_EVENT0("rpc", "CQLInboundCall::ParseFrom");

  trace_->AddChildTrace(transfer->trace());
  // Parsing of CQL message is deferred to CQLServiceImpl::Handle. Just save the serialized data.
  serialized_request_ = transfer->data();

  // Fill the service name method name to transfer the call to. The method name is for debug
  // tracing only. Inside CQLServiceImpl::Handle, we rely on the opcode to dispatch the execution.
  RemoteMethodPB remote_method_pb;
  remote_method_pb.set_service_name("yb.cqlserver.CQLServerService");
  remote_method_pb.set_method_name("ExecuteRequest");
  remote_method_.FromPB(remote_method_pb);

  // Retain the buffer that we have a view into.
  transfer_.swap(transfer);
  return Status::OK();
}

void CQLInboundCall::RecordHandlingStarted(scoped_refptr<Histogram> incoming_queue_time) {
  if (resume_from_ == nullptr) {
    InboundCall::RecordHandlingStarted(incoming_queue_time);
  }
}

void CQLInboundCall::SerializeResponseTo(vector<Slice>* slices) const {
  TRACE_EVENT0("rpc", "CQLInboundCall::SerializeResponseTo");
  CHECK_GT(response_msg_buf_.size(), 0);
  slices->push_back(Slice(response_msg_buf_));
}

Status CQLInboundCall::SerializeResponseBuffer(const MessageLite& response,
                                               bool is_success) {
  if (!is_success) {
    const ErrorStatusPB& error_status = static_cast<const ErrorStatusPB&>(response);
    response_msg_buf_.append(EncodeAsError(error_status.message()));
    return Status::OK();
  }

  CHECK(!response_msg_buf_.empty()) << "CQL response is absent.";
  return Status::OK();
}

void CQLInboundCall::QueueResponseToConnection() {
  conn_->QueueResponseForCall(InboundCallPtr(this));
}

void CQLInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  int total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

string CQLInboundCall::ToString() const {
  return Substitute("CQL Call $0 from $1", remote_method_.ToString(), conn_->remote().ToString());
}

void CQLInboundCall::DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) {
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now(MonoTime::FINE).GetDeltaSince(timing_.time_received)
      .ToMicroseconds());
}

MonoTime CQLInboundCall::GetClientDeadline() const {
  // TODO(Robert) - fill in CQL timeout
  return MonoTime::Max();
}

scoped_refptr<Connection> CQLInboundCall::get_connection() {
  return conn_;
}

const scoped_refptr<Connection> CQLInboundCall::get_connection() const {
  return conn_;
}

sql::SqlSession::SharedPtr CQLInboundCall::GetSqlSession() const {
  return conn_->sql_session();
}

bool CQLInboundCall::TryResume() {
  if (resume_from_ == nullptr) {
    return false;
  }
  VLOG(2) << "Resuming " << ToString();
  resume_from_->Run();
  return true;
}

} // namespace rpc
} // namespace yb
