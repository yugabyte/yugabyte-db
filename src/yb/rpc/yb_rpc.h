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

#ifndef YB_RPC_YB_RPC_H
#define YB_RPC_YB_RPC_H

#include "yb/rpc/binary_call_parser.h"
#include "yb/rpc/connection_context.h"
#include "yb/rpc/rpc_with_call_id.h"

namespace yb {
namespace rpc {

class YBConnectionContext : public ConnectionContextWithCallId, public BinaryCallParserListener {
 public:
  YBConnectionContext();
  ~YBConnectionContext();

 private:
  uint64_t ExtractCallId(InboundCall* call) override;

  size_t BufferLimit() override;

  Result<size_t> ProcessCalls(const ConnectionPtr& connection,
                              const IoVecs& data,
                              ReadBufferFull read_buffer_full) override;

  void Connected(const ConnectionPtr& connection) override;
  void AssignConnection(const ConnectionPtr& connection) override;

  // Takes ownership of call_data content.
  CHECKED_STATUS HandleCall(const ConnectionPtr& connection, std::vector<char>* call_data) override;

  // Takes ownership of call_data content.
  CHECKED_STATUS HandleInboundCall(const ConnectionPtr& connection, std::vector<char>* call_data);

  RpcConnectionPB::StateType State() override { return state_; }

  RpcConnectionPB::StateType state_ = RpcConnectionPB::UNKNOWN;

  BinaryCallParser parser_;
};

class YBInboundCall : public InboundCall {
 public:
  YBInboundCall(ConnectionPtr conn, CallProcessedListener call_processed_listener);
  explicit YBInboundCall(const RemoteMethod& remote_method);
  virtual ~YBInboundCall();

  // Is this a local call?
  virtual bool IsLocalCall() const { return false; }

  // Parse an inbound call message.
  //
  // This only deserializes the call header, populating the 'header_' and
  // 'serialized_request_' member variables. The actual call parameter is
  // not deserialized, as this may be CPU-expensive, and this is called
  // from the reactor thread.
  //
  // Takes ownership of call_data content.
  CHECKED_STATUS ParseFrom(std::vector<char>* call_data);

  int32_t call_id() const {
    return header_.call_id();
  }

  const RemoteMethod& remote_method() const {
    return remote_method_;
  }

  // See RpcContext::AddRpcSidecar()
  CHECKED_STATUS AddRpcSidecar(RefCntBuffer car, int* idx);

  // See RpcContext::ResetRpcSidecars()
  void ResetRpcSidecars();

  // Serializes 'response' into the InboundCall's internal buffer, and marks
  // the call as a success. Enqueues the response back to the connection
  // that made the call.
  //
  // This method deletes the InboundCall object, so no further calls may be
  // made after this one.
  void RespondSuccess(const google::protobuf::MessageLite& response);

  // Serializes a failure response into the internal buffer, marking the
  // call as a failure. Enqueues the response back to the connection that
  // made the call.
  //
  // This method deletes the InboundCall object, so no further calls may be
  // made after this one.
  void RespondFailure(ErrorStatusPB::RpcErrorCodePB error_code,
                      const Status &status) override;

  void RespondApplicationError(int error_ext_id, const std::string& message,
                               const google::protobuf::MessageLite& app_error_pb);

  // Convert an application error extension to an ErrorStatusPB.
  // These ErrorStatusPB objects are what are returned in application error responses.
  static void ApplicationErrorToPB(int error_ext_id, const std::string& message,
                                   const google::protobuf::MessageLite& app_error_pb,
                                   ErrorStatusPB* err);

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  void Serialize(std::deque<RefCntBuffer>* output) const override;

  void LogTrace() const override;
  std::string ToString() const override;
  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override;

  MonoTime GetClientDeadline() const override;

  const std::string& method_name() const override {
    return remote_method_.method_name();
  }

  const std::string& service_name() const override {
    return remote_method_.service_name();
  }

  virtual CHECKED_STATUS ParseParam(google::protobuf::Message *message);

  void RespondBadMethod();

 protected:
  // Vector of additional sidecars that are tacked on to the call's response
  // after serialization of the protobuf. See rpc/rpc_sidecar.h for more info.
  std::vector<RefCntBuffer> sidecars_;

  // Serialize and queue the response.
  virtual void Respond(const google::protobuf::MessageLite& response, bool is_success);

 private:
  // Serialize a response message for either success or failure. If it is a success,
  // 'response' should be the user-defined response type for the call. If it is a
  // failure, 'response' should be an ErrorStatusPB instance.
  CHECKED_STATUS SerializeResponseBuffer(const google::protobuf::MessageLite& response,
                                         bool is_success);

  // The header of the incoming call. Set by ParseFrom()
  RequestHeader header_;

  // The buffers for serialized response. Set by SerializeResponseBuffer().
  RefCntBuffer response_buf_;

  // Proto service this calls belongs to. Used for routing.
  // This field is filled in when the inbound request header is parsed.
  RemoteMethod remote_method_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_YB_RPC_H
