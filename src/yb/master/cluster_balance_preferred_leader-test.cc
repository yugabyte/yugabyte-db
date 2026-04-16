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

#include "yb/master/load_balancer_mocked-test_base.h"
#include "yb/util/status_log.h"

namespace yb {
namespace master {
using std::make_shared;
using std::string;
using std::vector;

class TestLoadBalancerPreferredLeader : public LoadBalancerMockedBase {
 protected:
  TSDescriptorVector GenerateDefaultTsDesc() {
    TSDescriptorVector ts_descs = {
        SetupTS("0000", "a", ""), SetupTS("1111", "b", ""), SetupTS("2222", "c", "")};
    return ts_descs;
  }

  TSDescriptorVector Generate5NodeTsDesc() {
    TSDescriptorVector ts_descs = {
        SetupTS("0000", "a", ""), SetupTS("1111", "b", ""), SetupTS("2222", "c", ""),
        SetupTS("3333", "d", ""), SetupTS("4444", "e", "")};
    return ts_descs;
  }

  std::shared_ptr<TSDescriptor> SetupTSWithCloudInfo(
      const string& uuid, const string& cloud, const string& region, const string& zone) {
    NodeInstancePB node;
    node.set_permanent_uuid(uuid);

    TSRegistrationPB reg;
    auto hp = reg.mutable_common()->add_private_rpc_addresses();
    hp->set_host(uuid);
    auto ci = reg.mutable_common()->mutable_cloud_info();
    ci->set_placement_cloud(cloud);
    ci->set_placement_region(region);
    ci->set_placement_zone(zone);

    auto result = TSDescriptorTestUtil::RegisterNew(
        node, reg, CloudInfoPB(), nullptr, RegisteredThroughHeartbeat::kTrue);
    CHECK(result.ok()) << result.status();
    return *result;
  }

  // Generates all combinations of cloud0/cloud1, region a/b/c, and zone x/y/z.
  TSDescriptorVector Generate18NodeTsDesc() {
    TSDescriptorVector ts_descs;
    int idx = 0;
    for (const auto& cloud : {"cloud0", "cloud1"}) {
      for (const auto& region : {"a", "b", "c"}) {
        for (const auto& zone : {"x", "y", "z"}) {
          ts_descs.push_back(SetupTSWithCloudInfo(
              std::to_string(idx++), cloud, region, zone));
        }
      }
    }
    return ts_descs;
  }

  // Prepare affinitized leaders. If zone is "*", the placement_zone field
  // is not set, which acts as a wildcard matching any zone.
  void PrepareAffinitizedLeaders(const vector<vector<string>>& zones) {
    for (const auto& zone_set : zones) {
      AffinitizedZonesSet new_zone;
      for (const string& zone : zone_set) {
        CloudInfoPB ci;
        ci.set_placement_cloud(default_cloud);
        ci.set_placement_region(default_region);
        if (zone != "*") {
          ci.set_placement_zone(zone);
        }
        new_zone.insert(ci);
      }
      affinitized_zones_.push_back(new_zone);
    }
  }

  // Prepare affinitized leaders with cloud/region/zone tuples.
  // If region is "*", placement_region is not set (matches any region/zone in cloud).
  // If zone is "*", placement_zone is not set (matches any zone in cloud/region).
  void PrepareAffinitizedLeadersWithCloudInfo(
      const vector<vector<std::tuple<string, string, string>>>& cloud_region_zones) {
    for (const auto& zone_set : cloud_region_zones) {
      AffinitizedZonesSet new_zone;
      for (const auto& [cloud, region, zone] : zone_set) {
        CloudInfoPB ci;
        ci.set_placement_cloud(cloud);
        if (region != "*") {
          ci.set_placement_region(region);
          if (zone != "*") {
            ci.set_placement_zone(zone);
          }
        }
        new_zone.insert(ci);
      }
      affinitized_zones_.push_back(new_zone);
    }
  }

