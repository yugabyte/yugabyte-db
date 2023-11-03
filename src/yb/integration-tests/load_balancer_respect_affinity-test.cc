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

const MonoDelta kDefaultTimeout = 30000ms;

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

  Status WaitLoadBalanced(MonoDelta timeout) {
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

TEST_F(LoadBalancerRespectAffinityTest,
       TransactionUsePreferredZones) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  ASSERT_OK(yb_admin_client_->SetPreferredZones({"c.r.z1"}));

  // First test whether load is correctly balanced when transaction tablet leaders are not
  // using preferred zones.
  ASSERT_OK(WaitLoadBalanced(kDefaultTimeout * 2));

  ASSERT_OK(WaitFor([&]() {
    return AreLeadersOnPreferredOnly();
  }, kDefaultTimeout, "AreLeadersOnPreferredOnly"));

  // Now test that once setting this gflag, after leader load re-balances all leaders are
  // in the preferred zone.
  for (ExternalDaemon* daemon : external_mini_cluster()->master_daemons()) {
    ASSERT_OK(external_mini_cluster()->
      SetFlag(daemon, "transaction_tables_use_preferred_zones", "1"));
  }

  ASSERT_OK(WaitLoadBalanced(kDefaultTimeout * 2));

  ASSERT_OK(WaitFor([&]() {
    return AreLeadersOnPreferredOnly();
  }, kDefaultTimeout, "AreLeadersOnPreferredOnly"));

  // Now test that toggling the gflag back to false rebalances the transaction tablet leaders
  // to not just be on preferred zones.
  for (ExternalDaemon* daemon : external_mini_cluster()->master_daemons()) {
    ASSERT_OK(external_mini_cluster()->
      SetFlag(daemon, "transaction_tables_use_preferred_zones", "0"));
  }

  ASSERT_OK(WaitLoadBalanced(kDefaultTimeout * 2));

  ASSERT_OK(WaitFor([&]() {
    return AreLeadersOnPreferredOnly();
  }, kDefaultTimeout, "AreLeadersOnPreferredOnly"));
}

} // namespace integration_tests
} // namespace yb
