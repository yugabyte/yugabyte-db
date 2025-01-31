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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/common/constants.h"
#include "yb/master/catalog_manager-test_base.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"

namespace yb {
namespace master {

using std::shared_ptr;
using std::make_shared;
using std::string;
using std::vector;
using strings::Substitute;

// Test of the tablet assignment algorithm for splits done at table creation time.
// This tests that when we define a split, the tablet lands on the expected
// side of the split, i.e. it's a closed interval on the start key and an open
// interval on the end key (non-inclusive).
TEST(TableInfoTest, TestAssignmentRanges) {
  const string table_id = CURRENT_TEST_NAME();
  scoped_refptr<TableInfo> table(new TableInfo(table_id, /* colocated */ false));
  vector<TabletInfoPtr> tablets;

  // Define & create the splits.
  vector<string> split_keys = {"a", "b", "c"};  // The keys we split on.
  const size_t kNumSplits = split_keys.size();
  const int kNumReplicas = 1;

  ASSERT_OK(CreateTable(split_keys, kNumReplicas, true, table.get(), &tablets));

  ASSERT_EQ(table->LockForRead()->pb.replication_info().live_replicas().num_replicas(),
            kNumReplicas) << "Invalid replicas for created table.";

  // Ensure they give us what we are expecting.
  for (size_t i = 0; i <= kNumSplits; i++) {
    // Calculate the tablet id and start key.
    const string& start_key = (i == 0) ? "" : split_keys[i - 1];
    const string& end_key = (i == kNumSplits) ? "" : split_keys[i];
    string tablet_id = Substitute("tablet-$0-$1", start_key, end_key);

    // Query using the start key.
    GetTableLocationsRequestPB req;
    req.set_max_returned_locations(1);
    req.mutable_table()->mutable_table_name()->assign(table_id);
    req.mutable_partition_key_start()->assign(start_key);
    vector<TabletInfoPtr> tablets_in_range = ASSERT_RESULT(table->GetTabletsInRange(&req));

    // Only one tablet should own this key.
    ASSERT_EQ(1, tablets_in_range.size());
    // The tablet with range start key matching 'start_key' should be the owner.
    ASSERT_EQ(tablet_id, (*tablets_in_range.begin())->tablet_id());
    LOG(INFO) << "Key " << start_key << " found in tablet " << tablet_id;
  }

  for (const TabletInfoPtr& tablet : tablets) {
    auto lock = tablet->LockForWrite();
    ASSERT_TRUE(ASSERT_RESULT(table->RemoveTablet(tablet->id())));
  }
}

TEST(TestTSDescriptor, TestReplicaCreationsDecay) {
  TSDescriptor ts("test", RegisteredThroughHeartbeat::kTrue, CloudInfoPB(), nullptr);
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

TEST(TestCatalogManager, TestLoadCountMultiAZ) {
  std::shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a");
  std::shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "b");
  std::shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "c");
  std::shared_ptr<TSDescriptor> ts3 = SetupTS("3333", "a");
  std::shared_ptr<TSDescriptor> ts4 = SetupTS("4444", "a");
  ts0->set_num_live_replicas(6);
  ts1->set_num_live_replicas(17);
  ts2->set_num_live_replicas(19);
  ts3->set_num_live_replicas(6);
  ts4->set_num_live_replicas(6);
  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3, ts4};

  ZoneToDescMap zone_to_ts;
  ASSERT_OK(CatalogManagerUtil::GetPerZoneTSDesc(ts_descs, &zone_to_ts));
  ASSERT_EQ(3, zone_to_ts.size());
  ASSERT_EQ(3, zone_to_ts.find("aws:us-west-1:a")->second.size());
  ASSERT_EQ(1, zone_to_ts.find("aws:us-west-1:b")->second.size());
  ASSERT_EQ(1, zone_to_ts.find("aws:us-west-1:c")->second.size());

  ASSERT_OK(CatalogManagerUtil::IsLoadBalanced(ts_descs));
}

