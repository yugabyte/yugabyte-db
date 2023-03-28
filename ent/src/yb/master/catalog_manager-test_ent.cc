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

#include "./catalog_manager-test_base.h"
#include "yb/util/status_log.h"

namespace yb {
namespace master {
namespace enterprise {

TEST(TestCatalogManagerEnterprise, TestLeaderLoadBalancedAffinitizedLeaders) {
  // Note that this is essentially using transaction_tables_use_preferred_zones = true
  ReplicationInfoPB replication_info;
  const std::vector<std::string> az_list = {"a", "b", "c"};
  SetupClusterConfigEnt(
      az_list, {} /* read only */, {{"a"}} /* affinitized leaders */, &replication_info);

  std::shared_ptr<TSDescriptor> ts0 = SetupTSEnt("0000", "a", "");
  std::shared_ptr<TSDescriptor> ts1 = SetupTSEnt("1111", "a", "");
  std::shared_ptr<TSDescriptor> ts2 = SetupTSEnt("2222", "b", "");
  std::shared_ptr<TSDescriptor> ts3 = SetupTSEnt("3333", "b", "");
  std::shared_ptr<TSDescriptor> ts4 = SetupTSEnt("4444", "c", "");

  ASSERT_TRUE(ts0->IsAcceptingLeaderLoad(replication_info));
  ASSERT_TRUE(ts1->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts2->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts3->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts4->IsAcceptingLeaderLoad(replication_info));

  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3, ts4};

  scoped_refptr<TableInfo> table(new TableInfo(CURRENT_TEST_NAME() /* table_id */));
  std::vector<scoped_refptr<TabletInfo>> tablets;
  CreateTable(az_list, 1 /* num_replicas */, false /* setup_placement */, table.get(), &tablets);

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
  SetupClusterConfigEnt(
      az_list /* az list */, {"d"} /* read only */, {} /* affinitized leaders */,
      &replication_info);

  std::shared_ptr<TSDescriptor> ts0 = SetupTSEnt("0000", "a", "");
  std::shared_ptr<TSDescriptor> ts1 = SetupTSEnt("1111", "b", "");
  std::shared_ptr<TSDescriptor> ts2 = SetupTSEnt("2222", "c", "");
  std::shared_ptr<TSDescriptor> ts3 = SetupTSEnt("3333", "d", "read_only");

  ASSERT_TRUE(ts0->IsAcceptingLeaderLoad(replication_info));
  ASSERT_TRUE(ts1->IsAcceptingLeaderLoad(replication_info));
  ASSERT_TRUE(ts2->IsAcceptingLeaderLoad(replication_info));
  ASSERT_FALSE(ts3->IsAcceptingLeaderLoad(replication_info));

  TSDescriptorVector ts_descs = {ts0, ts1, ts2, ts3};

  scoped_refptr<TableInfo> table(new TableInfo(CURRENT_TEST_NAME() /* table_id */));
  std::vector<scoped_refptr<TabletInfo>> tablets;
  CreateTable(az_list, 1 /* num_replicas */, false /* setup_placement */, table.get(), &tablets);

  SimulateSetLeaderReplicas(tablets, {2, 1, 1, 0} /* leader_counts */, ts_descs);

  ASSERT_OK(
      CatalogManagerUtil::AreLeadersOnPreferredOnly(ts_descs, replication_info, nullptr, {table}));

  SimulateSetLeaderReplicas(tablets, {1, 1, 1, 1} /* leader_counts */, ts_descs);

  ASSERT_NOK(
      CatalogManagerUtil::AreLeadersOnPreferredOnly(ts_descs, replication_info, nullptr, {table}));
}

TEST(TestCatalogManagerEnterprise, TestLoadBalancedReadOnlySameAz) {
  ReplicationInfoPB replication_info;
  SetupClusterConfigEnt({"a"} /* az list */, {"a"} /* read only */,
                        {} /* affinitized leaders */, &replication_info);
  std::shared_ptr<TSDescriptor> ts0 = SetupTSEnt("0000", "a", "");
  std::shared_ptr<TSDescriptor> ts1 = SetupTSEnt("1111", "a", "");
  std::shared_ptr<TSDescriptor> ts2 = SetupTSEnt("2222", "a", "read_only");
  std::shared_ptr<TSDescriptor> ts3 = SetupTSEnt("3333", "a", "read_only");

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

} // namespace enterprise
} // namespace master
} // namespace yb
