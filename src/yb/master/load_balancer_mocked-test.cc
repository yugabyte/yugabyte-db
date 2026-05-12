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

#include <gflags/gflags_declare.h>
#include <gtest/gtest.h>
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/master/load_balancer_mocked-test_base.h"
#include "yb/tablet/tablet_types.pb.h"

namespace yb {
namespace master {

class LoadBalancerMockedTest : public LoadBalancerMockedBase {
 protected:
  void SetTabletReplicaSizes(const TabletInfoPtr& tablet, int tablet_size) {
    auto replica_map = std::make_shared<TabletReplicaMap>(*tablet->GetReplicaLocations());
    for (auto& [ts_uuid, replica] : *replica_map) {
      // The total size is the only field we should use for cluster balancing.
      // The SST and WAL sizes are more up-to-date but do not include snapshots.
      replica.drive_info.total_size = tablet_size;
    }
    tablet->SetReplicaLocations(replica_map);
  }
};

TEST_F(LoadBalancerMockedTest, TestStartingTablet) {
  PrepareTestStateSingleAz();

  // Set one tablet to starting.
  auto replica_map = std::make_shared<TabletReplicaMap>(*tablets_[0]->GetReplicaLocations());
  replica_map->begin()->second.role = PeerRole::NON_PARTICIPANT;
  replica_map->begin()->second.state = tablet::RaftGroupStatePB::BOOTSTRAPPING;
  tablets_[0]->SetReplicaLocations(replica_map);

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  ASSERT_EQ(GetTotalOverreplication(), 0);
  ASSERT_EQ(GetTotalStartingTablets(), 1);
  ASSERT_EQ(GetTotalRunningTablets(), total_num_tablets_ - 1);
}

TEST_F(LoadBalancerMockedTest, TestPendingStepdown) {
  PrepareTestStateSingleAz();

  // Find a leader and follower to use for stepdown.
  auto tablet_id = tablets_[0]->id();
  TabletServerId old_leader_ts, new_leader_ts;
  for (const auto& replica : *tablets_[0]->GetReplicaLocations()) {
    if (replica.second.role == PeerRole::LEADER) {
      old_leader_ts = replica.first;
    } else {
      new_leader_ts = replica.first;
    }
  }

  // Add a pending stepdown for one of the tablets.
  pending_stepdown_leader_tasks_[tablet_id] = new_leader_ts;

  auto pending_tasks = ASSERT_RESULT(ResetLoadBalancerAndAnalyzeTablets());
  ASSERT_EQ(pending_tasks.stepdowns, 1);
  const auto* table_state = cb_.GetTableState(kTableId);
  const auto& tablet_meta = table_state->per_tablet_meta_.at(tablet_id);
  ASSERT_EQ(tablet_meta.leader_uuid, new_leader_ts);

  ASSERT_FALSE(table_state->per_ts_meta_.at(old_leader_ts).leaders.contains(tablet_id));
  ASSERT_TRUE(table_state->per_ts_meta_.at(new_leader_ts).leaders.contains(tablet_id));
}

TEST_F(LoadBalancerMockedTest, TestNoPlacement) {
  LOG(INFO) << "Testing with no placement information";
  PrepareTestStateMultiAz();
  PlacementInfoPB* cluster_placement = replication_info_.mutable_live_replicas();
  cluster_placement->set_num_replicas(kDefaultNumReplicas);
  // Analyze the tablets into the internal state.
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Check some base expectations for balanced cluster.
  ASSERT_EQ(0, GetTotalOverreplication());
  ASSERT_EQ(0, GetTotalStartingTablets());
  ASSERT_EQ(total_num_tablets_, GetTotalRunningTablets());

  // Add the fourth TS in there, set it in the same az as ts0.
  ts_descs_.push_back(SetupTS("3333", "a"));

  // Reset the load state and recompute.
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::string placeholder;
  // Lowest load should be the last, empty TS.
  std::string expected_to_ts = ts_descs_[3]->permanent_uuid();
  // Equal load across the first three TSs. Picking the one with largest ID in string compare.
  std::string expected_from_ts = ts_descs_[2]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts));

  // Perform another round on the updated in-memory load. The move should have made ts2 less
  // loaded, so next tablet should come from ts1 to ts3.
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts));

  // One more round, finally expecting to move from ts0 to ts3.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts));

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
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Check some base expectations for balanced cluster.
  ASSERT_EQ(0, GetTotalOverreplication());
  ASSERT_EQ(0, GetTotalStartingTablets());
  ASSERT_EQ(total_num_tablets_, GetTotalRunningTablets());

  // Equal load across the first three TSs, but placement dictates we can only move from ts0 to
  // ts4 or ts5. We should pick the lowest uuid one, which is ts4.
  std::string placeholder;
  std::string expected_from_ts = ts_descs_[0]->permanent_uuid();
  std::string expected_to_ts = ts_descs_[4]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts));

  // Recompute and expect to move to next least loaded TS, which matches placement, which should
  // be ts5 now. Load should still move from the only TS in the correct placement, which is ts0.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts));
}