TEST(TestCatalogManager, TestLoadCountSingleAZ) {
  std::shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a");
  std::shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "a");
  std::shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "a");
  std::shared_ptr<TSDescriptor> ts3 = SetupTS("3333", "a");
  std::shared_ptr<TSDescriptor> ts4 = SetupTS("4444", "a");
  ts0->set_num_live_replicas(4);
  ts1->set_num_live_replicas(5);
  ts2->set_num_live_replicas(6);
  ts3->set_num_live_replicas(5);
  ts4->set_num_live_replicas(4);
  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3, ts4};

  ZoneToDescMap zone_to_ts;
  ASSERT_OK(CatalogManagerUtil::GetPerZoneTSDesc(ts_descs, &zone_to_ts));
  ASSERT_EQ(1, zone_to_ts.size());
  ASSERT_EQ(5, zone_to_ts.find("aws:us-west-1:a")->second.size());

  ASSERT_OK(CatalogManagerUtil::IsLoadBalanced(ts_descs));
}

TEST(TestCatalogManager, TestLoadNotBalanced) {
  std::shared_ptr <TSDescriptor> ts0 = SetupTS("0000", "a");
  std::shared_ptr <TSDescriptor> ts1 = SetupTS("1111", "a");
  std::shared_ptr <TSDescriptor> ts2 = SetupTS("2222", "c");
  ts0->set_num_live_replicas(4);
  ts1->set_num_live_replicas(50);
  ts2->set_num_live_replicas(16);
  TSDescriptorVector ts_descs = {ts0, ts1, ts2};

  ASSERT_NOK(CatalogManagerUtil::IsLoadBalanced(ts_descs));
}

TEST(TestCatalogManager, TestLoadBalancedRFgtAZ) {
  std::shared_ptr <TSDescriptor> ts0 = SetupTS("0000", "a");
  std::shared_ptr <TSDescriptor> ts1 = SetupTS("1111", "b");
  std::shared_ptr <TSDescriptor> ts2 = SetupTS("2222", "b");
  ts0->set_num_live_replicas(8);
  ts1->set_num_live_replicas(8);
  ts2->set_num_live_replicas(8);
  TSDescriptorVector ts_descs = {ts0, ts1, ts2};

  ZoneToDescMap zone_to_ts;
  ASSERT_OK(CatalogManagerUtil::GetPerZoneTSDesc(ts_descs, &zone_to_ts));
  ASSERT_EQ(2, zone_to_ts.size());
  ASSERT_EQ(1, zone_to_ts.find("aws:us-west-1:a")->second.size());
  ASSERT_EQ(2, zone_to_ts.find("aws:us-west-1:b")->second.size());

  ASSERT_OK(CatalogManagerUtil::IsLoadBalanced(ts_descs));
}

TEST(TestCatalogManager, TestLoadBalancedPerAZ) {
  std::shared_ptr <TSDescriptor> ts0 = SetupTS("0000", "a");
  std::shared_ptr <TSDescriptor> ts1 = SetupTS("1111", "b");
  std::shared_ptr <TSDescriptor> ts2 = SetupTS("2222", "b");
  std::shared_ptr <TSDescriptor> ts3 = SetupTS("3333", "b");
  ts0->set_num_live_replicas(32);
  ts1->set_num_live_replicas(22);
  ts2->set_num_live_replicas(21);
  ts3->set_num_live_replicas(21);
  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3};

  ZoneToDescMap zone_to_ts;
  ASSERT_OK(CatalogManagerUtil::GetPerZoneTSDesc(ts_descs, &zone_to_ts));
  ASSERT_EQ(2, zone_to_ts.size());
  ASSERT_EQ(1, zone_to_ts.find("aws:us-west-1:a")->second.size());
  ASSERT_EQ(3, zone_to_ts.find("aws:us-west-1:b")->second.size());

  ASSERT_OK(CatalogManagerUtil::IsLoadBalanced(ts_descs));
}

