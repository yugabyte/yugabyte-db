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

#ifndef YB_INTEGRATION_TESTS_MINI_CLUSTER_BASE_H_
#define YB_INTEGRATION_TESTS_MINI_CLUSTER_BASE_H_

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_fwd.h"

namespace yb {

namespace client {
class YBClientBuilder;
class YBClient;
} // namespace client

// Base class for ExternalMiniCluster and MiniCluster with common interface required by
// ClusterVerifier and TestWorkload. Introduced in order to be able to use ClusterVerifier
// for both types of mini cluster.
class MiniClusterBase {
 public:

  // Create a client configured to talk to this cluster.  Builder may contain override options for
  // the client. The master address will be overridden to talk to the running master.
  // If 'builder' is not specified, default options will be used.
  // Client is wrapped into a holder which will shutdown client on holder destruction.
  //
  // REQUIRES: the cluster must have already been Start()ed.

  // Created client won't shutdown messenger.
  Result<std::unique_ptr<client::YBClient>> CreateClient(rpc::Messenger* messenger);

  // Created client will shutdown messenger on client shutdown.
  Result<std::unique_ptr<client::YBClient>> CreateClient(
      client::YBClientBuilder* builder = nullptr);

  // Created client gets messenger ownership and will shutdown messenger on client shutdown.
  Result<std::unique_ptr<client::YBClient>> CreateClient(
      std::unique_ptr<rpc::Messenger>&& messenger);

  Result<HostPort> GetLeaderMasterBoundRpcAddr();

  bool running() const { return running_.load(std::memory_order_acquire); }

 protected:
  virtual ~MiniClusterBase() = default;

  std::atomic<bool> running_ { false };

 private:
  virtual void ConfigureClientBuilder(client::YBClientBuilder* builder) = 0;

  virtual Result<HostPort> DoGetLeaderMasterBoundRpcAddr() = 0;
};

}  // namespace yb

#endif // YB_INTEGRATION_TESTS_MINI_CLUSTER_BASE_H_
