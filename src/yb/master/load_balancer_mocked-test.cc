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
#include "yb/gutil/dynamic_annotations.h"
#include "yb/master/load_balancer_mocked-test_base.h"

namespace yb {
namespace master {

class LoadBalancerMockedTest : public LoadBalancerMockedBase {};

TEST_F(LoadBalancerMockedTest, TestNoPlacement) {
  LOG(INFO) << "Testing with no placement information";
  PrepareTestStateMultiAz();
  PlacementInfoPB* cluster_placement = replication_info_.mutable_live_replicas();
  cluster_placement->set_num_replicas(kDefaultNumReplicas);
  // Analyze the tablets into the internal state.
  ASSERT_OK(AnalyzeTablets());

  // Check some base expectations for balanced cluster.
  ASSERT_EQ(0, GetTotalOverreplication());
  ASSERT_EQ(0, GetTotalStartingTablets());
  ASSERT_EQ(total_num_tablets_, GetTotalRunningTablets());

  // Add the fourth TS in there, set it in the same az as ts0.
  ts_descs_.push_back(SetupTS("3333", "a"));

  // Reset the load state and recompute.
  ResetState();
  ASSERT_OK(AnalyzeTablets());

  std::string placeholder;
  // Lowest load should be the last, empty TS.
  std::string expected_to_ts = ts_descs_[3]->permanent_uuid();
  // Equal load across the first three TSs. Picking the one with largest ID in string compare.
  std::string expected_from_ts = ts_descs_[2]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

  // Perform another round on the updated in-memory load. The move should have made ts2 less
  // loaded, so next tablet should come from ts1 to ts3.
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

  // One more round, finally expecting to move from ts0 to ts3.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

  // Final check on in-memory state after in-memory moves.
  ASSERT_EQ(total_num_tablets_ - 3, GetTotalRunningTablets());
  ASSERT_EQ(3, GetTotalStartingTablets());
  ASSERT_EQ(0, GetTotalOverreplication());
}

TEST_F(LoadBalancerMockedTest, TestWithPlacement) {
  LOG(INFO) << "Testing with placement information";
  PrepareTestStateMultiAz();
  // Setup cluster level placement to the same 3 AZs as our tablet servers.
  SetupClusterConfig({"a", "b", "c"}, &replication_info_);

  // Add three TSs, one in wrong AZ, two in right AZs.
  ts_descs_.push_back(SetupTS("3333", "WRONG"));
  ts_descs_.push_back(SetupTS("4444", "a"));
  ts_descs_.push_back(SetupTS("5555", "a"));

  // Analyze the tablets into the internal state.
  ASSERT_OK(AnalyzeTablets());

  // Check some base expectations for balanced cluster.
  ASSERT_EQ(0, GetTotalOverreplication());
  ASSERT_EQ(0, GetTotalStartingTablets());
  ASSERT_EQ(total_num_tablets_, GetTotalRunningTablets());

  // Equal load across the first three TSs, but placement dictates we can only move from ts0 to
  // ts4 or ts5. We should pick the lowest uuid one, which is ts4.
  std::string placeholder;
  std::string expected_from_ts = ts_descs_[0]->permanent_uuid();
  std::string expected_to_ts = ts_descs_[4]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

  // Recompute and expect to move to next least loaded TS, which matches placement, which should
  // be ts5 now. Load should still move from the only TS in the correct placement, which is ts0.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts);
}

TEST_F(LoadBalancerMockedTest, TestWithMissingPlacement) {
  LOG(INFO) << "Testing with tablet servers missing placement information";
  PrepareTestStateMultiAz();
  // Setup cluster level placement to multiple AZs.
  SetupClusterConfig({"a", "b", "c"}, &replication_info_);

  // Remove the only tablet peer from AZ "c".
  for (const auto& tablet : tablets_) {
    std::shared_ptr<TabletReplicaMap> replica_map =
      std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());
    replica_map->erase(ts_descs_[2]->permanent_uuid());
    tablet->SetReplicaLocations(replica_map);
  }
  // Remove the tablet server from the list.
  ts_descs_.pop_back();

