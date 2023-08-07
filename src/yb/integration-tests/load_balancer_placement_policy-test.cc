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

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master_client.proxy.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

using std::string;
using std::vector;

using namespace std::literals;

DECLARE_int32(catalog_manager_bg_task_wait_ms);

namespace yb {
namespace integration_tests {

const auto kDefaultTimeout = 30000ms;

class LoadBalancerPlacementPolicyTest : public YBTableTestBase {
 protected:
  void SetUp() override {
    YBTableTestBase::SetUp();

    yb_admin_client_ = std::make_unique<tools::ClusterAdminClient>(
        external_mini_cluster()->GetMasterAddresses(), kDefaultTimeout);

    ASSERT_OK(yb_admin_client_->Init());
  }

  bool use_external_mini_cluster() override { return true; }

  int num_tablets() override {
    return 4;
  }

  bool enable_ysql() override {
    // Do not create the transaction status table.
    return false;
  }

  void GetLoadOnTservers(const string tablename,
                         size_t num_tservers,
                         vector<int> *const out_load_per_tserver) {
    out_load_per_tserver->clear();
    for (size_t i = 0; i < num_tservers; ++i) {
      const int count = ASSERT_RESULT(GetLoadOnTserver(
          external_mini_cluster()->tablet_server(i), tablename));
      out_load_per_tserver->emplace_back(count);
    }
  }

  Result<uint32_t> GetLoadOnTserver(ExternalTabletServer* server, const string tablename) {
    auto proxy = GetMasterLeaderProxy<master::MasterClientProxy>();
    master::GetTableLocationsRequestPB req;
    req.mutable_table()->set_table_name(tablename);
    req.mutable_table()->mutable_namespace_()->set_name(table_name().namespace_name());
    master::GetTableLocationsResponsePB resp;

    rpc::RpcController rpc;
    rpc.set_timeout(kDefaultTimeout);
    RETURN_NOT_OK(proxy.GetTableLocations(req, &resp, &rpc));

    uint32_t count = 0;
    std::vector<string> replicas;
    for (const auto& loc : resp.tablet_locations()) {
      for (const auto& replica : loc.replicas()) {
        if (replica.ts_info().permanent_uuid() == server->instance_id().permanent_uuid()) {
          replicas.push_back(loc.tablet_id());
          count++;
        }
      }
    }
    LOG(INFO) << Format("For ts $0, table name $1 tablet count $2",
                        server->instance_id().permanent_uuid(), tablename, count);
    return count;
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--placement_cloud=c");
    opts->extra_tserver_flags.push_back("--placement_region=r");
    opts->extra_tserver_flags.push_back("--placement_zone=z${index}");
    opts->extra_master_flags.push_back("--tserver_unresponsive_timeout_ms=5000");
  }

  void WaitForLoadBalancerToBeActive() {
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
          return !is_idle;
        },  kDefaultTimeout * 2, "IsLoadBalancerActive"));
  }

  void WaitForLoadBalancerToBeIdle() {
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return client_->IsLoadBalancerIdle();
      },  kDefaultTimeout * 4, "IsLoadBalancerIdle"));
  }

  void WaitForLoadBalancer() {
    WaitForLoadBalancerToBeActive();
    WaitForLoadBalancerToBeIdle();
  }

  void AddNewTserverToZone(
    const string& zone,
    const size_t expected_num_tservers,
    const string& placement_uuid = "") {

    std::vector<std::string> extra_opts;
    extra_opts.push_back("--placement_cloud=c");
    extra_opts.push_back("--placement_region=r");
    extra_opts.push_back("--placement_zone=" + zone);

    if (!placement_uuid.empty()) {
      extra_opts.push_back("--placement_uuid=" + placement_uuid);
    }

    ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
    ASSERT_OK(
        external_mini_cluster()->WaitForTabletServerCount(expected_num_tservers, kDefaultTimeout));
  }

  void AddNewTserverToLocation(const string& cloud, const string& region,
                               const string& zone, const size_t expected_num_tservers,
                               const string& placement_uuid = "") {

    std::vector<std::string> extra_opts;
    extra_opts.push_back("--placement_cloud=" + cloud);
    extra_opts.push_back("--placement_region=" + region);
    extra_opts.push_back("--placement_zone=" + zone);

    if (!placement_uuid.empty()) {
      extra_opts.push_back("--placement_uuid=" + placement_uuid);
    }

    ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
    ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(expected_num_tservers,
      kDefaultTimeout));
  }

  std::unique_ptr<tools::ClusterAdminClient> yb_admin_client_;
};