TEST_F(LoadBalancerMockedTest, TestWithMissingPlacement) {
  LOG(INFO) << "Testing with tablet servers missing placement information";
  PrepareTestStateMultiAz();
  // Setup cluster level placement to multiple AZs.
  SetupClusterConfig({"a", "b", "c"}, &replication_info_);

  // Remove the only tablet peer from AZ "c".
  for (const auto& tablet : tablets_) {
    RemoveReplica(tablet.get(), ts_descs_[2]);
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  // Remove the tablet server from the list.
  ts_descs_.pop_back();

  // Add some more servers in that same AZ, so we get to pick among them.
  ts_descs_.push_back(SetupTS("2new", "c"));
  ts_descs_.push_back(SetupTS("3new", "c"));

  // Load up data.
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // First we'll fill up the missing placements for all 4 tablets.
  std::string placeholder;
  std::string expected_to_ts, expected_from_ts;
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  // Now registered load should be 4,4,2,2. However, we cannot move load from AZ "a" and "b" to
  // the servers in AZ "c", under normal load conditions, so we should fail the call.
  ASSERT_NOK(TestAddLoad(placeholder, placeholder, placeholder));
}

TEST_F(LoadBalancerMockedTest, TestOverReplication) {
  LOG(INFO) << "Testing with tablet servers with over-replication";
  PrepareTestStateMultiAz();
  // Setup cluster config.
  SetupClusterConfig({"a"}, &replication_info_);

  // Remove the 2 tablet peers that are wrongly placed.
  for (const auto& tablet : tablets_) {
    auto replica_map = std::make_shared<TabletReplicaMap>(*tablet->GetReplicaLocations());
    replica_map->erase(ts_descs_[1]->permanent_uuid());
    replica_map->erase(ts_descs_[2]->permanent_uuid());
    tablet->SetReplicaLocations(replica_map);
  }
  // Remove the two wrong tablet servers from the list.
  ts_descs_.pop_back();
  ts_descs_.pop_back();
  // Add a tablet server with proper placement and a tablet peer for all tablets. Now all tablets
  // should have 2 peers.
  // Using empty ts_uuid here since our mocked PendingTasksUnlocked only returns empty ts_uuids.
  ts_descs_.push_back(SetupTS("1111", "a"));
  for (auto tablet : tablets_) {
    AddRunningReplica(tablet.get(), ts_descs_[1]);
  }

  // Setup some new tablet servers and replicas in both the same and wrong AZs and confirm that
  // the algorithms work as expected.
  ts_descs_.push_back(SetupTS("2222", "WRONG"));
  ts_descs_.push_back(SetupTS("3333", "a"));
  ts_descs_.push_back(SetupTS("4444", "a"));
  // We'll keep this as empty, for use as a sink for tablets.
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

  // Add all tablets to the list of tablets with a pending add operation and verify that calling
  // HandleAddReplicas fails because all the tablets have a pending add operation.
  pending_add_replica_tasks_.clear();
  for (const auto& tablet : tablets_) {
    pending_add_replica_tasks_[tablet->id()] = "1111";
  }

  auto pending_tasks = ASSERT_RESULT(ResetLoadBalancerAndAnalyzeTablets());
  ASSERT_EQ(pending_tasks.adds, pending_add_replica_tasks_.size());

  std::string placeholder, tablet_id;
  ASSERT_NOK(TestAddLoad(tablet_id, placeholder, placeholder));

  // Clear pending_add_replica_tasks_ and reset the state of ClusterLoadBalancer.
  pending_add_replica_tasks_.clear();
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Check that if adding replicas, we'll notice the wrong placement and adjust it.
  std::string expected_tablet_id, expected_from_ts, expected_to_ts;
  expected_tablet_id = tablets_[2]->tablet_id();
  expected_from_ts = ts_descs_[2]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  ASSERT_OK(TestAddLoad(expected_tablet_id, expected_from_ts, expected_to_ts));

  // Add all tablets to the list of tablets with a pending remove operation and verify that
  // calling HandleRemoveReplicas fails because all the tablets have a pending remove operation.
  pending_remove_replica_tasks_.clear();
  for (const auto& tablet : tablets_) {
    pending_remove_replica_tasks_[tablet->id()] = "1111";
  }
  pending_tasks = ASSERT_RESULT(ResetLoadBalancerAndAnalyzeTablets());
  ASSERT_EQ(pending_tasks.removes, pending_remove_replica_tasks_.size());
  ASSERT_FALSE(ASSERT_RESULT(HandleRemoveReplicas(&tablet_id, &placeholder)));

  // Clear pending_remove_replica_tasks_ and reset the state of ClusterLoadBalancer.
  pending_remove_replica_tasks_.clear();
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

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
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::string placeholder, expected_from_ts, expected_to_ts;
  // Expecting that we move load from ts0 which is blacklisted, to ts5. This is because ts4 is
  // also blacklisted and ts0 has a valid placement, so we try to find a server in the same
  // placement.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  for (const auto& tablet : tablets_) {
    ASSERT_OK(TestAddLoad(tablet->tablet_id(), expected_from_ts, expected_to_ts));
  }

  // There is some opportunity to equalize load across the remaining servers also. However,
  // we cannot do so until the next run since all tablets have just been moved once.
  ASSERT_NOK(TestAddLoad(placeholder, placeholder, placeholder));

  // Move tablets off ts0 to ts5.
  for (const auto& tablet : tablets_) {
    RemoveReplica(tablet.get(), ts_descs_[0]);
    AddRunningReplica(tablet.get(), ts_descs_[5]);
  }

  // Reset the load state and recompute.
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Now that we have reinitialized for the next run, we can try to equalize load across the
  // remaining servers. Our load on non-blacklisted servers is: ts1:4, ts2:4, ts3:0, ts5:4. Of
  // this, we can only balance from ts1 to ts3, as they are in the same AZ.
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts));
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts));
  // Now we should have no more tablets we are able to move.
  ASSERT_NOK(TestAddLoad(placeholder, placeholder, placeholder));
}

