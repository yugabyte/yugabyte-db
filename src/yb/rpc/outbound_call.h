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

#include <stdint.h>

#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <boost/functional/hash.hpp>
#include "yb/util/flags.h"
#include <glog/logging.h>

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/call_data.h"
#include "yb/rpc/constants.h"
#include "yb/rpc/lightweight_message.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/rpc_call.h"
#include "yb/rpc/rpc_header.pb.h"
#include "yb/rpc/serialization.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/thread_pool.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/status_fwd.h"
#include "yb/util/atomic.h"
#include "yb/util/locks.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/object_pool.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/shared_lock.h"
#include "yb/util/slice.h"
#include "yb/util/trace.h"

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace yb {
namespace rpc {

// Used to key on Connection information.
// For use as a key in an unordered STL collection, use ConnectionIdHash and ConnectionIdEqual.
// This class is copyable for STL compatibility, but not assignable (use CopyFrom() for that).
class ConnectionId {
 public:
  ConnectionId() {}

  // Convenience constructor.
  ConnectionId(const Endpoint& remote, size_t idx, const Protocol* protocol)
      : remote_(remote), idx_(idx), protocol_(protocol) {}

  // The remote address.
  const Endpoint& remote() const { return remote_; }
  uint8_t idx() const { return idx_; }
  const Protocol* protocol() const { return protocol_; }

  // Returns a string representation of the object, not including the password field.
  std::string ToString() const;

  size_t HashCode() const;

 private:
  // Remember to update HashCode() and Equals() when new fields are added.
  Endpoint remote_;
  uint8_t idx_ = 0;  // Connection index, used to support multiple connections to the same server.
  const Protocol* protocol_ = nullptr;
};

class ConnectionIdHash {
 public:
  std::size_t operator() (const ConnectionId& conn_id) const;
};

inline bool operator==(const ConnectionId& lhs, const ConnectionId& rhs) {
  return lhs.remote() == rhs.remote() && lhs.idx() == rhs.idx() && lhs.protocol() == rhs.protocol();
}

// Container for OutboundCall metrics
struct OutboundCallMetrics {
  explicit OutboundCallMetrics(const scoped_refptr<MetricEntity>& metric_entity);

  scoped_refptr<EventStats> queue_time;
  scoped_refptr<EventStats> send_time;
  scoped_refptr<EventStats> time_to_response;
};

// A response to a call, on the client side.
// Upon receiving a response, this is allocated in the reactor thread and filled
// into the OutboundCall instance via OutboundCall::SetResponse.
//
// This may either be a success or error response.
//
// This class takes care of separating out the distinct payload slices sent
// over.
class CallResponse {
 public:
  CallResponse();

  CallResponse(CallResponse&& rhs) = default;
  CallResponse& operator=(CallResponse&& rhs) = default;

  // Parse the response received from a call. This must be called before any
  // other methods on this object. Takes ownership of data content.
  Status ParseFrom(CallData* data);

  // Return true if the call succeeded.
  bool is_success() const {
    DCHECK(parsed_);
    return !header_.is_error();
  }

  // Return the call ID that this response is related to.
  int32_t call_id() const {
    DCHECK(parsed_);
    return header_.call_id();
  }

  // Return the serialized response data. This is just the response "body" --
  // either a serialized ErrorStatusPB, or the serialized user response protobuf.
  const Slice &serialized_response() const {
    DCHECK(parsed_);
    return serialized_response_;
  }

  Result<SidecarHolder> GetSidecarHolder(size_t idx) const;

  // Extract sidecar with specified index to out.
  Result<RefCntSlice> ExtractSidecar(size_t idx) const;

  // Transfer all sidecars to specified context, returning the first transferred sidecar index in
  // the context.
  size_t TransferSidecars(Sidecars* dest);

  size_t DynamicMemoryUsage() const {
    return DynamicMemoryUsageOf(header_, response_data_, sidecars_);
  }

 private:
  // True once ParseFrom() is called.
  bool parsed_;

  // The parsed header.
  ResponseHeader header_;

  // The slice of data for the encoded protobuf response.
  // This slice refers to memory allocated by transfer_
  Slice serialized_response_;

  // The incoming transfer data - retained because serialized_response_
  // and sidecar_slices_ refer into its data.
  CallData response_data_;

  ReceivedSidecars sidecars_;

  DISALLOW_COPY_AND_ASSIGN(CallResponse);
};

class InvokeCallbackTask : public rpc::ThreadPoolTask {
 public:
  InvokeCallbackTask() {}

  void SetOutboundCall(OutboundCallPtr call) { call_ = std::move(call); }

  void Run() override;

  void Done(const Status& status) override;

  virtual ~InvokeCallbackTask() {}

 private:
  OutboundCallPtr call_;
};

// Tracks the status of a call on the client side.
//
// This is an internal-facing class -- clients interact with the
// RpcController class.
//
// This is allocated by the Proxy when a call is first created,
// then passed to the reactor thread to send on the wire. It's typically
// kept using a shared_ptr because a call may terminate in any number
// of different threads, making it tricky to enforce single ownership.
class OutboundCall : public RpcCall {
 public:
  OutboundCall(const RemoteMethod& remote_method,
               const std::shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
               std::shared_ptr<const OutboundMethodMetrics> method_metrics,
               AnyMessagePtr response_storage,
               RpcController* controller,
               std::shared_ptr<RpcMetrics> rpc_metrics,
               ResponseCallback callback,
               ThreadPool* callback_thread_pool);

  virtual ~OutboundCall();

  // Serialize the given request PB into this call's internal storage.
  //
  // Because the data is fully serialized by this call, 'req' may be
  // subsequently mutated with no ill effects.
  virtual Status SetRequestParam(
      AnyMessageConstPtr req, std::unique_ptr<Sidecars> sidecars, const MemTrackerPtr& mem_tracker);

  // Serialize the call for the wire. Requires that SetRequestParam()
  // is called first. This is called from the Reactor thread.
  void Serialize(ByteBlocks* output) ON_REACTOR_THREAD override;

  // Callback after the call has been put on the outbound connection queue.
  void SetQueued();

  // Update the call state to show that the request has been sent.
  // Could be called on already finished call in case it was already timed out.
  void SetSent();

  // Outbound call could be moved to final state only once,
  // so only one of SetFinished/SetTimedOut/SetFailed/SetResponse can be called.

  // Update the call state to show that the call has finished.
  void SetFinished();

  // Mark the call as failed. This also triggers the callback to notify
  // the caller. If the call failed due to a remote error, then err_pb
  // should be set to the error returned by the remote server.
  void SetFailed(const Status& status, std::unique_ptr<ErrorStatusPB> err_pb = nullptr)
      EXCLUDES(mtx_);

  // Mark the call as timed out. This also triggers the callback to notify
  // the caller.
  void SetTimedOut() ON_REACTOR_THREAD EXCLUDES(mtx_);

  // Fill in the call response.
  void SetResponse(CallResponse&& resp) ON_REACTOR_THREAD;

  bool IsTimedOut() const;

  // Is the call finished?
  bool IsFinished() const override final;

  std::string ToString() const override;

  bool DumpPB(const DumpRunningRpcsRequestPB& req, RpcCallInProgressPB* resp) EXCLUDES(mtx_)
      override;

  std::string LogPrefix() const override;

  // This is only called before the call is queued, so no synchronization is needed.
  void SetConnectionId(const ConnectionId& value, const std::string* hostname) {
    conn_id_ = value;
    hostname_ = hostname;
  }

  void SetThreadPoolFailure(const Status& status) EXCLUDES(mtx_) {
    std::lock_guard lock(mtx_);
    thread_pool_failure_ = status;
  }

  const Status& thread_pool_failure() const EXCLUDES(mtx_) {
    std::lock_guard lock(mtx_);
    return thread_pool_failure_;
  }

  void InvokeCallbackSync();

  // ----------------------------------------------------------------------------------------------
  // Getters
  // ----------------------------------------------------------------------------------------------

  const ConnectionId& conn_id() const { return conn_id_; }
  const std::string& hostname() const { return *hostname_; }
  const RemoteMethod& remote_method() const { return remote_method_; }
  RpcController* controller() { return controller_; }
  const RpcController* controller() const { return controller_; }
  AnyMessagePtr response() const { return response_; }

  int32_t call_id() const {
    return call_id_;
  }

  Trace* trace() {
    return trace_.get();
  }

  RpcMetrics& rpc_metrics() {
    return *rpc_metrics_;
  }

  size_t ObjectSize() const override { return sizeof(*this); }

  size_t DynamicMemoryUsage() const override {
    return DynamicMemoryUsageAllowSizeOf(error_pb_) +
           DynamicMemoryUsageOf(buffer_, call_response_, trace_);
  }

 protected:
  friend class RpcController;

  // See appropriate comments in CallResponse.
  virtual Result<RefCntSlice> ExtractSidecar(size_t idx) const;
  virtual size_t TransferSidecars(Sidecars* dest);

  // ----------------------------------------------------------------------------------------------
  // Protected fields set in constructor
  // ----------------------------------------------------------------------------------------------

  const std::string* hostname_;
  CoarseTimePoint start_;
  RpcController* controller_;

  // Pointer for the protobuf where the response should be written.
  // Can be used only while callback_ object is alive.
  AnyMessagePtr response_;

  // The trace buffer.
  scoped_refptr<Trace> trace_;

  // conn_id_ is not set in the constructor, but is set in SetConnectionId together with hostname_
  // before the call is queued, so no synchronization is needed.
  ConnectionId conn_id_;

 private:
  friend class RpcController;

  typedef RpcCallState State;

  static std::string StateName(State state);

  void NotifyTransferred(const Status& status, Connection* conn) override;

  bool SetState(State new_state);
  State state() const;

  // return current status
  Status status() const EXCLUDES(mtx_);

  // Return the error protobuf, if a remote error occurred.
  // This will only be non-NULL if status().IsRemoteError().
  const ErrorStatusPB* error_pb() const EXCLUDES(mtx_);

  Status InitHeader(RequestHeader* header);

  // Invokes the user-provided callback. Uses callback_thread_pool_ if set. This is only invoked
  // after a successful transition of the call state to one of the final states, so it should be
  // called exactly once.
  void InvokeCallback();

  Result<uint32_t> TimeoutMs() const;

  // ----------------------------------------------------------------------------------------------
  // Private fields set in constructor
  // ----------------------------------------------------------------------------------------------

  const int32_t call_id_;

  // The remote method being called.
  const RemoteMethod& remote_method_;

  ResponseCallback callback_;
  ThreadPool* const callback_thread_pool_;
  const std::shared_ptr<OutboundCallMetrics> outbound_call_metrics_;
  const std::shared_ptr<RpcMetrics> rpc_metrics_;
  const std::shared_ptr<const OutboundMethodMetrics> method_metrics_;

  // ----------------------------------------------------------------------------------------------
  // Fields that may be mutated by the reactor thread while the client thread reads them.
  // ----------------------------------------------------------------------------------------------

  mutable simple_spinlock mtx_;
  Status status_ GUARDED_BY(mtx_);
  std::unique_ptr<ErrorStatusPB> error_pb_ GUARDED_BY(mtx_);
  Status thread_pool_failure_ GUARDED_BY(mtx_);

  // ----------------------------------------------------------------------------------------------

  InvokeCallbackTask callback_task_;

  // Buffers for storing segments of the wire-format request.
  // This buffer is written to by the client thread before the call is queued, and read by the
  // reactor thread, so no synchronization is needed.
  RefCntBuffer buffer_;
  std::unique_ptr<Sidecars> sidecars_;

  // Consumption of buffer_. Same synchronization rules as buffer_.
  ScopedTrackedConsumption buffer_consumption_;

  // Once a response has been received for this call, contains that response.
  // This is written to by the reactor thread, and read by the client thread after the call is
  // complete, so no synchronization is needed, and the functions extracting data from this object
  // are annotated with NO_THREAD_SAFETY_ANALYSIS.
  CallResponse call_response_ GUARDED_BY_REACTOR_THREAD;

  // ----------------------------------------------------------------------------------------------
  // Atomic fields
  // ----------------------------------------------------------------------------------------------

  std::atomic<State> state_{State::READY};

  DISALLOW_COPY_AND_ASSIGN(OutboundCall);
};

class RpcErrorTag : public IntegralErrorTag<ErrorStatusPB::RpcErrorCodePB> {
 public:
  static constexpr uint8_t kCategory = 15;

  static std::string ToMessage(Value value) {
    return ErrorStatusPB::RpcErrorCodePB_Name(value);
  }
};

typedef StatusErrorCodeImpl<RpcErrorTag> RpcError;

}  // namespace rpc
}  // namespace yb