  // Helper for wildcard leader preference tests.
  // is_preferred_ts: predicate returning true for TSs that should match the wildcard.
  // Verifies that leaders are distributed only among preferred TSs.
  void TestWildcardLeaderPreference(
      std::function<bool(const std::shared_ptr<TSDescriptor>&)> is_preferred_ts) {
    // Build set of preferred TS uuids.
    std::set<string> preferred_ts_uuids;
    for (const auto& ts : ts_descs_) {
      if (is_preferred_ts(ts)) {
        preferred_ts_uuids.insert(ts->permanent_uuid());
      }
    }
    ASSERT_GT(preferred_ts_uuids.size(), 0) << "At least one TS should match the preference";

    // Initially, distribute all leaders to ts0.
    for (const auto& tablet : tablets_) {
      MoveTabletLeader(tablet.get(), ts_descs_[0]);
    }

    ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

    string tablet_id, from_ts, to_ts;
    std::map<string, int> leader_count;
    for (const auto& ts : ts_descs_) {
      leader_count[ts->permanent_uuid()] = 0;
    }
    leader_count[ts_descs_[0]->permanent_uuid()] = narrow_cast<int>(tablets_.size());

    // Run the load balancer until no more moves.
    while (ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts))) {
      leader_count[from_ts]--;
      leader_count[to_ts]++;
      auto tablet_it = std::find_if(
          tablets_.begin(), tablets_.end(),
          [&tablet_id](const auto& t) { return t->id() == tablet_id; });
      ASSERT_NE(tablet_it, tablets_.end());
      auto to_ts_it = std::find_if(
          ts_descs_.begin(), ts_descs_.end(),
          [&to_ts](const auto& ts) { return ts->permanent_uuid() == to_ts; });
      ASSERT_NE(to_ts_it, ts_descs_.end());
      MoveTabletLeader(tablet_it->get(), *to_ts_it);
    }

    // Verify: all leaders should be on preferred TSs, none on non-preferred TSs.
    int total_leaders_on_preferred = 0;
    for (const auto& ts : ts_descs_) {
      const auto& uuid = ts->permanent_uuid();
      if (preferred_ts_uuids.count(uuid)) {
        total_leaders_on_preferred += leader_count[uuid];
      } else {
        ASSERT_EQ(leader_count[uuid], 0)
            << "Non-preferred TS " << uuid << " should have no leaders";
      }
    }
    ASSERT_EQ(total_leaders_on_preferred, narrow_cast<int>(tablets_.size()))
        << "All leaders should be on preferred TSs";

    // Verify: leaders should be evenly distributed (max - min <= 1).
    int max_leaders_on_single_ts = 0;
    int min_leaders_on_single_ts = std::numeric_limits<int>::max();
    for (const auto& uuid : preferred_ts_uuids) {
      max_leaders_on_single_ts = std::max(max_leaders_on_single_ts, leader_count[uuid]);
      min_leaders_on_single_ts = std::min(min_leaders_on_single_ts, leader_count[uuid]);
    }
    ASSERT_LE(max_leaders_on_single_ts - min_leaders_on_single_ts, 1)
        << "Leaders should be evenly distributed among preferred TSs";
  }

  void AddFollowerReplica(TabletInfo* tablet, std::shared_ptr<yb::master::TSDescriptor> ts_desc) {
    std::shared_ptr<TabletReplicaMap> replicas =
        std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());

    TabletReplica replica;
    NewReplica(
        ts_desc, tablet::RaftGroupStatePB::RUNNING, PeerRole::FOLLOWER,
        consensus::PeerMemberType::VOTER, &replica);
    InsertOrDie(replicas.get(), ts_desc->permanent_uuid(), replica);
    tablet->SetReplicaLocations(replicas);
  }

  void RemoveReplica(TabletInfo* tablet, std::shared_ptr<yb::master::TSDescriptor> ts_desc) {
    std::shared_ptr<TabletReplicaMap> replicas =
        std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());

    replicas->erase(ts_desc->permanent_uuid());
  }
};

