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

#pragma once

#include <stdint.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>

#include <boost/version.hpp>

#include "yb/rpc/binary_call_parser.h"
#include "yb/rpc/circular_read_buffer.h"
#include "yb/rpc/connection_context.h"
#include "yb/rpc/connection.h"
#include "yb/rpc/reactor_thread_role.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc_with_call_id.h"
#include "yb/rpc/serialization.h"
#include "yb/rpc/sidecars.h"

#include "yb/util/ev_util.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/size_literals.h"
#include "yb/util/write_buffer.h"

namespace yb {
namespace rpc {

const char* const kUnknownRemoteMethod = "UNKNOWN_METHOD";

class YBConnectionContext : public ConnectionContextWithCallId, public BinaryCallParserListener {
 public:
  YBConnectionContext(
      size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker,
      const MemTrackerPtr& call_tracker);
  ~YBConnectionContext();

  const MemTrackerPtr& call_tracker() const { return call_tracker_; }

  void SetEventLoop(ev::loop_ref* loop) override;

  void Shutdown(const Status& status) override;

 protected:
  BinaryCallParser& parser() { return parser_; }

  ev::loop_ref* loop_ = nullptr;

  EvTimerHolder timer_;

 private:
  uint64_t ExtractCallId(InboundCall* call) override;

  StreamReadBuffer& ReadBuffer() override {
    return read_buffer_;
  }

  BinaryCallParser parser_;

  CircularReadBuffer read_buffer_;

  const MemTrackerPtr call_tracker_;
};

class YBInboundConnectionContext : public YBConnectionContext {
 public:
  YBInboundConnectionContext(
      size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker,
      const MemTrackerPtr& call_tracker)
      : YBConnectionContext(receive_buffer_size, buffer_tracker, call_tracker) {}

  static std::string Name() { return "Inbound RPC"; }
 private:
  // Takes ownership of call_data content.
  Status HandleCall(const ConnectionPtr& connection, CallData* call_data)
      ON_REACTOR_THREAD override;
  Status Connected(const ConnectionPtr& connection) override;
  Result<ProcessCallsResult> ProcessCalls(
      const ConnectionPtr& connection,
      const IoVecs& data,
      ReadBufferFull read_buffer_full) ON_REACTOR_THREAD override;

  // Takes ownership of call_data content.
  Status HandleInboundCall(const ConnectionPtr& connection, std::vector<char>* call_data);

  void HandleTimeout(ev::timer& watcher, int revents) ON_REACTOR_THREAD;  // NOLINT

  RpcConnectionPB::StateType State() override { return state_; }

  RpcConnectionPB::StateType state_ = RpcConnectionPB::UNKNOWN;

  void UpdateLastWrite(const ConnectionPtr& connection) override;

  std::weak_ptr<Connection> connection_;

  // Last time data was sent to network layer below application.
  CoarseTimePoint last_write_time_;
  // Last time we queued heartbeat for sending.
  CoarseTimePoint last_heartbeat_sending_time_;
};

class YBInboundCall : public InboundCall {
 public:
  YBInboundCall(ConnectionPtr conn, CallProcessedListener* call_processed_listener);
  explicit YBInboundCall(RpcMetrics* rpc_metrics, const RemoteMethod& remote_method);
  virtual ~YBInboundCall();

  // Parse an inbound call message.
  //
  // This only deserializes the call header, populating the 'header_' and
  // 'serialized_request_' member variables. The actual call parameter is
  // not deserialized, as this may be CPU-expensive, and this is called
  // from the reactor thread.
  //
  // Takes ownership of call_data content.
  Status ParseFrom(const MemTrackerPtr& mem_tracker, CallData* call_data);

  int32_t call_id() const {
    return header_.call_id;
  }

  Slice serialized_remote_method() const override {
    return header_.remote_method;
  }

  Slice method_name() const override;

  Sidecars& sidecars() {
    return sidecars_;
  }

  // Serializes 'response' into the InboundCall's internal buffer, and marks
  // the call as a success. Enqueues the response back to the connection
  // that made the call.
  //
  // This method deletes the InboundCall object, so no further calls may be
  // made after this one.
  void RespondSuccess(AnyMessageConstPtr response);

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
  void DoSerialize(ByteBlocks* output) override;

  void LogTrace() const override;
  std::string ToString() const override;
  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) override;

  CoarseTimePoint GetClientDeadline() const override;

  MonoTime ReceiveTime() const {
    return timing_.time_received;
  }

  virtual Status ParseParam(RpcCallParams* params);

  size_t ObjectSize() const override { return sizeof(*this); }

  Result<RefCntSlice> ExtractSidecar(size_t idx) const;

  size_t DynamicMemoryUsage() const override {
    return InboundCall::DynamicMemoryUsage() +
           response_data_memory_usage_.load(std::memory_order_acquire);
  }

 protected:
  ScopedTrackedConsumption consumption_;

  Sidecars sidecars_;

  // Serialize and queue the response.
  virtual void Respond(AnyMessageConstPtr response, bool is_success);

 private:
  // Serialize a response message for either success or failure. If it is a success,
  // 'response' should be the user-defined response type for the call. If it is a
  // failure, 'response' should be an ErrorStatusPB instance.
  Status SerializeResponseBuffer(AnyMessageConstPtr response, bool is_success);

  // Returns number of bytes copied.
  size_t CopyToLastSidecarBuffer(const Slice& slice);
  void AllocateSidecarBuffer(size_t size);

  // The header of the incoming call. Set by ParseFrom()
  ParsedRequestHeader header_;

  // The buffers for serialized response. Set by SerializeResponseBuffer().
  RefCntBuffer response_buf_;
  std::atomic<size_t> response_data_memory_usage_{0};

  ReceivedSidecars received_sidecars_;

  // Cache of result of YBInboundCall::ToString().
  mutable std::string cached_to_string_;
};

class YBOutboundConnectionContext : public YBConnectionContext {
 public:
  YBOutboundConnectionContext(
      size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker,
      const MemTrackerPtr& call_tracker)
      : YBConnectionContext(receive_buffer_size, buffer_tracker, call_tracker) {}

  static std::string Name() { return "Outbound RPC"; }

 private:
  RpcConnectionPB::StateType State() override {
    return RpcConnectionPB::OPEN;
  }

  // Takes ownership of call_data content.
  Status HandleCall(const ConnectionPtr& connection, CallData* call_data)
      ON_REACTOR_THREAD override;
  Status Connected(const ConnectionPtr& connection) override;
  Status AssignConnection(const ConnectionPtr& connection) override;
  Result<ProcessCallsResult> ProcessCalls(
      const ConnectionPtr& connection,
      const IoVecs& data,
      ReadBufferFull read_buffer_full) ON_REACTOR_THREAD override;

  void UpdateLastRead(const ConnectionPtr& connection) override;

  void HandleTimeout(ev::timer& watcher, int revents) ON_REACTOR_THREAD;  // NOLINT

  std::weak_ptr<Connection> connection_;

  CoarseTimePoint last_read_time_;
};

} // namespace rpc
} // namespace yb
