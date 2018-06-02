// Copyright (c) YugaByte, Inc.

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/master_backup.proxy.h"

namespace yb {
namespace enterprise {

using yb::master::MasterBackupServiceProxy;

std::shared_ptr<MasterBackupServiceProxy> ExternalMiniCluster::master_backup_proxy() {
  CHECK_EQ(masters_.size(), 1);
  return master_backup_proxy(0);
}

std::shared_ptr<MasterBackupServiceProxy> ExternalMiniCluster::master_backup_proxy(int idx) {
  CHECK_LT(idx, masters_.size());
  return std::make_shared<MasterBackupServiceProxy>(
      proxy_cache_.get(), CHECK_NOTNULL(master(idx))->bound_rpc_addr());
}

} // namespace enterprise
} // namespace yb