TEST_F(LoadBalancerMockedTest, TestAddReplicaToTSWithPendingDelete) {
  PrepareTestStateSingleAz();
  auto underreplicated_tablet = tablets_[0];
  auto pending_delete_tablet = tablets_[1];
  auto destination_ts = ts_descs_[2];

  RemoveReplica(underreplicated_tablet.get(), destination_ts);
  MoveTabletLeader(underreplicated_tablet.get(), ts_descs_[0]);
  destination_ts->AddPendingTabletDelete(pending_delete_tablet->id());
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  std::string tablet_id, out_ts, to_ts;
  // We should still be able to add a replica to the TS with the pending delete.
  ASSERT_OK(TestAddLoad(underreplicated_tablet->id(), "", destination_ts->permanent_uuid()));
}

// It's not clear how realistic this scenario is. The load balancer has checks in the addition
// validation code for adding another tablet replica to a TS that already has that tablet in the
// RUNNING, UNKNOWN, NOT_STARTED, or BOOTSTRAPPING states.
TEST_F(LoadBalancerMockedTest, TestAddReplicaToTSWithPendingDeleteForSameTablet) {
  PrepareTestStateSingleAz();
  auto tablet = tablets_[0];
  auto destination_ts = ts_descs_[2];

  RemoveReplica(tablet.get(), destination_ts);
  destination_ts->AddPendingTabletDelete(tablet->id());
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  std::string tablet_id, out_ts, to_ts;
  auto replica_added = ASSERT_RESULT(HandleAddReplicas(&tablet_id, &out_ts, &to_ts));
  EXPECT_FALSE(replica_added);
  EXPECT_EQ(tablet_id, "");
  EXPECT_EQ(out_ts, "");
  EXPECT_EQ(to_ts, "");
}

class LoadBalancerMockedCloudInfoSimilarityTest : public LoadBalancerMockedTest,
                                                  public testing::WithParamInterface<bool> {};
INSTANTIATE_TEST_SUITE_P(, LoadBalancerMockedCloudInfoSimilarityTest, ::testing::Bool());