TEST_F(LoadBalancerPlacementPolicyTest, CreateTableWithPlacementPolicyTest) {
  // Set cluster placement policy.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  const string& create_custom_policy_table = "creation-placement-test";
  const yb::client::YBTableName placement_table(
    YQL_DATABASE_CQL, table_name().namespace_name(), create_custom_policy_table);

  yb::client::YBSchemaBuilder b;
  yb::client::YBSchema schema;
  b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
  ASSERT_OK(b.Build(&schema));

  // Set placement policy for the new table that is different from the cluster placement policy.
  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(2);
  auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
  auto* cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud("c");
  cloud_info->set_placement_region("r");
  cloud_info->set_placement_zone("z1");
  placement_block->set_min_num_replicas(1);

  placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
  cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud("c");
  cloud_info->set_placement_region("r");
  cloud_info->set_placement_zone("z2");
  placement_block->set_min_num_replicas(1);

  ASSERT_OK(NewTableCreator()->table_name(placement_table).schema(&schema).replication_info(
    replication_info).Create());

  vector<int> counts_per_ts;
  int64 num_tservers = num_tablet_servers();
  GetLoadOnTservers(create_custom_policy_table, num_tservers, &counts_per_ts);
  // Verify that the tserver in zone0 does not have any tablets assigned to it.
  ASSERT_EQ(counts_per_ts[0], 0);
  // Verify that the tservers in z1 and z2 have tablets assigned to them.
  ASSERT_EQ(counts_per_ts[1], 4);
  ASSERT_EQ(counts_per_ts[2], 4);

  // Verify that modifying the placement info for a table with custom placement
  // policy works as expected.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
    placement_table, "c.r.z0,c.r.z1,c.r.z2", 3, ""));
  WaitForLoadBalancer();

  // The replication factor increased to 3, and the placement info now has all 3 zones.
  // Thus, all tservers should have 4 tablets.
  GetLoadOnTservers(create_custom_policy_table, num_tservers, &counts_per_ts);
  for (int ii = 0; ii < 3; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 4);
  }
}

TEST_F(LoadBalancerPlacementPolicyTest, CreateTableWithNondefaultMinNumReplicas) {
  // Set cluster placement policy.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));
  const int new_num_tservers = 4;
  AddNewTserverToZone("z0", new_num_tservers);

  const string& create_custom_policy_table = "creation-placement-test";
  const yb::client::YBTableName placement_table(
    YQL_DATABASE_CQL, table_name().namespace_name(), create_custom_policy_table);

  yb::client::YBSchemaBuilder b;
  yb::client::YBSchema schema;
  b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
  ASSERT_OK(b.Build(&schema));

  // ModifyTablePlacementInfo defaults to 1 min_num_replica, so test table placement with a
  // non-default value of 2.
  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(3);
  auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
  auto* cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud("c");
  cloud_info->set_placement_region("r");
  cloud_info->set_placement_zone("z0");
  placement_block->set_min_num_replicas(2);

  placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
  cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud("c");
  cloud_info->set_placement_region("r");
  cloud_info->set_placement_zone("z1");
  placement_block->set_min_num_replicas(1);

  ASSERT_OK(NewTableCreator()->table_name(placement_table).schema(&schema).replication_info(
    replication_info).Create());

  vector<int> counts_per_ts;
  GetLoadOnTservers(create_custom_policy_table, new_num_tservers, &counts_per_ts);

  // Verify that the tservers in z0 and z1 each have one replicas of the tablets, and z2 has none.
  ASSERT_EQ(counts_per_ts[0], num_tablets()); // z0
  ASSERT_EQ(counts_per_ts[1], num_tablets()); // z1
  ASSERT_EQ(counts_per_ts[2], 0);             // z2
  ASSERT_EQ(counts_per_ts[3], num_tablets()); // z0
}

