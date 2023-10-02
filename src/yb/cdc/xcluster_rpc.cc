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

#include "yb/cdc/xcluster_rpc.h"

#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"

#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/client-internal.h"
#include "yb/client/table.h"
#include "yb/client/meta_cache.h"
#include "yb/client/tablet_rpc.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/trace.h"

DEFINE_test_flag(bool, xcluster_print_write_request, false,
    "When enabled the write request will be printed to the log.");

using namespace std::literals;

using yb::tserver::TabletServerErrorPB;
using yb::tserver::TabletServerServiceProxy;
using yb::tserver::WriteRequestPB;
using yb::tserver::WriteResponsePB;

namespace yb::rpc::xcluster {

namespace {
std::string WriteRequestPBToString(const WriteRequestPB &req) {
  if (FLAGS_TEST_xcluster_print_write_request) {
    return req.ShortDebugString();
  }

  return "WriteRequestPB";
}
}  // namespace

class GetCompatibleSchemaVersionRpc : public rpc::Rpc, public client::internal::TabletRpc {
 public:
  GetCompatibleSchemaVersionRpc(
      CoarseTimePoint deadline, client::internal::RemoteTablet *tablet, client::YBClient *client,
      tserver::GetCompatibleSchemaVersionRequestPB *req,
      GetCompatibleSchemaVersionCallback callback, bool use_local_tserver)
      : rpc::Rpc(deadline, client->messenger(), &client->proxy_cache()),
        trace_(new Trace),
        invoker_(
            use_local_tserver /* local_tserver_only */, false /* consistent_prefix */, client, this,
            this, tablet, nullptr /* table */, mutable_retrier(), trace_.get()),
        callback_(std::move(callback)) {
    req_.Swap(req);
  }

  virtual ~GetCompatibleSchemaVersionRpc() { CHECK(called_); }

  void SendRpc() override { invoker_.Execute(tablet_id()); }

  void InvokeCallback(const Status &status) {
    if (!called_) {
      called_ = true;
      callback_(status, std::move(req_), std::move(resp_));
    } else {
      LOG(WARNING) << "Multiple invocation of GetCompatibleSchemaVersionRpc: " << status << " : "
                   << resp_.DebugString();
    }
  }

  void Finished(const Status &status) override {
    Status new_status = status;
    if (invoker_.Done(&new_status)) {
      InvokeCallback(new_status);
    }
  }

  void Failed(const Status &status) override {}

  const TabletServerErrorPB *response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

 private:
  void SendRpcToTserver(int attempt_num) override {
    InvokeAsync(
        invoker_.proxy().get(), PrepareController(),
        std::bind(&GetCompatibleSchemaVersionRpc::Finished, this, Status::OK()));
  }

  const std::string &tablet_id() const { return req_.tablet_id(); }

  std::string ToString() const override {
    return Format("GetCompatibleSchemaVersionRpc: $0, retrier: $1", req_, retrier());
  }

  void InvokeAsync(
      TabletServerServiceProxy *proxy, rpc::RpcController *controller,
      rpc::ResponseCallback callback) {
    proxy->GetCompatibleSchemaVersionAsync(req_, &resp_, controller, std::move(callback));
  }

  TracePtr trace_;
  client::internal::TabletInvoker invoker_;
  tserver::GetCompatibleSchemaVersionRequestPB req_;
  tserver::GetCompatibleSchemaVersionResponsePB resp_;
  GetCompatibleSchemaVersionCallback callback_;
  bool called_ = false;
};

rpc::RpcCommandPtr CreateGetCompatibleSchemaVersionRpc(
    CoarseTimePoint deadline, client::internal::RemoteTablet *tablet, client::YBClient *client,
    tserver::GetCompatibleSchemaVersionRequestPB *req, GetCompatibleSchemaVersionCallback callback,
    bool use_local_tserver) {
  return std::make_shared<GetCompatibleSchemaVersionRpc>(
      deadline, tablet, client, req, std::move(callback), use_local_tserver);
}

class XClusterWriteRpc : public rpc::Rpc, public client::internal::TabletRpc {
 public:
  XClusterWriteRpc(
      CoarseTimePoint deadline, client::internal::RemoteTablet *tablet,
      const std::shared_ptr<client::YBTable> &table, client::YBClient *client, WriteRequestPB *req,
      XClusterWriteCallback callback, bool use_local_tserver)
      : rpc::Rpc(deadline, client->messenger(), &client->proxy_cache()),
        trace_(new Trace),
        invoker_(
            use_local_tserver /* local_tserver_only */, false /* consistent_prefix */, client, this,
            this, tablet, table, mutable_retrier(), trace_.get()),
        callback_(std::move(callback)),
        table_(table) {
    req_.Swap(req);
  }