  // Add some more servers in that same AZ, so we get to pick among them.
  ts_descs_.push_back(SetupTS("2new", "c"));
  ts_descs_.push_back(SetupTS("3new", "c"));

  // Load up data.
  ASSERT_OK(AnalyzeTablets());

  // First we'll fill up the missing placements for all 4 tablets.
  std::string placeholder;
  std::string expected_to_ts, expected_from_ts;
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  // Now registered load should be 4,4,2,2. However, we cannot move load from AZ "a" and "b" to
  // the servers in AZ "c", under normal load conditions, so we should fail the call.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));
}

TEST_F(LoadBalancerMockedTest, TestOverReplication) {
  LOG(INFO) << "Testing with tablet servers with over-replication";
  PrepareTestStateMultiAz();
  // Setup cluster config.
  SetupClusterConfig({"a"}, &replication_info_);

  // Remove the 2 tablet peers that are wrongly placed and assign a new one that is properly
  // placed.
  for (const auto& tablet : tablets_) {
    std::shared_ptr<TabletReplicaMap> replica_map =
      std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());
    replica_map->erase(ts_descs_[1]->permanent_uuid());
    replica_map->erase(ts_descs_[2]->permanent_uuid());
    tablet->SetReplicaLocations(replica_map);
  }
  // Remove the two wrong tablet servers from the list.
  ts_descs_.pop_back();
  ts_descs_.pop_back();
  // Add a tablet server with proper placement and peer it to all tablets. Now all tablets
  // should have 2 peers.
  // Using empty ts_uuid here since our mocked PendingTasksUnlocked only returns empty ts_uuids.
  ts_descs_.push_back(SetupTS("", "a"));
  for (auto tablet : tablets_) {
    AddRunningReplica(tablet.get(), ts_descs_[1]);
  }

  // Setup some new tablet servers as replicas in both the same and wrong AZs and confirm that
  // the algorithms work as expected.
  ts_descs_.push_back(SetupTS("2222", "WRONG"));
  ts_descs_.push_back(SetupTS("3333", "a"));
  ts_descs_.push_back(SetupTS("4444", "a"));
  // We'll keep this as empty, for use as a sync for tablets.
  ts_descs_.push_back(SetupTS("5555", "a"));

  // Over-replicate a tablet, with a wrongly placed replica.
  AddRunningReplica(tablets_[0].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[0].get(), ts_descs_[3]);

  // Over-replicate a tablet, with properly placed replicas.
  AddRunningReplica(tablets_[1].get(), ts_descs_[3]);
  AddRunningReplica(tablets_[1].get(), ts_descs_[4]);

  // Bring a tablet to proper replication, but with a wrongly placed replica.
  AddRunningReplica(tablets_[2].get(), ts_descs_[2]);

  // Bring a tablet to proper replication, with all replicas in the correct placement.
  AddRunningReplica(tablets_[3].get(), ts_descs_[4]);

  ASSERT_OK(AnalyzeTablets());

