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
#include <algorithm>

#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master_cluster.proxy.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"

DECLARE_int32(catalog_manager_bg_task_wait_ms);

using namespace std::literals;
using strings::Substitute;

namespace yb {
namespace integration_tests {

const auto kDefaultTimeout = 30000ms;

bool placement_locations_equal(
    const std::string& lhs_cloud, const std::string& lhs_region, const std::string& lhs_zone,
    const std::string& rhs_cloud, const std::string& rhs_region, const std::string& rhs_zone) {
  return lhs_cloud == rhs_cloud && lhs_region == rhs_region && lhs_zone == rhs_zone;
}

bool placement_locations_equal(
    const CloudInfoPB& cloud_info, const std::string& cloud, const std::string& region,
    const std::string& zone) {
  return placement_locations_equal(
      cloud_info.placement_cloud(), cloud_info.placement_region(), cloud_info.placement_zone(),
      cloud, region, zone);
}

bool placement_locations_equal(const CloudInfoPB& lhs, const CloudInfoPB& rhs) {
  return placement_locations_equal(
      lhs.placement_cloud(), lhs.placement_region(), lhs.placement_zone(), rhs.placement_cloud(),
      rhs.placement_region(), rhs.placement_zone());
}

class SysCatalogRespectAffinityTest : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  size_t num_masters() override {
    return 3;
  }

  size_t num_tablet_servers() override {
    return 3;
  }

  bool enable_ysql() override {
    return false;
  }

  Result<master::ListMastersResponsePB> ListMasters() {
    master::ListMastersRequestPB req;
    master::ListMastersResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kDefaultTimeout);
    auto proxy = GetMasterLeaderProxy<master::MasterClusterProxy>();
    RETURN_NOT_OK(proxy.ListMasters(req, &resp, &rpc));
    return resp;
  }

  Result<bool> IsMasterLeaderInZone(
      const std::string& placement_cloud,
      const std::string& placement_region,
      const std::string& placement_zone) {
    auto result = ListMasters();
    RETURN_NOT_OK(result);
    return std::find_if(
               result->masters().begin(), result->masters().end(), [&](const auto& master) {
                 return master.role() == yb::PeerRole::LEADER &&
                        placement_locations_equal(
                            master.registration().cloud_info(), placement_cloud, placement_region,
                            placement_zone);
               }) != result->masters().end();
  }

  Result<bool> IsMasterLeaderInZones(const std::vector<CloudInfoPB>& cloud_infos) {
    auto result = ListMasters();
    RETURN_NOT_OK(result);
    return std::find_if(
               result->masters().begin(), result->masters().end(), [&](const auto& master) {
                 return master.role() == yb::PeerRole::LEADER &&
                        std::find_if(
                            cloud_infos.begin(), cloud_infos.end(), [&](const auto& cloud_info) {
                              return placement_locations_equal(
                                  master.registration().cloud_info(), cloud_info);
                            }) != cloud_infos.end();
               }) != result->masters().end();
  }

