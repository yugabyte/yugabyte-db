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

  void PrepareAffinitizedLeaders(const vector<vector<string>>& zones) {
    for (const auto& zone_set : zones) {
      AffinitizedZonesSet new_zone;
      for (const string& zone : zone_set) {
        CloudInfoPB ci;
        ci.set_placement_cloud(default_cloud);
        ci.set_placement_region(default_region);
        ci.set_placement_zone(zone);
        new_zone.insert(ci);
      }
      affinitized_zones_.push_back(new_zone);
    }
  }

  void AddFollowerReplica(TabletInfo* tablet, std::shared_ptr<yb::master::TSDescriptor> ts_desc) {
    std::shared_ptr<TabletReplicaMap> replicas =
        std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());

    TabletReplica replica;
    NewReplica(ts_desc.get(), tablet::RaftGroupStatePB::RUNNING, PeerRole::FOLLOWER, &replica);
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

  string placeholder;

  // First we make sure that no load balancing happens during the live iteration.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));

  cb_.SetOptions(READ_ONLY, read_only_placement_uuid);
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  // Now load balance an read_only replica.
  string expected_from_ts = ts_descs_[3]->permanent_uuid();
  string expected_to_ts = ts_descs_[4]->permanent_uuid();
  string expected_tablet_id = tablets_[0]->tablet_id();
  TestAddLoad(expected_tablet_id, expected_from_ts, expected_to_ts);
  RemoveReplica(tablet_map_[expected_tablet_id].get(), ts_descs_[3]);
  AddFollowerReplica(tablet_map_[expected_tablet_id].get(), ts_descs_[4]);

  ClearTabletsAddedForTest();

  expected_from_ts = ts_descs_[3]->permanent_uuid();
  expected_to_ts = ts_descs_[4]->permanent_uuid();
  expected_tablet_id = tablets_[1]->tablet_id();
  TestAddLoad(expected_tablet_id, expected_from_ts, expected_to_ts);
  RemoveReplica(tablet_map_[expected_tablet_id].get(), ts_descs_[3]);
  AddFollowerReplica(tablet_map_[expected_tablet_id].get(), ts_descs_[4]);

  // Then make sure there is no more balancing, since both ts3 and ts4 have 2 replicas each.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));
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

  cb_.SetOptions(READ_ONLY, read_only_placement_uuid);
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

}  // namespace master
}  // namespace yb
