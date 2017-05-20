//
// Copyright (c) YugaByte, Inc.
//
#include "yb/rpc/cql_rpc.h"

#include "yb/cqlserver/cql_message.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/redis_encoding.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/debug/trace_event.h"

using yb::cqlserver::CQLMessage;
using namespace std::placeholders;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);

namespace yb {
namespace rpc {

CQLConnectionContext::CQLConnectionContext()
    : sql_session_(new sql::SqlSession()) {
}

void CQLConnectionContext::RunNegotiation(ConnectionPtr connection, const MonoTime& deadline) {
  Negotiation::CQLNegotiation(std::move(connection), deadline);
}

Status CQLConnectionContext::ProcessCalls(const ConnectionPtr& connection,
                                          Slice slice,
                                          size_t* consumed) {
  auto pos = slice.data();
  const auto end = slice.end();
  while (end - pos >= CQLMessage::kMessageHeaderLength) {
    // Extract the body length field in buf_[5..8] and update the total length of the frame.
    size_t body_length = NetworkByteOrder::Load32(pos + CQLMessage::kHeaderPosLength);
    size_t total_length = CQLMessage::kMessageHeaderLength + body_length;
    if (total_length > CQLMessage::kMaxMessageLength) {
      return STATUS_SUBSTITUTE(NetworkError,
                               "the frame had a length of $0, but we only support "
                               "messages up to $1 bytes long.",
                               total_length,
                               CQLMessage::kMaxMessageLength);
    }

    if (pos + total_length > end) {
      break;
    }

    RETURN_NOT_OK(HandleInboundCall(connection, Slice(pos, total_length)));
    pos += total_length;
  }

  *consumed = pos - slice.data();
  if (pos != slice.end()) {
    DVLOG(1) << "Pending CQL data: " << slice.end() - pos;
  }
  return Status::OK();
}

size_t CQLConnectionContext::BufferLimit() {
  return CQLMessage::kMaxMessageLength;
}

Status CQLConnectionContext::HandleInboundCall(const ConnectionPtr& connection, Slice slice) {
  auto reactor_thread = connection->reactor_thread();
  DCHECK(reactor_thread->IsCurrentThread());

  CQLInboundCall* call;
  InboundCallPtr call_ptr(call = new CQLInboundCall(connection,
                                                    call_processed_listener(),
                                                    sql_session_));

  Status s = call->ParseFrom(slice);
  if (!s.ok()) {
    LOG(WARNING) << connection->ToString() << ": received bad data: " << s.ToString();
    return STATUS_SUBSTITUTE(NetworkError, "Bad data: $0", s.ToString());
  }

  Enqueue(std::move(call_ptr));

  return Status::OK();
}

CQLInboundCall::CQLInboundCall(ConnectionPtr conn,
                               CallProcessedListener call_processed_listener,
                               sql::SqlSession::SharedPtr sql_session)
    : InboundCall(std::move(conn), std::move(call_processed_listener)),
      sql_session_(std::move(sql_session)) {
}

Status CQLInboundCall::ParseFrom(Slice source) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "CQLInboundCall", this);
  TRACE_EVENT0("rpc", "CQLInboundCall::ParseFrom");

  // Parsing of CQL message is deferred to CQLServiceImpl::Handle. Just save the serialized data.
  request_data_.assign(source.data(), source.end());
  serialized_request_ = Slice(request_data_.data(), request_data_.size());

  // Fill the service name method name to transfer the call to. The method name is for debug
  // tracing only. Inside CQLServiceImpl::Handle, we rely on the opcode to dispatch the execution.
  remote_method_ = RemoteMethod("yb.cqlserver.CQLServerService", "ExecuteRequest");

  return Status::OK();
}

void CQLInboundCall::Serialize(std::deque<util::RefCntBuffer>* output) const {
  TRACE_EVENT0("rpc", "CQLInboundCall::Serialize");
  CHECK_GT(response_msg_buf_.size(), 0);

  output->push_back(response_msg_buf_);
}

Status CQLInboundCall::SerializeResponseBuffer(const google::protobuf::MessageLite& response,
                                               bool is_success) {
  if (!is_success) {
    const ErrorStatusPB& error_status = static_cast<const ErrorStatusPB&>(response);
    // TODO proper error reporting
    response_msg_buf_ = util::RefCntBuffer(EncodeAsError(error_status.message()));
    return Status::OK();
  }

  CHECK(response_msg_buf_) << "CQL response is absent.";
  return Status::OK();
}

void CQLInboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  int total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces || total_time > FLAGS_rpc_slow_query_threshold_ms)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

std::string CQLInboundCall::ToString() const {
  return strings::Substitute("CQL Call $0 from $1",
                             remote_method_.ToString(),
                             connection()->remote().ToString());
}

void CQLInboundCall::DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) {
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(
      MonoTime::FineNow().GetDeltaSince(timing_.time_received).ToMicroseconds());
}

MonoTime CQLInboundCall::GetClientDeadline() const {
  // TODO(Robert) - fill in CQL timeout
  return MonoTime::Max();
}

void CQLInboundCall::RecordHandlingStarted(scoped_refptr<Histogram> incoming_queue_time) {
  if (resume_from_ == nullptr) {
    InboundCall::RecordHandlingStarted(incoming_queue_time);
  }
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