TEST_F(LoadBalancerPlacementPolicyTest, PlacementPolicyTest) {
  // Set cluster placement policy.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Add a new tserver to zone 1.
  auto num_tservers = num_tablet_servers() + 1;
  AddNewTserverToZone("z1", num_tservers);

  WaitForLoadBalancer();

  // Create another table for which we will set custom placement info.
  const string& custom_policy_table = "placement-test";
  const yb::client::YBTableName placement_table(
    YQL_DATABASE_CQL, table_name().namespace_name(), custom_policy_table);
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
    placement_table.namespace_name(),
    placement_table.namespace_type()));

  yb::client::YBSchemaBuilder b;
  yb::client::YBSchema schema;
  b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
  b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
  ASSERT_OK(b.Build(&schema));

  ASSERT_OK(NewTableCreator()->table_name(placement_table).schema(&schema).Create());

  // New table creation may already leave the cluster balanced with no work for LB to do.
  WaitForLoadBalancerToBeIdle();

  // Modify the placement info for the table.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(placement_table, "c.r.z1,c.r.z2", 3, ""));

  WaitForLoadBalancer();

  // Test 1: Verify placement of tablets for the table with modified placement info.
  vector<int> counts_per_ts;
  GetLoadOnTservers(custom_policy_table, num_tservers, &counts_per_ts);
  // ts0 in c.r.z0 should have no tablets in it.
  ASSERT_EQ(counts_per_ts[0], 0);
  // The other tablet servers should have tablets spread equally.
  ASSERT_EQ(counts_per_ts[1], counts_per_ts[2]);
  ASSERT_EQ(counts_per_ts[2], counts_per_ts[3]);

  // The table with cluster placement policy should have tablets spread across all tservers.
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  for (size_t ii = 0; ii < num_tservers; ++ii) {
    ASSERT_GT(counts_per_ts[ii], 0);
  }

  // Test 2: Verify that custom placement info is honored when tservers are added.
  // Add two new tservers in both z0 and z2.
  ++num_tservers;
  AddNewTserverToZone("z0", num_tservers);

  ++num_tservers;
  AddNewTserverToZone("z2", num_tservers);

  WaitForLoadBalancer();

  GetLoadOnTservers(custom_policy_table, num_tservers, &counts_per_ts);
  for (size_t ii = 0; ii < num_tservers; ++ii) {
    if (ii == 0 || ii == 4) {
      // The table with custom policy should have no tablets in z0, i.e. ts0 and ts4.
      ASSERT_EQ(counts_per_ts[ii], 0);
      continue;
    }
    // The other tablet servers should have tablets in them.
    ASSERT_GT(counts_per_ts[ii], 0);
  }

  // Test 3: Verify that custom placement info is honored when tservers are removed.
  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
    external_mini_cluster()->master(),
    external_mini_cluster()->tablet_server(4)));
  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
    external_mini_cluster()->master(),
    external_mini_cluster()->tablet_server(5)));
  WaitForLoadBalancer();

  num_tservers -= 2;
  GetLoadOnTservers(custom_policy_table, num_tservers, &counts_per_ts);
  // ts0 in c.r.z0 should have no tablets in it.
  ASSERT_EQ(counts_per_ts[0], 0);
  // The other tablet servers should have tablets spread equally.
  ASSERT_EQ(counts_per_ts[1], counts_per_ts[2]);
  ASSERT_EQ(counts_per_ts[2], counts_per_ts[3]);

  // The table with cluster placement policy should continue to have tablets spread across all
  // tservers.
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  for (size_t ii = 0; ii < num_tservers; ++ii) {
    ASSERT_GT(counts_per_ts[ii], 0);
  }
}