TEST_F(TestLoadBalancerPreferredLeader, TestBalancingOneAffinitizedLeader) {
  TSDescriptorVector ts_descs(GenerateDefaultTsDesc());
  PrepareTestState(ts_descs);
  PrepareAffinitizedLeaders({{"a"}});

  int i = 0;
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[(i % 2) + 1]);
    i++;
  }
  LOG(INFO) << "Leader distribution: 0 2 2";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::map<string, int> from_count;
  std::unordered_set<string> tablets_moved;

  string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_2;
  ts_0 = ts_descs_[0]->permanent_uuid();
  ts_1 = ts_descs_[1]->permanent_uuid();
  ts_2 = ts_descs_[2]->permanent_uuid();

  from_count[ts_0] = 0;
  from_count[ts_1] = 0;
  from_count[ts_2] = 0;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  LOG(INFO) << "Leader distribution: 2 1 1";

  // Make sure one tablet moved from each ts1 and ts2.
  ASSERT_EQ(1, from_count[ts_1]);
  ASSERT_EQ(1, from_count[ts_2]);

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  LOG(INFO) << "Leader distribution: 4 0 0";
  // Make sure two tablets moved from each ts1 and ts2.
  ASSERT_EQ(2, from_count[ts_1]);
  ASSERT_EQ(2, from_count[ts_2]);

  // Make sure all the tablets moved are distinct.  ASSERT_EQ(4, tablets_moved.size());

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(TestLoadBalancerPreferredLeader, TestBalancingTwoAffinitizedLeaders) {
  TSDescriptorVector ts_descs(GenerateDefaultTsDesc());
  PrepareTestState(ts_descs);
  PrepareAffinitizedLeaders({{"b", "c"}});
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  LOG(INFO) << "Leader distribution: 4 0 0";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::map<string, int> to_count;
  std::unordered_set<string> tablets_moved;

  string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_2;
  ts_0 = ts_descs_[0]->permanent_uuid();
  ts_1 = ts_descs_[1]->permanent_uuid();
  ts_2 = ts_descs_[2]->permanent_uuid();

  to_count[ts_0] = 0;
  to_count[ts_1] = 0;
  to_count[ts_2] = 0;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(from_ts, ts_0);
  tablets_moved.insert(tablet_id);
  to_count[to_ts]++;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(from_ts, ts_0);
  tablets_moved.insert(tablet_id);
  to_count[to_ts]++;

  LOG(INFO) << "Leader distribution: 2 1 1";
  // Make sure one tablet moved to each ts1 and ts2.
  ASSERT_EQ(1, to_count[ts_1]);
  ASSERT_EQ(1, to_count[ts_2]);

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(from_ts, ts_0);
  tablets_moved.insert(tablet_id);
  to_count[to_ts]++;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(from_ts, ts_0);
  tablets_moved.insert(tablet_id);
  to_count[to_ts]++;

  LOG(INFO) << "Leader distribution: 0 2 2";
  // Make sure two tablets moved to each ts1 and ts2.
  ASSERT_EQ(2, to_count[ts_1]);
  ASSERT_EQ(2, to_count[ts_2]);

  // Make sure all the tablets moved are distinct.
  ASSERT_EQ(4, tablets_moved.size());

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(TestLoadBalancerPreferredLeader, TestReadOnlyLoadBalancing) {
  TSDescriptorVector ts_descs(GenerateDefaultTsDesc());
  PrepareTestState(ts_descs);

  // RF = 3 + 1, 4 TS, 3 AZ, 4 tablets.
  // Normal setup.
  // then create new node in az1 with all replicas.
  SetupClusterConfig(
      {"a", "b", "c"} /* az list */, {"a"} /* read only */, {} /* affinitized leaders */,
      &replication_info_);
  ts_descs_.push_back(SetupTS("3333", "a", read_only_placement_uuid /* placement_uuid */));
  ts_descs_.push_back(SetupTS("4444", "a", read_only_placement_uuid /* placement_uuid */));

  // Adding all read_only replicas.
  for (const auto& tablet : tablets_) {
    AddFollowerReplica(tablet.get(), ts_descs_[3]);
  }

  LOG(INFO) << "The replica count for each read_only tserver is: ts3: 4, ts4: 0";
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // First we make sure that no load balancing happens during the live iteration.
  ASSERT_NOK(TestAddLoad("", "", ""));

  cb_.SetOptions(ReplicaType::kReadOnly, read_only_placement_uuid);
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  // Now load balance a read_only replica.
  string expected_from_ts = ts_descs_[3]->permanent_uuid();
  string expected_to_ts = ts_descs_[4]->permanent_uuid();
  string actual_tablet_id;
  ASSERT_OK(TestAddLoad("", expected_from_ts, expected_to_ts, &actual_tablet_id));
  RemoveReplica(tablet_map_[actual_tablet_id].get(), ts_descs_[3]);
  AddFollowerReplica(tablet_map_[actual_tablet_id].get(), ts_descs_[4]);

  ClearTabletsAddedForTest();

  expected_from_ts = ts_descs_[3]->permanent_uuid();
  expected_to_ts = ts_descs_[4]->permanent_uuid();
  ASSERT_OK(TestAddLoad("", expected_from_ts, expected_to_ts, &actual_tablet_id));
  RemoveReplica(tablet_map_[actual_tablet_id].get(), ts_descs_[3]);
  AddFollowerReplica(tablet_map_[actual_tablet_id].get(), ts_descs_[4]);

  // Then make sure there is no more balancing, since both ts3 and ts4 have 2 replicas each.
  ASSERT_NOK(TestAddLoad("", "", ""));
  cb_.ResetOptions();
}

TEST_F(TestLoadBalancerPreferredLeader, TestLeaderBalancingWithReadOnly) {
  TSDescriptorVector ts_descs(GenerateDefaultTsDesc());
  PrepareTestState(ts_descs);

  // RF = 3 + 1, 4 TS, 3 AZ, 4 tablets.
  SetupClusterConfig(
      {"a", "b", "c"} /* az list */, {"a"} /* read only */, {} /* affinitized leaders */,
      &replication_info_);
  ts_descs_.push_back(SetupTS("3333", "a", read_only_placement_uuid));

  // Adding all read_only replicas.
  for (const auto& tablet : tablets_) {
    AddFollowerReplica(tablet.get(), ts_descs_[3]);
  }

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  string placeholder;
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  cb_.SetOptions(ReplicaType::kReadOnly, read_only_placement_uuid);
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
  cb_.ResetOptions();
}

TEST_F(TestLoadBalancerPreferredLeader, TestAlreadyBalancedAffinitizedLeaders) {
  TSDescriptorVector ts_descs(GenerateDefaultTsDesc());
  PrepareTestState(ts_descs);
  PrepareAffinitizedLeaders({{"a"}});

  // Move all leaders to ts0.
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  LOG(INFO) << "Leader distribution: 4 0 0";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  string placeholder;
  // Only the affinitized zone contains tablet leaders, should be no movement.
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(TestLoadBalancerPreferredLeader, TestAlreadyBalancedMultiAffinitizedLeaders) {
  TSDescriptorVector ts_descs(Generate5NodeTsDesc());
  PrepareTestState(ts_descs);
  PrepareAffinitizedLeaders({{"a"}, {"b"}});

  // Move all leaders to ts0.
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  LOG(INFO) << "Leader distribution: 4 0 0";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  string placeholder;
  // Only the affinitized zone contains tablet leaders, should be no movement.
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(TestLoadBalancerPreferredLeader, TestBalancingOneMultiAffinitizedLeader) {
  TSDescriptorVector ts_descs(Generate5NodeTsDesc());
  PrepareTestState(ts_descs);

  PrepareAffinitizedLeaders({{"a"}, {"b"}});

  int i = 0;
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[(i % 2) + 1]);
    i++;
  }
  LOG(INFO) << "Leader distribution: 0 2 2";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::map<string, int> from_count;
  std::unordered_set<string> tablets_moved;

  string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_2;
  ts_0 = ts_descs_[0]->permanent_uuid();
  ts_1 = ts_descs_[1]->permanent_uuid();
  ts_2 = ts_descs_[2]->permanent_uuid();

  from_count[ts_0] = 0;
  from_count[ts_1] = 0;
  from_count[ts_2] = 0;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  LOG(INFO) << "Leader distribution: 2 1 1";

  // Make sure two tablets moved from ts2.
  ASSERT_EQ(2, from_count[ts_2]);

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, ts_0);
  tablets_moved.insert(tablet_id);
  from_count[from_ts]++;

  LOG(INFO) << "Leader distribution: 4 0 0";
  // Make sure two tablets moved from each ts1 and ts2.
  ASSERT_EQ(2, from_count[ts_1]);

  // Make sure all the tablets moved are distinct.
  ASSERT_EQ(4, tablets_moved.size());

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(TestLoadBalancerPreferredLeader, TestBalancingMultiPrimaryAffinitizedLeader) {
  TSDescriptorVector ts_descs(Generate5NodeTsDesc());
  PrepareTestState(ts_descs);

  PrepareAffinitizedLeaders({{"a", "b"}, {"c"}});

  // Stop Tablet Server and move out its leaders.
  StopTsHeartbeat(ts_descs_[0]);
  MoveTabletLeader(tablets_[0].get(), ts_descs_[3]);
  MoveTabletLeader(tablets_[1].get(), ts_descs_[1]);
  MoveTabletLeader(tablets_[2].get(), ts_descs_[3]);
  MoveTabletLeader(tablets_[3].get(), ts_descs_[1]);
  LOG(INFO) << "Leader distribution: 0 2 0 2";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_3;
  ts_0 = ts_descs_[0]->permanent_uuid();
  ts_1 = ts_descs_[1]->permanent_uuid();
  ts_3 = ts_descs_[3]->permanent_uuid();

  TestMoveLeader(&placeholder, ts_3, ts_1);
  TestMoveLeader(&placeholder, ts_3, ts_1);

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
  LOG(INFO) << "Leader distribution: 0 4 0 0";

  MoveTabletLeader(tablets_[0].get(), ts_descs_[1]);
  MoveTabletLeader(tablets_[2].get(), ts_descs_[1]);

  ResumeTsHeartbeat(ts_descs_[0]);
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  TestMoveLeader(&placeholder, ts_1, ts_0);
  TestMoveLeader(&placeholder, ts_1, ts_0);
  LOG(INFO) << "Leader distribution: 2 2 0 0";

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(TestLoadBalancerPreferredLeader, TestBalancingMultiSecondaryAffinitizedLeader) {
  TSDescriptorVector ts_descs(Generate5NodeTsDesc());
  PrepareTestState(ts_descs);

  PrepareAffinitizedLeaders({{"a"}, {"b", "c"}, {"d"}});

  // Stop Tablet Server and move out its leaders.
  StopTsHeartbeat(ts_descs_[0]);
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[3]);
  }
  LOG(INFO) << "Leader distribution: 0 0 0 4";
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::map<string, int> to_count;
  string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_2, ts_3;
  ts_0 = ts_descs_[0]->permanent_uuid();
  ts_1 = ts_descs_[1]->permanent_uuid();
  ts_2 = ts_descs_[2]->permanent_uuid();
  ts_3 = ts_descs_[3]->permanent_uuid();

  for (auto& ts : ts_descs_) {
    to_count[ts->permanent_uuid()] = 0;
  }

  for (size_t i = 0; i < tablets_.size(); i++) {
    ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
    ASSERT_EQ(from_ts, ts_3);
    to_count[to_ts]++;

    auto tablet_to_move = std::find_if(
        tablets_.begin(), tablets_.end(),
        [&tablet_id](const auto& tablet) { return tablet->tablet_id() == tablet_id; });
    ASSERT_NE(tablet_to_move, tablets_.end());

    auto to_ts_server = std::find_if(
        ts_descs_.begin(), ts_descs_.end(),
        [&to_ts](const auto& ts) { return ts->permanent_uuid() == to_ts; });
    ASSERT_NE(to_ts_server, ts_descs_.end());

    MoveTabletLeader(tablet_to_move->get(), *(to_ts_server));
  }

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  ASSERT_EQ(2, to_count[ts_1]);
  ASSERT_EQ(2, to_count[ts_2]);
  LOG(INFO) << "Leader distribution: 0 2 2 0";

  ResumeTsHeartbeat(ts_descs_[0]);
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  TestMoveLeader(&placeholder, "", ts_0);
  TestMoveLeader(&placeholder, "", ts_0);
  TestMoveLeader(&placeholder, "", ts_0);
  TestMoveLeader(&placeholder, "", ts_0);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  LOG(INFO) << "Leader distribution: 4 0 0 0";
}

