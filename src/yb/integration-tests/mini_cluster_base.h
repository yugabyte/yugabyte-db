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

#include "yb/util/status.h"
#include "yb/util/net/sockaddr.h"

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
  CHECKED_STATUS CreateClient(client::YBClientBuilder* builder,
      std::shared_ptr<client::YBClient>* client) {
    return DoCreateClient(builder, client);
  }

  CHECKED_STATUS CreateClient(std::shared_ptr<client::YBClient>* client) {
    return DoCreateClient(nullptr /* builder */, client);
  }

  Endpoint GetLeaderMasterBoundRpcAddr() {
    return DoGetLeaderMasterBoundRpcAddr();
  }

 protected:
  virtual ~MiniClusterBase() = default;

 private:
  virtual CHECKED_STATUS DoCreateClient(client::YBClientBuilder* builder,
      std::shared_ptr<client::YBClient>* client) = 0;
  virtual Endpoint DoGetLeaderMasterBoundRpcAddr() = 0;
};

}  // namespace yb

#endif // YB_INTEGRATION_TESTS_MINI_CLUSTER_BASE_H_