TEST_F(LoadBalancerPlacementPolicyTest, AlterPlacementDataConsistencyTest) {
  // Set cluster placement policy.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1", 2, ""));

  // Start workload on a table.
  const string& table = "placement-data-consistency-test";
  const yb::client::YBTableName placement_table(
    YQL_DATABASE_CQL, table_name().namespace_name(), table);

  TestWorkload workload(external_mini_cluster());
  workload.set_table_name(placement_table);
  workload.set_sequential_write(true);
  workload.Setup();
  workload.Start();

  // Change its placement policy such that it now has additional replicas spanning additional
  // tservers.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
      placement_table, "c.r.z0,c.r.z1,c.r.z2", 3, ""));
  WaitForLoadBalancer();

  // Verify that the placement policy is honored.
  vector<int> counts_per_ts;
  GetLoadOnTservers(table, num_tablet_servers(), &counts_per_ts);
  for (int ii = 0; ii < 3; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 1);
  }

  // Change placement policy such that it now spans lesser replicas spanning fewer tservers.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(placement_table, "c.r.z0", 1, ""));
  WaitForLoadBalancer();

  // Verify that placement policy is honored.
  GetLoadOnTservers(table, num_tablet_servers(), &counts_per_ts);
  // The table is RF1 and confined to zone 0. Ts0 should have 1 tablet.
  // The other two tablet servers should not have any tablets.
  ASSERT_EQ(counts_per_ts[0], 1);
  ASSERT_EQ(counts_per_ts[1], 0);
  ASSERT_EQ(counts_per_ts[2], 0);

  // Verify that the data inserted is still sane.
  workload.StopAndJoin();
  auto rows_inserted = workload.rows_inserted();
  LOG(INFO) << "Number of rows inserted: " << rows_inserted;

  // Verify that number of rows is as expected.
  ClusterVerifier cluster_verifier(external_mini_cluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(
    placement_table, ClusterVerifier::EXACTLY, rows_inserted));
}

// HandleAddIfMissingPlacement should not add replicas to zones outside the placement policy.
TEST_F(LoadBalancerPlacementPolicyTest, UnderreplicatedAdd) {
  const int consider_failed_sec = 3;
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Add a tserver in a new zone that is not part of the placement info.
  const int new_num_tservers = 4;
  AddNewTserverToZone("z3", new_num_tservers);

  ASSERT_OK(external_mini_cluster()->SetFlagOnTServers(
      "follower_unavailable_considered_failed_sec", std::to_string(consider_failed_sec)));
  external_mini_cluster()->tablet_server(0)->Shutdown(SafeShutdown::kTrue);
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(
      3 /* num_tservers */, 10s /* timeout */));

  // Wait for ts0 removed from quorum.
  vector<int> counts_per_ts;
  const vector<int> expected_counts_per_ts = {0, 4, 4, 0};
  ASSERT_OK(WaitFor([&] {
    GetLoadOnTservers(table_name().table_name(), new_num_tservers, &counts_per_ts);
    return counts_per_ts == expected_counts_per_ts;
  }, 10s * kTimeMultiplier, "Wait for ts0 removed from quorum."));

  // Should not add a replica in ts3 since that does not fix the under-replication in z0.
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * 2ms);
  WaitForLoadBalancerToBeIdle();
  GetLoadOnTservers(table_name().table_name(), new_num_tservers, &counts_per_ts);
  ASSERT_EQ(counts_per_ts, expected_counts_per_ts);

  ASSERT_OK(external_mini_cluster()->tablet_server(0)->Start());
}

