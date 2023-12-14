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
#include <vector>

#include <boost/optional/optional.hpp>
#include "yb/util/logging.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/call_data.h"
#include "yb/rpc/rpc_call.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/rpc_header.pb.h"
#include "yb/rpc/thread_pool.h"

#include "yb/yql/cql/ql/ql_session.h"

#include "yb/util/faststring.h"
#include "yb/util/lockfree.h"
#include "yb/util/locks.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/memory/memory.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace yb {

class EventStats;
class Histogram;
class Trace;

namespace rpc {

struct InboundCallTiming {
  MonoTime time_received;   // Time the call was first accepted.
  MonoTime time_handled;    // Time the call handler was kicked off.
  MonoTime time_completed;  // Time the call handler completed.
};

class InboundCallHandler {
 public:
  virtual void Handle(InboundCallPtr call) = 0;

  virtual void Failure(const InboundCallPtr& call, const Status& status) = 0;

  virtual boost::optional<int64_t> CallQueued(int64_t rpc_queue_limit) = 0;

  virtual void CallDequeued() = 0;

 protected:
  ~InboundCallHandler() = default;
};

// Inbound call on server
class InboundCall : public RpcCall, public MPSCQueueEntry<InboundCall> {
 public:
  class CallProcessedListener {
   public:
    virtual void CallProcessed(InboundCall* call) = 0;
    virtual ~CallProcessedListener() = default;
  };

  InboundCall(ConnectionPtr conn, RpcMetrics* rpc_metrics,
              CallProcessedListener* call_processed_listener);
  virtual ~InboundCall();

  void SetRpcMethodMetrics(std::reference_wrapper<const RpcMethodMetrics> value);

  // Is this a local call?
  virtual bool IsLocalCall() const { return false; }

  // Return the serialized request parameter protobuf.
  Slice serialized_request() const {
    return serialized_request_;
  }

  void set_method_index(size_t value) {
    method_index_ = value;
  }

  size_t method_index() const {
    return method_index_;
  }

  virtual const Endpoint& remote_address() const;
  virtual const Endpoint& local_address() const;

  ConnectionPtr connection() const;
  ConnectionContext& connection_context() const;

  inline Trace* trace() const EXCLUDES(mutex_) {
    return trace_.load(std::memory_order_relaxed);
  }

  // When this InboundCall was received (instantiated).
  // Should only be called once on a given instance.
  // Not thread-safe. Should only be called by the current "owner" thread.
  void RecordCallReceived();

  // When RPC call Handle() was called on the server side.
  // Updates the Histogram with time elapsed since the call was received,
  // and should only be called once on a given instance.
  // Not thread-safe. Should only be called by the current "owner" thread.
  virtual void RecordHandlingStarted(scoped_refptr<EventStats> incoming_queue_time);

  // When RPC call Handle() completed execution on the server side.
  // Updates the Histogram with time elapsed since the call was started,
  // and should only be called once on a given instance.
  // Not thread-safe. Should only be called by the current "owner" thread.
  void RecordHandlingCompleted();

  // Return true if the deadline set by the client has already elapsed.
  // In this case, the server may stop processing the call, since the
  // call response will be ignored anyway.
  bool ClientTimedOut() const;

  // Return an upper bound on the client timeout deadline. This does not
  // account for transmission delays between the client and the server.
  // If the client did not specify a deadline, returns MonoTime::Max().
  virtual CoarseTimePoint GetClientDeadline() const = 0;

  virtual void DoSerialize(ByteBlocks* output) = 0;

  // Returns the time spent in the service queue -- from the time the call was received, until
  // it gets handled.
  MonoDelta GetTimeInQueue() const;

  virtual ThreadPoolTask* BindTask(InboundCallHandler* handler) {
    return BindTask(handler, std::numeric_limits<int64_t>::max());
  }

  void ResetCallProcessedListener() {
    call_processed_listener_ = nullptr;
  }