TEST_P(LoadBalancerMockedCloudInfoSimilarityTest, TestChooseTabletInSameZone) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_ignore_cloud_info_similarity) = GetParam();
  LOG(INFO) << Format("Testing that (if possible) we move a tablet whose leader is in the same "
                      "zone/region as the new tserver with "
                      "FLAGS_load_balancer_ignore_cloud_info_similarity=$0.",
                      FLAGS_load_balancer_ignore_cloud_info_similarity);
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
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::string placeholder, expected_from_ts, expected_to_ts, actual_tablet_id;
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts, &actual_tablet_id));

  auto moved_tablet_leader = ASSERT_RESULT(tablet_map_[actual_tablet_id]->GetLeader());
  // If ignoring cloud info, we should move a tablet whose leader is not in zone c (by the order
  // of the tablets, the first non-leader tablet we encounter is tablet 1 and we do not expect
  // to replace it). Otherwise, we should pick the tablet whose leader IS in zone c.
  if (FLAGS_load_balancer_ignore_cloud_info_similarity) {
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
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Since tablet 0 on ts0 is the leader, it won't be moved.
  std::string placeholder, expected_from_ts, expected_to_ts, actual_tablet_id1, actual_tablet_id2;
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[5]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts, &actual_tablet_id1));
  ASSERT_OK(TestAddLoad(placeholder, expected_from_ts, expected_to_ts, &actual_tablet_id2));
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
    RemoveReplica(tablet.get(), ts_descs_[2]);
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  // Remove the tablet server from the list.
  ts_descs_.pop_back();

  // Add back 1 new server in that same AZ. So we should add missing placements to this new TS.
  ts_descs_.push_back(SetupTS("1new", "c"));

  // Load up data.
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // First we'll fill up the missing placements for all 4 tablets.
  std::string placeholder;
  std::string expected_to_ts;
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));

  // Add yet 1 more server in that same AZ for some load-balancing.
  ts_descs_.push_back(SetupTS("2new", "c"));

  // Add the missing placements to the first new TS.
  AddRunningReplica(tablets_[0].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[1].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[2].get(), ts_descs_[2]);
  AddRunningReplica(tablets_[3].get(), ts_descs_[2]);

  // Reset the load state and recompute.
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Now we should be able to move 2 tablets to the second new TS.
  expected_to_ts = ts_descs_[3]->permanent_uuid();
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));
  ASSERT_OK(TestAddLoad(placeholder, placeholder, expected_to_ts));

  // And the load should now be balanced so no more move is expected.
  ASSERT_NOK(TestAddLoad(placeholder, placeholder, placeholder));
}

TEST_F(LoadBalancerMockedTest, TestBalancingLeaders) {
  LOG(INFO) << "Testing moving overloaded leaders";
  PrepareTestStateMultiAz();
  // Move all leaders to ts0.
  for (const auto& tablet : tablets_) {
    MoveTabletLeader(tablet.get(), ts_descs_[0]);
  }
  LOG(INFO) << "Leader distribution: 4 0 0";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

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

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Only 1 leader should be moved off from ts0 to ts2.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 more leader to ts1 and blacklist ts0
  MoveTabletLeader(tablets_[1].get(), ts_descs_[1]);
  blacklist_.add_hosts()->set_host(ts_descs_[0]->permanent_uuid());
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist: ts0";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // With ts0 blacklisted, the 2 leaders on ts0 should be moved to some undetermined servers.
  // ts1 still has 2 leaders and ts2 has 0 so 1 leader should be moved to ts2.
  ASSERT_NOK(TestAddLoad(placeholder, placeholder, placeholder));
  ASSERT_FALSE(ASSERT_RESULT(HandleRemoveReplicas(&placeholder, &placeholder)));
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Clear the blacklist.
  blacklist_.Clear();
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist cleared.";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Only 1 tablets should be moved off from ts1 to ts2.
  expected_from_ts = ts_descs_[1]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader from ts1 to ts2.
  MoveTabletLeader(tablets_[1].get(), ts_descs_[2]);
  LOG(INFO) << "Leader distribution: 2 1 1";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // The distribution is as balanced as it can be so there shouldn't be any move.
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));
}