  // Add all tablets to the list of tablets with a pending add operation and verify that calling
  // HandleAddReplicas fails because all the tablets have a pending add operation.
  pending_add_replica_tasks_.clear();
  for (const auto& tablet : tablets_) {
    pending_add_replica_tasks_.push_back(tablet->id());
  }
  int count = 0;
  int pending_add_count = 0;
  ASSERT_OK(CountPendingTasksUnlocked(cur_table_uuid_, &pending_add_count, &count, &count));
  ASSERT_EQ(pending_add_count, pending_add_replica_tasks_.size());
  ASSERT_OK(AnalyzeTablets());
  std::string placeholder, tablet_id;
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &placeholder, &placeholder)));

  // Clear pending_add_replica_tasks_ and reset the state of ClusterLoadBalancer.
  pending_add_replica_tasks_.clear();
  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Check that if adding replicas, we'll notice the wrong placement and adjust it.
  std::string expected_tablet_id, expected_from_ts, expected_to_ts;
  expected_tablet_id = tablets_[2]->tablet_id();
  expected_from_ts = ts_descs_[2]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  TestAddLoad(expected_tablet_id, expected_from_ts, expected_to_ts);

  // Add all tablets to the list of tablets with a pending remove operation and verify that
  // calling HandleRemoveReplicas fails because all the tablets have a pending remove operation.
  pending_remove_replica_tasks_.clear();
  for (const auto& tablet : tablets_) {
    pending_remove_replica_tasks_.push_back(tablet->id());
  }
  int pending_remove_count = 0;
  ASSERT_OK(
      CountPendingTasksUnlocked(cur_table_uuid_, &count, &pending_remove_count, &count));
  ASSERT_EQ(pending_remove_count, pending_remove_replica_tasks_.size());
  ASSERT_OK(AnalyzeTablets());
  ASSERT_FALSE(ASSERT_RESULT(HandleRemoveReplicas(&tablet_id, &placeholder)));

  // Clear pending_remove_replica_tasks_ and reset the state of ClusterLoadBalancer.
  pending_remove_replica_tasks_.clear();
  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Check that removing replicas, we take out the wrong placement one first.
  expected_tablet_id = tablets_[0]->tablet_id();
  expected_from_ts = ts_descs_[2]->permanent_uuid();
  TestRemoveLoad(expected_tablet_id, expected_from_ts);
  // Check that trying to remove another replica, will take out one from the
  // last over-replicated set. Both ts0 and ts1 are on the same load,
  // so we'll pick the highest uuid one to remove.
  expected_tablet_id = tablets_[1]->tablet_id();
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  TestRemoveLoad(expected_tablet_id, expected_from_ts);
  // Check that trying to remove another replica will fail, as we have no more over-replication.
  ASSERT_FALSE(ASSERT_RESULT(HandleRemoveReplicas(&placeholder, &placeholder)));
}

TEST_F(LoadBalancerMockedTest, TestWithBlacklist) {
  LOG(INFO) << "Testing with tablet servers with blacklist";
  PrepareTestStateMultiAz();
  // Setup cluster config.
  SetupClusterConfig({"a", "b", "c"}, &replication_info_);

  // Blacklist the first host in AZ "a".
  blacklist_.add_hosts()->set_host(ts_descs_[0]->permanent_uuid());

  // Add two tablet servers in AZ "a" and one in AZ "b".
  ts_descs_.push_back(SetupTS("3333", "b"));
  ts_descs_.push_back(SetupTS("4444", "a"));
  ts_descs_.push_back(SetupTS("5555", "a"));

  // Blacklist the first new tablet server in AZ "a" so we show it isn't picked.
  blacklist_.add_hosts()->set_host(ts_descs_[4]->permanent_uuid());

  // Prepare the data.
  ASSERT_OK(AnalyzeTablets());

  std::string placeholder, expected_from_ts, expected_to_ts;
  // Expecting that we move load from ts0 which is blacklisted, to ts5. This is because ts4 is
  // also blacklisted and ts0 has a valid placement, so we try to find a server in the same
  // placement.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  for (const auto& tablet : tablets_) {
    TestAddLoad(tablet->tablet_id(), expected_from_ts, expected_to_ts);
  }

  // There is some opportunity to equalize load across the remaining servers also. However,
  // we cannot do so until the next run since all tablets have just been moved once.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));

  // Move tablets off ts0 to ts5.
  for (const auto& tablet : tablets_) {
    RemoveReplica(tablet.get(), ts_descs_[0]);
    AddRunningReplica(tablet.get(), ts_descs_[5]);
  }

  // Reset the load state and recompute.
  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Now that we have reinitialized for the next run, we can try to equalize load across the
  // remaining servers. Our load on non-blacklisted servers is: ts1:4, ts2:4, ts3:0, ts5:4. Of
  // this, we can only balance from ts1 to ts3, as they are in the same AZ.
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts);
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts);
  // Now we should have no more tablets we are able to move.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));
}

class LoadBalancerMockedCloudInfoSimilarityTest : public LoadBalancerMockedTest,
                                                  public testing::WithParamInterface<bool> {};