TEST_F(TestLoadBalancerPreferredLeader, TestBalancingMultiNodeAffinitizedLeader) {
  TSDescriptorVector ts_descs = {
      SetupTS("0000", "a", ""), SetupTS("1111", "a", ""), SetupTS("2222", "b", ""),
      SetupTS("3333", "c", ""), SetupTS("4444", "d", "")};
  PrepareTestState(ts_descs);

  PrepareAffinitizedLeaders({{"a"}, {"b"}});

  RemoveReplica(tablets_[0].get(), ts_descs[0]);
  RemoveReplica(tablets_[1].get(), ts_descs[0]);
  RemoveReplica(tablets_[2].get(), ts_descs[1]);
  RemoveReplica(tablets_[3].get(), ts_descs[1]);

  // Stop Tablet Server and move out its leaders.
  StopTsHeartbeat(ts_descs_[0]);
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[3]);
  }
  LOG(INFO) << "Leader distribution: 0 0 0 4";
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::map<string, int> to_count;
  string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_2, ts_3, ts_4;
  ts_0 = ts_descs_[0]->permanent_uuid();
  ts_1 = ts_descs_[1]->permanent_uuid();
  ts_2 = ts_descs_[2]->permanent_uuid();
  ts_3 = ts_descs_[3]->permanent_uuid();
  ts_4 = ts_descs_[4]->permanent_uuid();

  for (auto& ts : ts_descs_) {
    to_count[ts->permanent_uuid()] = 0;
  }

  for (size_t i = 0; i < tablets_.size(); i++) {
    ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts)));
    ASSERT_EQ(from_ts, ts_3);
    to_count[to_ts]++;

    auto tablet_to_move = std::find_if(
        tablets_.begin(), tablets_.end(),
        [&tablet_id](const auto& tablet) { return tablet->tablet_id() == tablet_id; });
    ASSERT_NE(tablet_to_move, tablets_.end());

    auto to_ts_server = std::find_if(
        ts_descs_.begin(), ts_descs_.end(),
        [&to_ts](const auto& ts) { return ts->permanent_uuid() == to_ts; });
    ASSERT_NE(to_ts_server, ts_descs_.end());

    MoveTabletLeader(tablet_to_move->get(), *(to_ts_server));
  }

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  ASSERT_EQ(2, to_count[ts_1]);
  ASSERT_EQ(2, to_count[ts_2]);
  LOG(INFO) << "Leader distribution: 0 2 2 0";

  ResumeTsHeartbeat(ts_descs_[0]);
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  TestMoveLeader(&placeholder, "", ts_0);
  TestMoveLeader(&placeholder, "", ts_0);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  LOG(INFO) << "Leader distribution: 2 2 0 0";
}