TEST_F(LoadBalancerMockedTest, TestMissingPlacementSingleAz) {
  LOG(INFO) << "Testing single az deployment where min_num_replicas different from num_replicas";
  PrepareTestStateSingleAz();
  // Setup cluster level placement to single AZ.
  SetupClusterConfig({"a"}, &replication_info_);

  // Under-replicate tablets 0, 1, 2.
  for (int i = 0; i < 3; i++) {
    RemoveReplica(tablets_[i].get(), ts_descs_[i]);
    // Ensure the tablet has a leader.
    MoveTabletLeader(tablets_[i].get(), ts_descs_[(i + 1) % 3]);
  }

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::string placeholder, expected_tablet_id, expected_from_ts, expected_to_ts;

  // Make sure a replica is added for tablet 0 to ts 0.
  expected_tablet_id = tablets_[0]->tablet_id();
  expected_to_ts = ts_descs_[0]->permanent_uuid();

  ASSERT_OK(TestAddLoad(expected_tablet_id, placeholder, expected_to_ts));

  // Make sure a replica is added for tablet 1 to ts 1.
  expected_tablet_id = tablets_[1]->tablet_id();
  expected_to_ts = ts_descs_[1]->permanent_uuid();

  ASSERT_OK(TestAddLoad(expected_tablet_id, placeholder, expected_to_ts));

  // Make sure a replica is added for tablet 2 to ts 2.
  expected_tablet_id = tablets_[2]->tablet_id();
  expected_to_ts = ts_descs_[2]->permanent_uuid();

  ASSERT_OK(TestAddLoad(expected_tablet_id, placeholder, expected_to_ts));

  // Everything is normal now, load balancer shouldn't do anything.
  ASSERT_NOK(TestAddLoad(placeholder, placeholder, placeholder));
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

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

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

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Only 1 leader should be moved off from ts0 to ts2.
  expected_from_ts = ts_descs_[0]->permanent_uuid();
  expected_to_ts = ts_descs_[2]->permanent_uuid();
  TestMoveLeader(&placeholder, expected_from_ts, expected_to_ts);
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 more leader to ts1 and blacklist ts0
  MoveTabletLeader(tablets_[1].get(), ts_descs_[1]);
  blacklist_.add_hosts()->set_host(ts_descs_[0]->permanent_uuid());
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist: ts0";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Clear the blacklist.
  blacklist_.Clear();
  LOG(INFO) << "Leader distribution: 2 2 0. Blacklist cleared.";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Again all tablet servers have leaders below threshold so no move is expected.
  ASSERT_FALSE(ASSERT_RESULT(HandleLeaderMoves(&placeholder, &placeholder, &placeholder)));

  // Move 1 leader from ts1 to ts2.
  MoveTabletLeader(tablets_[1].get(), ts_descs_[2]);
  LOG(INFO) << "Leader distribution: 2 1 1";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

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
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Ensure the tablet is picked.
  TestRemoveLoad(tablets_[0]->tablet_id(), "");
}

TEST_F(LoadBalancerMockedTest, LimitRbsPerTserver) {
  GetOptions()->kMaxInboundRemoteBootstrapsPerTs = 1;
  PrepareTestStateMultiAz();
  // Remove all replicas from ts2.
  for (const auto& tablet : tablets_) {
    RemoveReplica(tablet.get(), ts_descs_[2]);
  }

  // Add another tserver in zone c.
  ts_descs_.push_back(SetupTS("3333", "c"));

  // Load balancer should add a replica to ts2, then ts3, then stop because of the inbound RBS
  // limit.
  std::string tablet_id, from_ts, to_ts;
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  ASSERT_OK(TestAddLoad("", "", ts_descs_[2]->permanent_uuid()));
  ASSERT_OK(TestAddLoad("", "", ts_descs_[3]->permanent_uuid()));
  ASSERT_EQ(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)), false);
}

TEST_F(LoadBalancerMockedTest, ExcludeOverreplicationInSourceLoad) {
  PrepareTestStateMultiAz();
  // Add another tserver in zone c.
  const auto& new_ts = ts_descs_.emplace_back(SetupTS("3333", "c"));
  for (int i = 0; i < 2; ++i) {
    AddRunningReplica(tablets_[i].get(), new_ts);
  }

  // Should not find a replica to move because, after accounting for over-replicated replicas we
  // might remove, we have only 2 tablets on ts2 (and 2 on ts3).
  std::string tablet_id, from_ts, to_ts;
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  ASSERT_EQ(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)), false);
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

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Leader blacklist ts2
  AddLeaderBlacklist(ts_descs_[2]->permanent_uuid());
  LOG(INFO) << "Leader distribution: 2 1 1. Leader Blacklist: ts2";

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

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

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

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

class LoadBalancerMockedTestManyTablets : public LoadBalancerMockedTest {
 protected:
  int NumTablets() const override { return 40; }
};

TEST_F(LoadBalancerMockedTestManyTablets, SizeAwareBalancing) {
  google::SetVLOGLevel("cluster_balance", 4);
  google::SetVLOGLevel("cluster_balance_util", 4);
  PrepareTestStateSingleAz();
  auto new_ts = ts_descs_.emplace_back(SetupTS("3333", "a"));

  // We should schedule two tablet moves because we want 100 MB of tablet movement and each tablet
  // is 40 MB. The minimum number of remote bootstraps is set to 1 so that we are constrained by
  // the data limit.
  GetOptions()->kMaxInboundBytesPerTs = 100_MB;
  GetOptions()->kMinInboundRemoteBootstrapsPerTs = 1;
  for (auto& tablet : tablets_) {
    SetTabletReplicaSizes(tablet, 40_MB);
  }
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());
  std::string tablet_id, from_ts, to_ts;

  // Should be able to schedule exactly two moves.
  ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, new_ts->permanent_uuid());
  ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, new_ts->permanent_uuid());
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));

  // If we bump the data limit to 120 MB, we should be able to add one more tablet.
  GetOptions()->kMaxInboundBytesPerTs = 120_MB;
  ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, new_ts->permanent_uuid());
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
}