  virtual ~XClusterWriteRpc() { CHECK(called_); }

  void SendRpc() override { invoker_.Execute(tablet_id()); }

  void Finished(const Status &status) override {
    Status new_status = status;
    if (invoker_.Done(&new_status)) {
      // Check for any errors due to consumer side tablet splitting. If so, then mark partitions
      // as stale so that when we retry the ApplyChanges call, we will refresh the partitions and
      // apply changes to the proper tablets.
      if (new_status.IsNotFound() ||
          client::internal::ErrorCode(response_error()) ==
              tserver::TabletServerErrorPB::TABLET_SPLIT ||
          client::ClientError(new_status) == client::ClientErrorCode::kTablePartitionListIsStale) {
        table_->MarkPartitionsAsStale();
      }
      InvokeCallback(new_status);
    }
  }

  void Failed(const Status &status) override {}

  void Abort() override { rpc::Rpc::Abort(); }

  const TabletServerErrorPB *response_error() const override {
    return resp_.has_error() ? &resp_.error() : nullptr;
  }

 private:
  void SendRpcToTserver(int attempt_num) override {
    InvokeAsync(
        invoker_.proxy().get(), PrepareController(),
        std::bind(&XClusterWriteRpc::Finished, this, Status::OK()));
  }

  const std::string &tablet_id() const { return req_.tablet_id(); }

  std::string ToString() const override {
    return Format("XClusterWriteRpc: $0, retrier: $1", WriteRequestPBToString(req_), retrier());
  }

  void InvokeCallback(const Status &status) {
    if (!called_) {
      called_ = true;
      callback_(status, std::move(resp_));
    } else {
      LOG(WARNING) << "Multiple invocation of XClusterWriteRpc: " << status.ToString() << " : "
                   << WriteRequestPBToString(req_);
    }
  }

  void InvokeAsync(
      TabletServerServiceProxy *proxy, rpc::RpcController *controller,
      rpc::ResponseCallback callback) {
    proxy->WriteAsync(req_, &resp_, controller, std::move(callback));
  }