TEST_F(TestLoadBalancerPreferredLeader, TestBalancingWithLeaderBlacklist) {
  TSDescriptorVector ts_descs = {
      SetupTS("0000", "a", ""), SetupTS("1111", "a", ""), SetupTS("2222", "b", ""),
      SetupTS("3333", "c", ""), SetupTS("4444", "d", "")};
  PrepareTestState(ts_descs);

  RemoveReplica(tablets_[0].get(), ts_descs[1]);
  RemoveReplica(tablets_[1].get(), ts_descs[1]);
  RemoveReplica(tablets_[2].get(), ts_descs[0]);
  RemoveReplica(tablets_[3].get(), ts_descs[0]);

  int i = 0;
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[i / 2]);
    i++;
  }

  PrepareAffinitizedLeaders({{"a"}, {"b"}});
  LOG(INFO) << "Leader distribution: 2 2 0";

  // Leader blacklist ts2.
  AddLeaderBlacklist(ts_descs_[0]->permanent_uuid());
  LOG(INFO) << "Leader Blacklist: ts0";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  string placeholder, tablet_id, expected_from_ts, expected_to_ts;

  // With ts0 leader blacklisted, the leader on ts2 should be moved to ts1.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&tablet_id, expected_from_ts, expected_to_ts);
  ASSERT_TRUE(tablet_id == tablets_[0].get()->id() || tablet_id == tablets_[1].get()->id());
  TestMoveLeader(&tablet_id, expected_from_ts, expected_to_ts);
  ASSERT_TRUE(tablet_id == tablets_[0].get()->id() || tablet_id == tablets_[1].get()->id());
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move leader to ts1.
  MoveTabletLeader(tablets_[0].get(), ts_descs_[2]);
  MoveTabletLeader(tablets_[1].get(), ts_descs_[2]);
  LOG(INFO) << "Leader distribution: 0 2 2";

  // Clear leader blacklist.
  ClearLeaderBlacklist();
  LOG(INFO) << "Leader Blacklist cleared.";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  expected_from_ts = ts_descs_[2]->permanent_uuid();
  expected_to_ts = ts_descs_[0]->permanent_uuid();

  TestMoveLeader(&tablet_id, expected_from_ts, expected_to_ts);
  ASSERT_TRUE(tablet_id == tablets_[0].get()->id() || tablet_id == tablets_[1].get()->id());
  TestMoveLeader(&tablet_id, expected_from_ts, expected_to_ts);
  ASSERT_TRUE(tablet_id == tablets_[0].get()->id() || tablet_id == tablets_[1].get()->id());

  LOG(INFO) << "Leader distribution: 2 2 0 0";
}

