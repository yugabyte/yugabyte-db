// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#include <gtest/gtest.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/ts_descriptor.h"
#include "yb/util/test_util.h"

namespace yb {
namespace master {

using strings::Substitute;
using std::shared_ptr;
using std::make_shared;

void CreateTable(
    const vector<string> split_keys, const int num_replicas, TableInfo* table,
    vector<scoped_refptr<TabletInfo>>* tablets) {
  const int kNumSplits = split_keys.size();
  for (int i = 0; i <= kNumSplits; i++) {
    const string& start_key = (i == 0) ? "" : split_keys[i - 1];
    const string& end_key = (i == kNumSplits) ? "" : split_keys[i];
    string tablet_id = Substitute("tablet-$0-$1", start_key, end_key);

    TabletInfo* tablet = new TabletInfo(table, tablet_id);
    TabletMetadataLock meta_lock(tablet, TabletMetadataLock::WRITE);

    PartitionPB* partition = meta_lock.mutable_data()->pb.mutable_partition();
    partition->set_partition_key_start(start_key);
    partition->set_partition_key_end(end_key);
    meta_lock.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);

    table->AddTablet(tablet);
    meta_lock.Commit();
    tablets->push_back(make_scoped_refptr(tablet));
  }

  TableMetadataLock meta_lock(table, TableMetadataLock::WRITE);
  meta_lock.mutable_data()->pb.mutable_placement_info()->set_num_replicas(num_replicas);
  meta_lock.Commit();

  // The splits are of the form ("-a", "a-b", "b-c", "c-"), hence the +1.
  ASSERT_EQ(tablets->size(), split_keys.size() + 1);
}

void NewReplica(
    TSDescriptor* ts_desc, tablet::TabletStatePB state, bool is_leader, TabletReplica* replica) {
  replica->ts_desc = ts_desc;
  replica->state = state;
  if (is_leader) {
    replica->role = consensus::RaftPeerPB::LEADER;
  } else {
    replica->role = consensus::RaftPeerPB::FOLLOWER;
  }
}

class TestLoadBalancer : public ClusterLoadBalancer {
 public:
  typedef unordered_map<string, scoped_refptr<TabletInfo>> TabletInfoMap;
  TestLoadBalancer(string table_id) : ClusterLoadBalancer(nullptr) {
    // Create the table.
    scoped_refptr<TableInfo> table(new TableInfo(table_id));
    vector<scoped_refptr<TabletInfo>> tablets;

    // Generate 12 tablets total: 4 splits and 3 replicas each.
    vector<string> splits = {"a", "b", "c"};
    const int num_replicas = 3;
    total_num_tablets_ = num_replicas * (splits.size() + 1);

    CreateTable(splits, num_replicas, table.get(), &tablets);

    tablets_ = std::move(tablets);

    const int kHighNumber = 100;
    options_.kMaxConcurrentAdds = kHighNumber;
    options_.kMaxConcurrentRemovals = kHighNumber;
    options_.kAllowLimitStartingTablets = false;
    options_.kAllowLimitOverReplicatedTablets = false;
  }

  void TestAlgorithm() {
    // Assign them initially only to the first three TSs.
    shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a");
    shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "b");
    shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "c");

    TSDescriptorVector ts_descs = {ts0, ts1, ts2};

    PrepareTestState(ts_descs);
    TestNoPlacement();

    PrepareTestState(ts_descs);
    TestWithPlacement();

    PrepareTestState(ts_descs);
    TestWithMissingPlacement();

    PrepareTestState(ts_descs);
    TestOverReplication();

