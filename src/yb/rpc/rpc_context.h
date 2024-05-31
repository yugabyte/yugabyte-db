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
#pragma once

#include <string>

#include <boost/type_traits/is_detected.hpp>

#include "yb/rpc/rpc_header.pb.h"
#include "yb/rpc/serialization.h"
#include "yb/rpc/service_if.h"

#include "yb/ash/wait_state.h"

#include "yb/util/memory/arena.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"
#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

namespace yb {

class Trace;
class WriteBuffer;

namespace rpc {

class YBInboundCall;

class RpcCallParams {
 public:
  virtual ~RpcCallParams() = default;

  virtual Result<size_t> ParseRequest(Slice param, const RefCntBuffer& buffer) = 0;
  virtual AnyMessageConstPtr SerializableResponse() = 0;
};

class RpcCallPBParams : public RpcCallParams {
 public:
  Result<size_t> ParseRequest(Slice param, const RefCntBuffer& buffer) override;

  AnyMessageConstPtr SerializableResponse() override;

  virtual google::protobuf::Message& request() = 0;
  virtual google::protobuf::Message& response() = 0;

  static google::protobuf::Message* CastMessage(const AnyMessagePtr& msg);

  static const google::protobuf::Message* CastMessage(const AnyMessageConstPtr& msg);
};

template <class Req, class Resp>
class RpcCallPBParamsImpl : public RpcCallPBParams {
 public:
  using RequestType = Req;
  using ResponseType = Resp;

  RpcCallPBParamsImpl() = default;

  Req& request() override {
    return req_;
  }

  Resp& response() override {
    return resp_;
  }

 private:
  Req req_;
  Resp resp_;
};

class RpcCallLWParams : public RpcCallParams {
 public:
  Result<size_t> ParseRequest(Slice param, const RefCntBuffer& buffer) override;
  AnyMessageConstPtr SerializableResponse() override;

  virtual LightweightMessage& request() = 0;
  virtual LightweightMessage& response() = 0;

  static LightweightMessage* CastMessage(const AnyMessagePtr& msg);

  static const LightweightMessage* CastMessage(const AnyMessageConstPtr& msg);

 private:
  RefCntBuffer buffer_;
};

template <class Req, class Resp>
class RpcCallLWParamsImpl : public RpcCallLWParams {
 public:
  using RequestType = Req;
  using ResponseType = Resp;

  Req& request() override {
    return req_;
  }

  Resp& response() override {
    return resp_;
  }

  RpcCallLWParamsImpl() : req_(&arena_), resp_(&arena_) {}

 private:
  ThreadSafeArena arena_;
  Req req_;
  Resp resp_;
};

template <class T>
using MutableErrorDetector = decltype(boost::declval<T&>().mutable_error());

template <bool>
struct ResponseErrorHelper;

template <>
struct ResponseErrorHelper<true> {
  template <class T>
  static auto Apply(T* t) {
    return t->mutable_error();
  }
};

template <>
struct ResponseErrorHelper<false> {
  template <class T>
  static auto Apply(T* t) {
    return t->mutable_status();
  }
};

template <class T>
auto ResponseError(T* t) {
  return ResponseErrorHelper<boost::is_detected_v<MutableErrorDetector, T>>::Apply(t);
}

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
             std::shared_ptr<RpcCallParams> params);
  explicit RpcContext(std::shared_ptr<LocalYBInboundCall> call);

  RpcContext(RpcContext&& rhs) = default;
  RpcContext& operator=(RpcContext&& rhs) = default;

  RpcContext(const RpcContext&) = delete;
  void operator=(const RpcContext&) = delete;

  ~RpcContext();

  explicit operator bool() const {
    return call_ != nullptr;
  }

  // Return the trace buffer for this call.
  Trace* trace();

  // Ensure that this call has a trace associated with it.
  void EnsureTraceCreated();

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

  Sidecars& sidecars();

  // Return the remote endpoint which sent the current RPC call.
  const Endpoint& remote_address() const;
  // Return the local endpoint which received the current RPC call.
  const Endpoint& local_address() const;

  // A string identifying the requestor -- both the user info and the IP address.
  // Suitable for use in log messages.
  std::string requestor_string() const;

  // Return an upper bound on the client timeout deadline. This does not
  // account for transmission delays between the client and the server.
  // If the client did not specify a deadline, returns MonoTime::Max().
  CoarseTimePoint GetClientDeadline() const;

  MonoTime ReceiveTime() const;

  RpcCallParams& params() {
    return *params_;
  }

  const std::shared_ptr<RpcCallParams>& shared_params() const {
    return params_;
  }

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

  const ash::WaitStateInfoPtr& wait_state() const;

  Result<RefCntSlice> ExtractSidecar(size_t idx) const;

 private:
  std::shared_ptr<YBInboundCall> call_;
  std::shared_ptr<RpcCallParams> params_;
  bool responded_ = false;
};

void PanicRpc(RpcContext* context, const char* file, int line_number, const std::string& message);

#define PANIC_RPC(rpc_context, message) \
  do { \
    yb::rpc::PanicRpc((rpc_context), __FILE__, __LINE__, (message)); \
  } while (false)

inline std::string RequestorString(yb::rpc::RpcContext* rpc) {
  if (rpc) {
    return rpc->requestor_string();
  } else {
    return "internal request";
  }
}

} // namespace rpc
} // namespace yb