// Test that wildcard region in leader preference works correctly.
// cloud=cloud0, region=*, zone=* should prefer all 9 TSs in cloud0.
TEST_F(TestLoadBalancerPreferredLeader, TestBalancingWildcardRegionLeaderPreference) {
  TSDescriptorVector ts_descs(Generate18NodeTsDesc());
  PrepareTestState(ts_descs);
  PrepareAffinitizedLeadersWithCloudInfo({{{"cloud0", "*", "*"}}});
  TestWildcardLeaderPreference([](const std::shared_ptr<TSDescriptor>& ts) {
    return ts->GetRegistration().cloud_info().placement_cloud() == "cloud0";
  });
}

// Test that wildcard zone in leader preference works correctly.
// cloud=cloud0, region=a, zone=* should prefer only 3 TSs in cloud0, region a.
TEST_F(TestLoadBalancerPreferredLeader, TestBalancingWildcardZoneLeaderPreference) {
  TSDescriptorVector ts_descs(Generate18NodeTsDesc());
  PrepareTestState(ts_descs);
  PrepareAffinitizedLeadersWithCloudInfo({{{"cloud0", "a", "*"}}});
  TestWildcardLeaderPreference([](const std::shared_ptr<TSDescriptor>& ts) {
    const auto registration = ts->GetRegistration();
    const auto& ci = registration.cloud_info();
    return ci.placement_cloud() == "cloud0" && ci.placement_region() == "a";
  });
}

