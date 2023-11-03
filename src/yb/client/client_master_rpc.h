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

#pragma once

#include "yb/client/client.h"
#include "yb/client/client-internal.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_error.h"

#include "yb/rpc/rpc.h"

namespace yb {
namespace client {
namespace internal {

class ClientMasterRpcBase : public rpc::Rpc {
 public:
  ClientMasterRpcBase(YBClient::Data* client_data, CoarseTimePoint deadline);

  ClientMasterRpcBase(YBClient* client, CoarseTimePoint deadline)
      : ClientMasterRpcBase(client->data_.get(), deadline) {}

  virtual ~ClientMasterRpcBase() = default;

  void SendRpc() override;

  rpc::Rpcs::Handle* RpcHandle() {
    return &retained_self_;
  }

  virtual bool ShouldRetry(const Status& status) {
    return false;
  }

 protected:
  template <class Proxy>
  std::shared_ptr<Proxy> master_proxy() {
    return master_proxy_helper(static_cast<const Proxy*>(nullptr));
  }

  std::shared_ptr<master::MasterAdminProxy> master_admin_proxy() {
    return client_data_->master_admin_proxy();
  }

  auto master_proxy_helper(const master::MasterAdminProxy*) {
    return master_admin_proxy();
  }

  std::shared_ptr<master::MasterBackupProxy> master_backup_proxy() {
    return client_data_->master_backup_proxy();
  }

  auto master_proxy_helper(const master::MasterBackupProxy*) {
    return master_backup_proxy();
  }

  std::shared_ptr<master::MasterClusterProxy> master_cluster_proxy() {
    return client_data_->master_cluster_proxy();
  }

  auto master_proxy_helper(const master::MasterClusterProxy*) {
    return master_cluster_proxy();
  }

  std::shared_ptr<master::MasterDclProxy> master_dcl_proxy() {
    return client_data_->master_dcl_proxy();
  }

  auto master_proxy_helper(const master::MasterDclProxy*) {
    return master_dcl_proxy();
  }

  std::shared_ptr<master::MasterDdlProxy> master_ddl_proxy() {
    return client_data_->master_ddl_proxy();
  }

  auto master_proxy_helper(const master::MasterDdlProxy*) {
    return master_ddl_proxy();
  }

  std::shared_ptr<master::MasterClientProxy> master_client_proxy() {
    return client_data_->master_client_proxy();
  }

  auto master_proxy_helper(const master::MasterClientProxy*) {
    return master_client_proxy();
  }

  std::shared_ptr<master::MasterReplicationProxy> master_replication_proxy() {
    return client_data_->master_replication_proxy();
  }

  auto master_proxy_helper(const master::MasterReplicationProxy*) {
    return master_replication_proxy();
  }

  std::shared_ptr<master::MasterEncryptionProxy> master_encryption_proxy() {
    return client_data_->master_encryption_proxy();
  }

  auto master_proxy_helper(const master::MasterEncryptionProxy*) {
    return master_encryption_proxy();
  }

  std::shared_ptr<master::MasterTestProxy> master_test_proxy() {
    return client_data_->master_test_proxy();
  }

  auto master_proxy_helper(const master::MasterTestProxy*) {
    return master_test_proxy();
  }


  virtual void CallRemoteMethod() = 0;

  virtual void ProcessResponse(const Status& status) = 0;

  virtual Status ResponseStatus() = 0;

  void ResetMasterLeader(Retry retry);

  void NewLeaderMasterDeterminedCb(const Status& status);

  void Finished(const Status& status) override;

  std::string LogPrefix() const;

 private:
  YBClient::Data* const client_data_;
  rpc::Rpcs::Handle retained_self_;
};

template <class Resp>
Status StatusFromResp(const Resp& resp) {
  if (!resp.has_error()) {
    return Status::OK();
  }
  auto result = StatusFromPB(resp.error().status());
  if (resp.error().code() != master::MasterErrorPB::UNKNOWN_ERROR &&
      master::MasterError(result) != resp.error().code()) {
    result = result.CloneAndAddErrorCode(master::MasterError(resp.error().code()));
  }
  return result;
}

// Gets data from the leader master. If the leader master
// is down, waits for a new master to become the leader, and then gets
// the data from the new leader master.
template<class Req, class Resp>
class ClientMasterRpc : public ClientMasterRpcBase {
 public:
  template<class... Args>
  explicit ClientMasterRpc(Args&&... args) : ClientMasterRpcBase(std::forward<Args>(args)...) {}

 protected:
  Status ResponseStatus() override {
    return StatusFromResp(resp_);
  }

  Req req_;
  Resp resp_;
};

} // namespace internal
} // namespace client
} // namespace yb