    PrepareTestState(ts_descs);
    TestWithBlacklist();
  }

 protected:
  void TestWithBlacklist() {
    LOG(INFO) << "Testing with tablet servers with blacklist";
    // Setup cluster config.
    SetupClusterConfig(/* multi_az = */ true);

    // Blacklist the first host in AZ "a".
    blacklist_.add_hosts()->set_host(ts_descs_[0]->permanent_uuid());

    // Add two tablet servers in AZ "a" and one in AZ "b".
    ts_descs_.push_back(SetupTS("3333", "b"));
    ts_descs_.push_back(SetupTS("4444", "a"));
    ts_descs_.push_back(SetupTS("5555", "a"));

    // Blacklist the first new tablet server in AZ "a" so we show it isn't picked.
    blacklist_.add_hosts()->set_host(ts_descs_[4]->permanent_uuid());

    // Prepare the data.
    AnalyzeTablets();

    string placeholder, expected_from_ts, expected_to_ts;
    // Expecting that we move load from ts0 which is blacklisted, to ts5. This is because ts4 is
    // also blacklisted and ts0 has a valid placement, so we try to find a server in the same
    // placement.
    expected_from_ts = ts_descs_[0]->permanent_uuid();
    expected_to_ts = ts_descs_[5]->permanent_uuid();
    for (const auto& tablet : tablets_) {
      TestAddLoad(placeholder, expected_from_ts, expected_to_ts);
    }
    // Now that we have moved off of the blacklisted ts0, we can try to equalize load across the
    // remaining servers. Our load on non-blacklisted servers is: ts1:4, ts2:4, ts3:0, ts5:4. Of
    // this, we can only balance from ts1 to ts3, as they are in the same AZ.
    expected_from_ts = ts_descs_[1]->permanent_uuid();
    expected_to_ts = ts_descs_[3]->permanent_uuid();
    TestAddLoad(placeholder, expected_from_ts, expected_to_ts);
    TestAddLoad(placeholder, expected_from_ts, expected_to_ts);
    // Now we should have no more tablets we are able to move.
    ASSERT_FALSE(HandleAddReplicas(&placeholder, &placeholder, &placeholder));
  }

  void TestOverReplication() {
    LOG(INFO) << "Testing with tablet servers with over-replication";
    // Setup cluster config.
    SetupClusterConfig(/* multi_az = */ false);

    // Remove the 2 tablet peers that are wrongly placed and assign a new one that is properly
    // placed.
    for (auto tablet : tablets_) {
      TabletInfo::ReplicaMap replica_map;
      tablet->GetReplicaLocations(&replica_map);
      replica_map.erase(ts_descs_[1]->permanent_uuid());
      replica_map.erase(ts_descs_[2]->permanent_uuid());
      tablet->SetReplicaLocations(replica_map);
    }
    // Remove the two wrong tablet servers from the list.
    ts_descs_.pop_back();
    ts_descs_.pop_back();
    // Add a tablet server with proper placement and peer it to all tablets. Now all tablets
    // should have 2 peers.
    ts_descs_.push_back(SetupTS("1new", "a"));
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

    AnalyzeTablets();

    // Check that if adding replicas, we'll notice the wrong placement and adjust it.
    string expected_tablet_id, expected_from_ts, expected_to_ts;
    expected_tablet_id = tablets_[2]->tablet_id();
    expected_from_ts = ts_descs_[2]->permanent_uuid();
    expected_to_ts = ts_descs_[5]->permanent_uuid();
    TestAddLoad(expected_tablet_id, expected_from_ts, expected_to_ts);

    // Check that removing replicas, we take out the wrong placement one first.
    expected_tablet_id = tablets_[0]->tablet_id();
    expected_from_ts = ts_descs_[2]->permanent_uuid();
    TestRemoveLoad(expected_tablet_id, expected_from_ts);
    // Check that trying to remove another replica, will take out one from the last over-replicated
    // set. Both ts0 and ts1 are on the same load, so we'll pick the highest uuid one to remove.
    expected_tablet_id = tablets_[1]->tablet_id();
    expected_from_ts = ts_descs_[1]->permanent_uuid();
    TestRemoveLoad(expected_tablet_id, expected_from_ts);
    // Check that trying to remove another replica will fail, as we have no more over-replication.
    string placeholder;
    ASSERT_FALSE(HandleRemoveReplicas(&placeholder, &placeholder));
  }

  void TestWithMissingPlacement() {
    LOG(INFO) << "Testing with tablet servers missing placement information";
    // Setup cluster level placement to multiple AZs.
    SetupClusterConfig(/* multi_az = */ true);

    // Remove the only tablet peer from AZ "c".
    for (const auto tablet : tablets_) {
      TabletInfo::ReplicaMap replica_map;
      tablet->GetReplicaLocations(&replica_map);
      replica_map.erase(ts_descs_[2]->permanent_uuid());
      tablet->SetReplicaLocations(replica_map);
    }
    // Remove the tablet server from the list.
    ts_descs_.pop_back();

    // Add some more servers in that same AZ, so we get to pick among them.
    ts_descs_.push_back(SetupTS("2new", "c"));
    ts_descs_.push_back(SetupTS("3new", "c"));

    // Load up data.
    AnalyzeTablets();

    // First we'll fill up the missing placements for all 4 tablets.
    string placeholder;
    string expected_to_ts, expected_from_ts;
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
    ASSERT_FALSE(HandleAddReplicas(&placeholder, &placeholder, &placeholder));
  }

  void TestWithPlacement() {
    LOG(INFO) << "Testing with placement information";
    // Setup cluster level placement to the same 3 AZs as our tablet servers.
    SetupClusterConfig(/* multi_az = */ true);

    // Add three TSs, one in wrong AZ, two in right AZs.
    ts_descs_.push_back(SetupTS("3333", "WRONG"));
    ts_descs_.push_back(SetupTS("4444", "a"));
    ts_descs_.push_back(SetupTS("5555", "a"));

    // Analyze the tablets into the internal state.
    AnalyzeTablets();

    // Check some base expectations for balanced cluster.
    ASSERT_EQ(0, get_total_over_replication());
    ASSERT_EQ(0, get_total_starting_tablets());
    ASSERT_EQ(total_num_tablets_, get_total_running_tablets());

    // Equal load across the first three TSs, but placement dictates we can only move from ts0 to
    // ts4 or ts5. We should pick the lowest uuid one, which is ts4.
    string placeholder;
    string expected_from_ts = ts_descs_[0]->permanent_uuid();
    string expected_to_ts = ts_descs_[4]->permanent_uuid();
    TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

    // Recompute and expect to move to next least loaded TS, which matches placement, which should
    // be ts5 now. Load should still move from the only TS in the correct placement, which is ts0.
    expected_from_ts = ts_descs_[0]->permanent_uuid();
    expected_to_ts = ts_descs_[5]->permanent_uuid();
    TestAddLoad(placeholder, expected_from_ts, expected_to_ts);
  }

  void TestNoPlacement() {
    LOG(INFO) << "Testing with no placement information";
    // Analyze the tablets into the internal state.
    AnalyzeTablets();

    // Check some base expectations for balanced cluster.
    ASSERT_EQ(0, get_total_over_replication());
    ASSERT_EQ(0, get_total_starting_tablets());
    ASSERT_EQ(total_num_tablets_, get_total_running_tablets());

    // Add the fourth TS in there, set it in the same az as ts0.
    ts_descs_.push_back(SetupTS("3333", "a"));

    // Reset the load state and recompute.
    ResetState();
    AnalyzeTablets();

    string placeholder;
    // Lowest load should be the last, empty TS.
    string expected_to_ts = ts_descs_[3]->permanent_uuid();
    // Equal load across the first three TSs. Picking the one with largest ID in string compare.
    string expected_from_ts = ts_descs_[2]->permanent_uuid();
    TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

    // Perform another round on the updated in-memory load. The move should have made ts2 less
    // loaded, so next tablet should come from ts1 to ts3.
    expected_from_ts = ts_descs_[1]->permanent_uuid();
    TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

    // One more round, finally expecting to move from ts0 to ts3.
    expected_from_ts = ts_descs_[0]->permanent_uuid();
    TestAddLoad(placeholder, expected_from_ts, expected_to_ts);

    // Final check on in-memory state after in-memory moves.
    ASSERT_EQ(total_num_tablets_ - 3, get_total_running_tablets());
    ASSERT_EQ(3, get_total_starting_tablets());
    ASSERT_EQ(0, get_total_over_replication());
  }

  // Methods to prepare the state of the current test.
 protected:
  void PrepareTestState(const TSDescriptorVector& ts_descs) {
    // Clear old state.
    ResetState();
    cluster_placement_.Clear();
    blacklist_.Clear();
    tablet_map_.clear();
    ts_descs_.clear();

    // Set TS desc.
    ts_descs_ = ts_descs;

    // Reset the tablet map tablets.
    for (const auto tablet : tablets_) {
      tablet_map_[tablet->tablet_id()] = tablet;
    }

    // Prepare the replicas.
    tablet::TabletStatePB state = tablet::RUNNING;
    for (int i = 0; i < tablets_.size(); ++i) {
      TabletInfo::ReplicaMap replica_map;
      for (int j = 0; j < ts_descs_.size(); ++j) {
        TabletReplica replica;
        auto ts_desc = ts_descs_[j];
        bool is_leader = i % ts_descs_.size() == j;
        NewReplica(ts_desc.get(), state, is_leader, &replica);
        InsertOrDie(&replica_map, ts_desc->permanent_uuid(), replica);
      }
      // Set the replica locations directly into the tablet map.
      tablet_map_[tablets_[i]->tablet_id()]->SetReplicaLocations(replica_map);
    }
  }

  shared_ptr<TSDescriptor> SetupTS(const string& uuid, const string& az) {
    NodeInstancePB node;
    node.set_permanent_uuid(uuid);

    TSRegistrationPB reg;
    // Fake host:port combo, with uuid as host, for ease of testing.
    auto hp = reg.mutable_common()->add_rpc_addresses();
    hp->set_host(uuid);
    // Same cloud info as cluster config, with modifyable AZ.
    auto ci = reg.mutable_common()->mutable_cloud_info();
    ci->set_placement_cloud("aws");
    ci->set_placement_region("us-west-1");
    ci->set_placement_zone(az);

    shared_ptr<TSDescriptor> ts(new TSDescriptor(node.permanent_uuid()));
    ts->Register(node, reg);
    return ts;
  }

  void SetupClusterConfig(bool multi_az) {
    cluster_placement_.set_num_replicas(3);
    auto pb = cluster_placement_.add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud("aws");
    pb->mutable_cloud_info()->set_placement_region("us-west-1");
    pb->mutable_cloud_info()->set_placement_zone("a");
    pb->set_min_num_replicas(1);

    if (multi_az) {
      pb = cluster_placement_.add_placement_blocks();
      pb->mutable_cloud_info()->set_placement_cloud("aws");
      pb->mutable_cloud_info()->set_placement_region("us-west-1");
      pb->mutable_cloud_info()->set_placement_zone("b");
      pb->set_min_num_replicas(1);

      pb = cluster_placement_.add_placement_blocks();
      pb->mutable_cloud_info()->set_placement_cloud("aws");
      pb->mutable_cloud_info()->set_placement_region("us-west-1");
      pb->mutable_cloud_info()->set_placement_zone("c");
      pb->set_min_num_replicas(1);
    }
  }

  // Tester methods that actually do the calls and asserts.
 protected:
  void TestRemoveLoad(const string& expected_tablet_id, const string& expected_from_ts) {
    string tablet_id, from_ts;
    ASSERT_TRUE(HandleRemoveReplicas(&tablet_id, &from_ts));
    if (!expected_tablet_id.empty()) {
      ASSERT_EQ(expected_tablet_id, tablet_id);
    }
    if (!expected_from_ts.empty()) {
      ASSERT_EQ(expected_from_ts, from_ts);
    }
  }

  void TestAddLoad(
      const string& expected_tablet_id, const string& expected_from_ts,
      const string& expected_to_ts) {
    string tablet_id, from_ts, to_ts;
    ASSERT_TRUE(HandleAddReplicas(&tablet_id, &from_ts, &to_ts));
    if (!expected_tablet_id.empty()) {
      ASSERT_EQ(expected_tablet_id, tablet_id);
    }
    if (!expected_from_ts.empty()) {
      ASSERT_EQ(expected_from_ts, from_ts);
    }
    if (!expected_to_ts.empty()) {
      ASSERT_EQ(expected_to_ts, to_ts);
    }
  }

  // Overrides for base class functionality to bypass calling CatalogManager.
 protected:
  void GetAllLiveDescriptors(TSDescriptorVector* ts_descs) const OVERRIDE { *ts_descs = ts_descs_; }

  const TabletInfoMap& GetTabletMap() const OVERRIDE { return tablet_map_; }

  const PlacementInfoPB& GetClusterPlacementInfo() const OVERRIDE { return cluster_placement_; }

  const BlacklistPB& GetServerBlacklist() const OVERRIDE { return blacklist_; }

  void AddRunningReplica(TabletInfo* tablet, shared_ptr<TSDescriptor> ts_desc) {
    TabletInfo::ReplicaMap replicas;
    tablet->GetReplicaLocations(&replicas);

    TabletReplica replica;
    NewReplica(ts_desc.get(), tablet::TabletStatePB::RUNNING, false, &replica);
    InsertOrDie(&replicas, ts_desc->permanent_uuid(), replica);
    tablet->SetReplicaLocations(replicas);
  }

  void SendReplicaChanges(
      scoped_refptr<TabletInfo> tablet, const string& ts_uuid, const bool is_add,
      const bool stepdown_if_leader) OVERRIDE {
    // Do nothing.
  }

 private:
  int total_num_tablets_;
  vector<scoped_refptr<TabletInfo>> tablets_;
  BlacklistPB blacklist_;

  TSDescriptorVector ts_descs_;
  TabletInfoMap tablet_map_;
  PlacementInfoPB cluster_placement_;
};

