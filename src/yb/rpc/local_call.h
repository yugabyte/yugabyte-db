//
// Copyright (c) YugaByte, Inc.
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
  LocalOutboundCall(const ConnectionId& conn_id, const RemoteMethod& remote_method,
                    const std::shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
                    google::protobuf::Message* response_storage,
                    RpcController* controller, ResponseCallback callback);

  CHECKED_STATUS SetRequestParam(const google::protobuf::Message& req) override;

  const std::shared_ptr<LocalYBInboundCall>& CreateLocalInboundCall();

 protected:
  void Serialize(std::deque<util::RefCntBuffer> *output) const override;

  CHECKED_STATUS GetSidecar(int idx, Slice* sidecar) const override;

 private:
  friend class LocalYBInboundCall;

  const google::protobuf::Message* req_ = nullptr;

  std::shared_ptr<LocalYBInboundCall> inbound_call_;
};

// A short-circuited YB inbound call.
class LocalYBInboundCall : public YBInboundCall {
 public:
  LocalYBInboundCall(const RemoteMethod& remote_method,
                     std::weak_ptr<LocalOutboundCall> outbound_call,
                     const MonoTime& deadline);

  bool IsLocalCall() const override { return true; }

  const UserCredentials& user_credentials() const override;
  const Endpoint& remote_address() const override;
  const Endpoint& local_address() const override;
  MonoTime GetClientDeadline() const override { return deadline_; }

  CHECKED_STATUS ParseParam(google::protobuf::Message* message) override;

  const google::protobuf::Message* request() const { return outbound_call()->req_; }
  google::protobuf::Message* response() const { return outbound_call()->response(); }

 protected:
  void Respond(const google::protobuf::MessageLite& response, bool is_success) override;

 private:
  friend class LocalOutboundCall;

  std::shared_ptr<LocalOutboundCall> outbound_call() const { return outbound_call_.lock(); }

  const std::vector<util::RefCntBuffer>& sidecars() const { return sidecars_; }

  // Weak pointer back to the outbound call owning this inbound call to avoid circular reference.
  std::weak_ptr<LocalOutboundCall> outbound_call_;

  const MonoTime deadline_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_LOCAL_CALL_H
