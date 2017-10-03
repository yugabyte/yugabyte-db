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

#include "kudu/rpc/inbound_call.h"

#include <memory>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/connection.h"
#include "kudu/rpc/rpc_introspection.pb.h"
#include "kudu/rpc/rpc_sidecar.h"
#include "kudu/rpc/serialization.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/metrics.h"
#include "kudu/util/trace.h"

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::MessageLite;
using google::protobuf::io::CodedOutputStream;
using std::shared_ptr;
using std::vector;
using strings::Substitute;

DEFINE_bool(rpc_dump_all_traces, false,
            "If true, dump all RPC traces at INFO level");
TAG_FLAG(rpc_dump_all_traces, advanced);
TAG_FLAG(rpc_dump_all_traces, runtime);


namespace kudu {
namespace rpc {

InboundCall::InboundCall(Connection* conn)
  : conn_(conn),
    sidecars_deleter_(&sidecars_),
    trace_(new Trace) {
  RecordCallReceived();
}

InboundCall::~InboundCall() {}

Status InboundCall::ParseFrom(gscoped_ptr<InboundTransfer> transfer) {
  TRACE_EVENT_FLOW_BEGIN0("rpc", "InboundCall", this);
  TRACE_EVENT0("rpc", "InboundCall::ParseFrom");
  RETURN_NOT_OK(serialization::ParseMessage(transfer->data(), &header_, &serialized_request_));

  // Adopt the service/method info from the header as soon as it's available.
  if (PREDICT_FALSE(!header_.has_remote_method())) {
    return Status::Corruption("Non-connection context request header must specify remote_method");
  }
  if (PREDICT_FALSE(!header_.remote_method().IsInitialized())) {
    return Status::Corruption("remote_method in request header is not initialized",
                              header_.remote_method().InitializationErrorString());
  }
  remote_method_.FromPB(header_.remote_method());

  // Retain the buffer that we have a view into.
  transfer_.swap(transfer);
  return Status::OK();
}

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
  conn_->QueueResponseForCall(gscoped_ptr<InboundCall>(this).Pass());
}

Status InboundCall::SerializeResponseBuffer(const MessageLite& response,
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

void InboundCall::SerializeResponseTo(vector<Slice>* slices) const {
  TRACE_EVENT0("rpc", "InboundCall::SerializeResponseTo");
  CHECK_GT(response_hdr_buf_.size(), 0);
  CHECK_GT(response_msg_buf_.size(), 0);
  slices->reserve(slices->size() + 2 + sidecars_.size());
  slices->push_back(Slice(response_hdr_buf_));
  slices->push_back(Slice(response_msg_buf_));
  for (RpcSidecar* car : sidecars_) {
    slices->push_back(car->AsSlice());
  }
}

Status InboundCall::AddRpcSidecar(gscoped_ptr<RpcSidecar> car, int* idx) {
  // Check that the number of sidecars does not exceed the number of payload
  // slices that are free (two are used up by the header and main message
  // protobufs).
  if (sidecars_.size() + 2 > OutboundTransfer::kMaxPayloadSlices) {
    return Status::ServiceUnavailable("All available sidecars already used");
  }
  sidecars_.push_back(car.release());
  *idx = sidecars_.size() - 1;
  return Status::OK();
}

string InboundCall::ToString() const {
  return Substitute("Call $0 from $1 (request call id $2)",
                      remote_method_.ToString(),
                      conn_->remote().ToString(),
                      header_.call_id());
}

void InboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                         RpcCallInProgressPB* resp) {
  resp->mutable_header()->CopyFrom(header_);
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
  resp->set_micros_elapsed(MonoTime::Now(MonoTime::FINE).GetDeltaSince(timing_.time_received)
                           .ToMicroseconds());
}

void InboundCall::LogTrace() const {
  MonoTime now = MonoTime::Now(MonoTime::FINE);
  int total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();

  if (header_.has_timeout_millis() && header_.timeout_millis() > 0) {
    double log_threshold = header_.timeout_millis() * 0.75f;
    if (total_time > log_threshold) {
      // TODO: consider pushing this onto another thread since it may be slow.
      // The traces may also be too large to fit in a log message.
      LOG(WARNING) << ToString() << " took " << total_time << "ms (client timeout "
                   << header_.timeout_millis() << ").";
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

const UserCredentials& InboundCall::user_credentials() const {
  return conn_->user_credentials();
}

const Sockaddr& InboundCall::remote_address() const {
  return conn_->remote();
}

const scoped_refptr<Connection>& InboundCall::connection() const {
  return conn_;
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

bool InboundCall::ClientTimedOut() const {
  if (!header_.has_timeout_millis() || header_.timeout_millis() == 0) {
    return false;
  }

  MonoTime now = MonoTime::Now(MonoTime::FINE);
  int total_time = now.GetDeltaSince(timing_.time_received).ToMilliseconds();
  return total_time > header_.timeout_millis();
}

MonoTime InboundCall::GetClientDeadline() const {
  if (!header_.has_timeout_millis() || header_.timeout_millis() == 0) {
    return MonoTime::Max();
  }
  MonoTime deadline = timing_.time_received;
  deadline.AddDelta(MonoDelta::FromMilliseconds(header_.timeout_millis()));
  return deadline;
}

} // namespace rpc
} // namespace kudu