INSTANTIATE_TEST_SUITE_P(, LoadBalancerMockedCloudInfoSimilarityTest, ::testing::Bool());

TEST_P(LoadBalancerMockedCloudInfoSimilarityTest, TestChooseTabletInSameZone) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_ignore_cloud_info_similarity) = GetParam();
  LOG(INFO) << Format("Testing that (if possible) we move a tablet whose leader is in the same "
                      "zone/region as the new tserver with "
                      "FLAGS_load_balancer_ignore_cloud_info_similarity=$0.",
                      GetAtomicFlag(&FLAGS_load_balancer_ignore_cloud_info_similarity));
  PrepareTestStateMultiAz();
  // Setup cluster config. Do not set placement info for the table, so its tablets can be moved
  // freely from ts0 to the new tserver.
  PlacementInfoPB* placement_info = replication_info_.mutable_live_replicas();
  placement_info->set_num_replicas(kDefaultNumReplicas);

  // Add three more tablet servers
  ts_descs_.push_back(SetupTS("3333", "a"));
  ts_descs_.push_back(SetupTS("4444", "b"));
  ts_descs_.push_back(SetupTS("5555", "c"));

  // Move 2 tablets from ts1 and ts2 each to ts3 and ts4, leaving ts0 with 4 tablets, ts1..4
  // with 2 tablets and ts5 with none.
  RemoveReplica(tablets_[0].get(), ts_descs_[1]);
  AddRunningReplica(tablets_[0].get(), ts_descs_[3]);
  RemoveReplica(tablets_[1].get(), ts_descs_[1]);
  AddRunningReplica(tablets_[1].get(), ts_descs_[3]);
  RemoveReplica(tablets_[0].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[0].get(), ts_descs_[4]);
  RemoveReplica(tablets_[1].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[1].get(), ts_descs_[4]);

  // Tablet leaders are as follows: Tablet 0: ts0, tablet 1: unassigned (because of the move
  // above), tablet 2: ts2, tablet 3: ts0. Assign tablet 1's leader to be to ts4 now.
  MoveTabletLeader(tablets_[1].get(), ts_descs_[4]);
  ASSERT_OK(AnalyzeTablets());

  std::string placeholder, expected_from_ts, expected_to_ts, actual_tablet_id;
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts, &actual_tablet_id);

  const auto* moved_tablet_leader = ASSERT_RESULT(tablet_map_[actual_tablet_id]->GetLeader());
  // If ignoring cloud info, we should move a tablet whose leader is not in zone c (by the order
  // of the tablets, the first non-leader tablet we encounter is tablet 1 and we do not expect
  // to replace it). Otherwise, we should pick the tablet whose leader IS in zone c.
  if (GetAtomicFlag(&FLAGS_load_balancer_ignore_cloud_info_similarity)) {
    ASSERT_NE(moved_tablet_leader->GetCloudInfo().placement_zone(), "c");
  } else {
    ASSERT_EQ(moved_tablet_leader->GetCloudInfo().placement_zone(), "c");
  }
}