// Test of the tablet assignment algorithm for splits done at table creation time.
// This tests that when we define a split, the tablet lands on the expected
// side of the split, i.e. it's a closed interval on the start key and an open
// interval on the end key (non-inclusive).
TEST(TableInfoTest, TestAssignmentRanges) {
  const string table_id = CURRENT_TEST_NAME();
  scoped_refptr<TableInfo> table(new TableInfo(table_id));
  vector<scoped_refptr<TabletInfo>> tablets;

  // Define & create the splits.
  vector<string> split_keys = {"a", "b", "c"}; // The keys we split on.
  const int kNumSplits = split_keys.size();
  const int kNumReplicas = 1;

  CreateTable(split_keys, kNumReplicas, table.get(), &tablets);

  {
    TableMetadataLock meta_lock(table.get(), TableMetadataLock::READ);
    ASSERT_EQ(meta_lock.data().pb.placement_info().num_replicas(), kNumReplicas)
        << "Invalid replicas for created table.";
  }

  // Ensure they give us what we are expecting.
  for (int i = 0; i <= kNumSplits; i++) {
    // Calculate the tablet id and start key.
    const string& start_key = (i == 0) ? "" : split_keys[i - 1];
    const string& end_key = (i == kNumSplits) ? "" : split_keys[i];
    string tablet_id = Substitute("tablet-$0-$1", start_key, end_key);

    // Query using the start key.
    GetTableLocationsRequestPB req;
    req.set_max_returned_locations(1);
    req.mutable_table()->mutable_table_name()->assign(table_id);
    req.mutable_partition_key_start()->assign(start_key);
    vector<scoped_refptr<TabletInfo> > tablets_in_range;
    table->GetTabletsInRange(&req, &tablets_in_range);

    // Only one tablet should own this key.
    ASSERT_EQ(1, tablets_in_range.size());
    // The tablet with range start key matching 'start_key' should be the owner.
    ASSERT_EQ(tablet_id, (*tablets_in_range.begin())->tablet_id());
    LOG(INFO) << "Key " << start_key << " found in tablet " << tablet_id;
  }

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    ASSERT_TRUE(table->RemoveTablet(
        tablet->metadata().state().pb.partition().partition_key_start()));
  }
}

