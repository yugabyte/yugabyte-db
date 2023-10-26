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

#ifndef YB_RPC_LOCAL_CALL_H
#define YB_RPC_LOCAL_CALL_H

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
                    AnyMessagePtr response_storage, RpcController* controller,
                    std::shared_ptr<RpcMetrics> rpc_metrics, ResponseCallback callback,
                    ThreadPool* callback_thread_pool);

  Status SetRequestParam(AnyMessageConstPtr req, const MemTrackerPtr& mem_tracker) override;

  const std::shared_ptr<LocalYBInboundCall>& CreateLocalInboundCall();

  size_t ObjectSize() const override { return sizeof(*this); }

  const AnyMessageConstPtr& request() const {
    return req_;
  }

  bool is_local() const override { return true; }

 protected:
  void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) override;

  Result<Slice> GetSidecar(size_t idx) const override;
  Result<SidecarHolder> GetSidecarHolder(size_t idx) const override;

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

  size_t AddRpcSidecar(Slice car) override {
    auto buf = RefCntBuffer(car);
    sidecars_.push_back(std::move(buf));
    return sidecars_.size() - 1;
  }

  std::shared_ptr<LocalOutboundCall> outbound_call() const {
    return outbound_call_.lock();
  }

 protected:
  void Respond(AnyMessageConstPtr req, bool is_success) override;

 private:
  friend class LocalOutboundCall;

  Result<size_t> ParseRequest(Slice param) override;
  AnyMessageConstPtr SerializableResponse() override;

  boost::container::small_vector<RefCntBuffer, kMinBufferForSidecarSlices> sidecars_;

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

} // namespace rpc
} // namespace yb

#endif // YB_RPC_LOCAL_CALL_H
