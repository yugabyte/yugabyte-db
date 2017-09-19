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
#include "yb/cqlserver/cql_rpc.h"

#include "yb/cqlserver/cql_message.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/negotiation.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/util/debug/trace_event.h"

using yb::cqlserver::CQLMessage;
using namespace std::literals; // NOLINT
using namespace std::placeholders;

DECLARE_bool(rpc_dump_all_traces);
DECLARE_int32(rpc_slow_query_threshold_ms);

namespace yb {
namespace cqlserver {

CQLConnectionContext::CQLConnectionContext()
    : ql_session_(new ql::QLSession()) {
}

void CQLConnectionContext::RunNegotiation(rpc::ConnectionPtr connection, const MonoTime& deadline) {
  CHECK_EQ(connection->direction(), rpc::ConnectionDirection::SERVER);
  connection->CompleteNegotiation(Status::OK());
}

Status CQLConnectionContext::ProcessCalls(const rpc::ConnectionPtr& connection,
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

Status CQLConnectionContext::HandleInboundCall(const rpc::ConnectionPtr& connection, Slice slice) {
  auto reactor = connection->reactor();
  DCHECK(reactor->IsCurrentThread());

  auto call = std::make_shared<CQLInboundCall>(connection,
      call_processed_listener(),
      ql_session_);

  Status s = call->ParseFrom(slice);
  if (!s.ok()) {
    LOG(WARNING) << connection->ToString() << ": received bad data: " << s.ToString();
    return STATUS_SUBSTITUTE(NetworkError, "Bad data: $0", s.ToString());
  }

  s = Store(call.get());
  if (!s.ok()) {
    return s;
  }

  reactor->messenger()->QueueInboundCall(call);

  return Status::OK();
}

uint64_t CQLConnectionContext::ExtractCallId(rpc::InboundCall* call) {
  return down_cast<CQLInboundCall*>(call)->stream_id();
}

CQLInboundCall::CQLInboundCall(rpc::ConnectionPtr conn,
                               CallProcessedListener call_processed_listener,
                               ql::QLSession::SharedPtr ql_session)
    : InboundCall(std::move(conn), std::move(call_processed_listener)),
      ql_session_(std::move(ql_session)) {
}

Status CQLInboundCall::ParseFrom(Slice source) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "CQLInboundCall", this);
  TRACE_EVENT0("rpc", "CQLInboundCall::ParseFrom");

  // Parsing of CQL message is deferred to CQLServiceImpl::Handle. Just save the serialized data.
  request_data_.assign(source.data(), source.end());
  serialized_request_ = Slice(request_data_.data(), request_data_.size());

  // Fill the service name method name to transfer the call to. The method name is for debug
  // tracing only. Inside CQLServiceImpl::Handle, we rely on the opcode to dispatch the execution.
  stream_id_ = cqlserver::CQLRequest::ParseStreamId(serialized_request_);

  return Status::OK();
}

const std::string& CQLInboundCall::service_name() const {
  static std::string result = "yb.cqlserver.CQLServerService"s;
  return result;
}

const std::string& CQLInboundCall::method_name() const {
  static std::string result = "ExecuteRequest"s;
  return result;
}

void CQLInboundCall::Serialize(std::deque<RefCntBuffer>* output) const {
  TRACE_EVENT0("rpc", "CQLInboundCall::Serialize");
  CHECK_GT(response_msg_buf_.size(), 0);

  output->push_back(response_msg_buf_);
}

void CQLInboundCall::RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code,
                                    const Status& status) {
  response_msg_buf_ = RefCntBuffer(status.message().data(), status.message().size());

  QueueResponse(false);
}

void CQLInboundCall::RespondSuccess(const RefCntBuffer& buffer,
                                    const yb::rpc::RpcMethodMetrics& metrics) {
  RecordHandlingCompleted(metrics.handler_latency);
  response_msg_buf_ = buffer;

  QueueResponse(true);
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
  return Format("CQL Call from $0", connection()->remote());
}

void CQLInboundCall::DumpPB(const rpc::DumpRunningRpcsRequestPB& req,
                            rpc::RpcCallInProgressPB* resp) {
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

} // namespace cqlserver
} // namespace yb