  Result<ServerRegistrationPB> GetMasterLeaderRegistration() {
    master::GetMasterRegistrationRequestPB req;
    master::GetMasterRegistrationResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kDefaultTimeout);
    auto proxy = GetMasterLeaderProxy<master::MasterClusterProxy>();
    RETURN_NOT_OK(proxy.GetMasterRegistration(req, &resp, &rpc));
    return resp.registration();
  }

  Result<std::string> GetMasterLeaderZone() {
    return VERIFY_RESULT(GetMasterLeaderRegistration()).cloud_info().placement_zone();
  }

  Status BlacklistLeader() {
    auto reg = VERIFY_RESULT(GetMasterLeaderRegistration());
    std::vector<HostPort> leader_hps;
    std::transform(reg.private_rpc_addresses().begin(),
                   reg.private_rpc_addresses().end(),
                   std::back_inserter(leader_hps),
                   HostPortFromPB);
    std::transform(reg.broadcast_addresses().begin(),
                   reg.broadcast_addresses().end(),
                   std::back_inserter(leader_hps),
                   HostPortFromPB);
    return yb_admin_client_->ChangeBlacklist(leader_hps, true, true);
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
  ASSERT_OK(yb_admin_client_->SetPreferredZones({Substitute("c.r.z$0", next_zone_idx)}));
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

TEST_F(SysCatalogRespectAffinityTest, TestMultiplePriorityPreferredZones) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  int leader_zone_idx = leader_zone[1] - '0';
  int first_zone_idx = (leader_zone_idx + 1) % 3;
  int second_zone_idx = (leader_zone_idx + 2) % 3;

  ASSERT_OK(yb_admin_client_->SetPreferredZones(
      {Substitute("c.r.z$0:1", first_zone_idx), Substitute("c.r.z$0:2", second_zone_idx)}));
  ASSERT_OK(WaitFor(
      [&]() { return IsMasterLeaderInZone("c", "r", Substitute("z$0", first_zone_idx)); },
      kDefaultTimeout,
      "Master leader stepdown"));

  external_mini_cluster()->master(first_zone_idx)->Shutdown();
  ASSERT_OK(WaitFor(
      [&]() { return IsMasterLeaderInZone("c", "r", Substitute("z$0", second_zone_idx)); },
      kDefaultTimeout,
      "Master leader stepdown"));
}

TEST_F(SysCatalogRespectAffinityTest, TestMultiplePreferredZonesWithBlacklist) {
  // Two preferred zones. Blacklisted sys catalog leader in a preferred zone.
  // The sys catalog leader should step down to the master in the other preferred zone.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  int leader_zone_idx = leader_zone[1] - '0';
  int first_zone_idx = (leader_zone_idx + 1) % 3;
  ASSERT_OK(yb_admin_client_->SetPreferredZones(
      {Substitute("c.r.z$0", leader_zone_idx), Substitute("c.r.z$0", first_zone_idx)}));
  ASSERT_OK(BlacklistLeader());
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", Substitute("z$0", first_zone_idx));
  }, kDefaultTimeout, "Master leader stepdown"));
}

TEST_F(SysCatalogRespectAffinityTest, TestInPreferredZoneWithBlacklist) {
  // One preferred zone. Blacklisted sys catalog in the preferred zone.
  // The sys catalog leader should step down to a master in any other zone.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  ASSERT_OK(yb_admin_client_->SetPreferredZones({ Substitute("c.r.$0", leader_zone) }));
  int leader_zone_idx = leader_zone[1] - '0';
  std::string first_zone = Substitute("z$0", (leader_zone_idx + 1) % 3);
  std::string second_zone = Substitute("z$0", (leader_zone_idx + 2) % 3);
  CloudInfoPB cloud_info;
  cloud_info.set_placement_cloud("c");
  cloud_info.set_placement_region("r");
  cloud_info.set_placement_zone(first_zone);
  std::vector<CloudInfoPB> cloud_infos;
  cloud_infos.push_back(cloud_info);
  cloud_info.set_placement_zone(second_zone);
  cloud_infos.push_back(cloud_info);

  ASSERT_OK(BlacklistLeader());
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZones(cloud_infos);
  }, kDefaultTimeout, "Master leader stepdown"));
}

TEST_F(SysCatalogRespectAffinityTest, TestNoPreferredZonesWithBlacklist) {
  // No preferred zones. Blacklisted sys catalog leader.
  // The sys catalog leader should step down to a master in any other zone.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  std::string leader_zone = ASSERT_RESULT(GetMasterLeaderZone());
  int leader_zone_idx = leader_zone[1] - '0';
  std::string first_zone = Substitute("z$0", (leader_zone_idx + 1) % 3);
  std::string second_zone = Substitute("z$0", (leader_zone_idx + 2) % 3);
  CloudInfoPB cloud_info;
  cloud_info.set_placement_cloud("c");
  cloud_info.set_placement_region("r");
  cloud_info.set_placement_zone(first_zone);
  std::vector<CloudInfoPB> cloud_infos;
  cloud_infos.push_back(cloud_info);
  cloud_info.set_placement_zone(second_zone);
  cloud_infos.push_back(cloud_info);

  ASSERT_OK(BlacklistLeader());
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZones(cloud_infos);
  }, kDefaultTimeout, "Master leader stepdown"));
}

