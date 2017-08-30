//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/transaction_rpc.h"

#include "yb/client/client.h"
#include "yb/client/tablet_rpc.h"

#include "yb/rpc/rpc.h"

#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

using namespace std::literals;

namespace yb {
namespace client {

namespace {

class TransactionRpcBase : public rpc::Rpc, public internal::TabletRpc {
 public:
  TransactionRpcBase(const MonoTime& deadline,
                     internal::RemoteTablet* tablet,
                     YBClient* client)
      : rpc::Rpc(deadline, client->messenger()),
        trace_(new Trace),
        invoker_(false /* consistent_prefix */,
                 client,
                 this,
                 this,
                 tablet,
                 mutable_retrier(),
                 trace_.get()) {
  }

  virtual ~TransactionRpcBase() {}

  void SendRpc() override {
    invoker_.Execute();
  }

  void Start() {
    if (invoker_.tablet()) {
      SendRpc();
    } else {
      invoker_.LookupTablet(tablet_id());
    }
  }

  void SendRpcCb(const Status& status) override {
    Status new_status = status;
    if (invoker_.Done(&new_status)) {
      InvokeCallback(new_status);
      delete this;
    }
  }

  // TODO(dtxn)
  void Failed(const Status& status) override {}

 private:
  void SendRpcToTserver() override {
    InvokeAsync(invoker_.proxy().get(),
                mutable_retrier()->mutable_controller(),
                std::bind(&TransactionRpcBase::SendRpcCb, this, Status::OK()));
  }

  virtual void InvokeCallback(const Status& status) = 0;
  virtual const TabletId& tablet_id() const = 0;
  virtual void InvokeAsync(tserver::TabletServerServiceProxy* proxy,
                           rpc::RpcController* controller,
                           rpc::ResponseCallback callback) = 0;

  TracePtr trace_;
  internal::TabletInvoker invoker_;
};

// UpdateTransactionRpc is used to call UpdateTransaction remote method of appropriate tablet.
template <class Traits>
class TransactionRpc : public TransactionRpcBase {
 public:
  TransactionRpc(const MonoTime& deadline,
                 internal::RemoteTablet* tablet,
                 YBClient* client,
                 typename Traits::Request* req,
                 typename Traits::Callback callback)
      : TransactionRpcBase(deadline, tablet, client),
        callback_(std::move(callback)) {
    req_.Swap(req);
  }

  virtual ~TransactionRpc() {}

  const tserver::TabletServerErrorPB* response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

 private:
  const std::string& tablet_id() const override {
    return req_.tablet_id();
  }

  std::string ToString() const override {
    return Format("$0: $1", Traits::kName, req_);
  }

  void InvokeCallback(const Status& status) override {
    Traits::CallCallback(callback_, status, resp_);
  }

  void InvokeAsync(tserver::TabletServerServiceProxy* proxy,
                   rpc::RpcController* controller,
                   rpc::ResponseCallback callback) override {
    Traits::InvokeAsync(proxy, req_, &resp_, controller, std::move(callback));
  }

  typename Traits::Request req_;
  typename Traits::Response resp_;
  typename Traits::Callback callback_;
};

struct UpdateTransactionTraits {
  static constexpr const char* kName = "UpdateTransaction";

  typedef tserver::UpdateTransactionRequestPB Request;
  typedef tserver::UpdateTransactionResponsePB Response;
  typedef UpdateTransactionCallback Callback;

  static void CallCallback(
      const Callback& callback, const Status& status, const Response& response) {
    callback(status);
  }

  static void InvokeAsync(tserver::TabletServerServiceProxy* proxy,
                          const Request& request,
                          Response* response,
                          rpc::RpcController* controller,
                          rpc::ResponseCallback callback) {
    proxy->UpdateTransactionAsync(request, response, controller, std::move(callback));
  }
};

constexpr const char* UpdateTransactionTraits::kName;

struct GetTransactionStatusTraits {
  static constexpr const char* kName = "GetTransactionStatus";

  typedef tserver::GetTransactionStatusRequestPB Request;
  typedef tserver::GetTransactionStatusResponsePB Response;
  typedef GetTransactionStatusCallback Callback;

  static void CallCallback(
      const Callback& callback, const Status& status, const Response& response) {
    callback(status, response);
  }

  static void InvokeAsync(tserver::TabletServerServiceProxy* proxy,
                          const Request& request,
                          Response* response,
                          rpc::RpcController* controller,
                          rpc::ResponseCallback callback) {
    proxy->GetTransactionStatusAsync(request, response, controller, std::move(callback));
  }
};

constexpr const char* GetTransactionStatusTraits::kName;

} // namespace

void UpdateTransaction(const MonoTime& deadline,
                       internal::RemoteTablet* tablet,
                       YBClient* client,
                       tserver::UpdateTransactionRequestPB* req,
                       UpdateTransactionCallback callback) {
  auto* rpc = new TransactionRpc<UpdateTransactionTraits>(
      deadline, tablet, client, req, std::move(callback));
  rpc->Start();
}

void GetTransactionStatus(const MonoTime& deadline,
                          internal::RemoteTablet* tablet,
                          YBClient* client,
                          tserver::GetTransactionStatusRequestPB* req,
                          GetTransactionStatusCallback callback) {
  auto* rpc = new TransactionRpc<GetTransactionStatusTraits>(
      deadline, tablet, client, req, std::move(callback));
  rpc->Start();
}

} // namespace client
} // namespace yb
