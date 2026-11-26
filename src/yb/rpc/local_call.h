//
// Copyright (c) YugabyteDB, Inc.
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

#include <set>

#include "yb/gutil/casts.h"

#include "yb/rpc/outbound_call.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/scheduler.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/util/dist_trace.h"

namespace yb {
namespace rpc {

class LocalYBInboundCall;

// A short-circuited outbound call.
class LocalOutboundCall : public OutboundCall {
 public:
  LocalOutboundCall(const RemoteMethod& remote_method,
                    const std::shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
                    AnyMessagePtr response_storage,
                    RpcController* controller,
                    std::shared_ptr<RpcMetrics> rpc_metrics,
                    ResponseCallback callback,
                    ThreadPool* callback_thread_pool);

  Status SetRequestParam(
      AnyMessageConstPtr req, std::unique_ptr<Sidecars> sidecars,
      const MemTrackerPtr& mem_tracker) override;

  // Non-null buffers_holder lets the call time out at its deadline, see
  // RpcController::set_local_call_buffers_holder.
  const std::shared_ptr<LocalYBInboundCall>& CreateLocalInboundCall(
      CallStateListenerFactory* call_state_listener_factory,
      std::shared_ptr<void> buffers_holder);

  size_t ObjectSize() const override { return sizeof(*this); }

  const AnyMessageConstPtr& request() const {
    return req_;
  }

  std::optional<opentelemetry::trace::SpanContext> otel_span_context() const {
    if (otel_span_) {
      return otel_span_->GetContext();
    }
    return std::nullopt;
  }

  bool is_local() const override { return true; }

 protected:
  void Serialize(ByteBlocks* output) override;

  Result<RefCntSlice> ExtractSidecar(size_t idx) const override;
  size_t TransferSidecars(Sidecars* context) override;

 private:
  friend class LocalYBInboundCall;

  void TimeOut();

  AnyMessageConstPtr req_;

  std::shared_ptr<LocalYBInboundCall> inbound_call_;
};

// A short-circuited YB inbound call.
class LocalYBInboundCall : public YBInboundCall, public RpcCallParams {
 public:
  LocalYBInboundCall(RpcMetrics* rpc_metrics, const RemoteMethod& remote_method,
                     std::weak_ptr<LocalOutboundCall> outbound_call,
                     AnyMessageConstPtr request, AnyMessagePtr response, ThreadPoolTag pool_tag,
                     CoarseTimePoint deadline, std::shared_ptr<void> buffers_holder,
                     CallStateListenerFactory* call_state_listener_factory);

  bool IsLocalCall() const override { return true; }

  // Request and response are owned by the caller, and stay valid until this call responds.
  const AnyMessageConstPtr& request() const { return request_; }
  const AnyMessagePtr& response() const { return response_; }

  // Whether the caller should get TimedOut at the deadline, without waiting for the handler.
  bool can_time_out() const { return can_time_out_; }

  // Fails the outbound call with TimedOut, unless this call has already responded.
  void TimeOut();

  const Endpoint& remote_address() const override;
  const Endpoint& local_address() const override;
  CoarseTimePoint GetClientDeadline() const override { return deadline_; }

  ThreadPoolTag pool_tag() const override;

  Status ParseParam(RpcCallParams* params) override;

  size_t ObjectSize() const override { return sizeof(*this); }

  std::shared_ptr<LocalOutboundCall> outbound_call() const {
    return outbound_call_.lock();
  }

  std::string ToString() const override {
    return yb::Format("Local call. call-id : $0 ", call_id());
  }

 protected:
  void Respond(AnyMessageConstPtr req, bool is_success) override;

  void UpdateWaitStateInfo() override;

 private:
  friend class LocalOutboundCall;

  Status ParseRequest(Slice param, const RefCntBuffer& buffer) override;
  AnyMessageConstPtr SerializableResponse() override;

  // Weak pointer back to the outbound call owning this inbound call to avoid circular reference.
  std::weak_ptr<LocalOutboundCall> outbound_call_;

  const AnyMessageConstPtr request_;
  const AnyMessagePtr response_;
  const ThreadPoolTag pool_tag_;
  const CoarseTimePoint deadline_;

  // Keeps request_ and response_ alive after the outbound call has timed out.
  std::shared_ptr<void> buffers_holder_;
  const bool can_time_out_;

  // Set by whichever comes first: the response or the timeout.
  std::atomic<bool> completed_{false};
};

template <class Params, class F>
auto HandleCall(InboundCallPtr call, F f) {
  auto yb_call = std::static_pointer_cast<YBInboundCall>(call);
  if (yb_call->IsLocalCall()) {
    auto local_call = std::static_pointer_cast<LocalYBInboundCall>(yb_call);
    auto* req = yb::down_cast<const typename Params::RequestType*>(
        Params::CastMessage(local_call->request()));
    auto* resp = yb::down_cast<typename Params::ResponseType*>(
        Params::CastMessage(local_call->response()));
    RpcContext rpc_context(std::move(local_call));
    // Current only for the synchronous part of the handler; the span itself lives on the call and
    // ends at respond time, possibly on another thread.
    dist_trace::ScopedAdoptSpan span_scope(yb_call->server_span());
    f(req, resp, std::move(rpc_context));
  } else {
    auto params = std::make_shared<Params>();
    auto* req = &params->request();
    auto* resp = &params->response();
    RpcContext rpc_context(yb_call, std::move(params));
    if (!rpc_context.responded()) {
      dist_trace::ScopedAdoptSpan span_scope(yb_call->server_span());
      f(req, resp, std::move(rpc_context));
    }
  }
}

// Tracks local calls being handled, and times out the ones that can time out.
class LocalYBInboundCallTracker : public InboundCall::CallProcessedListener {
 public:
  explicit LocalYBInboundCallTracker(Scheduler& scheduler) : scheduler_(scheduler) {}

  void Enqueue(const LocalYBInboundCallPtr& call) EXCLUDES(lock_);
  void CallProcessed(InboundCall* call) override EXCLUDES(lock_);
  Status DumpRunningRpcs(const DumpRunningRpcsRequestPB& req, DumpRunningRpcsResponsePB* resp);

 private:
  void ScheduleTimeoutCheck(CoarseTimePoint deadline) REQUIRES(lock_);
  void CheckTimeouts(ScheduledTaskId task_id, const Status& status) EXCLUDES(lock_);

  Scheduler& scheduler_;
  std::mutex lock_;
  std::unordered_map<int64_t, LocalYBInboundCallWeakPtr> calls_being_handled_ GUARDED_BY(lock_);

  // Deadlines and instance ids of the calls that can time out.
  std::set<std::pair<CoarseTimePoint, int64_t>> deadlines_ GUARDED_BY(lock_);
  CoarseTimePoint next_timeout_check_ GUARDED_BY(lock_) = CoarseTimePoint::max();
  ScheduledTaskId timeout_check_task_ GUARDED_BY(lock_) = kUninitializedScheduledTaskId;
};

} // namespace rpc
} // namespace yb
