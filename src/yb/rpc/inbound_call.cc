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
#include "yb/rpc/serialization.h"
#include "yb/rpc/service_pool.h"
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
DEFINE_bool(collect_end_to_end_traces, false,
            "If true, collected traces includes information for sub-components "
            "potentially running on a different server. ");
DEFINE_int32(print_trace_every, 0,
             "Controls the rate at which traces are printed. Setting this to 0 "
             "disables printing the collected traces.");
DEFINE_int32(rpc_slow_query_threshold_ms, 50,
             "Traces for calls that take longer than this threshold (in ms) are logged");
TAG_FLAG(rpc_dump_all_traces, advanced);
TAG_FLAG(rpc_dump_all_traces, runtime);
TAG_FLAG(collect_end_to_end_traces, advanced);
TAG_FLAG(collect_end_to_end_traces, runtime);
TAG_FLAG(print_trace_every, advanced);
TAG_FLAG(print_trace_every, runtime);
TAG_FLAG(rpc_slow_query_threshold_ms, advanced);
TAG_FLAG(rpc_slow_query_threshold_ms, runtime);

namespace yb {
namespace rpc {

InboundCall::InboundCall()
    : trace_(new Trace) {
  TRACE_TO(trace_, "Created InboundCall");
  RecordCallReceived();
}

InboundCall::~InboundCall() {
  TRACE_TO(trace_, "Destroying InboundCall");
  YB_LOG_IF_EVERY_N(INFO, FLAGS_print_trace_every > 0, FLAGS_print_trace_every)
      << "Tracing op: \n " << trace_->DumpToString(true);
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
  get_connection()->QueueOutboundData(InboundCallPtr(this));
}

Status InboundCall::AddRpcSidecar(util::RefCntBuffer car, int* idx) {
  // Check that the number of sidecars does not exceed the number of payload
  // slices that are free (two are used up by the header and main message
  // protobufs).
  if (sidecars_.size() + 2 > OutboundTransfer::kMaxPayloadSlices) {
    return STATUS(ServiceUnavailable, "All available sidecars already used");
  }
  *idx = sidecars_.size();
  sidecars_.push_back(std::move(car));
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

bool InboundCall::ClientTimedOut() const {
  auto deadline = GetClientDeadline();
  if (deadline.Equals(MonoTime::Max())) {
    return false;
  }

  MonoTime now = MonoTime::Now(MonoTime::FINE);
  return deadline.ComesBefore(now);
}

const UserCredentials& InboundCall::user_credentials() const {
  return get_connection()->user_credentials();
}

}  // namespace rpc
}  // namespace yb