TEST(TestCatalogManager, TestGetPlacementUuidFromRaftPeer) {
  // Test a voter peer is assigned a live placement.
  ReplicationInfoPB replication_info;
  SetupClusterConfigWithReadReplicas({"a", "b", "c"}, {{"d"}}, &replication_info);
  consensus::RaftPeerPB raft_peer;
  SetupRaftPeer(consensus::PeerMemberType::VOTER, "a", &raft_peer);
  ASSERT_EQ(kLivePlacementUuid, ASSERT_RESULT(
      CatalogManagerUtil::GetPlacementUuidFromRaftPeer(replication_info, raft_peer)));
  SetupRaftPeer(consensus::PeerMemberType::PRE_VOTER, "b", &raft_peer);
  ASSERT_EQ(kLivePlacementUuid, ASSERT_RESULT(
      CatalogManagerUtil::GetPlacementUuidFromRaftPeer(replication_info, raft_peer)));

  // Test a observer peer is assigned to the rr placement.
  SetupRaftPeer(consensus::PeerMemberType::OBSERVER, "d", &raft_peer);
  ASSERT_EQ(Format(kReadReplicaPlacementUuidPrefix, 0), ASSERT_RESULT(
      CatalogManagerUtil::GetPlacementUuidFromRaftPeer(replication_info, raft_peer)));

  // Now test multiple rr placements.
  SetupClusterConfigWithReadReplicas({"a", "b", "c"}, {{"d"}, {"e"}}, &replication_info);
  SetupRaftPeer(consensus::PeerMemberType::PRE_OBSERVER, "d", &raft_peer);
  ASSERT_EQ(Format(kReadReplicaPlacementUuidPrefix, 0), ASSERT_RESULT(
      CatalogManagerUtil::GetPlacementUuidFromRaftPeer(replication_info, raft_peer)));
  SetupRaftPeer(consensus::PeerMemberType::OBSERVER, "e", &raft_peer);
  ASSERT_EQ(Format(kReadReplicaPlacementUuidPrefix, 1), ASSERT_RESULT(
      CatalogManagerUtil::GetPlacementUuidFromRaftPeer(replication_info, raft_peer)));

  // Test peer with invalid cloud info throws error.
  SetupRaftPeer(consensus::PeerMemberType::PRE_OBSERVER, "c", &raft_peer);
  ASSERT_NOK(CatalogManagerUtil::GetPlacementUuidFromRaftPeer(replication_info, raft_peer));

  // Test cluster config with rr placements with same cloud info throws error.
  SetupClusterConfigWithReadReplicas({"a", "b", "c"}, {{"d"}, {"d"}}, &replication_info);
  SetupRaftPeer(consensus::PeerMemberType::OBSERVER, "d", &raft_peer);
  ASSERT_NOK(CatalogManagerUtil::GetPlacementUuidFromRaftPeer(replication_info, raft_peer));
}

namespace {

void SetTabletState(TabletInfo* tablet, const SysTabletsEntryPB::State& state) {
  auto lock = tablet->LockForWrite();
  lock.mutable_data()->pb.set_state(state);
  lock.Commit();
}

const std::string GetSplitKey(const std::string& start_key, const std::string& end_key) {
  const auto split_key = start_key.length() < end_key.length()
      ? start_key + static_cast<char>(end_key[start_key.length()] >> 1)
      : start_key + "m";

  CHECK_LT(start_key, split_key) << " end_key: " << end_key;
  if (!end_key.empty()) {
    CHECK_LT(split_key, end_key) << " start_key: " << start_key;
  }

  return split_key;
}

Result<std::array<TabletInfoPtr, kNumSplitParts>> SplitTablet(
    const TabletInfoPtr& source_tablet) {
  auto lock = source_tablet->LockForRead();
  const auto partition = lock->pb.partition();

  const auto split_key =
      GetSplitKey(partition.partition_key_start(), partition.partition_key_end());

  auto child1 = VERIFY_RESULT(CreateTablet(
      source_tablet->table(), source_tablet->tablet_id() + ".1", partition.partition_key_start(),
      split_key, lock->pb.split_depth() + 1));
  auto child2 = VERIFY_RESULT(CreateTablet(
      source_tablet->table(), source_tablet->tablet_id() + ".2", split_key,
      partition.partition_key_end(), lock->pb.split_depth() + 1));
  std::array<TabletInfoPtr, kNumSplitParts> result = { child1, child2 };
  return result;
}

void SplitAndDeleteTablets(const TabletInfos& tablets_to_split, TabletInfos* post_split_tablets) {
  for (const auto& source_tablet : tablets_to_split) {
    ASSERT_NOK(CatalogManagerUtil::CheckIfCanDeleteSingleTablet(source_tablet));
    auto child_tablets = ASSERT_RESULT(SplitTablet(source_tablet));
    for (const auto& child : child_tablets) {
      LOG(INFO) << "Child tablet " << child->tablet_id()
                << " partition: " << AsString(child->LockForRead()->pb.partition())
                << " state: "
                << SysTabletsEntryPB_State_Name(child->LockForRead()->pb.state());
      post_split_tablets->push_back(child);
    }
    SetTabletState(child_tablets[1].get(), SysTabletsEntryPB::CREATING);
    // We shouldn't be able to delete source tablet when 2nd child is still creating.
    ASSERT_NOK(CatalogManagerUtil::CheckIfCanDeleteSingleTablet(source_tablet));
    SetTabletState(child_tablets[1].get(), SysTabletsEntryPB::RUNNING);
    ASSERT_OK(CatalogManagerUtil::CheckIfCanDeleteSingleTablet(source_tablet));
    SetTabletState(source_tablet.get(), SysTabletsEntryPB::DELETED);
    ASSERT_NOK(CatalogManagerUtil::CheckIfCanDeleteSingleTablet(source_tablet));
  }
}

} // namespace