// HandleAddIfWrongPlacement should not add replicas to zones outside the placement policy when
// moving off of a blacklisted node.
TEST_F(LoadBalancerPlacementPolicyTest, BlacklistedAdd) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Add a tserver in a new zone that is not part of the placement info.
  int num_tservers = 4;
  AddNewTserverToZone("z3", num_tservers);

  // Blacklist a tserver to give it "wrong" placement.
  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
    external_mini_cluster()->GetLeaderMaster(),
    external_mini_cluster()->tablet_server(0)
  ));

  SleepFor(3s * kTimeMultiplier);
  WaitForLoadBalancerToBeIdle();

  // Should not move from ts0 as we do not have an alternative in the same zone.
  vector<int> counts_per_ts;
  vector<int> expected_counts_per_ts = {4, 4, 4, 0};
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  ASSERT_VECTORS_EQ(counts_per_ts, expected_counts_per_ts);

  // Should move from the ts0 replica to other tserver in zone 0 (ts4).
  ++num_tservers;
  AddNewTserverToZone("z0", num_tservers);
  WaitForLoadBalanceCompletion();

  expected_counts_per_ts = {0, 4, 4, 0, 4};
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  ASSERT_VECTORS_EQ(counts_per_ts, expected_counts_per_ts);
}

// HandleAddIfWrongPlacement should move replicas to the appropriate zones after placement is
// altered.
TEST_F(LoadBalancerPlacementPolicyTest, AlterPlacement) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Add a tserver in a new zone that is not part of the placement info.
  const int new_num_tservers = 4;
  AddNewTserverToZone("z3", new_num_tservers);

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z1,c.r.z2,c.r.z3", 3, ""));
  WaitForLoadBalanceCompletion();

  // HandleAddIfMissingPlacement should add a ts3 replica to fix minimum placement in z0, then
  // HandleRemoveIfWrongPlacement should remove the ts0 replica to fix the over-replication.
  vector<int> counts_per_ts;
  vector<int> expected_counts_per_ts = {0, 4, 4, 4};
  GetLoadOnTservers(table_name().table_name(), new_num_tservers, &counts_per_ts);
  ASSERT_VECTORS_EQ(counts_per_ts, expected_counts_per_ts);
}

TEST_F(LoadBalancerPlacementPolicyTest, ModifyPlacementUUIDTest) {
  // Set cluster placement policy.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Add 2 tservers with custom placement uuid.
  auto num_tservers = num_tablet_servers() + 1;
  const string& random_placement_uuid = "19dfa091-2b53-434f-b8dc-97280a5f8831";
  AddNewTserverToZone("z1", num_tservers, random_placement_uuid);
  AddNewTserverToZone("z2", ++num_tservers, random_placement_uuid);

  vector<int> counts_per_ts;
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  // The first 3 tservers should have equal number of tablets allocated to them, but the new
  // tservers should not.
  for (int ii = 0; ii < 3; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 4);
  }
  ASSERT_EQ(counts_per_ts[3], 0);
  ASSERT_EQ(counts_per_ts[4], 0);

  // Now there are 2 tservers with custom placement_uuid and 3 tservers with default placement_uuid.
  // Modify the cluster config to have new placement_uuid matching the new tservers.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z1,c.r.z2", 2, random_placement_uuid));

  // Change the table placement policy and verify that the change reflected.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
    table_name(), "c.r.z1,c.r.z2", 2, random_placement_uuid));
  WaitForLoadBalancer();

  // There must now be tablets on the new tservers.
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  ASSERT_EQ(counts_per_ts[3], 4);
  ASSERT_EQ(counts_per_ts[4], 4);

  // Modify the placement policy with different zones and replication factor but with same
  // placement uuid.
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
    table_name(), "c.r.z2", 1, random_placement_uuid));
  WaitForLoadBalancer();

  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  // TS3 belongs to zone1 and will have 0 tablets whereas since TS4 is in zone2 it should have 4
  // tablets allotted to it.
  ASSERT_EQ(counts_per_ts[3], 0);
  ASSERT_EQ(counts_per_ts[4], 4);
}

