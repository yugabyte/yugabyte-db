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

#include "yb/tserver/cdc_rpc.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/tablet_rpc.h"

#include "yb/rpc/rpc.h"

#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

using namespace std::literals;

namespace yb {
namespace tserver {
namespace enterprise {

class CDCWriteRpc : public rpc::Rpc, public client::internal::TabletRpc {
 public:
  CDCWriteRpc(CoarseTimePoint deadline,
              client::internal::RemoteTablet *tablet,
              client::YBClient *client,
              WriteRequestPB *req,
              WriteCDCRecordCallback callback)
      : rpc::Rpc(deadline, client->messenger(), &client->proxy_cache()),
        trace_(new Trace),
        invoker_(false /* local_tserver_only */,
                 false /* consistent_prefix */,
                 client,
                 this,
                 this,
                 tablet,
                 mutable_retrier(),
                 trace_.get()),
        callback_(std::move(callback)) {
    req_.Swap(req);
  }

  ~CDCWriteRpc() = default;

  void SendRpc() override {
    invoker_.Execute(tablet_id());
  }

  void Finished(const Status &status) override {
    Status new_status = status;
    if (invoker_.Done(&new_status)) {
      InvokeCallback(new_status);
    }
  }

  void Failed(const Status &status) override {}

  void Abort() override {
    rpc::Rpc::Abort();
  }

  const tserver::TabletServerErrorPB *response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

 private:
  void SendRpcToTserver() override {
    InvokeAsync(invoker_.proxy().get(),
                PrepareController(),
                std::bind(&CDCWriteRpc::Finished, this, Status::OK()));
  }

  const std::string &tablet_id() const {
    return req_.tablet_id();
  }

  std::string ToString() const override {
    return Format("CDCWriteRpc: $0, retrier: $1", req_, retrier());
  }

  void InvokeCallback(const Status &status) {
    callback_(status, resp_);
  }

  void InvokeAsync(tserver::TabletServerServiceProxy *proxy,
                   rpc::RpcController *controller,
                   rpc::ResponseCallback callback) {
    proxy->WriteAsync(req_, &resp_, controller, std::move(callback));
  }

  TracePtr trace_;
  client::internal::TabletInvoker invoker_;
  WriteRequestPB req_;
  WriteResponsePB resp_;
  WriteCDCRecordCallback callback_;
};

rpc::RpcCommandPtr WriteCDCRecord(
    CoarseTimePoint deadline,
    client::internal::RemoteTablet* tablet,
    client::YBClient* client,
    WriteRequestPB* req,
    WriteCDCRecordCallback callback) {
  return rpc::StartRpc<CDCWriteRpc>(
      deadline, tablet, client, req, std::move(callback));
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