TEST_F(LoadBalancerMockedTest, TestMovingMultipleTabletsFromSameServer) {
  LOG(INFO) << "Testing moving multiple tablets from the same tablet server";
  PrepareTestStateMultiAz();
  PlacementInfoPB *cluster_placement = replication_info_.mutable_live_replicas();
  cluster_placement->set_num_replicas(kDefaultNumReplicas);

  // Add three more tablet servers
  ts_descs_.push_back(SetupTS("3333", "a"));
  ts_descs_.push_back(SetupTS("4444", "a"));
  ts_descs_.push_back(SetupTS("5555", "a"));

  // Move 2 tablets from ts1 and ts2 each to ts3 and ts4, leaving ts0 with 4 tablets, ts1..4
  // with 2 tablets and ts5 with none.
  RemoveReplica(tablets_[0].get(), ts_descs_[1]);
  AddRunningReplica(tablets_[0].get(), ts_descs_[3]);
  RemoveReplica(tablets_[1].get(), ts_descs_[1]);
  AddRunningReplica(tablets_[1].get(), ts_descs_[3]);
  RemoveReplica(tablets_[0].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[0].get(), ts_descs_[4]);
  RemoveReplica(tablets_[1].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[1].get(), ts_descs_[4]);

  // Tablet leaders are as follows: Tablet 0: ts0, tablet 1: unassigned (because of the move
  // above), tablet 2: ts2, tablet 3: ts0. Assign tablet 1's leader to be ts4.
  MoveTabletLeader(tablets_[1].get(), ts_descs_[4]);
  ASSERT_OK(AnalyzeTablets());

  // Since tablet 0 on ts0 is the leader, it won't be moved.
  std::string placeholder, expected_from_ts, expected_to_ts, actual_tablet_id1, actual_tablet_id2;
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts, &actual_tablet_id1);
  TestAddLoad(placeholder, expected_from_ts, expected_to_ts, &actual_tablet_id2);
  ASSERT_NE(actual_tablet_id1, actual_tablet_id2);
  ASSERT_EQ(ts_descs_[0]->permanent_uuid(),
            ASSERT_RESULT(tablets_[0]->GetLeader())->permanent_uuid());
  ASSERT_EQ(tablets_[0]->GetReplicaLocations()->count(ts_descs_[0]->permanent_uuid()), 1);
}

TEST_F(LoadBalancerMockedTest, TestWithMissingPlacementAndLoadImbalance) {
  LOG(INFO) << "Testing with tablet servers missing placement and load imbalance";
  PrepareTestStateMultiAz();
  // Setup cluster level placement to multiple AZs.
  SetupClusterConfig({"a", "b", "c"}, &replication_info_);

  // Remove the only tablet peer from AZ "c".
  for (const auto& tablet : tablets_) {
    std::shared_ptr<TabletReplicaMap> replica_map =
      std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());
    replica_map->erase(ts_descs_[2]->permanent_uuid());
    tablet->SetReplicaLocations(replica_map);
  }
  // Remove the tablet server from the list.
  ts_descs_.pop_back();

  // Add back 1 new server in that same AZ. So we should add missing placments to this new TS.
  ts_descs_.push_back(SetupTS("1new", "c"));

  // Load up data.
  ASSERT_OK(AnalyzeTablets());

  // First we'll fill up the missing placements for all 4 tablets.
  std::string placeholder;
  std::string expected_to_ts;
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  TestAddLoad(placeholder, placeholder, expected_to_ts);

  // Add yet 1 more server in that same AZ for some load-balancing.
  ts_descs_.push_back(SetupTS("2new", "c"));

  ASSERT_OK(AnalyzeTablets());

  // Since we have just filled up the missing placements for all 4 tablets, we cannot rebalance
  // the tablets to the second new TS until the next run.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));

  // Add the missing placements to the first new TS.
  AddRunningReplica(tablets_[0].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[1].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[2].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[3].get(), ts_descs_[2]);

  // Reset the load state and recompute.
  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Now we should be able to move 2 tablets to the second new TS.
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  TestAddLoad(placeholder, placeholder, expected_to_ts);
  TestAddLoad(placeholder, placeholder, expected_to_ts);

  // And the load should now be balanced so no more move is expected.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));
}