  TracePtr trace_;
  client::internal::TabletInvoker invoker_;
  WriteRequestPB req_;
  WriteResponsePB resp_;
  XClusterWriteCallback callback_;
  bool called_ = false;
  const std::shared_ptr<client::YBTable> table_;
};

rpc::RpcCommandPtr CreateXClusterWriteRpc(
    CoarseTimePoint deadline, client::internal::RemoteTablet *tablet,
    const std::shared_ptr<client::YBTable> &table, client::YBClient *client, WriteRequestPB *req,
    XClusterWriteCallback callback, bool use_local_tserver) {
  return std::make_shared<XClusterWriteRpc>(
      deadline, tablet, table, client, req, std::move(callback), use_local_tserver);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

class GetChangesRpc : public rpc::Rpc, public client::internal::TabletRpc {
 public:
  GetChangesRpc(
      CoarseTimePoint deadline, client::internal::RemoteTablet *tablet, client::YBClient *client,
      cdc::GetChangesRequestPB *req, GetChangesRpcCallback callback)
      : rpc::Rpc(deadline, client->messenger(), &client->proxy_cache()),
        trace_(new Trace),
        invoker_(
            false /* local_tserver_only */, false /* consistent_prefix */, client, this, this,
            tablet, nullptr /* table */, mutable_retrier(), trace_.get(),
            master::IncludeInactive::kTrue),
        callback_(std::move(callback)) {
    req_.Swap(req);
  }

  virtual ~GetChangesRpc() { CHECK(called_); }

  void SendRpc() override { invoker_.Execute(tablet_id()); }

  void Finished(const Status &status) override {
    auto retained = shared_from_this();  // Ensure we don't destruct until after the callback.
    Status new_status = status;
    if (invoker_.Done(&new_status)) {
      InvokeCallback(new_status);
    }
  }

  void Failed(const Status &status) override {}

  void Abort() override { rpc::Rpc::Abort(); }

  const tserver::TabletServerErrorPB *response_error() const override {
    // Clear the contents of last_error_, since this function is invoked again on retry.
    last_error_.Clear();

    if (resp_.has_error()) {
      if (resp_.error().has_code()) {
        // Map CDC Errors to TabletServer Errors.
        switch (resp_.error().code()) {
          case cdc::CDCErrorPB::TABLET_NOT_FOUND:
            last_error_.set_code(tserver::TabletServerErrorPB::TABLET_NOT_FOUND);
            if (resp_.error().has_status()) {
              last_error_.mutable_status()->CopyFrom(resp_.error().status());
            }
            return &last_error_;
          case cdc::CDCErrorPB::LEADER_NOT_READY:
            last_error_.set_code(tserver::TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE);
            if (resp_.error().has_status()) {
              last_error_.mutable_status()->CopyFrom(resp_.error().status());
            }
            return &last_error_;
          // TS.STALE_FOLLOWER => pattern not used.
          default:
            break;
        }
      }
    }
    return nullptr;
  }

  void SendRpcToTserver(int attempt_num) override {
    // should be fast because the proxy cache has EndPoint from the tablet lookup.
    cdc_proxy_ = std::make_shared<cdc::CDCServiceProxy>(
        &invoker_.client().proxy_cache(), invoker_.ProxyEndpoint());

    auto self = std::static_pointer_cast<GetChangesRpc>(shared_from_this());
    InvokeAsync(
        cdc_proxy_.get(), PrepareController(),
        std::bind(&GetChangesRpc::Finished, self, Status::OK()));
  }

 private:
  const std::string &tablet_id() const { return req_.tablet_id(); }

  std::string ToString() const override {
    return Format("XClusterGetChangesRpc: $0, retrier: $1", req_, retrier());
  }

  void InvokeCallback(const Status &status) {
    if (!called_) {
      called_ = true;
      // Can std::move since this is only called once the rpc is in a Done state, at which point
      // resp_ will no longer be modified or accessed.
      callback_(status, std::move(resp_));
    } else {
      LOG(WARNING) << "Multiple invocation of XClusterGetChangesRpc: " << status.ToString() << " : "
                   << req_.DebugString();
    }
  }

  void InvokeAsync(
      cdc::CDCServiceProxy *cdc_proxy, rpc::RpcController *controller,
      rpc::ResponseCallback callback) {
    cdc_proxy->GetChangesAsync(req_, &resp_, controller, std::move(callback));
  }

  TracePtr trace_;
  client::internal::TabletInvoker invoker_;

  cdc::GetChangesRequestPB req_;
  cdc::GetChangesResponsePB resp_;
  GetChangesRpcCallback callback_;

  std::shared_ptr<cdc::CDCServiceProxy> cdc_proxy_;
  mutable tserver::TabletServerErrorPB last_error_;
  bool called_ = false;
};

MUST_USE_RESULT rpc::RpcCommandPtr CreateGetChangesRpc(
    CoarseTimePoint deadline, client::internal::RemoteTablet *tablet, client::YBClient *client,
    cdc::GetChangesRequestPB *req, GetChangesRpcCallback callback) {
  return std::make_shared<GetChangesRpc>(deadline, tablet, client, req, std::move(callback));
}

}  // namespace yb::rpc::xcluster
