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
#ifndef KUDU_RPC_RPC_CONTEXT_H
#define KUDU_RPC_RPC_CONTEXT_H

#include <string>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/rpc/rpc_header.pb.h"
#include "kudu/rpc/service_if.h"
#include "kudu/util/status.h"

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

namespace kudu {

class Sockaddr;
class Trace;

namespace rpc {

class InboundCall;
class RpcSidecar;
class UserCredentials;


#define PANIC_RPC(rpc_context, message) \
  do { \
    if (rpc_context) {                              \
      rpc_context->Panic(__FILE__, __LINE__, (message));  \
    } else { \
      LOG(FATAL) << message; \
    } \
  } while (0)

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
  RpcContext(InboundCall *call,
             const google::protobuf::Message *request_pb,
             google::protobuf::Message *response_pb,
             RpcMethodMetrics metrics);

  ~RpcContext();

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
  //     extend kudu.rpc.ErrorStatusPB {
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
  // Upon success, writes the index of the sidecar (necessary to be retrieved
  // later) to 'idx'. Call may fail if all sidecars have already been used
  // by the RPC response.
  Status AddRpcSidecar(gscoped_ptr<RpcSidecar> car, int* idx);

  // Return the credentials of the remote user who made this call.
  const UserCredentials& user_credentials() const;

  // Return the remote IP address and port which sent the current RPC call.
  const Sockaddr& remote_address() const;

  // A string identifying the requestor -- both the user info and the IP address.
  // Suitable for use in log messages.
  std::string requestor_string() const;

  const google::protobuf::Message *request_pb() const { return request_pb_.get(); }
  google::protobuf::Message *response_pb() const { return response_pb_.get(); }

  // Return an upper bound on the client timeout deadline. This does not
  // account for transmission delays between the client and the server.
  // If the client did not specify a deadline, returns MonoTime::Max().
  MonoTime GetClientDeadline() const;

  // Panic the server. This logs a fatal error with the given message, and
  // also includes the current RPC request, requestor, trace information, etc,
  // to make it easier to debug.
  //
  // Call this via the PANIC_RPC() macro.
  void Panic(const char* filepath, int line_number, const std::string& message)
    __attribute__((noreturn));

 private:
  InboundCall* const call_;
  const gscoped_ptr<const google::protobuf::Message> request_pb_;
  const gscoped_ptr<google::protobuf::Message> response_pb_;
  RpcMethodMetrics metrics_;
};

} // namespace rpc
} // namespace kudu
#endif