TEST_F(LoadBalancerMockedTestManyTablets, SizeAwareBalancingMinRbs) {
  PrepareTestStateSingleAz();
  auto new_ts = ts_descs_.emplace_back(SetupTS("3333", "a"));
  for (auto& tablet : tablets_) {
    SetTabletReplicaSizes(tablet, 101_MB);
  }
  GetOptions()->kMaxInboundBytesPerTs = 100_MB;
  GetOptions()->kMinInboundRemoteBootstrapsPerTs = 2;
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Even though each tablet is larger than the inbound bytes limit, we should still add replicas
  // until we have the minimum number of remote bootstraps.
  std::string tablet_id, from_ts, to_ts;
  ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, new_ts->permanent_uuid());
  ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, new_ts->permanent_uuid());
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
}

TEST_F(LoadBalancerMockedTestManyTablets, SizeAwareBalancingNoSizeData) {
  PrepareTestStateSingleAz();
  auto new_ts = ts_descs_.emplace_back(SetupTS("3333", "a"));
  GetOptions()->kMaxInboundBytesPerTs = 100_MB;
  GetOptions()->kMinInboundRemoteBootstrapsPerTs = 1;
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // Should only schedule the minimum number of moves because we conservatively estimate the size of
  // a tablet with no data to be 1 GB.
  std::string tablet_id, from_ts, to_ts;
  ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
  ASSERT_EQ(to_ts, new_ts->permanent_uuid());
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
}

TEST_F(LoadBalancerMockedTestManyTablets, SizeAwareBalancingManyTablets) {
  PrepareTestStateSingleAz();
  auto new_ts = ts_descs_.emplace_back(SetupTS("3333", "a"));
  for (auto& tablet : tablets_) {
    SetTabletReplicaSizes(tablet, 1_MB);
  }
  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  // With default flags and small tablet sizes, we should be able to move 20 tablets in the first
  // run. At the 21st tablet, the accounting for over-replication will prevent further moves
  // because we account for the over-replication on all tservers since we don't know which we will
  // remove from: 40 tablets - 20 over-replicated tablets = 20 tablets of load on the source.
  std::string tablet_id, from_ts, to_ts;
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
    ASSERT_EQ(to_ts, new_ts->permanent_uuid());
  }
  // If we start tracking over-replication on just one tserver, we should remove the following line
  // and assert that we can move all 30 tablets in one run.
  ASSERT_FALSE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
}

class LoadBalancerRF5MockedTest : public LoadBalancerMockedTest {
  int NumReplicas() const override { return 5; }
};

TEST_F(LoadBalancerRF5MockedTest, TestUnderReplicatedPriority) {
  // Test that we prioritize more under-replicated tablets when adding replicas.
  PrepareTestStateMultiAz();
  // Setup cluster level placement to multiple AZs.
  SetupClusterConfig({"a", "b", "c", "d", "e"}, &replication_info_, /*rf*/ 5);

  auto leader_ts = ts_descs_[0];
  auto underreplicated_by_1_tablet = tablets_[0];
  auto underreplicated_by_2_tablet = tablets_[1];
  auto az_d_ts = ts_descs_[3];
  auto az_e_ts = ts_descs_[4];

  // Remove one replica from e for the first tablet.
  RemoveReplica(underreplicated_by_1_tablet.get(), az_e_ts);
  // Remove 2 replicas from e and d for the second tablet.
  RemoveReplica(underreplicated_by_2_tablet.get(), az_d_ts);
  RemoveReplica(underreplicated_by_2_tablet.get(), az_e_ts);

  ASSERT_OK(ResetLoadBalancerAndAnalyzeTablets());

  std::string placeholder;
  std::string expected_tablet, expected_to_ts;
  // First should move the handle the most underreplicated tablet.
  // Will move to the e node since it has the least load and is underreplicated.
  expected_tablet = underreplicated_by_2_tablet->tablet_id();
  expected_to_ts = az_e_ts->permanent_uuid();
  ASSERT_OK(TestAddLoad(expected_tablet, placeholder, expected_to_ts));

  // Next we move the next most underreplicated tablet.
  // Will move to the e node since it is underreplicated.
  expected_tablet = underreplicated_by_1_tablet->tablet_id();
  expected_to_ts = az_e_ts->permanent_uuid();
  ASSERT_OK(TestAddLoad(expected_tablet, placeholder, expected_to_ts));

  // Shouldn't perform any more moves (can't move a tablet multiple times in one run).
  ASSERT_FALSE(ASSERT_RESULT(HandleOneAddIfMissingPlacement(placeholder, placeholder)));
  ASSERT_NOK(TestAddLoad(placeholder, placeholder, placeholder));
}

