//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/transaction_rpc.h"

#include "yb/client/client.h"
#include "yb/client/tablet_rpc.h"

#include "yb/rpc/rpc.h"

#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

namespace yb {
namespace client {

namespace {

// UpdateTransactionRpc is used to call UpdateTransaction remote method of appropriate tablet.
class UpdateTransactionRpc : public rpc::Rpc, public internal::TabletRpc {
 public:
  UpdateTransactionRpc(const MonoTime& deadline,
                       internal::RemoteTablet* tablet,
                       YBClient* client,
                       tserver::UpdateTransactionRequestPB* req,
                       UpdateTransactionCallback callback)
      : rpc::Rpc(deadline, client->messenger()),
        trace_(new Trace),
        invoker_(false, client, this, this, tablet, mutable_retrier(), trace_.get()),
        callback_(std::move(callback)) {
    req_.Swap(req);
  }

  virtual ~UpdateTransactionRpc() {}

  void SendRpc() override {
    invoker_.Execute();
  }

  void Start() {
    if (invoker_.tablet()) {
      SendRpc();
    } else {
      invoker_.LookupTablet(req_.tablet_id());
    }
  }

  const tserver::TabletServerErrorPB* response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

 private:
  std::string ToString() const override {
    return Format("UpdateTransaction: $0", req_);
  }

  void SendRpcCb(const Status& status) override {
    Status new_status = status;
    if (invoker_.Done(&new_status)) {
      callback_(new_status);
      delete this;
    }
  }

  // TODO(dtxn)
  void Failed(const Status& status) override {}

  void SendRpcToTserver() override {
    LOG(INFO) << "UpdateTransactionAsync: " << req_.ShortDebugString();
    invoker_.proxy()->UpdateTransactionAsync(req_, &resp_, mutable_retrier()->mutable_controller(),
        std::bind(&UpdateTransactionRpc::SendRpcCb, this, Status::OK()));
  }

  TracePtr trace_;
  internal::TabletInvoker invoker_;
  tserver::UpdateTransactionRequestPB req_;
  tserver::UpdateTransactionResponsePB resp_;
  UpdateTransactionCallback callback_;
};

} // namespace

void UpdateTransaction(const MonoTime& deadline,
                       internal::RemoteTablet* tablet,
                       YBClient* client,
                       tserver::UpdateTransactionRequestPB* req,
                       UpdateTransactionCallback callback) {
  auto* rpc = new UpdateTransactionRpc(deadline, tablet, client, req, std::move(callback));
  rpc->Start();
}

} // namespace client
} // namespace yb
