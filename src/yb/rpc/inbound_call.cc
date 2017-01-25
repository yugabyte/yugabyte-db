// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "yb/rpc/inbound_call.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/connection.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/rpc/redis_encoding.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rpc_sidecar.h"
#include "yb/rpc/serialization.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/metrics.h"
#include "yb/util/trace.h"

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::MessageLite;
using google::protobuf::io::CodedOutputStream;
using std::shared_ptr;
using std::vector;
using strings::Substitute;
using yb::RedisResponsePB;

DEFINE_bool(rpc_dump_all_traces, false,
            "If true, dump all RPC traces at INFO level");
TAG_FLAG(rpc_dump_all_traces, advanced);
TAG_FLAG(rpc_dump_all_traces, runtime);


namespace yb {
namespace rpc {

InboundCall::InboundCall()
    : sidecars_deleter_(&sidecars_),
      trace_(new Trace) {
  RecordCallReceived();
}

InboundCall::~InboundCall() {}

void InboundCall::RespondSuccess(const MessageLite& response) {
  TRACE_EVENT0("rpc", "InboundCall::RespondSuccess");
  Respond(response, true);
}

void InboundCall::RespondFailure(ErrorStatusPB::RpcErrorCodePB error_code,
                                 const Status& status) {
  TRACE_EVENT0("rpc", "InboundCall::RespondFailure");
  ErrorStatusPB err;
  err.set_message(status.ToString());
  err.set_code(error_code);

  Respond(err, false);
}

void InboundCall::RespondApplicationError(int error_ext_id, const std::string& message,
                                          const MessageLite& app_error_pb) {
  ErrorStatusPB err;
  ApplicationErrorToPB(error_ext_id, message, app_error_pb, &err);
  Respond(err, false);
}

void InboundCall::ApplicationErrorToPB(int error_ext_id, const std::string& message,
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

void InboundCall::Respond(const MessageLite& response,
                          bool is_success) {
  TRACE_EVENT_FLOW_END0("rpc", "InboundCall", this);
  Status s = SerializeResponseBuffer(response, is_success);
  if (PREDICT_FALSE(!s.ok())) {
    // TODO: test error case, serialize error response instead
    LOG(DFATAL) << "Unable to serialize response: " << s.ToString();
  }

  TRACE_EVENT_ASYNC_END1("rpc", "InboundCall", this,
                         "method", remote_method_.method_name());
  TRACE_TO(trace_, "Queueing $0 response", is_success ? "success" : "failure");

  LogTrace();
  QueueResponseToConnection();
}

Status InboundCall::AddRpcSidecar(gscoped_ptr<RpcSidecar> car, int* idx) {
  // Check that the number of sidecars does not exceed the number of payload
  // slices that are free (two are used up by the header and main message
  // protobufs).
  if (sidecars_.size() + 2 > OutboundTransfer::kMaxPayloadSlices) {
    return STATUS(ServiceUnavailable, "All available sidecars already used");
  }
  sidecars_.push_back(car.release());
  *idx = sidecars_.size() - 1;
  return Status::OK();
}

const Sockaddr& InboundCall::remote_address() const {
  return get_connection()->remote();
}

const scoped_refptr<Connection> InboundCall::connection() const {
  return get_connection();
}

Trace* InboundCall::trace() {
  return trace_.get();
}

void InboundCall::RecordCallReceived() {
  TRACE_EVENT_ASYNC_BEGIN0("rpc", "InboundCall", this);
  DCHECK(!timing_.time_received.Initialized());  // Protect against multiple calls.
  timing_.time_received = MonoTime::Now(MonoTime::FINE);
}

void InboundCall::RecordHandlingStarted(scoped_refptr<Histogram> incoming_queue_time) {
  DCHECK(incoming_queue_time != nullptr);
  DCHECK(!timing_.time_handled.Initialized());  // Protect against multiple calls.
  timing_.time_handled = MonoTime::Now(MonoTime::FINE);
  incoming_queue_time->Increment(
      timing_.time_handled.GetDeltaSince(timing_.time_received).ToMicroseconds());
}

void InboundCall::RecordHandlingCompleted(scoped_refptr<Histogram> handler_run_time) {
  DCHECK(handler_run_time != nullptr);
  DCHECK(!timing_.time_completed.Initialized());  // Protect against multiple calls.
  timing_.time_completed = MonoTime::Now(MonoTime::FINE);
  handler_run_time->Increment(
      timing_.time_completed.GetDeltaSince(timing_.time_handled).ToMicroseconds());
}

const UserCredentials& InboundCall::user_credentials() const {
  return get_connection()->user_credentials();
}

YBInboundCall::YBInboundCall(YBConnection* conn) : conn_(conn) {}

bool YBInboundCall::ClientTimedOut() const {
  if (!header_.has_timeout_millis() || header_.timeout_millis() == 0) {
    return false;
  }

  MonoTime now = MonoTime::Now(MonoTime::FINE);
  int total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();
  return total_time > header_.timeout_millis();
}

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

Status YBInboundCall::SerializeResponseBuffer(const MessageLite& response,
                                              bool is_success) {
  uint32_t protobuf_msg_size = response.ByteSize();

  ResponseHeader resp_hdr;
  resp_hdr.set_call_id(header_.call_id());
  resp_hdr.set_is_error(!is_success);
  uint32_t absolute_sidecar_offset = protobuf_msg_size;
  for (RpcSidecar* car : sidecars_) {
    resp_hdr.add_sidecar_offsets(absolute_sidecar_offset);
    absolute_sidecar_offset += car->AsSlice().size();
  }

  int additional_size = absolute_sidecar_offset - protobuf_msg_size;
  RETURN_NOT_OK(serialization::SerializeMessage(response, &response_msg_buf_,
                                                additional_size, true));
  int main_msg_size = additional_size + response_msg_buf_.size();
  RETURN_NOT_OK(serialization::SerializeHeader(resp_hdr, main_msg_size,
                                               &response_hdr_buf_));
  return Status::OK();
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

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

void YBInboundCall::QueueResponseToConnection() {
  conn_->QueueResponseForCall(gscoped_ptr<InboundCall>(this).Pass());
}

scoped_refptr<Connection> YBInboundCall::get_connection() {
  return conn_;
}

const scoped_refptr<Connection> YBInboundCall::get_connection() const {
  return conn_;
}

void YBInboundCall::SerializeResponseTo(vector<Slice>* slices) const {
  TRACE_EVENT0("rpc", "YBInboundCall::SerializeResponseTo");
  CHECK_GT(response_hdr_buf_.size(), 0);
  CHECK_GT(response_msg_buf_.size(), 0);
  slices->reserve(slices->size() + 2 + sidecars_.size());
  slices->push_back(Slice(response_hdr_buf_));
  slices->push_back(Slice(response_msg_buf_));
  for (RpcSidecar* car : sidecars_) {
    slices->push_back(car->AsSlice());
  }
}

RedisInboundCall::RedisInboundCall(RedisConnection* conn) : conn_(conn) {}

Status RedisInboundCall::ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "RedisInboundCall", this);
  TRACE_EVENT0("rpc", "RedisInboundCall::ParseFrom");

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
    const ErrorStatusPB& error_status = ErrorStatusPB();
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

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took " << total_time << "ms. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

void RedisInboundCall::QueueResponseToConnection() {
  conn_->QueueResponseForCall(gscoped_ptr<InboundCall>(this).Pass());
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

CQLInboundCall::CQLInboundCall(CQLConnection* conn) : conn_(conn) {}

Status CQLInboundCall::ParseFrom(gscoped_ptr<AbstractInboundTransfer> transfer) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "CQLInboundCall", this);
  TRACE_EVENT0("rpc", "CQLInboundCall::ParseFrom");

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
  conn_->QueueResponseForCall(gscoped_ptr<InboundCall>(this).Pass());
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

}  // namespace rpc
}  // namespace yb