TEST(TestCatalogManager, CheckIfCanDeleteSingleTablet) {
  const string table_id = CURRENT_TEST_NAME();
  scoped_refptr<TableInfo> table(new TableInfo(table_id, /* colocated */ false));
  TabletInfos pre_split_tablets;

  const std::vector<std::string> pre_split_keys = {"a", "b", "c"};
  const int kNumReplicas = 1;

  ASSERT_OK(CreateTable(pre_split_keys, kNumReplicas, true, table.get(), &pre_split_tablets));

  for (const auto& tablet : pre_split_tablets) {
    ASSERT_NOK(CatalogManagerUtil::CheckIfCanDeleteSingleTablet(tablet));
  }

  TabletInfos first_level_splits;
  NO_FATALS(SplitAndDeleteTablets(pre_split_tablets, &first_level_splits));

  for (const auto& tablet : pre_split_tablets) {
    SetTabletState(tablet.get(), SysTabletsEntryPB::RUNNING);
  }

  TabletInfos second_level_splits;
  NO_FATALS(SplitAndDeleteTablets(first_level_splits, &second_level_splits));

  // We should be able to delete pre split tablets covered by 2nd level split tablets.
  for (const auto& source_tablet : pre_split_tablets) {
    ASSERT_OK(CatalogManagerUtil::CheckIfCanDeleteSingleTablet(source_tablet));
  }
}

