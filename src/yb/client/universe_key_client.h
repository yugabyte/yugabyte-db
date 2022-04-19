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

#ifndef YB_CLIENT_UNIVERSE_KEY_CLIENT_H
#define YB_CLIENT_UNIVERSE_KEY_CLIENT_H

#include <condition_variable>
#include <mutex>
#include <unordered_set>

#include "yb/encryption/encryption.fwd.h"

#include "yb/master/master_encryption.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_util.h"

namespace yb {

namespace client {

class UniverseKeyClient {
 public:
  UniverseKeyClient(const std::vector<HostPort>& hps,
                    rpc::ProxyCache* proxy_cache,
                    std::function<void(const encryption::UniverseKeysPB&)> callback)
        : hps_(hps), proxy_cache_(proxy_cache), callback_(std::move(callback)) {}

  void GetUniverseKeyRegistryAsync();

  void GetUniverseKeyRegistrySync();

 private:

  void ProcessGetUniverseKeyRegistryResponse(
    std::shared_ptr<master::GetUniverseKeyRegistryResponsePB> resp,
    std::shared_ptr<rpc::RpcController> rpc,
    HostPort hp);

  void SendAsyncRequest(HostPort host_port);

  mutable std::mutex mutex_;
  mutable std::condition_variable cond_;

  std::vector<HostPort> hps_;
  rpc::ProxyCache* proxy_cache_;
  std::function<void(const encryption::UniverseKeysPB&)> callback_;

  bool callback_triggered_ = false;
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_UNIVERSE_KEY_CLIENT_H
