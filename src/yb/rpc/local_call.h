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

#include "yb/gutil/casts.h"

#include "yb/rpc/outbound_call.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/yb_rpc.h"

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

  const std::shared_ptr<LocalYBInboundCall>& CreateLocalInboundCall();

  size_t ObjectSize() const override { return sizeof(*this); }

  const AnyMessageConstPtr& request() const {
    return req_;
  }

  bool is_local() const override { return true; }

 protected:
  void Serialize(ByteBlocks* output) override;

  Result<RefCntSlice> ExtractSidecar(size_t idx) const override;
  size_t TransferSidecars(Sidecars* context) override;

 private:
  friend class LocalYBInboundCall;

  AnyMessageConstPtr req_;

  std::shared_ptr<LocalYBInboundCall> inbound_call_;
};

// A short-circuited YB inbound call.
class LocalYBInboundCall : public YBInboundCall, public RpcCallParams {
 public:
  LocalYBInboundCall(RpcMetrics* rpc_metrics, const RemoteMethod& remote_method,
                     std::weak_ptr<LocalOutboundCall> outbound_call,
                     CoarseTimePoint deadline);

  bool IsLocalCall() const override { return true; }

  const Endpoint& remote_address() const override;
  const Endpoint& local_address() const override;
  CoarseTimePoint GetClientDeadline() const override { return deadline_; }

  Status ParseParam(RpcCallParams* params) override;

  size_t ObjectSize() const override { return sizeof(*this); }

  std::shared_ptr<LocalOutboundCall> outbound_call() const {
    return outbound_call_.lock();
  }

 protected:
  void Respond(AnyMessageConstPtr req, bool is_success) override;

 private:
  friend class LocalOutboundCall;

  Result<size_t> ParseRequest(Slice param, const RefCntBuffer& buffer) override;
  AnyMessageConstPtr SerializableResponse() override;

  // Weak pointer back to the outbound call owning this inbound call to avoid circular reference.
  std::weak_ptr<LocalOutboundCall> outbound_call_;

  const CoarseTimePoint deadline_;
};

template <class Params, class F>
auto HandleCall(InboundCallPtr call, F f) {
  auto yb_call = std::static_pointer_cast<YBInboundCall>(call);
  if (yb_call->IsLocalCall()) {
    auto local_call = std::static_pointer_cast<LocalYBInboundCall>(yb_call);
    auto outbound_call = local_call->outbound_call();
    auto* req = yb::down_cast<const typename Params::RequestType*>(
        Params::CastMessage(outbound_call->request()));
    auto* resp = yb::down_cast<typename Params::ResponseType*>(
        Params::CastMessage(outbound_call->response()));
    RpcContext rpc_context(std::move(local_call));
    f(req, resp, std::move(rpc_context));
  } else {
    auto params = std::make_shared<Params>();
    auto* req = &params->request();
    auto* resp = &params->response();
    RpcContext rpc_context(yb_call, std::move(params));
    if (!rpc_context.responded()) {
      f(req, resp, std::move(rpc_context));
    }
  }
}

class LocalYBInboundCallTracker : public InboundCall::CallProcessedListener {
  public:
    void CallProcessed(InboundCall* call) override EXCLUDES(lock_);
    void Enqueue(InboundCall* call, InboundCallWeakPtr call_ptr) EXCLUDES(lock_);
  private:
    simple_spinlock lock_;
    std::unordered_map<InboundCall*, InboundCallWeakPtr> calls_being_handled_ GUARDED_BY(lock_);
};

} // namespace rpc
} // namespace yb
