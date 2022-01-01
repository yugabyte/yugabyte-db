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

#include "yb/client/universe_key_client.h"

#include "yb/encryption/encryption.pb.h"

#include "yb/master/master_encryption.proxy.h"

#include "yb/rpc/rpc_controller.h"

using namespace std::chrono_literals;

namespace yb {
namespace client {

void UniverseKeyClient::GetUniverseKeyRegistryAsync() {
  for (const auto& host_port : hps_) {
    SendAsyncRequest(host_port);
  }
}

void UniverseKeyClient::GetUniverseKeyRegistrySync() {
  for (const auto& host_port : hps_) {
    SendAsyncRequest(host_port);
  }
  std::unique_lock<decltype(mutex_)> l(mutex_);
  cond_.wait(l, [&] { return callback_triggered_; } );
}

void UniverseKeyClient::SendAsyncRequest(HostPort host_port) {
  master::GetUniverseKeyRegistryRequestPB req;
  auto resp = std::make_shared<master::GetUniverseKeyRegistryResponsePB>();
  auto rpc = std::make_shared<rpc::RpcController>();
  rpc->set_timeout(10s);

  master::MasterEncryptionProxy peer_proxy(proxy_cache_, host_port);
  peer_proxy.GetUniverseKeyRegistryAsync(
      req, resp.get(), rpc.get(),
      std::bind(&UniverseKeyClient::ProcessGetUniverseKeyRegistryResponse, this, resp, rpc,
                host_port));
}

void UniverseKeyClient::ProcessGetUniverseKeyRegistryResponse(
      std::shared_ptr<master::GetUniverseKeyRegistryResponsePB> resp,
      std::shared_ptr<rpc::RpcController> rpc,
      HostPort host_port) {
  if (!rpc->status().ok() || resp->has_error()) {
    LOG(WARNING) << Format("Rpc status: $0, resp: $1", rpc->status(), resp->ShortDebugString());
    // Always retry the request on failure.
    SendAsyncRequest(host_port);
    return;
  }
  std::unique_lock<decltype(mutex_)> l(mutex_);
  LOG(INFO) << "Received universe keys from master: " << host_port.ToString();
  callback_(resp->universe_keys());
  callback_triggered_ = true;
  cond_.notify_all();
}

} // namespace client
} // namespace yb
