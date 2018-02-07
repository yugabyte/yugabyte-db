// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_H
#define ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_H

#include "../../../../src/yb/integration-tests/external_mini_cluster.h"

namespace yb {

namespace master {
class MasterBackupServiceProxy;
}  // namespace master

namespace enterprise {

class ExternalMiniCluster : public yb::ExternalMiniCluster {
  typedef yb::ExternalMiniCluster super;
 public:
  explicit ExternalMiniCluster(const ExternalMiniClusterOptions& opts) : super(opts) {}

  // If the cluster is configured for a single non-distributed master, return a backup proxy
  // to that master. Requires that the single master is running.
  std::shared_ptr<master::MasterBackupServiceProxy> master_backup_proxy();

  // Returns an RPC backup proxy to the master at 'idx'.
  // Requires that the master at 'idx' is running.
  std::shared_ptr<master::MasterBackupServiceProxy> master_backup_proxy(int idx);

 private:
  FRIEND_TEST(MasterFailoverTest, TestKillAnyMaster);

  DISALLOW_COPY_AND_ASSIGN(ExternalMiniCluster);
};

} // namespace enterprise
} // namespace yb
#endif // ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_H
