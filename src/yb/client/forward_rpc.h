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

#ifndef YB_CLIENT_FORWARD_RPC_H_
#define YB_CLIENT_FORWARD_RPC_H_

#include "yb/client/tablet_rpc.h"

#include "yb/common/common_types.pb.h"

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/rpc_context.h"

namespace yb {
namespace client {

class YBClient;

namespace internal {

template <class Req, class Resp>
class ForwardRpc : public rpc::Rpc, public TabletRpc {
 public:
  ForwardRpc(const Req *req, Resp *res, rpc::RpcContext &&context,
             YBConsistencyLevel consistency_level,
             YBClient *client);

  virtual ~ForwardRpc();

  void SendRpc() override;

  std::string ToString() const override;

  const RemoteTablet& tablet() const { return *tablet_invoker_.tablet(); }

  const tserver::TabletServerErrorPB* response_error() const override {
    return res_->has_error() ? &res_->error() : nullptr;
  }

 protected:
  void Finished(const Status& status) override;

  void Failed(const Status& status) override;

  virtual void SendRpcToTserver(int attempt_num) override = 0;

  virtual bool read_only() const = 0;

  virtual void PopulateResponse() = 0;

 protected:
  // The request protobuf.
  const Req *req_;

  // The response protobuf.
  Resp *res_;

  // The rpc context for the current rpc.
  rpc::RpcContext context_;

  // The trace buffer.
  scoped_refptr<Trace> trace_;

  MonoTime start_;

  // The invoker that sends the request to the appropriate tablet server.
  TabletInvoker tablet_invoker_;

  rpc::RpcCommandPtr retained_self_;
};

class ForwardWriteRpc : public ForwardRpc<tserver::WriteRequestPB, tserver::WriteResponsePB> {
 public:
  explicit ForwardWriteRpc(const tserver::WriteRequestPB *req,
                           tserver::WriteResponsePB *resp,
                           rpc::RpcContext &&context,
                           YBClient *client);

  virtual ~ForwardWriteRpc();

 private:
  void SendRpcToTserver(int attempt_num) override;

  void PopulateResponse() override;

  bool read_only() const override {
    return false;
  }
};

class ForwardReadRpc : public ForwardRpc<tserver::ReadRequestPB, tserver::ReadResponsePB> {
 public:
  explicit ForwardReadRpc(const tserver::ReadRequestPB *req,
                          tserver::ReadResponsePB *res,
                          rpc::RpcContext&& context,
                          YBClient *client);

  virtual ~ForwardReadRpc();

 private:
  void SendRpcToTserver(int attempt_num) override;

  void PopulateResponse() override;

  bool read_only() const override {
    return true;
  }
};

}  // namespace internal
}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_FORWARD_RPC_H_