// Test multi-priority wildcard preferences.
// Priority 1: cloud0.a.x (specific zone) - 1 TS
// Priority 2: cloud0.a.* (wildcard) - 3 TSs (x, y, z)
// Leaders should initially go to cloud0.a.x. When that TS is unavailable,
// leaders should fall back to cloud0.a.y or cloud0.a.z.
TEST_F(TestLoadBalancerPreferredLeader, TestBalancingMultiPriorityWildcardLeaderPreference) {
  TSDescriptorVector ts_descs(Generate18NodeTsDesc());
  PrepareTestState(ts_descs);

  PrepareAffinitizedLeadersWithCloudInfo({{{"cloud0", "a", "x"}}, {{"cloud0", "a", "*"}}});

  std::shared_ptr<TSDescriptor> priority1_ts;
  for (const auto& ts : ts_descs_) {
    const auto registration = ts->GetRegistration();
    const auto& ci = registration.cloud_info();
    if (ci.placement_cloud() == "cloud0" && ci.placement_region() == "a" &&
        ci.placement_zone() == "x") {
      priority1_ts = ts;
      break;
    }
  }
  ASSERT_NE(priority1_ts, nullptr);

  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_.back());
  }

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  string tablet_id, from_ts, to_ts;
  std::map<string, int> leader_count;
  for (const auto& ts : ts_descs_) {
    leader_count[ts->permanent_uuid()] = 0;
  }
  leader_count[ts_descs_.back()->permanent_uuid()] = narrow_cast<int>(tablets_.size());

  while (ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts))) {
    leader_count[from_ts]--;
    leader_count[to_ts]++;
    auto tablet_it = std::find_if(
        tablets_.begin(), tablets_.end(),
        [&tablet_id](const auto& t) { return t->id() == tablet_id; });
    ASSERT_NE(tablet_it, tablets_.end());
    auto to_ts_it = std::find_if(
        ts_descs_.begin(), ts_descs_.end(),
        [&to_ts](const auto& ts) { return ts->permanent_uuid() == to_ts; });
    ASSERT_NE(to_ts_it, ts_descs_.end());
    MoveTabletLeader(tablet_it->get(), *to_ts_it);
  }

  ASSERT_EQ(leader_count[priority1_ts->permanent_uuid()], narrow_cast<int>(tablets_.size()))
      << "All leaders should be on priority 1 TS (cloud0.a.x)";

  StopTsHeartbeat(priority1_ts);
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_.back());
  }
  leader_count[priority1_ts->permanent_uuid()] = 0;
  leader_count[ts_descs_.back()->permanent_uuid()] = narrow_cast<int>(tablets_.size());

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  while (ASSERT_RESULT(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts))) {
    leader_count[from_ts]--;
    leader_count[to_ts]++;
    auto tablet_it = std::find_if(
        tablets_.begin(), tablets_.end(),
        [&tablet_id](const auto& t) { return t->id() == tablet_id; });
    ASSERT_NE(tablet_it, tablets_.end());
    auto to_ts_it = std::find_if(
        ts_descs_.begin(), ts_descs_.end(),
        [&to_ts](const auto& ts) { return ts->permanent_uuid() == to_ts; });
    ASSERT_NE(to_ts_it, ts_descs_.end());
    MoveTabletLeader(tablet_it->get(), *to_ts_it);
  }

  int leaders_on_priority2 = 0;
  for (const auto& ts : ts_descs_) {
    const auto registration = ts->GetRegistration();
    const auto& ci = registration.cloud_info();
    if (ci.placement_cloud() == "cloud0" && ci.placement_region() == "a" &&
        ci.placement_zone() != "x") {
      leaders_on_priority2 += leader_count[ts->permanent_uuid()];
    }
  }
  ASSERT_EQ(leaders_on_priority2, narrow_cast<int>(tablets_.size()))
      << "All leaders should fall back to priority 2 (cloud0.a.y or cloud0.a.z)";

  std::vector<int> priority2_counts;
  for (const auto& ts : ts_descs_) {
    const auto registration = ts->GetRegistration();
    const auto& ci = registration.cloud_info();
    if (ci.placement_cloud() == "cloud0" && ci.placement_region() == "a" &&
        ci.placement_zone() != "x") {
      priority2_counts.push_back(leader_count[ts->permanent_uuid()]);
    }
  }
  int max_count = *std::max_element(priority2_counts.begin(), priority2_counts.end());
  int min_count = *std::min_element(priority2_counts.begin(), priority2_counts.end());
  ASSERT_LE(max_count - min_count, 1)
      << "Leaders should be evenly distributed among priority 2 TSs";
}

}  // namespace master
}  // namespace yb