class OptimalLoadDistributionTest : public YBTest {
 protected:
  Status AssertLoadDistribution(
      const std::vector<std::shared_ptr<TSDescriptor>>& ts_descs,
      const TsTableLoadMap& load_map, const std::vector<int>& expected_load) {
    if (expected_load.size() != ts_descs.size()) {
      return STATUS(InvalidArgument, "Expected load size doesn't match ts_descs_ size");
    }
    for (size_t i = 0; i < ts_descs.size(); i++) {
      SCHECK_EQ(load_map.at(ts_descs[i]->permanent_uuid()),
                expected_load[i], IllegalState,
                Format("Load for TS $0 does not match", ts_descs[i]->permanent_uuid()));
    }
    return Status::OK();
  }

  std::vector<std::shared_ptr<TSDescriptor>> SetupTservers(int rf) {
    std::vector<std::shared_ptr<TSDescriptor>> ts_descs;
    for (int i = 0; i < rf; ++i) {
      // Create TS with uuid 0000, 1111, etc and AZ a, b, etc.
      ts_descs.push_back(SetupTS(std::string(4, '0' + i), std::string(1, 'a' + i)));
    }
    return ts_descs;
  }

  ReplicationInfoPB GetReplicationInfo(std::vector<std::string> azs) {
    ReplicationInfoPB replication_info;
    SetupClusterConfig(azs, &replication_info);
    return replication_info;
  }
};

TEST_F(OptimalLoadDistributionTest, EvenDistribution) {
  auto ts_descs = SetupTservers(3);
  auto replication_info = GetReplicationInfo({"a", "b", "c"});
  TsTableLoadMap current_load = {};
  auto map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 1));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {1, 1, 1}));
  // Same test with more tablets.
  map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 3));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {3, 3, 3}));
}

TEST_F(OptimalLoadDistributionTest, UnevenTserverCount) {
  auto ts_descs = SetupTservers(3);
  auto replication_info = GetReplicationInfo({"a", "b", "c"});
  TsTableLoadMap current_load = {};
  // Add a new TS in AZ "c" and check the optimal load distribution.
  ts_descs.push_back(SetupTS("3333", "c"));
  auto map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 2 /* num_tablets */));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {2, 2, 1, 1}));
}

TEST_F(OptimalLoadDistributionTest, BreakTiesByCurrentLoad) {
  // With 3 tablets in each zone and two tservers in zone c, we should prefer to put the extra
  // tablet in zone c on the TS that already has more replicas.
  auto ts_descs = SetupTservers(3);
  ts_descs.push_back(SetupTS("3333", "c"));
  auto replication_info = GetReplicationInfo({"a", "b", "c"});
  TsTableLoadMap current_load = {};

  // Give the second tserver in zone c an extra replica to start.
  current_load[ts_descs[2]->permanent_uuid()] = 1;
  current_load[ts_descs[3]->permanent_uuid()] = 2;
  auto map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 3 /* num_tablets */));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {3, 3, 1, 2}));

  // Give the first tserver in zone c more load and the optimal distribution should change (to
  // minimize moves).
  current_load[ts_descs[2]->permanent_uuid()] = 2;
  current_load[ts_descs[3]->permanent_uuid()] = 1;
  map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 3 /* num_tablets */));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {3, 3, 2, 1}));
}

TEST_F(OptimalLoadDistributionTest, NotEnoughTservers) {
  auto ts_descs = SetupTservers(3);
  auto replication_info = GetReplicationInfo({"a", "b", "c"});
  TsTableLoadMap current_load = {};

  // No tservers to host the tablet.
  auto status = CalculateOptimalLoadDistribution(
      {}, replication_info.live_replicas(), current_load, 1);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(
      status.ToString(), "Not enough tservers to host the required number of replicas "
      "(have 0, need 3)");

  // There are enough tservers globally, but not enough in zone c.
  ts_descs.push_back(SetupTS("3333", "a"));
  status = CalculateOptimalLoadDistribution(
      {ts_descs[0], ts_descs[1], ts_descs[3]}, replication_info.live_replicas(), current_load,
      1);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(
      status.ToString(),
      "Not enough tservers to host the minimum number of replicas in placement block");
}

TEST_F(OptimalLoadDistributionTest, Slack) {
  // Set up replication state with slack.
  auto ts_descs = SetupTservers(3);
  auto replication_info = GetReplicationInfo({"a", "b", "c"});
  replication_info.mutable_live_replicas()->mutable_placement_blocks(0)->set_min_num_replicas(0);

  // Zone a should take all the slack load.
  TsTableLoadMap current_load = {};
  auto map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 5));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {5, 5, 5}));

  // With more tservers in zone b, zones a and b should share the slack load.
  ts_descs.push_back(SetupTS("3333", "b"));
  map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 6));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {4, 4, 6, 4}));
}