TEST(TestTSDescriptor, TestReplicaCreationsDecay) {
  TSDescriptor ts("test");
  ASSERT_EQ(0, ts.RecentReplicaCreations());
  ts.IncrementRecentReplicaCreations();

  // The load should start at close to 1.0.
  double val_a = ts.RecentReplicaCreations();
  ASSERT_NEAR(1.0, val_a, 0.05);

  // After 10ms it should have dropped a bit, but still be close to 1.0.
  SleepFor(MonoDelta::FromMilliseconds(10));
  double val_b = ts.RecentReplicaCreations();
  ASSERT_LT(val_b, val_a);
  ASSERT_NEAR(0.99, val_a, 0.05);

  if (AllowSlowTests()) {
    // After 10 seconds, we should have dropped to 0.5^(10/60) = 0.891
    SleepFor(MonoDelta::FromSeconds(10));
    ASSERT_NEAR(0.891, ts.RecentReplicaCreations(), 0.05);
  }
}

TEST(TestLoadBalancer, TestLoadBalancerAlgorithm) {
  const string table_id = CURRENT_TEST_NAME();
  shared_ptr<TestLoadBalancer> lb = make_shared<TestLoadBalancer>(table_id);
  lb->TestAlgorithm();
}

} // namespace master
} // namespace yb
