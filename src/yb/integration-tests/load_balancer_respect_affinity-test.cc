// Copyright (c) YugabyteDB, Inc.
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

#include <gtest/gtest.h>

#include "yb/client/client.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master_cluster.proxy.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"

using namespace std::literals;

namespace yb {
namespace integration_tests {

const MonoDelta kDefaultTimeout = 60000ms;

class LoadBalancerRespectAffinityTest : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  size_t num_masters() override {
    return 3;
  }

  size_t num_tablet_servers() override {
    return 3;
  }

  Result<bool> IsLoadBalanced() {
    return client_->IsLoadBalanced(narrow_cast<uint32_t>(num_tablet_servers()));
  }

  Status WaitForLoadBalancing(MonoDelta timeout = kDefaultTimeout) {
    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      return !VERIFY_RESULT(IsLoadBalanced());
    }, timeout, "IsLoadBalanced"));
    return WaitFor([&]() -> Result<bool> {
      return IsLoadBalanced();
    }, timeout, "IsLoadBalanced");
  }

  Result<bool> AreLeadersOnPreferredOnly() {
    master::AreLeadersOnPreferredOnlyRequestPB req;
    master::AreLeadersOnPreferredOnlyResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kDefaultTimeout);
    auto proxy = GetMasterLeaderProxy<master::MasterClusterProxy>();
    RETURN_NOT_OK(proxy.AreLeadersOnPreferredOnly(req, &resp, &rpc));
    return !resp.has_error();
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--placement_cloud=c");
    opts->extra_tserver_flags.push_back("--placement_region=r");
    opts->extra_tserver_flags.push_back("--placement_zone=z${index}");
  }
};

TEST_F(LoadBalancerRespectAffinityTest, TransactionUsePreferredZones) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  ASSERT_OK(yb_admin_client_->SetPreferredZones({"c.r.z1"}));

  // First test whether load is correctly balanced when transaction tablet leaders are not
  // using preferred zones.
  ASSERT_OK(WaitForLoadBalancing());

  ASSERT_OK(WaitFor([&]() {
    return AreLeadersOnPreferredOnly();
  }, kDefaultTimeout, "AreLeadersOnPreferredOnly"));

  // Now test that once setting this gflag, after leader load re-balances all leaders are
  // in the preferred zone.
  for (ExternalDaemon* daemon : external_mini_cluster()->master_daemons()) {
    ASSERT_OK(external_mini_cluster()->
      SetFlag(daemon, "transaction_tables_use_preferred_zones", "1"));
  }

  ASSERT_OK(WaitForLoadBalancing());

  ASSERT_OK(WaitFor([&]() {
    return AreLeadersOnPreferredOnly();
  }, kDefaultTimeout, "AreLeadersOnPreferredOnly"));

  // Now test that toggling the gflag back to false rebalances the transaction tablet leaders
  // to not just be on preferred zones.
  for (ExternalDaemon* daemon : external_mini_cluster()->master_daemons()) {
    ASSERT_OK(external_mini_cluster()->
      SetFlag(daemon, "transaction_tables_use_preferred_zones", "0"));
  }

  ASSERT_OK(WaitForLoadBalancing());

  ASSERT_OK(WaitFor([&]() {
    return AreLeadersOnPreferredOnly();
  }, kDefaultTimeout, "AreLeadersOnPreferredOnly"));
}

TEST_F(LoadBalancerRespectAffinityTest, RemoveReplicaRespectsPreferredZones) {
  // Set placement and preferred zones and wait for load balancing to complete.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  ASSERT_OK(yb_admin_client_->SetPreferredZones({"c.r.z0"}));
  ASSERT_OK(WaitForLoadBalancing());

  // Disable leader moves so only replica removal triggers leader stepdowns.
  for (ExternalDaemon* daemon : external_mini_cluster()->master_daemons()) {
    ASSERT_OK(external_mini_cluster()->SetFlag(
        daemon, "load_balancer_max_concurrent_moves", "0"));
  }

  // Add a fourth tserver in zone z0 (same as first tserver).
  std::vector<std::string> extra_flags = {
      "--placement_cloud=c", "--placement_region=r", "--placement_zone=z0",
  };
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_flags));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(4, kDefaultTimeout));

  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
    external_mini_cluster()->GetLeaderMaster(), external_mini_cluster()->tablet_server(0)));
  ASSERT_OK(WaitForLoadBalancing());

  // Verify that the leaders are still only in the preferred zone.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return AreLeadersOnPreferredOnly();
  }, kDefaultTimeout, "AreLeadersOnPreferredOnly"));
}

TEST_F(LoadBalancerRespectAffinityTest, RemoveReplicaBalancesLeadersByLeaderLoad) {
  // Test that when we do RemoveReplica for a replica that is the leader, when picking the new
  // leader to step down to, we break ties (between tservers of the same affinity) by leader load.

  // Add a fourth tserver in zone z0.
  std::vector<std::string> extra_flags = {
      "--placement_cloud=c", "--placement_region=r", "--placement_zone=z0",
  };
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_flags));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(4, kDefaultTimeout));

  // Start with all leaders on z1.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  ASSERT_OK(WaitForLoadBalancing());
  ASSERT_OK(AreLeadersOnPreferredOnly());

  // Disable leader moves so only replica removal triggers leader stepdowns.
  for (ExternalDaemon* daemon : external_mini_cluster()->master_daemons()) {
    ASSERT_OK(external_mini_cluster()->SetFlag(
        daemon, "load_balancer_max_concurrent_moves", "0"));
  }

  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
    external_mini_cluster()->GetLeaderMaster(), external_mini_cluster()->tablet_server(0)));
  ASSERT_OK(WaitForLoadBalancing());
  // Verify that leader counts are balanced across the tservers.
  auto leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(table_name()));
  auto max_leaders = 0;
  auto min_leaders = 100000;
  for (const auto& [ts_uuid, leaders] : leader_counts) {
    if (ts_uuid == external_mini_cluster()->tablet_server(0)->uuid()) {
      continue;
    }
    max_leaders = std::max(max_leaders, leaders);
    min_leaders = std::min(min_leaders, leaders);
  }
  ASSERT_LE(max_leaders - min_leaders, 1);
}

} // namespace integration_tests
} // namespace yb
