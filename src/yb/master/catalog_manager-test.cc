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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-test_base.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/util/format.h"

namespace yb {
namespace master {

using std::shared_ptr;
using std::make_shared;
using std::string;
using std::vector;

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
    string tablet_id = Format("tablet-$0-$1", start_key, end_key);

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

Result<std::array<TabletInfoPtr, kDefaultNumSplitParts>> SplitTablet(
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
  std::array<TabletInfoPtr, kDefaultNumSplitParts> result = { child1, child2 };
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

namespace {

std::vector<TabletServerId> SortedUuids(
    const CMPerTableLoadState& state, const TSDescriptorVector& ts_descs) {
  std::vector<TabletServerId> result;
  for (size_t idx : state.sorted_by_load()) {
    result.push_back(ts_descs[idx]->permanent_uuid());
  }
  return result;
}

}  // namespace

TEST(TestCatalogManager, PerTableLoadStateOrder) {
  TSDescriptorVector ts_descs = {
      SetupTS("0000", "a"), SetupTS("1111", "a"), SetupTS("2222", "a"), SetupTS("3333", "a")};
  CMGlobalLoadState global_state;
  global_state.per_ts_replica_load_ = {{"0000", 5}, {"1111", 3}, {"2222", 3}, {"3333", 9}};
  CMPerTableLoadState state(&global_state);
  ASSERT_OK(state.Init(ts_descs, {{"0000", 2}, {"1111", 2}, {"2222", 2}, {"3333", 0}}));

  // Table load first, then global load, then uuid.
  ASSERT_EQ(SortedUuids(state, ts_descs),
            (std::vector<TabletServerId>{"3333", "1111", "2222", "0000"}));
}

TEST(TestCatalogManager, PerTableLoadStateAddReplica) {
  TSDescriptorVector ts_descs = {
      SetupTS("0000", "a"), SetupTS("1111", "a"), SetupTS("2222", "a"), SetupTS("3333", "a")};
  CMGlobalLoadState global_state;
  global_state.per_ts_replica_load_ = {{"0000", 0}, {"1111", 0}, {"2222", 0}, {"3333", 0}};
  CMPerTableLoadState state(&global_state);
  ASSERT_OK(state.Init(ts_descs, {{"0000", 0}, {"1111", 0}, {"2222", 0}, {"3333", 0}}));
  ASSERT_EQ(SortedUuids(state, ts_descs),
            (std::vector<TabletServerId>{"0000", "1111", "2222", "3333"}));

  // The tserver that gained a replica moves behind every tserver with equal load.
  state.AddReplica(0);
  ASSERT_EQ(SortedUuids(state, ts_descs),
            (std::vector<TabletServerId>{"1111", "2222", "3333", "0000"}));
  state.AddReplica(2);
  ASSERT_EQ(SortedUuids(state, ts_descs),
            (std::vector<TabletServerId>{"1111", "3333", "0000", "2222"}));
  state.AddReplica(2);
  ASSERT_EQ(SortedUuids(state, ts_descs),
            (std::vector<TabletServerId>{"1111", "3333", "0000", "2222"}));
  state.AddReplica(1);
  state.AddReplica(3);
  ASSERT_EQ(SortedUuids(state, ts_descs),
            (std::vector<TabletServerId>{"0000", "1111", "3333", "2222"}));

  // The global load follows the picks.
  ASSERT_EQ(2, ASSERT_RESULT(global_state.GetGlobalReplicaLoad("2222")));
  ASSERT_EQ(1, ASSERT_RESULT(global_state.GetGlobalReplicaLoad("0000")));
}

using consensus::PeerMemberType;

namespace {

PlacementInfoPB MakePlacement(int num_replicas, const std::string& placement_uuid = "") {
  PlacementInfoPB placement;
  placement.set_num_replicas(num_replicas);
  if (!placement_uuid.empty()) {
    placement.set_placement_uuid(placement_uuid);
  }
  return placement;
}

// An empty zone makes a region-level block, which matches every zone of the default region.
void AddBlock(
    PlacementInfoPB* placement, const std::string& zone, int min_num_replicas,
    std::optional<int> max_num_replicas = std::nullopt) {
  auto* block = placement->add_placement_blocks();
  block->mutable_cloud_info()->set_placement_cloud(default_cloud);
  block->mutable_cloud_info()->set_placement_region(default_region);
  if (!zone.empty()) {
    block->mutable_cloud_info()->set_placement_zone(zone);
  }
  block->set_min_num_replicas(min_num_replicas);
  if (max_num_replicas) {
    block->set_max_num_replicas(*max_num_replicas);
  }
}

struct LoadState {
  CMGlobalLoadState global;
  CMPerTableLoadState table{&global};
};

// Zero global load, the given per-table load (default 0) for every tserver.
Status InitLoadState(
    const TSDescriptorVector& ts_descs,
    const std::unordered_map<TabletServerId, uint32_t>& table_load, LoadState* state) {
  std::unordered_map<TabletServerId, uint32_t> full_table_load;
  for (const auto& ts : ts_descs) {
    state->global.per_ts_replica_load_[ts->permanent_uuid()] = 0;
    full_table_load[ts->permanent_uuid()] = FindWithDefault(table_load, ts->permanent_uuid(), 0);
  }
  return state->table.Init(ts_descs, full_table_load);
}

std::vector<TabletServerId> PeerUuids(const consensus::RaftConfigPB& config) {
  std::vector<TabletServerId> result;
  for (const auto& peer : config.peers()) {
    result.push_back(peer.permanent_uuid());
  }
  return result;
}

}  // namespace

TEST(TestCatalogManager, PlaceReplicasWithoutPlacementBlocks) {
  TSDescriptorVector ts_descs = {
      SetupTS("0000", "a"), SetupTS("1111", "a"), SetupTS("2222", "a"), SetupTS("3333", "a"),
      SetupTS("4444", "a")};
  LoadState load;
  ASSERT_OK(InitLoadState(ts_descs, {{"0000", 2}, {"4444", 1}}, &load));
  auto candidates = ASSERT_RESULT(
      CatalogManager::BuildPlacementCandidates(MakePlacement(3), ts_descs, PeerMemberType::VOTER));
  consensus::RaftConfigPB config;
  ASSERT_OK(CatalogManager::SelectReplicasForPlacement(ts_descs, candidates, &config, &load.table));
  // The three least loaded tservers of the placement.
  ASSERT_EQ(PeerUuids(config), (std::vector<TabletServerId>{"1111", "2222", "3333"}));
  for (const auto& peer : config.peers()) {
    ASSERT_EQ(peer.member_type(), PeerMemberType::VOTER);
  }
}

TEST(TestCatalogManager, PlaceReplicasPerBlockMinimums) {
  TSDescriptorVector ts_descs = {
      SetupTS("a000", "a"), SetupTS("a111", "a"), SetupTS("b000", "b"), SetupTS("c000", "c")};
  auto placement = MakePlacement(3);
  AddBlock(&placement, "a", 1);
  AddBlock(&placement, "b", 1);
  AddBlock(&placement, "c", 1);
  LoadState load;
  ASSERT_OK(InitLoadState(ts_descs, {{"a000", 1}}, &load));
  auto candidates = ASSERT_RESULT(
      CatalogManager::BuildPlacementCandidates(placement, ts_descs, PeerMemberType::VOTER));
  consensus::RaftConfigPB config;
  ASSERT_OK(CatalogManager::SelectReplicasForPlacement(ts_descs, candidates, &config, &load.table));
  // One replica per block, the least loaded tserver within the block.
  ASSERT_EQ(PeerUuids(config), (std::vector<TabletServerId>{"a111", "b000", "c000"}));
}

TEST(TestCatalogManager, PlaceReplicasBlockMaximumCapsLeftovers) {
  TSDescriptorVector ts_descs = {
      SetupTS("a000", "a"), SetupTS("a111", "a"), SetupTS("a222", "a"), SetupTS("b000", "b"),
      SetupTS("b111", "b")};
  auto placement = MakePlacement(3);
  AddBlock(&placement, "a", 1, /* max_num_replicas = */ 1);
  AddBlock(&placement, "b", 1);
  LoadState load;
  // Zone a is less loaded, so without the cap the leftover replica would go there.
  ASSERT_OK(InitLoadState(ts_descs, {{"b000", 1}, {"b111", 1}}, &load));
  auto candidates = ASSERT_RESULT(
      CatalogManager::BuildPlacementCandidates(placement, ts_descs, PeerMemberType::VOTER));
  consensus::RaftConfigPB config;
  ASSERT_OK(CatalogManager::SelectReplicasForPlacement(ts_descs, candidates, &config, &load.table));
  ASSERT_EQ(PeerUuids(config), (std::vector<TabletServerId>{"a000", "b000", "b111"}));
}

TEST(TestCatalogManager, PlaceReadReplicas) {
  TSDescriptorVector ts_descs = {
      SetupTS("0000", "a", "live"), SetupTS("1111", "a", "live"), SetupTS("r000", "a", "rr"),
      SetupTS("r111", "b", "rr"), SetupTS("r222", "c", "rr")};
  LoadState load;
  ASSERT_OK(InitLoadState(ts_descs, {{"r000", 1}}, &load));
  auto candidates = ASSERT_RESULT(CatalogManager::BuildPlacementCandidates(
      MakePlacement(2, "rr"), ts_descs, PeerMemberType::OBSERVER));
  consensus::RaftConfigPB config;
  ASSERT_OK(CatalogManager::SelectReplicasForPlacement(ts_descs, candidates, &config, &load.table));
  ASSERT_EQ(PeerUuids(config), (std::vector<TabletServerId>{"r111", "r222"}));
  for (const auto& peer : config.peers()) {
    ASSERT_EQ(peer.member_type(), PeerMemberType::OBSERVER);
  }
}

TEST(TestCatalogManager, PlaceReplicasExcludesOtherPlacementUuids) {
  TSDescriptorVector ts_descs = {
      SetupTS("0000", "a", "live"), SetupTS("r000", "a", "rr"), SetupTS("r111", "a", "rr")};
  LoadState load;
  ASSERT_OK(InitLoadState(ts_descs, {}, &load));
  auto candidates = ASSERT_RESULT(CatalogManager::BuildPlacementCandidates(
      MakePlacement(3, "live"), ts_descs, PeerMemberType::VOTER));
  ASSERT_EQ(candidates.num_allowed, 1u);
  ASSERT_EQ(candidates.all_allowed, (std::vector<bool>{true, false, false}));
  // Only the one live tserver is placed; the tablet starts under-replicated.
  consensus::RaftConfigPB config;
  ASSERT_OK(CatalogManager::SelectReplicasForPlacement(ts_descs, candidates, &config, &load.table));
  ASSERT_EQ(PeerUuids(config), (std::vector<TabletServerId>{"0000"}));
}

TEST(TestCatalogManager, PlaceReplicasBlockWithTooFewTservers) {
  TSDescriptorVector ts_descs = {SetupTS("a000", "a"), SetupTS("b000", "b")};
  auto placement = MakePlacement(3);
  AddBlock(&placement, "a", 2);
  AddBlock(&placement, "b", 1);
  LoadState load;
  ASSERT_OK(InitLoadState(ts_descs, {}, &load));
  auto candidates = ASSERT_RESULT(
      CatalogManager::BuildPlacementCandidates(placement, ts_descs, PeerMemberType::VOTER));
  consensus::RaftConfigPB config;
  // A block short of its minimum takes what it has; the tablet starts under-replicated.
  ASSERT_OK(CatalogManager::SelectReplicasForPlacement(ts_descs, candidates, &config, &load.table));
  ASSERT_EQ(PeerUuids(config), (std::vector<TabletServerId>{"a000", "b000"}));
}

TEST(TestCatalogManager, PlaceReplicasOverlappingBlocksFail) {
  TSDescriptorVector ts_descs = {SetupTS("a000", "a"), SetupTS("b000", "b")};
  auto placement = MakePlacement(2);
  // The region block comes first and takes the only tserver of zone a.
  AddBlock(&placement, "", 1);
  AddBlock(&placement, "a", 1);
  LoadState load;
  ASSERT_OK(InitLoadState(ts_descs, {{"b000", 1}}, &load));
  auto candidates = ASSERT_RESULT(
      CatalogManager::BuildPlacementCandidates(placement, ts_descs, PeerMemberType::VOTER));
  consensus::RaftConfigPB config;
  auto status =
      CatalogManager::SelectReplicasForPlacement(ts_descs, candidates, &config, &load.table);
  ASSERT_TRUE(status.IsIllegalState()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "Not enough tablet servers");
}

TEST(TestCatalogManager, PerTableLoadStateInitRequiresEveryTs) {
  TSDescriptorVector ts_descs = {SetupTS("0000", "a"), SetupTS("1111", "a")};
  CMGlobalLoadState global_state;
  global_state.per_ts_replica_load_ = {{"0000", 0}, {"1111", 0}};
  {
    CMPerTableLoadState state(&global_state);
    ASSERT_NOK_STR_CONTAINS(
        state.Init(ts_descs, {{"0000", 0}}), "table_load does not contain 1111");
  }
  {
    CMGlobalLoadState partial_global_state;
    partial_global_state.per_ts_replica_load_ = {{"0000", 0}};
    CMPerTableLoadState state(&partial_global_state);
    ASSERT_NOK_STR_CONTAINS(
        state.Init(ts_descs, {{"0000", 0}, {"1111", 0}}), "Could not locate tablet server 1111");
  }
}

TEST(TestCatalogManager, ValidateVectorIndexBackend) {
  SysTablesEntryPB table;
  table.set_name("vi");
  ASSERT_OK(ValidateVectorIndexBackend("table_id", table));

  auto& options = *table.mutable_index_info()->mutable_vector_idx_options();
  options.set_idx_type(PgVectorIndexType::HNSW);
  for (auto backend : {HnswBackend::YB_HNSW_USEARCH, HnswBackend::YB_HNSW_HNSWLIB}) {
    options.mutable_hnsw()->set_backend(backend);
    ASSERT_OK(ValidateVectorIndexBackend("table_id", table));
  }

  // An unset backend reads as DEPRECATED_USEARCH: such indexes predate block-based backends.
  options.mutable_hnsw()->clear_backend();
  auto status = ValidateVectorIndexBackend("table_id", table);
  ASSERT_TRUE(status.IsNotSupported()) << status;
  for (auto backend : {HnswBackend::DEPRECATED_USEARCH, HnswBackend::DEPRECATED_HNSWLIB}) {
    options.mutable_hnsw()->set_backend(backend);
    status = ValidateVectorIndexBackend("table_id", table);
    ASSERT_TRUE(status.IsNotSupported()) << status;
    ASSERT_STR_CONTAINS(status.ToString(), "no longer supported");
  }
}

}  // namespace master
} // namespace yb
