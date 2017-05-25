// Copyright (c) YugaByte, Inc.

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
// ClusterVerifier. Introduced in order to be able to use ClusterVerifier for both types
// of mini cluster.
class MiniClusterBase {
 public:
  CHECKED_STATUS CreateClient(client::YBClientBuilder* builder,
      std::shared_ptr<client::YBClient>* client) {
    return DoCreateClient(builder, client);
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
