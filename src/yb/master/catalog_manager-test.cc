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
  meta_lock.mutable_data()->pb.set_num_replicas(num_replicas);
  meta_lock.Commit();
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
  TestLoadBalancer(string table_id) : ClusterLoadBalancer(nullptr) { table_id_ = table_id; }

  void TestAlgorithm() {
    // Create the table.
    scoped_refptr<TableInfo> table(new TableInfo(table_id_));
    vector<scoped_refptr<TabletInfo>> tablets;

    // Generate 12 tablets total: 4 splits and 3 replicas each.
    vector<string> splits = {"a", "b", "c"};
    const int kNumReplicas = 3;
    const int kTotalNumTablets = kNumReplicas * (splits.size() + 1);
    CreateTable(splits, kNumReplicas, table.get(), &tablets);
    // The splits are ("-a", "a-b", "b-c", "c-"), hence the +1.
    ASSERT_EQ(tablets.size(), splits.size() + 1);

    // Assign them initially only to the first three TSs.
    shared_ptr<TSDescriptor> ts1(new TSDescriptor("1111"));
    shared_ptr<TSDescriptor> ts2(new TSDescriptor("2222"));
    shared_ptr<TSDescriptor> ts3(new TSDescriptor("3333"));
    TSDescriptorVector ts_descs = {ts1, ts2, ts3};
    PrepareTestState(ts_descs, tablets);

    // Analyze the tablets into the internal state.
    AnalyzeTablets();

    // Check some base expectations for balanced cluster.
    CHECK_EQ(0, get_total_over_replication()) << "Found invalid number of over-replicated tablets!";
    CHECK_EQ(0, get_total_starting_tablets()) << "Found invalid number of starting tablets!";
    CHECK_EQ(kTotalNumTablets, get_total_running_tablets())
        << "Found invalid number of running tablets!";

    // Add the fourth TS in there.
    shared_ptr<TSDescriptor> ts4(new TSDescriptor("4444"));
    ts_descs_.push_back(ts4);

    // Reset the load state and recompute.
    ResetState();
    AnalyzeTablets();

    // Equal load across the first three TSs. Picking the one with largest ID in string compare.
    string expected_from_ts = ts3->permanent_uuid();
    // Lowest load should be the last, empty TS.
    string expected_to_ts = ts4->permanent_uuid();
    TestLoadMove(expected_from_ts, expected_to_ts);

    // Perform another round on the updated in-memory load. The move should have made ts3 less
    // loaded, so next tablet should come from ts2 to ts4.
    expected_from_ts = ts2->permanent_uuid();
    TestLoadMove(expected_from_ts, expected_to_ts);

    // One more round, finally expecting to move from ts1 to ts4.
    expected_from_ts = ts1->permanent_uuid();
    TestLoadMove(expected_from_ts, expected_to_ts);

    // Final check on in-memory state after in-memory moves.
    CHECK_EQ(kTotalNumTablets - 3, get_total_running_tablets());
    CHECK_EQ(3, get_total_starting_tablets());
    CHECK_EQ(0, get_total_over_replication());
  }

 protected:
  void TestLoadMove(string expected_from_ts, string expected_to_ts) {
    string from_ts, to_ts, tablet_id;
    CHECK(GetLoadToMove(&from_ts, &to_ts, &tablet_id)) << Substitute(
        "Load balancer failed to find valid load to move! Expected from TS $0 to TS $1",
        expected_from_ts, expected_to_ts);

    CHECK_EQ(from_ts, expected_from_ts);
    CHECK_EQ(to_ts, expected_to_ts);

    AddInMemory(expected_from_ts, expected_to_ts, tablet_id);
  }

  void PrepareTestState(
      const TSDescriptorVector& ts_descs, vector<scoped_refptr<TabletInfo>> tablets) {
    ts_descs_ = ts_descs;

    // Prepare the replicas.
    tablet::TabletStatePB state = tablet::RUNNING;
    for (int i = 0; i < tablets.size(); ++i) {
      TabletInfo::ReplicaMap replica_map;
      for (int j = 0; j < ts_descs_.size(); ++j) {
        TabletReplica replica;
        auto ts_desc = ts_descs[j];
        bool is_leader = i % ts_descs_.size() == j;
        NewReplica(ts_desc.get(), state, is_leader, &replica);
        InsertOrDie(&replica_map, ts_desc->permanent_uuid(), replica);
      }
      tablets[i]->SetReplicaLocations(replica_map);
    }

    // Set tablets in class state.
    tablet_map_.clear();
    for (const auto tablet : tablets) {
      tablet_map_[tablet->tablet_id()] = tablet;
    }
  }

  // Getter overrides for base class functionality.
 protected:
  void GetAllLiveDescriptors(TSDescriptorVector* ts_descs) const OVERRIDE { *ts_descs = ts_descs_; }

  const TabletInfoMap& GetTabletMap() const OVERRIDE { return tablet_map_; }

 private:
  string table_id_;
  TSDescriptorVector ts_descs_;
  TabletInfoMap tablet_map_;
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
    ASSERT_EQ(meta_lock.data().pb.num_replicas(), kNumReplicas)
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
