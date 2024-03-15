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

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/secure_stream.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_fwd.h"

namespace yb {

class ExternalYbController;

namespace client {
class YBClientBuilder;
class YBClient;
class StatefulServiceClientBase;
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

  Result<std::unique_ptr<client::YBClient>> CreateSecureClient(
      const std::string& name, const std::string& host,
      std::unique_ptr<rpc::SecureContext>* secure_context);

  Result<HostPort> GetLeaderMasterBoundRpcAddr();

  bool running() const { return running_.load(std::memory_order_acquire); }

  template <class T>
  Result<std::unique_ptr<T>> CreateStatefulServiceClient() {
    auto client = std::make_unique<T>();
    RETURN_NOT_OK(InitStatefulServiceClient(client.get()));
    return client;
  }

  virtual std::vector<scoped_refptr<ExternalYbController>> yb_controller_daemons() const = 0;

 protected:
  virtual ~MiniClusterBase() = default;

  std::atomic<bool> running_ { false };
  std::unique_ptr<rpc::SecureContext> secure_context_;

  template<class Options>
  int32_t NumTabletsPerTransactionTable(Options options) {
    if (options.transaction_table_num_tablets > 0) {
      return options.transaction_table_num_tablets;
    }
    return std::max(2, static_cast<int32_t>(options.num_tablet_servers));
  }
 private:
  virtual void ConfigureClientBuilder(client::YBClientBuilder* builder) = 0;

  virtual Result<HostPort> DoGetLeaderMasterBoundRpcAddr() = 0;
  Status InitStatefulServiceClient(client::StatefulServiceClientBase* client);
};

}  // namespace yb
