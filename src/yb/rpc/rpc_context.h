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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_RPC_RPC_CONTEXT_H
#define YB_RPC_RPC_CONTEXT_H

#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/rpc/rpc_header.pb.h"
#include "yb/rpc/service_if.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

namespace yb {

class Trace;

namespace util {

class RefCntBuffer;

}

namespace rpc {

class YBInboundCall;

// The context provided to a generated ServiceIf. This provides
// methods to respond to the RPC. In the future, this will also
// include methods to access information about the caller: e.g
// authentication info, tracing info, and cancellation status.
//
// This is the server-side analogue to the RpcController class.
class RpcContext {
 public:
  // Create an RpcContext. This is called only from generated code
  // and is not a public API.
  RpcContext(std::shared_ptr<YBInboundCall> call,
             std::shared_ptr<google::protobuf::Message> request_pb,
             std::shared_ptr<google::protobuf::Message> response_pb,
             RpcMethodMetrics metrics);
  RpcContext(std::shared_ptr<LocalYBInboundCall> call,
             RpcMethodMetrics metrics);

  RpcContext(RpcContext&& rhs)
      : call_(std::move(rhs.call_)),
        request_pb_(std::move(rhs.request_pb_)),
        response_pb_(std::move(rhs.response_pb_)),
        metrics_(std::move(rhs.metrics_)),
        responded_(rhs.responded_) {
  }

  RpcContext(const RpcContext&) = delete;
  void operator=(const RpcContext&) = delete;

  ~RpcContext();

  explicit operator bool() const {
    return call_ != nullptr;
  }

  // Return the trace buffer for this call.
  Trace* trace();

  // Send a response to the call. The service may call this method
  // before or after returning from the original handler method,
  // and it may call this method from a different thread.
  //
  // The response should be prepared already in the response PB pointer
  // which was passed to the handler method.
  //
  // After this method returns, this RpcContext object is destroyed. The request
  // and response protobufs are also destroyed.
  void RespondSuccess();

  static void RespondSuccess(InboundCall* call);

  // Respond with an error to the client. This sends back an error with the code
  // ERROR_APPLICATION. Because there is no more specific error code passed back
  // to the client, most applications should create a custom error PB extension
  // and use RespondApplicationError(...) below. This method should only be used
  // for unexpected errors where the server doesn't expect the client to do any
  // more advanced handling.
  //
  // After this method returns, this RpcContext object is destroyed. The request
  // and response protobufs are also destroyed.
  void RespondFailure(const Status &status);

  // Respond with an RPC-level error. This typically manifests to the client as
  // a remote error, one whose handling is agnostic to the particulars of the
  // sent RPC. For example, ERROR_SERVER_TOO_BUSY usually causes the client to
  // retry the RPC at a later time.
  //
  // After this method returns, this RpcContext object is destroyed. The request
  // and response protobufs are also destroyed.
  void RespondRpcFailure(ErrorStatusPB_RpcErrorCodePB err, const Status& status);

  // Respond with an application-level error. This causes the caller to get a
  // RemoteError status with the provided string message. Additionally, a
  // service-specific error extension is passed back to the client. The
  // extension must be registered with the ErrorStatusPB protobuf. For
  // example:
  //
  //   message MyServiceError {
  //     extend yb.rpc.ErrorStatusPB {
  //       optional MyServiceError my_service_error_ext = 101;
  //     }
  //     // Add any extra fields or status codes you want to pass back to
  //     // the client here.
  //     required string extra_error_data = 1;
  //   }
  //
  // NOTE: the numeric '101' above must be an integer greater than 101
  // and must be unique across your code base.
  //
  // Given the above definition in your service protobuf file, you would
  // use this method like:
  //
  //   MyServiceError err;
  //   err.set_extra_error_data("foo bar");
  //   ctx->RespondApplicationError(MyServiceError::my_service_error_ext.number(),
  //                                "Some error occurred", err);
  //
  // The client side may then retreieve the error by calling:
  //   const MyServiceError& err_details =
  //     controller->error_response()->GetExtension(MyServiceError::my_service_error_ext);
  //
  // After this method returns, this RpcContext object is destroyed. The request
  // and response protobufs are also destroyed.
  void RespondApplicationError(int error_ext_id, const std::string& message,
                               const google::protobuf::Message& app_error_pb);

  // Adds an RpcSidecar to the response. This is the preferred method for
  // transferring large amounts of binary data, because this avoids additional
  // copies made by serializing the protobuf.
  //
  // Assumes no changes to the sidecar's data are made after insertion.
  //
  // Returns the index of the sidecar.
  size_t AddRpcSidecar(const Slice& car);

  // Removes all RpcSidecars.
  void ResetRpcSidecars();

  // Like in STL reserve preallocates buffer, so adding new sidecars would not allocate memory
  // up to space bytes.
  void ReserveSidecarSpace(size_t space);

  // Return the remote endpoint which sent the current RPC call.
  const Endpoint& remote_address() const;
  // Return the local endpoint which received the current RPC call.
  const Endpoint& local_address() const;

  // A string identifying the requestor -- both the user info and the IP address.
  // Suitable for use in log messages.
  std::string requestor_string() const;

  const google::protobuf::Message *request_pb() const { return request_pb_.get(); }
  google::protobuf::Message *response_pb() const { return response_pb_.get(); }

  // Return an upper bound on the client timeout deadline. This does not
  // account for transmission delays between the client and the server.
  // If the client did not specify a deadline, returns MonoTime::Max().
  CoarseTimePoint GetClientDeadline() const;

  MonoTime ReceiveTime() const;

  // Panic the server. This logs a fatal error with the given message, and
  // also includes the current RPC request, requestor, trace information, etc,
  // to make it easier to debug.
  //
  // Call this via the PANIC_RPC() macro.
  void Panic(const char* filepath, int line_number, const std::string& message)
    __attribute__((noreturn));

  // Returns true if the call has been responded.
  bool responded() const { return responded_; }

  // Closes connection that received this request.
  void CloseConnection();

  std::string ToString() const;

 private:
  std::shared_ptr<YBInboundCall> call_;
  std::shared_ptr<const google::protobuf::Message> request_pb_;
  std::shared_ptr<google::protobuf::Message> response_pb_;
  RpcMethodMetrics metrics_;
  bool responded_ = false;
};

void PanicRpc(RpcContext* context, const char* file, int line_number, const std::string& message);

#define PANIC_RPC(rpc_context, message) \
  do { \
    yb::rpc::PanicRpc((rpc_context), __FILE__, __LINE__, (message)); \
  } while (false)

} // namespace rpc
} // namespace yb
#endif // YB_RPC_RPC_CONTEXT_H