TEST_F(LoadBalancerMockedTest, TestBalancingLeaders) {
  LOG(INFO) << "Testing moving overloaded leaders";
  PrepareTestStateMultiAz();
  // Move all leaders to ts0.
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  LOG(INFO) << "Leader distribution: 4 0 0";

  ASSERT_OK(AnalyzeTablets());

  // Only 2 leaders should be moved off from ts0, 1 to ts2 and then another to ts1.
  std::string placeholder, tablet_id_1, tablet_id_2, expected_tablet_id,
      expected_from_ts, expected_to_ts;
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[1]->permanent_uuid();
  TestMoveLeader(&tablet_id_1, expected_from_ts, expected_to_ts);
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&tablet_id_2, expected_from_ts, expected_to_ts);
  // Ideally, we want to assert the leaders expected to be moved. However, since the tablets
  // are stored in an unordered_map in catalog manager and all leaders have same load gap, the
  // leaders being moved are not deterministic. So just make sure we are not moving the same
  // leader twice.
  ASSERT_NE(tablet_id_1, tablet_id_2);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader to ts1.
  MoveTabletLeader(tablets_[0].get(), ts_descs_[1]);
  LOG(INFO) << "Leader distribution: 3 1 0";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Only 1 leader should be moved off from ts0 to ts2.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 more leader to ts1 and blacklist ts0
  MoveTabletLeader(tablets_[1].get(), ts_descs_[1]);
  blacklist_.add_hosts()->set_host(ts_descs_[0]->permanent_uuid());
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist: ts0";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // With ts0 blacklisted, the 2 leaders on ts0 should be moved to some undetermined servers.
  // ts1 still has 2 leaders and ts2 has 0 so 1 leader should be moved to ts2.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));
  ASSERT_FALSE(ASSERT_RESULT(HandleRemoveReplicas(&placeholder, &placeholder)));
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Clear the blacklist.
  blacklist_.Clear();
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist cleared.";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Only 1 tablets should be moved off from ts1 to ts2.
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader from ts1 to ts2.
  MoveTabletLeader(tablets_[1].get(), ts_descs_[2]);
  LOG(INFO) << "Leader distribution: 2 1 1";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // The distribution is as balanced as it can be so there shouldn't be any move.
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(LoadBalancerMockedTest, TestMissingPlacementSingleAz) {
  LOG(INFO) << "Testing single az deployment where min_num_replicas different from num_replicas";
  PrepareTestStateSingleAz();
  // Setup cluster level placement to single AZ.
  SetupClusterConfig({"a"}, &replication_info_);

  // Under-replicate tablets 0, 1, 2.
  RemoveReplica(tablets_[0].get(), ts_descs_[0]);
  RemoveReplica(tablets_[1].get(), ts_descs_[1]);
  RemoveReplica(tablets_[2].get(), ts_descs_[2]);

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  std::string placeholder, expected_tablet_id, expected_from_ts, expected_to_ts;

  // Make sure a replica is added for tablet 0 to ts 0.
  expected_tablet_id = tablets_[0]->tablet_id();
  expected_to_ts = ts_descs_[0]->permanent_uuid();

  TestAddLoad(expected_tablet_id,  placeholder, expected_to_ts);

  // Make sure a replica is added for tablet 1 to ts 1.
  expected_tablet_id = tablets_[1]->tablet_id();
  expected_to_ts = ts_descs_[1]->permanent_uuid();

  TestAddLoad(expected_tablet_id,  placeholder, expected_to_ts);

  // Make sure a replica is added for tablet 2 to ts 2.
  expected_tablet_id = tablets_[2]->tablet_id();
  expected_to_ts = ts_descs_[2]->permanent_uuid();

  TestAddLoad(expected_tablet_id,  placeholder, expected_to_ts);

  // Everything is normal now, load balancer shouldn't do anything.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&placeholder, &placeholder, &placeholder)));
}