  virtual Slice serialized_remote_method() const = 0;
  virtual Slice method_name() const = 0;

//  virtual const std::string& service_name() const = 0;
  virtual void RespondFailure(ErrorStatusPB::RpcErrorCodePB error_code, const Status& status) = 0;

  // Do appropriate actions when call is timed out.
  //
  // message contains human readable information on why call timed out.
  //
  // Returns true if actions were applied, false if call was already processed.
  bool RespondTimedOutIfPending(const char* message);

  bool TryStartProcessing() {
    bool expected = false;
    if (!processing_started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return false;
    }
    if (tracker_) {
      tracker_->CallDequeued();
    }
    return true;
  }

  std::string LogPrefix() const override;

  template <class T, class ...Args>
  static std::shared_ptr<T> Create(Args&&... args) {
    // Not using make_shared here to make sure we can deallocate the object while the control block
    // is still kept allocated due to weak stored by ServicePool::QueuedCheckDeadline.
    // See #17726 for this bug and #17759 to track the "proper" fix in ServicePoolImpl.
    auto* call_ptr = new T(std::forward<Args>(args)...);
    auto result = std::shared_ptr<T>(call_ptr);
    result->RecordCallReceived();
    return result;
  }

  size_t DynamicMemoryUsage() const override;

  void Serialize(ByteBlocks* output) override final;

  const CallData& request_data() const { return request_data_; }

  int64_t GetRpcQueuePosition() const { return rpc_queue_position_; }

  // For requests that have requested traces to be collected, we will ensure
  // that trace_ is not null and can be used for collecting the requested data.
  void EnsureTraceCreated() EXCLUDES(mutex_);

  // Allows us to set a call processed listener if not already set.
  // Used in the context of a local inbound call to track pending local calls.
  void SetCallProcessedListener(CallProcessedListener* call_processed_listener);

 protected:
  ThreadPoolTask* BindTask(InboundCallHandler* handler, int64_t rpc_queue_limit);

  void NotifyTransferred(const Status& status, const ConnectionPtr& conn) override;

  virtual void Clear();

  // Log a WARNING message if the RPC response was slow enough that the
  // client likely timed out. This is based on the client-provided timeout
  // value.
  // Also can be configured to log _all_ RPC traces for help debugging.
  virtual void LogTrace() const = 0;

  void QueueResponse(bool is_success);

  // The serialized bytes of the request param protobuf. Set by ParseFrom().
  // This references memory held by 'request_data_'.
  Slice serialized_request_;

  // Data source of this call.
  CallData request_data_;

  // Timing information related to this RPC call.
  InboundCallTiming timing_;

  std::atomic<bool> processing_started_{false};

  std::atomic<bool> responded_{false};

  scoped_refptr<Counter> rpc_method_response_bytes_;
  scoped_refptr<Histogram> rpc_method_handler_latency_;

  mutable simple_spinlock mutex_;
  bool cleared_ GUARDED_BY(mutex_) = false;

 private:
  // The trace buffer.
  scoped_refptr<Trace> trace_holder_ GUARDED_BY(mutex_);
  std::atomic<Trace*> trace_ = nullptr;

  // The connection on which this inbound call arrived. Can be null for LocalYBInboundCall.
  ConnectionPtr conn_ = nullptr;
  RpcMetrics* rpc_metrics_;
  CallProcessedListener* call_processed_listener_;

  class InboundCallTask : public ThreadPoolTask {
   public:
    void Bind(InboundCallHandler* handler, InboundCallPtr call) {
      handler_ = handler;
      call_ = std::move(call);
    }

    void Run() override;

    void Done(const Status& status) override;

    virtual ~InboundCallTask() = default;

   private:
    InboundCallHandler* handler_;
    InboundCallPtr call_;
  };

  InboundCallTask task_;
  InboundCallHandler* tracker_ = nullptr;

  size_t method_index_ = 0;
  int64_t rpc_queue_position_ = -1;

  DISALLOW_COPY_AND_ASSIGN(InboundCall);
};

}  // namespace rpc
}  // namespace yb