TEST(TestCatalogManager, TestSetPreferredZones) {
  std::string z1 = "a", z2 = "b", z3 = "c";
  CloudInfoPB ci1;
  ci1.set_placement_cloud(default_cloud);
  ci1.set_placement_region(default_region);
  ci1.set_placement_zone(z1);
  CloudInfoPB ci2(ci1);
  ci2.set_placement_zone(z2);
  CloudInfoPB ci3(ci1);
  ci3.set_placement_zone(z3);

  ReplicationInfoPB replication_default;
  SetupClusterConfig({z1, z2, z3}, &replication_default);

  {
    LOG(INFO) << "Empty set";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;

    ASSERT_OK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
    ASSERT_EQ(replication_info.affinitized_leaders_size(), 0);
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 0);
  }

  {
    LOG(INFO) << "Old behavior";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    *req.add_preferred_zones() = ci1;

    ASSERT_OK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
    ASSERT_EQ(replication_info.affinitized_leaders_size(), 0);
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(0).placement_zone(), z1);
  }

  {
    LOG(INFO) << "Both old and new behavior";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    *req.add_preferred_zones() = ci1;
    *req.add_multi_preferred_zones()->add_zones() = ci1;

    ASSERT_OK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
    ASSERT_EQ(replication_info.affinitized_leaders_size(), 0);
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(0).placement_zone(), z1);
  }

  {
    LOG(INFO) << "Multiple priority level";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    *req.add_preferred_zones() = ci1;
    auto p1 = req.add_multi_preferred_zones();
    *p1->add_zones() = ci1;
    *p1->add_zones() = ci2;
    *req.add_multi_preferred_zones()->add_zones() = ci3;

    ASSERT_OK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
    ASSERT_EQ(replication_info.affinitized_leaders_size(), 0);
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 2);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones_size(), 2);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(0).placement_zone(), z1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(1).placement_zone(), z2);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(1).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(1).zones(0).placement_zone(), z3);
  }

  {
    LOG(INFO) << "Multiple priority level 2";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    *req.add_preferred_zones() = ci1;
    *req.add_multi_preferred_zones()->add_zones() = ci1;
    *req.add_multi_preferred_zones()->add_zones() = ci2;
    *req.add_multi_preferred_zones()->add_zones() = ci3;

    ASSERT_OK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
    ASSERT_EQ(replication_info.affinitized_leaders_size(), 0);
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 3);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(0).placement_zone(), z1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(1).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(1).zones(0).placement_zone(), z2);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(2).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(2).zones(0).placement_zone(), z3);
  }

  {
    LOG(INFO) << "Missing priority level";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    *req.add_multi_preferred_zones()->add_zones() = ci1;
    req.add_multi_preferred_zones();
    *req.add_multi_preferred_zones()->add_zones() = ci2;

    ASSERT_NOK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
  }

  {
    LOG(INFO) << "Duplicate entries in same priority";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    auto p1 = req.add_multi_preferred_zones();
    *p1->add_zones() = ci1;
    *p1->add_zones() = ci1;

    ASSERT_NOK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
  }

  {
    LOG(INFO) << "Duplicate entries in same priority";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    auto p1 = req.add_multi_preferred_zones();
    *p1->add_zones() = ci1;
    *p1->add_zones() = ci1;

    ASSERT_NOK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
  }

  {
    LOG(INFO) << "Duplicate entries in different priority";
    ReplicationInfoPB replication_info = replication_default;
    SetPreferredZonesRequestPB req;
    *req.add_multi_preferred_zones()->add_zones() = ci1;
    *req.add_multi_preferred_zones()->add_zones() = ci1;

    ASSERT_NOK(CatalogManagerUtil::SetPreferredZones(&req, &replication_info));
  }

  {
    LOG(INFO) << "Invalid leader zone";
    ReplicationInfoPB replication_info;
    SetupClusterConfig({z2, z3}, &replication_info);
    *replication_info.add_multi_affinitized_leaders()->add_zones() = ci1;

    ASSERT_NOK(CatalogManagerUtil::CheckValidLeaderAffinity(replication_info));
  }

  {
    LOG(INFO) << "Duplicate leader zone";
    ReplicationInfoPB replication_info = replication_default;
    *replication_info.add_multi_affinitized_leaders()->add_zones() = ci1;
    *replication_info.add_multi_affinitized_leaders()->add_zones() = ci1;

    ASSERT_NOK(CatalogManagerUtil::CheckValidLeaderAffinity(replication_info));
  }

  {
    LOG(INFO) << "Valid leader zone";
    ReplicationInfoPB replication_info = replication_default;
    auto first_zone_set = replication_info.add_multi_affinitized_leaders();
    *first_zone_set->add_zones() = ci1;
    *first_zone_set->add_zones() = ci2;
    *replication_info.add_multi_affinitized_leaders()->add_zones() = ci3;

    ASSERT_OK(CatalogManagerUtil::CheckValidLeaderAffinity(replication_info));
  }
}