TEST_F(LoadBalancerPlacementPolicyTest, PrefixPlacementTest) {
  int num_tservers = 3;

  // Test 1.
  // Set prefix cluster placement policy for this region.
  LOG(INFO) << "With c.r,c.r,c.r and num_replicas=3 as placement.";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r,c.r,c.r", 3, ""));
  // Don't need to wait for load balancer as we don't expect any movement.

  // Validate if min_num_replicas is set correctly.
  int min_num_replicas;
  ASSERT_OK(external_mini_cluster()->GetMinReplicaCountForPlacementBlock(
    external_mini_cluster()->master(), "c", "r", "", &min_num_replicas));

  ASSERT_EQ(min_num_replicas, 3);

  // Load should be evenly distributed onto the 3 TS in z0, z1 and z2.
  // With 4 tablets in a table and 3 replica per tablet, each TS should have 4 tablets.
  vector<int> counts_per_ts;
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  for (int ii = 0; ii < 3; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 4);
  }

  // Add 3 tservers in a different region (c.r2.z0, c.r2.z1, c.r2.z2).
  string cloud = "c", region = "r2", zone = "z0";
  AddNewTserverToLocation(cloud, region, zone, ++num_tservers);

  zone = "z1";
  AddNewTserverToLocation(cloud, region, zone, ++num_tservers);

  zone = "z2";
  AddNewTserverToLocation(cloud, region, zone, ++num_tservers);
  // Don't wait for load balancer as we don't anticipate any movement.
  LOG(INFO) << "Added 3 TS to Region r2.";

  // Test 2.
  // Modify placement policy to shift all the load to region r2.
  // From code perspective, this tests HandleAddIfMissingPlacement(),
  // and HandleRemoveReplica().
  // For each replica in r, there will first be a replica created
  // in r2 and then the replica will be removed from r.
  LOG(INFO) << "With c.r2,c.r2,c.r2 and num_replicas=3 as placement.";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r2,c.r2,c.r2", 3, ""));
  WaitForLoadBalancer();

  // Load should be evenly distributed onto the 3 TS in region r2.
  // With 4 tablets in a table and 3 replica per tablet, each TS should have 4 tablets.
  // TS in region r, shouldn't have any load.
  counts_per_ts.clear();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  for (int ii = 0; ii < 3; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 0);
  }

  for (int ii = 3; ii < 6; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 4);
  }

  // Test 3.
  // Shift all the load to region r now.
  // Set min_num_replica for region r to 1 keeping total replicas still 3.
  // From code perspective, this tests HandleAddIfMissingPlacement(),
  // HandleAddIfWrongPlacement() and HandleRemoveReplica().
  // For the second and third replica there won't be any addition to region r
  // because of missing placement (since min_num_replica is 1) but because of a wrong placement.
  LOG(INFO) << "With c.r and num_replicas=3 as placement.";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r", 3, ""));
  WaitForLoadBalancer();

  // Load should be evenly distributed onto the 3 TS in region r.
  // With 4 tablets in a table and 3 replica per tablet, each TS should have 4 tablets.
  // TS in region r2, shouldn't have any load.
  counts_per_ts.clear();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  for (int ii = 0; ii < 3; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 4);
  }

  for (int ii = 3; ii < 6; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 0);
  }

  // Test 4.
  // Reduce the num_replicas to 2 with the same placement.
  // This will test the over-replication part of the code. For each tablet, one replica
  // will be removed.
  LOG(INFO) << "With c.r,c.r and num_replicas=2 as placement.";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r,c.r", 2, ""));
  WaitForLoadBalancer();

  // Total replicas across all tablets: 2*4 = 8.
  // With 3 TS in region r this should split it in a permutation of 3+3+2.
  // TS in region r2 shouldn't have any load.
  counts_per_ts.clear();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  int total_load = 0;
  for (int ii = 0; ii < 3; ++ii) {
    ASSERT_GE(counts_per_ts[ii], 2);
    total_load += counts_per_ts[ii];
  }

  ASSERT_EQ(total_load, 8);

  for (int ii = 3; ii < 6; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 0);
  }

  // Test 5.
  // Blacklist a TS in region r.
  // This tests the blacklist portion of CanSelectWrongReplicaToMove().
  LOG(INFO) << "With c.r,c.r and num_replicas=2 as placement and a TS in region r blacklisted.";
  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
                                          external_mini_cluster()->master(),
                                          external_mini_cluster()->tablet_server(2)));

  WaitForLoadBalancer();
  LOG(INFO) << "Successfully blacklisted ts3.";

  counts_per_ts.clear();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  // 8 replicas distributed across TS with each TS containing 4.
  // No load in region r2.
  for (int ii = 0; ii < 2; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 4);
  }

  for (int ii = 3; ii < 6; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 0);
  }

  // Test 6.
  // Add a TS in region r, zone 2.
  LOG(INFO) << "With c.r,c.r and num_replicas=2 as placement, " <<
                "a blacklisted TS in region r and a new TS added in region r.";

  cloud = "c", region = "r", zone = "z2";
  AddNewTserverToLocation(cloud, region, zone, ++num_tservers);
  WaitForLoadBalancer();
  LOG(INFO) << "Successfully added a TS in region r.";

  counts_per_ts.clear();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  // 8 replicas should be split in permutation of 3+3+2.
  // No load in region r2.
  total_load = 0;
  for (int ii = 0; ii < 2; ++ii) {
    total_load += counts_per_ts[ii];
    ASSERT_GE(counts_per_ts[ii], 2);
  }

  for (int ii = 3; ii < 6; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 0);
  }

  ASSERT_GE(counts_per_ts[6], 2);

  total_load += counts_per_ts[6];
  ASSERT_EQ(total_load, 8);

  // Test 7.
  // Bump up the RF to 3 now keeping the same placement.
  // A replica will be added despite there not being any missing placement.
  LOG(INFO) << "With c.r,c.r and num_replicas=3 as placement, " <<
                "a blacklisted TS in region r and a new TS added in region r.";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r,c.r", 3, ""));
  WaitForLoadBalancer();

  counts_per_ts.clear();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  // Total replicas across all tablets: 3*4 = 12.
  // With 3 TS in region r this should split it in 4+4+4.
  for (int ii = 0; ii < 2; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 4);
  }

  for (int ii = 3; ii < 6; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 0);
  }

  ASSERT_EQ(counts_per_ts[6], 4);

  // Test 8.
  // Change the placement info to only the cloud (c.*.*)
  LOG(INFO) << "With c,c,c and num_replicas=3 as placement, " <<
                "a blacklisted TS in region r and a new TS added in region r.";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c,c,c", 3, ""));
  WaitForLoadBalancer();

  counts_per_ts.clear();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);

  // Total replicas across all tablets: 3*4 = 12.
  // With 6 TS (3 in region r and 3 in r2) this should split it in clusters of 2.
  for (int ii = 0; ii < 2; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 2);
  }

  for (int ii = 3; ii <= 6; ++ii) {
    ASSERT_EQ(counts_per_ts[ii], 2);
  }

  // Some cheap tests for validating user input.
  // Test 9 - Only prefixes allowed.
  LOG(INFO) << "With c..z0,c.r.z0,c.r2.z1 as placement";
  ASSERT_NOK(yb_admin_client_->ModifyPlacementInfo("c..z0,c.r.z0,c.r2.z1", 3, ""));

  // Test 10 - No two prefixes should overlap (-ve test case).
  LOG(INFO) << "With c.r2,c.r2.z0,c.r as placement";
  ASSERT_NOK(yb_admin_client_->ModifyPlacementInfo("c.r2,c.r2.z0,c.r", 3, ""));

  // Test 11 - No two prefixes should overlap (-ve test case).
  LOG(INFO) << "With c,c.r2.z0,c.r as placement";
  ASSERT_NOK(yb_admin_client_->ModifyPlacementInfo("c,c.r2.z0,c.r", 3, ""));

  // Test 12 - No two prefixes should overlap (+ve test case).
  LOG(INFO) << "With c.r.z0,c.r2.z0,c.r.z2 as placement";
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r2.z0,c.r.z2", 3, ""));

  // Test 13 - All CRZ empty allowed.
  LOG(INFO) << "With ,, as placement";
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(",,", 3, ""));

  // FIN: Thank you all for watching, have a great day ahead!
}

