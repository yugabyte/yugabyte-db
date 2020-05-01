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

#include "yb/rpc/outbound_call.h"
#include "yb/rpc/yb_rpc.h"

namespace yb {
namespace rpc {

class LocalYBInboundCall;

// A short-circuited outbound call.
class LocalOutboundCall : public OutboundCall {
 public:
  LocalOutboundCall(const RemoteMethod* remote_method,
                    const std::shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
                    google::protobuf::Message* response_storage,
                    RpcController* controller, RpcMetrics* rpc_metrics, ResponseCallback callback);

  CHECKED_STATUS SetRequestParam(
      const google::protobuf::Message& req, const MemTrackerPtr& mem_tracker) override;

  const std::shared_ptr<LocalYBInboundCall>& CreateLocalInboundCall();

  size_t ObjectSize() const override { return sizeof(*this); }

 protected:
  void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) override;

  Result<Slice> GetSidecar(int idx) const override;

 private:
  friend class LocalYBInboundCall;

  const google::protobuf::Message* req_ = nullptr;

  std::shared_ptr<LocalYBInboundCall> inbound_call_;
};

// A short-circuited YB inbound call.
class LocalYBInboundCall : public YBInboundCall {
 public:
  LocalYBInboundCall(RpcMetrics* rpc_metrics, const RemoteMethod& remote_method,
                     std::weak_ptr<LocalOutboundCall> outbound_call,
                     CoarseTimePoint deadline);

  bool IsLocalCall() const override { return true; }

  const Endpoint& remote_address() const override;
  const Endpoint& local_address() const override;
  CoarseTimePoint GetClientDeadline() const override { return deadline_; }

  CHECKED_STATUS ParseParam(google::protobuf::Message* message) override;

  const google::protobuf::Message* request() const { return outbound_call()->req_; }
  google::protobuf::Message* response() const { return outbound_call()->response(); }

  size_t ObjectSize() const override { return sizeof(*this); }

  size_t AddRpcSidecar(Slice car) override {
    sidecars_.push_back(RefCntBuffer(car));
    return sidecars_.size() - 1;
  }

 protected:
  void Respond(const google::protobuf::MessageLite& response, bool is_success) override;

 private:
  friend class LocalOutboundCall;

  std::shared_ptr<LocalOutboundCall> outbound_call() const { return outbound_call_.lock(); }

  boost::container::small_vector<RefCntBuffer, kMinBufferForSidecarSlices> sidecars_;

  // Weak pointer back to the outbound call owning this inbound call to avoid circular reference.
  std::weak_ptr<LocalOutboundCall> outbound_call_;

  const CoarseTimePoint deadline_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_LOCAL_CALL_H