TEST_F(LoadBalancerMockedTest, TestBalancingLeadersWithThreshold) {
  LOG(INFO) << "Testing moving overloaded leaders with threshold = 2";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_balance_threshold) = 2;
  PrepareTestStateMultiAz();
  // Move all leaders to ts0.
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  LOG(INFO) << "Leader distribution: 4 0 0";

  ASSERT_OK(AnalyzeTablets());

  // Only 2 leaders should be moved off from ts0 to ts1 and ts2 each.
  std::string placeholder, tablet_id_1, tablet_id_2, expected_tablet_id,
      expected_from_ts, expected_to_ts;
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[1]->permanent_uuid();
  TestMoveLeader(&tablet_id_1, expected_from_ts, expected_to_ts);
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&tablet_id_2, expected_from_ts, expected_to_ts);
  // Ideally, we want to assert the leaders expected to be moved. However, since the tablets
  // are stored in an unordered_map in catalog manager and all leaders have same load gap, the
  // leaders being moved are not deterministic. So just make sure we are not moving the same
  // leader twice.
  ASSERT_NE(tablet_id_1, tablet_id_2);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader to ts1.
  MoveTabletLeader(tablets_[0].get(), ts_descs_[1]);
  LOG(INFO) << "Leader distribution: 3 1 0";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Only 1 leader should be moved off from ts0 to ts2.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 more leader to ts1 and blacklist ts0
  MoveTabletLeader(tablets_[1].get(), ts_descs_[1]);
  blacklist_.add_hosts()->set_host(ts_descs_[0]->permanent_uuid());
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist: ts0";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Clear the blacklist.
  blacklist_.Clear();
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist cleared.";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // Again all tablet servers have leaders below threshold so no move is expected.
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader from ts1 to ts2.
  MoveTabletLeader(tablets_[1].get(), ts_descs_[2]);
  LOG(INFO) << "Leader distribution: 2 1 1";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  // The distribution is as balanced as it can be so there shouldn't be any move.
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(LoadBalancerMockedTest, TestLeaderOverReplication) {
  LOG(INFO) << "Skip leader TS being picked with over-replication.";
  PrepareTestStateMultiAz();
  replication_info_.mutable_live_replicas()->set_num_replicas(kDefaultNumReplicas);

  // Create one more TS.
  ts_descs_.push_back(SetupTS("3333", "a"));

  const auto& tablet = tablets_[0].get();
  // Over-replicate first tablet, with one extra replica.
  AddRunningReplica(tablet, ts_descs_[3]);

  // Move leader to first replica in the list (and will be most-loaded).
  MoveTabletLeader(tablet, ts_descs_[2]);

  // Load up data.
  ASSERT_OK(AnalyzeTablets());

  // Ensure the tablet is picked.
  TestRemoveLoad(tablets_[0]->tablet_id(), "");
}

TEST_F(LoadBalancerMockedTest, TestLeaderBlacklist) {
  LOG(INFO) << "Testing moving overloaded leaders";
  PrepareTestStateMultiAz();
  // Move leaders of tablet i to ts i%3.
  int i = 0;
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[i%3]);
    i++;
  }
  LOG(INFO) << "Leader distribution: 2 1 1";

  ASSERT_OK(AnalyzeTablets());

  // Leader blacklist ts2
  AddLeaderBlacklist(ts_descs_[2]->permanent_uuid());
  LOG(INFO) << "Leader distribution: 2 1 1. Leader Blacklist: ts2";

  ResetState();
  ASSERT_OK(AnalyzeTablets());

  std::string placeholder, tablet_id_1, tablet_id_2, expected_tablet_id,
      expected_from_ts, expected_to_ts;

  // With ts2 leader blacklisted, the leader on ts2 should be moved to ts1.
  expected_from_ts = ts_descs_[2]->permanent_uuid();
  expected_to_ts = ts_descs_[1]->permanent_uuid();
  TestMoveLeader(&tablet_id_1, expected_from_ts, expected_to_ts);
  ASSERT_EQ(tablet_id_1, tablets_[2].get()->id());
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader to ts1.
  MoveTabletLeader(tablets_[2].get(), ts_descs_[1]);
  LOG(INFO) << "Leader distribution: 2 2 0";

  // Clear leader blacklist.
  ClearLeaderBlacklist();
  LOG(INFO) << "Leader distribution: 2 2 0. Leader Blacklist cleared.";

  ASSERT_OK(AnalyzeTablets());

  // With ts2 no more leader blacklisted, a leader on ts0 or ts1 should be moved to ts2.
  expected_from_ts = "";
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader to ts2.
  for (const auto& tablet : tablets_) {
    if (tablet.get()->id() == placeholder) {
      MoveTabletLeader(tablet.get(), ts_descs_[2]);
      break;
    }
  }
  LOG(INFO) << "Leader distribution: 2 1 1 -OR- 1 2 1";
}


} // namespace master
} // namespace yb
