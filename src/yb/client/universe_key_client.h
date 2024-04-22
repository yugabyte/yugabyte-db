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

#include <condition_variable>
#include <mutex>
#include <unordered_set>

#include "yb/encryption/encryption.fwd.h"

#include "yb/master/master_encryption.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/backoff_waiter.h"
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
  // Asynchronous call from master Init to get the latest universe key from other masters' in-memory
  // state.
  void GetUniverseKeyRegistryAsync();

  // Synchronous call from tserver Init to get the full universe key registry from master
  // leader.
  static Result<encryption::UniverseKeyRegistryPB> GetFullUniverseKeyRegistry(
      const std::string& local_hosts, const std::string& master_addresses,
      const std::string& root_dir);

 private:

  void ProcessGetUniverseKeyRegistryResponse(
      std::shared_ptr<master::GetUniverseKeyRegistryResponsePB> resp,
      std::shared_ptr<rpc::RpcController> rpc,
      HostPort hp,
      CoarseBackoffWaiter backoff_waiter);

  void SendAsyncRequest(HostPort host_port, CoarseBackoffWaiter backoff_waiter);

  std::vector<HostPort> hps_;
  rpc::ProxyCache* proxy_cache_;
  std::function<void(const encryption::UniverseKeysPB&)> callback_;
};

} // namespace client
} // namespace yb