TEST(TestCatalogManagerEnterprise, TestLeaderLoadBalancedAffinitizedLeaders) {
  // Note that this is essentially using transaction_tables_use_preferred_zones = true
  ReplicationInfoPB replication_info;
  const std::vector<std::string> az_list = {"a", "b", "c"};
  SetupClusterConfig(
      az_list, {} /* read only */, {{"a"}} /* affinitized leaders */, &replication_info);

  std::shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a", "");
  std::shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "a", "");
  std::shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "b", "");
  std::shared_ptr<TSDescriptor> ts3 = SetupTS("3333", "b", "");
  std::shared_ptr<TSDescriptor> ts4 = SetupTS("4444", "c", "");

  ASSERT_TRUE(ts0->IsAcceptingLeaderLoad(replication_info));
  ASSERT_TRUE(ts1->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts2->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts3->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts4->IsAcceptingLeaderLoad(replication_info));

  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3, ts4};

  scoped_refptr<TableInfo> table(
      new TableInfo(CURRENT_TEST_NAME() /* table_id */, false /* colocated */));
  std::vector<TabletInfoPtr> tablets;
  ASSERT_OK(CreateTable(
      az_list, 1 /* num_replicas */, false /* setup_placement */, table.get(), &tablets));

  SimulateSetLeaderReplicas(tablets, {2, 1, 1, 0, 0} /* leader_counts */, ts_descs);

  ASSERT_NOK(CatalogManagerUtil::AreLeadersOnPreferredOnly(
      ts_descs, replication_info, nullptr /* tablespace_manager */, {table}));

  SimulateSetLeaderReplicas(tablets, {4, 0, 0, 0, 0} /* leader_counts */, ts_descs);

  ASSERT_OK(
      CatalogManagerUtil::AreLeadersOnPreferredOnly(ts_descs, replication_info, nullptr, {table}));

  SimulateSetLeaderReplicas(tablets, {3, 1, 0, 0, 0} /* leader_counts */, ts_descs);

  ASSERT_OK(
      CatalogManagerUtil::AreLeadersOnPreferredOnly(ts_descs, replication_info, nullptr, {table}));
}

TEST(TestCatalogManagerEnterprise, TestLeaderLoadBalancedReadOnly) {
  // Note that this is essentially using transaction_tables_use_preferred_zones = true
  ReplicationInfoPB replication_info;
  const std::vector<std::string> az_list = {"a", "b", "c"};
  SetupClusterConfig(
      az_list /* az list */, {"d"} /* read only */, {} /* affinitized leaders */,
      &replication_info);

  std::shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a", "");
  std::shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "b", "");
  std::shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "c", "");
  std::shared_ptr<TSDescriptor> ts3 = SetupTS("3333", "d", "read_only");

  ASSERT_TRUE(ts0->IsAcceptingLeaderLoad(replication_info));
  ASSERT_TRUE(ts1->IsAcceptingLeaderLoad(replication_info));
  ASSERT_TRUE(ts2->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts3->IsAcceptingLeaderLoad(replication_info));

  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3};

  scoped_refptr<TableInfo> table(
      new TableInfo(CURRENT_TEST_NAME() /* table_id */, false /* colocated */));
  std::vector<TabletInfoPtr> tablets;
  ASSERT_OK(CreateTable(
      az_list, 1 /* num_replicas */, false /* setup_placement */, table.get(), &tablets));

  SimulateSetLeaderReplicas(tablets, {2, 1, 1, 0} /* leader_counts */, ts_descs);

  ASSERT_OK(
      CatalogManagerUtil::AreLeadersOnPreferredOnly(ts_descs, replication_info, nullptr, {table}));

  SimulateSetLeaderReplicas(tablets, {1, 1, 1, 1} /* leader_counts */, ts_descs);

  ASSERT_NOK(
      CatalogManagerUtil::AreLeadersOnPreferredOnly(ts_descs, replication_info, nullptr, {table}));
}

TEST(TestCatalogManagerEnterprise, TestLoadBalancedReadOnlySameAz) {
  ReplicationInfoPB replication_info;
  SetupClusterConfig(
      {"a"} /* az list */, {"a"} /* read only */, {} /* affinitized leaders */, &replication_info);
  std::shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a", "");
  std::shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "a", "");
  std::shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "a", "read_only");
  std::shared_ptr<TSDescriptor> ts3 = SetupTS("3333", "a", "read_only");

  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3};

  ts0->set_num_live_replicas(6);
  ts1->set_num_live_replicas(6);
  ts2->set_num_live_replicas(12);
  ts3->set_num_live_replicas(12);
  ASSERT_OK(CatalogManagerUtil::IsLoadBalanced(ts_descs));

  ts0->set_num_live_replicas(6);
  ts1->set_num_live_replicas(6);
  ts2->set_num_live_replicas(8);
  ts3->set_num_live_replicas(4);
  ASSERT_NOK(CatalogManagerUtil::IsLoadBalanced(ts_descs));
}

}  // namespace master
} // namespace yb