TEST_F(OptimalLoadDistributionTest, SlackManyTservers) {
  // Create a replication info with min_num_replicas = 1 in 2 zones, leaving one slack replica.
  auto replication_info = GetReplicationInfo({"a", "b"});
  replication_info.mutable_live_replicas()->set_num_replicas(3);

  // Create 5 tservers in zone a and 2 in zone b. Placing the slack replica in either zone should
  // still be optimal even though adding to zone a results in lower average load (0.4 and 0.5
  // tablets per tserver vs 0.2 and 1).
  std::vector<std::shared_ptr<TSDescriptor>> ts_descs;
  for (int i = 0; i < 5; i++) {
    ts_descs.push_back(SetupTS(Format("zone_a_ts$0", i), "a"));
  }
  // Add two tservers to zone b so zone b can host the slack replica too.
  ts_descs.push_back(SetupTS("zone_b_ts0", "b"));
  ts_descs.push_back(SetupTS("zone_b_ts1", "b"));

  // Place the slack replica in zone a. This should be optimal.
  TsTableLoadMap current_load = {
    {ts_descs[0]->permanent_uuid(), 1},
    {ts_descs[1]->permanent_uuid(), 1},
    {ts_descs[5]->permanent_uuid(), 1},
  };
  auto map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 1));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {1, 1, 0, 0, 0, 1, 0}));

  // Placing the slack replica in zone b should also be optimal.
  current_load = {
    {ts_descs[0]->permanent_uuid(), 1},
    {ts_descs[5]->permanent_uuid(), 1},
    {ts_descs[6]->permanent_uuid(), 1},
  };
  map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 1));
  ASSERT_OK(AssertLoadDistribution(ts_descs, map, {1, 0, 0, 0, 0, 1, 1}));
}

TEST_F(OptimalLoadDistributionTest, SlackRf5) {
  // RF5 placement with 1 replica in zone a and b, 2 in zone c, and 1 slack replica.
  auto replication_info = GetReplicationInfo({"a", "b", "c"});
  replication_info.mutable_live_replicas()->set_num_replicas(5);
  replication_info.mutable_live_replicas()->mutable_placement_blocks(0)->set_min_num_replicas(1);
  replication_info.mutable_live_replicas()->mutable_placement_blocks(1)->set_min_num_replicas(1);
  replication_info.mutable_live_replicas()->mutable_placement_blocks(2)->set_min_num_replicas(2);

  // 5 tservers in each zone.
  std::vector<std::shared_ptr<TSDescriptor>> ts_descs;
  for (int i = 0; i < 5; i++) { ts_descs.push_back(SetupTS(Format("zone_a_ts$0", i), "a")); }
  for (int i = 0; i < 5; i++) { ts_descs.push_back(SetupTS(Format("zone_b_ts$0", i), "b")); }
  for (int i = 0; i < 5; i++) { ts_descs.push_back(SetupTS(Format("zone_c_ts$0", i), "c")); }

  // The slack should get spread across zones a and b.
  TsTableLoadMap current_load = {};
  auto map = ASSERT_RESULT(CalculateOptimalLoadDistribution(
      ts_descs, replication_info.live_replicas(), current_load, 30));
  for (int i = 0; i < 10; i++) { ASSERT_EQ(map.at(ts_descs[i]->permanent_uuid()), 9); }
  for (int i = 10; i < 15; i++) { ASSERT_EQ(map.at(ts_descs[i]->permanent_uuid()), 12); }
}

TEST_F(OptimalLoadDistributionTest, CalculateTableLoadDifference) {
  // Test with no difference.
  TsTableLoadMap current_load = {{"ts0", 1}, {"ts1", 1}, {"ts2", 1}};
  TsTableLoadMap goal_load =    {{"ts0", 1}, {"ts1", 1}, {"ts2", 1}};
  ASSERT_EQ(CalculateTableLoadDifference(current_load, goal_load), 0);

  // Test with some removes and adds. Only the adds should be reported.
  current_load = {{"ts0", 1}, {"ts1", 2}, {"ts2", 1}};
  goal_load =    {{"ts0", 1}, {"ts1", 1}, {"ts2", 2}};
  ASSERT_EQ(CalculateTableLoadDifference(current_load, goal_load), 1);

  // Missing tservers in either map should count as 0 load.
  current_load = {{"ts0", 1}, {"ts1", 1}};
  goal_load =    {{"ts1", 1}, {"ts2", 1}};
  ASSERT_EQ(CalculateTableLoadDifference(current_load, goal_load), 1);
}

} // namespace master
} // namespace yb
