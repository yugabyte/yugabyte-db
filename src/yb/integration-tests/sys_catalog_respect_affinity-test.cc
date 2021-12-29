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

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master_cluster.proxy.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"

DECLARE_int32(catalog_manager_bg_task_wait_ms);

using namespace std::literals;
using strings::Substitute;

namespace yb {
namespace integration_tests {

const auto kDefaultTimeout = 30000ms;

class SysCatalogRespectAffinityTest : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  int num_masters() override {
    return 3;
  }

  int num_tablet_servers() override {
    return 3;
  }

  bool enable_ysql() override {
    return false;
  }

  Result<bool> IsMasterLeaderInZone(
      const std::string& placement_cloud,
      const std::string& placement_region,
      const std::string& placement_zone) {
    master::ListMastersRequestPB req;
    master::ListMastersResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kDefaultTimeout);
    auto proxy = GetMasterLeaderProxy<master::MasterClusterProxy>();
    RETURN_NOT_OK(proxy.ListMasters(req, &resp, &rpc));

    for (const ServerEntryPB& master : resp.masters()) {
      if (master.role() == yb::PeerRole::LEADER) {
        auto cloud_info = master.registration().cloud_info();
        return (cloud_info.placement_cloud() == placement_cloud &&
                cloud_info.placement_region() == placement_region &&
                cloud_info.placement_zone() == placement_zone);
      }
    }

    return false;
  }

  Result<std::string> GetMasterLeaderZone() {
    master::GetMasterRegistrationRequestPB req;
    master::GetMasterRegistrationResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kDefaultTimeout);
    auto proxy = GetMasterLeaderProxy<master::MasterClusterProxy>();
    RETURN_NOT_OK(proxy.GetMasterRegistration(req, &resp, &rpc));
    return resp.registration().cloud_info().placement_zone();
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_master_flags.push_back("--sys_catalog_respect_affinity_task=true");
    opts->extra_master_flags.push_back("--placement_cloud=c");
    opts->extra_master_flags.push_back("--placement_region=r");
    opts->extra_master_flags.push_back("--placement_zone=z${index}");
  }
};

TEST_F(SysCatalogRespectAffinityTest, TestNoPreferredZones) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", leader_zone);
  }, kDefaultTimeout, "Master leader zone"));

  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_catalog_manager_bg_task_wait_ms));

  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", leader_zone);
  }, kDefaultTimeout, "Master leader zone"));
}

TEST_F(SysCatalogRespectAffinityTest, TestInPreferredZone) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  ASSERT_OK(yb_admin_client_->SetPreferredZones({ Substitute("c.r.$0", leader_zone) }));
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", leader_zone);
  }, kDefaultTimeout, "Master leader stepdown"));
}

TEST_F(SysCatalogRespectAffinityTest, TestMoveToPreferredZone) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  int leader_zone_idx = leader_zone[1] - '0';
  std::string next_zone = Substitute("z$0", (leader_zone_idx + 1) % 3);
  ASSERT_OK(yb_admin_client_->SetPreferredZones({ Substitute("c.r.$0", next_zone) }));
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", next_zone);
  }, kDefaultTimeout, "Master leader stepdown"));
}

// Note for these tests: there is an edge case where the master goes down between ListMasters
// and StepDown, which is not tested here. The behavior should be the same, however.

TEST_F(SysCatalogRespectAffinityTest, TestPreferredZoneMasterDown) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  int leader_zone_idx = leader_zone[1] - '0';
  int next_zone_idx = (leader_zone_idx + 1) % 3;

  external_mini_cluster()->master(next_zone_idx)->Shutdown();
  ASSERT_OK(yb_admin_client_->SetPreferredZones({ Substitute("c.r.z$0", next_zone_idx)}));
  SleepFor(MonoDelta::FromMilliseconds(2000));
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", leader_zone);
  }, kDefaultTimeout, "Master leader stepdown"));
}

TEST_F(SysCatalogRespectAffinityTest, TestMultiplePreferredZones) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  int leader_zone_idx = leader_zone[1] - '0';
  int first_zone_idx = (leader_zone_idx + 1) % 3;
  int second_zone_idx = (leader_zone_idx + 2) % 3;

  external_mini_cluster()->master(first_zone_idx)->Shutdown();
  ASSERT_OK(yb_admin_client_->SetPreferredZones(
      { Substitute("c.r.z$0", first_zone_idx), Substitute("c.r.z$0", second_zone_idx) }));
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", Substitute("z$0", second_zone_idx));
  }, kDefaultTimeout, "Master leader stepdown"));
}

} // namespace integration_tests
} // namespace yb