// Test that wildcard zone preference works with blacklist.
// We start with 3 masters in c:r:z0, c:r:z1, c:r:z2, then add a 4th master in c:r2:z3.
// The wildcard c.r.* matches z0, z1, z2 but not z3.
// When we blacklist the leader, it should step down to another node in c.r.*, not to z3.
TEST_F(SysCatalogRespectAffinityTest, TestWildcardZonePreferenceWithBlacklist) {
  // Add a 4th master in a different region (r2)
  auto* master_flags = external_mini_cluster()->mutable_extra_master_flags();
  master_flags->erase(
      std::remove_if(master_flags->begin(), master_flags->end(),
                     [](const std::string& flag) {
                       return flag.find("--placement_region=") != std::string::npos;
                     }),
      master_flags->end());
  master_flags->push_back("--placement_region=r2");

  auto new_master = ASSERT_RESULT(external_mini_cluster()->StartShellMaster());
  ASSERT_OK(external_mini_cluster()->ChangeConfig(new_master, consensus::ADD_SERVER));

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2,c.r2.z3", 4, ""));

  // Set wildcard zone preference c.r.* which matches z0, z1, z2 but NOT z3.
  ASSERT_OK(yb_admin_client_->SetPreferredZones({"c.r.*"}));

  std::vector<CloudInfoPB> preferred_zones;
  CloudInfoPB cloud_info;
  cloud_info.set_placement_cloud("c");
  cloud_info.set_placement_region("r");
  cloud_info.set_placement_zone("z0");
  preferred_zones.push_back(cloud_info);
  cloud_info.set_placement_zone("z1");
  preferred_zones.push_back(cloud_info);
  cloud_info.set_placement_zone("z2");
  preferred_zones.push_back(cloud_info);

  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZones(preferred_zones);
  }, kDefaultTimeout, "Master leader moves to wildcard-matching zone"));

  ASSERT_OK(BlacklistLeader());
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZones(preferred_zones);
  }, kDefaultTimeout, "Master leader stepdown stays in wildcard preference"));
}

// Test multi-priority wildcard preferences.
// First priority: c.r.z0 (specific zone)
// Second priority: c.r.* (wildcard matching all zones)
// The leader should start out in z0. After we shutdown z0,
// the master leader should fall back to c.r.z1 or c.r.z2.
TEST_F(SysCatalogRespectAffinityTest, TestMultiPriorityWildcardPreference) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Set multi-priority preference: first z0, then wildcard c.r.*.
  ASSERT_OK(yb_admin_client_->SetPreferredZones({"c.r.z0:1", "c.r.*:2"}));

  // Leader should end up in z0 (highest priority).
  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZone("c", "r", "z0");
  }, kDefaultTimeout, "Master leader moves to highest priority zone"));

  // Now shut down z0 master. Leader should step down to another zone
  // which is still in the wildcard preference c.r.*.
  external_mini_cluster()->master(0)->Shutdown();

  std::vector<CloudInfoPB> remaining_zones;
  CloudInfoPB cloud_info;
  cloud_info.set_placement_cloud("c");
  cloud_info.set_placement_region("r");
  cloud_info.set_placement_zone("z1");
  remaining_zones.push_back(cloud_info);
  cloud_info.set_placement_zone("z2");
  remaining_zones.push_back(cloud_info);

  ASSERT_OK(WaitFor([&]() {
    return IsMasterLeaderInZones(remaining_zones);
  }, kDefaultTimeout, "Master leader falls back to wildcard preference"));
}

} // namespace integration_tests
} // namespace yb
