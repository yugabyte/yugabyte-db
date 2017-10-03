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

#include "kudu/rpc/rpc_context.h"

#include <ostream>
#include <sstream>

#include "kudu/rpc/outbound_call.h"
#include "kudu/rpc/inbound_call.h"
#include "kudu/rpc/rpc_sidecar.h"
#include "kudu/rpc/service_if.h"
#include "kudu/util/hdr_histogram.h"
#include "kudu/util/metrics.h"
#include "kudu/util/trace.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/jsonwriter.h"
#include "kudu/util/pb_util.h"

using google::protobuf::Message;

namespace kudu {
namespace rpc {

namespace {

// Wrapper for a protobuf message which lazily converts to JSON when
// the trace buffer is dumped. This pushes the work of stringification
// to the trace dumping process.
class PbTracer : public debug::ConvertableToTraceFormat {
 public:
  enum {
    kMaxFieldLengthToTrace = 100
  };

  explicit PbTracer(const Message& msg) : msg_(msg.New()) {
    msg_->CopyFrom(msg);
  }

  virtual void AppendAsTraceFormat(std::string* out) const OVERRIDE {
    pb_util::TruncateFields(msg_.get(), kMaxFieldLengthToTrace);
    std::stringstream ss;
    JsonWriter jw(&ss, JsonWriter::COMPACT);
    jw.Protobuf(*msg_);
    out->append(ss.str());
  }
 private:
  const gscoped_ptr<Message> msg_;
};

scoped_refptr<debug::ConvertableToTraceFormat> TracePb(const Message& msg) {
  return make_scoped_refptr(new PbTracer(msg));
}
} // anonymous namespace

RpcContext::RpcContext(InboundCall *call,
                       const google::protobuf::Message *request_pb,
                       google::protobuf::Message *response_pb,
                       RpcMethodMetrics metrics)
  : call_(CHECK_NOTNULL(call)),
    request_pb_(request_pb),
    response_pb_(response_pb),
    metrics_(metrics) {
  VLOG(4) << call_->remote_method().service_name() << ": Received RPC request for "
          << call_->ToString() << ":" << std::endl << request_pb_->DebugString();
  TRACE_EVENT_ASYNC_BEGIN2("rpc_call", "RPC", this,
                           "call", call_->ToString(),
                           "request", TracePb(*request_pb_));
}

RpcContext::~RpcContext() {
}

void RpcContext::RespondSuccess() {
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  VLOG(4) << call_->remote_method().service_name() << ": Sending RPC success response for "
          << call_->ToString() << ":" << std::endl << response_pb_->DebugString();
  TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                         "response", TracePb(*response_pb_),
                         "trace", trace()->DumpToString(true));
  call_->RespondSuccess(*response_pb_);
  delete this;
}

void RpcContext::RespondFailure(const Status &status) {
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  VLOG(4) << call_->remote_method().service_name() << ": Sending RPC failure response for "
          << call_->ToString() << ": " << status.ToString();
  TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                         "status", status.ToString(),
                         "trace", trace()->DumpToString(true));
  call_->RespondFailure(ErrorStatusPB::ERROR_APPLICATION,
                        status);
  delete this;
}

void RpcContext::RespondRpcFailure(ErrorStatusPB_RpcErrorCodePB err, const Status& status) {
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  VLOG(4) << call_->remote_method().service_name() << ": Sending RPC failure response for "
          << call_->ToString() << ": " << status.ToString();
  TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                         "status", status.ToString(),
                         "trace", trace()->DumpToString(true));
  call_->RespondFailure(err, status);
  delete this;
}

void RpcContext::RespondApplicationError(int error_ext_id, const std::string& message,
                                         const Message& app_error_pb) {
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  if (VLOG_IS_ON(4)) {
    ErrorStatusPB err;
    InboundCall::ApplicationErrorToPB(error_ext_id, message, app_error_pb, &err);
    VLOG(4) << call_->remote_method().service_name() << ": Sending application error response for "
            << call_->ToString() << ":" << std::endl << err.DebugString();
    TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                           "response", TracePb(app_error_pb),
                           "trace", trace()->DumpToString(true));
  }
  call_->RespondApplicationError(error_ext_id, message, app_error_pb);
  delete this;
}

Status RpcContext::AddRpcSidecar(gscoped_ptr<RpcSidecar> car, int* idx) {
  return call_->AddRpcSidecar(car.Pass(), idx);
}

const UserCredentials& RpcContext::user_credentials() const {
  return call_->user_credentials();
}

const Sockaddr& RpcContext::remote_address() const {
  return call_->remote_address();
}

std::string RpcContext::requestor_string() const {
  return call_->user_credentials().ToString() + " at " +
    call_->remote_address().ToString();
}

MonoTime RpcContext::GetClientDeadline() const {
  return call_->GetClientDeadline();
}

Trace* RpcContext::trace() {
  return call_->trace();
}

void RpcContext::Panic(const char* filepath, int line_number, const string& message) {
  // Use the LogMessage class directly so that the log messages appear to come from
  // the line of code which caused the panic, not this code.
#define MY_ERROR google::LogMessage(filepath, line_number, google::GLOG_ERROR).stream()
#define MY_FATAL google::LogMessageFatal(filepath, line_number).stream()

  MY_ERROR << "Panic handling " << call_->ToString() << ": " << message;
  MY_ERROR << "Request:\n" << request_pb_->DebugString();
  Trace* t = trace();
  if (t) {
    MY_ERROR << "RPC trace:";
    t->Dump(&MY_ERROR, true);
  }
  MY_FATAL << "Exiting due to panic.";

#undef MY_ERROR
#undef MY_FATAL
}


} // namespace rpc
} // namespace kudu
