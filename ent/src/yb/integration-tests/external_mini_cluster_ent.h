// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_ENT_H
#define ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_ENT_H

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/rpc/rpc_fwd.h"

namespace yb {

namespace master {
class MasterBackupServiceProxy;
}  // namespace master

namespace rpc {
class SecureContext;
}

// If the cluster is configured for a single non-distributed master, return a backup proxy
// to that master. Requires that the single master is running.
std::shared_ptr<master::MasterBackupServiceProxy> master_backup_proxy(
    ExternalMiniCluster* cluster);

// Returns an RPC backup proxy to the master at 'idx'.
// Requires that the master at 'idx' is running.
std::shared_ptr<master::MasterBackupServiceProxy> master_backup_proxy(
    ExternalMiniCluster* cluster, int idx);

void StartSecure(
  std::unique_ptr<ExternalMiniCluster>* cluster,
  std::unique_ptr<rpc::SecureContext>* secure_context,
  std::unique_ptr<rpc::Messenger>* messenger,
  const std::vector<std::string>& master_flags = std::vector<std::string>());

} // namespace yb

#endif // ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_ENT_H