class LoadBalancerReadReplicaPlacementPolicyTest : public LoadBalancerPlacementPolicyTest {
 protected:
  const string kReadReplicaPlacementUuid = "read_replica";
};

class LoadBalancerReadReplicaPlacementPolicyBlacklistTest :
    public LoadBalancerReadReplicaPlacementPolicyTest, public ::testing::WithParamInterface<bool>
    {};
INSTANTIATE_TEST_SUITE_P(, LoadBalancerReadReplicaPlacementPolicyBlacklistTest, ::testing::Bool());

// Regression test for GitHub issue #15698, if using param false;
TEST_P(LoadBalancerReadReplicaPlacementPolicyBlacklistTest, Test) {
  bool use_empty_table_placement = GetParam();

  // Add 2 read replicas to cluster placement policy.
  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z0", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z0", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_EQ(num_tservers, 5);

  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z0:0", 1 /* replication_factor */, kReadReplicaPlacementUuid));

  DeleteTable();
  if (use_empty_table_placement) {
    master::ReplicationInfoPB ri;
    ASSERT_OK(NewTableCreator()->table_name(table_name())
        .schema(&schema_).replication_info(ri).Create());
  } else {
    ASSERT_OK(NewTableCreator()->table_name(table_name()).schema(&schema_).Create());
  }

  // There should be 2 tablets on each of the read replicas since we start with 4 tablets and
  // the replication factor for read replicas is 1.
  // Note that we shouldn't have to wait for the load balancer here, since the table creation
  // should evenly spread the tablets across both read replicas.
  vector<int> counts_per_ts;
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  vector<int> expected_counts_per_ts = {4, 4, 4, 2, 2};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  // Blacklist one of the read replicas. The tablets should all move to the other read replica.
  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
      external_mini_cluster()->master(),
      external_mini_cluster()->tablet_server(4)));
  WaitForLoadBalancer();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  expected_counts_per_ts = {4, 4, 4, 4, 0};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  // Clear the blacklist. The tablets should spread evenly across both read replicas.
  ASSERT_OK(external_mini_cluster()->ClearBlacklist(external_mini_cluster()->master()));
  WaitForLoadBalancer();
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  expected_counts_per_ts = {4, 4, 4, 2, 2};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

TEST_F(LoadBalancerReadReplicaPlacementPolicyTest, DefaultMinNumReplicas) {
  // Add 2 read replicas to cluster placement policy.
  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z0", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z1", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_EQ(num_tservers, 5);

  // Should fail because AddReadReplicaPlacementInfo defaults to one replica per placement block,
  // and the replication factor is less than the sum of replicas per placement block.
  ASSERT_NOK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z0,c.r.z1", 1 /* replication_factor */, kReadReplicaPlacementUuid));
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z0,c.r.z1", 2 /* replication_factor */, kReadReplicaPlacementUuid));

  DeleteTable();
  ASSERT_OK(NewTableCreator()->table_name(table_name()).schema(&schema_).Create());

  // Note that we shouldn't have to wait for the load balancer here, since the table creation
  // should evenly spread the tablets across both read replicas.
  vector<int> counts_per_ts;
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  vector<int> expected_counts_per_ts = {4, 4, 4, 4, 4};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

} // namespace integration_tests
} // namespace yb
