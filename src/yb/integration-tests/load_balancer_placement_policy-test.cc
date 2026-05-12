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

#include <algorithm>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"


#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_cluster_client.h"
#include "yb/master/master_types.pb.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_tablespace_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using std::string;
using std::vector;

using namespace std::literals;

DECLARE_int32(catalog_manager_bg_task_wait_ms);
METRIC_DECLARE_entity(cluster);
METRIC_DECLARE_gauge_uint32(total_table_load_difference);

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

  Result<vector<int>> GetLoadOnTserversByTableId(
      const TableId& table_id, size_t num_tservers) {
    vector<int> load_per_tserver;
    load_per_tserver.reserve(num_tservers);
    for (size_t i = 0; i < num_tservers; ++i) {
      int count = VERIFY_RESULT(GetLoadOnTserverByTableId(
          external_mini_cluster()->tablet_server(i), table_id));
      load_per_tserver.emplace_back(count);
    }
    return load_per_tserver;
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

  Result<uint32_t> GetLoadOnTserverByTableId(
      ExternalTabletServer* server, const TableId& table_id) {
    auto proxy = GetMasterLeaderProxy<master::MasterClientProxy>();
    master::GetTableLocationsRequestPB req;
    req.mutable_table()->set_table_id(table_id);
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
  ReplicationInfoPB replication_info;
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
  int new_num_tservers = 4;
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
  ReplicationInfoPB replication_info;
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

  TestYcqlWorkload workload(external_mini_cluster());
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
  const int consider_failed_sec = 6;
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

  // Should not move from ts0 as we do not have an alternative in the same zone.
  SleepFor(3s * kTimeMultiplier);
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

  // FIN: Thank you all for watching, have a great day ahead!
}

class LoadBalancerReadReplicaPlacementPolicyTest : public LoadBalancerPlacementPolicyTest {
 protected:
  const string kReadReplicaPlacementUuid = "read_replica";
};

class LoadBalancerReadReplicaPlacementPolicyYsqlTest :
    public LoadBalancerReadReplicaPlacementPolicyTest {
 protected:
  bool enable_ysql() override {
    return true;
  }

  Result<TableId> FindYsqlTableId(
      const std::string& database_name, const std::string& table_name) {
    auto tables = VERIFY_RESULT(
        client_->ListTables(table_name, /*exclude_ysql=*/false, database_name));
    auto table_it = std::find_if(
        tables.begin(), tables.end(), [&](const client::YBTableName& table) {
          return table.table_name() == table_name;
        });
    if (table_it == tables.end()) {
      return STATUS_FORMAT(
          NotFound, "Unable to find YSQL table $0.$1", database_name, table_name);
    }
    return table_it->table_id();
  }
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
    ReplicationInfoPB ri;
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

TEST_F(LoadBalancerReadReplicaPlacementPolicyTest, TotalTableLoadDifferenceMetric) {
  // Disable load balancer adds.
  ASSERT_OK(external_mini_cluster()->SetFlagOnMasters("load_balancer_max_concurrent_adds", "0"));
  size_t num_tservers = num_tablet_servers();

  // Add a tserver to z0 and a read replica to z0.
  AddNewTserverToZone("z0", ++num_tservers);
  AddNewTserverToZone("z0", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0, c.r.z1, c.r.z2", 3 /* replication_factor */, ""));
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z0", 1 /* replication_factor */, kReadReplicaPlacementUuid));

  vector<int> counts_per_ts;
  GetLoadOnTservers(table_name().table_name(), num_tservers, &counts_per_ts);
  vector<int> expected_counts_per_ts = {4, 4, 4, 0, 0};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  // Wait for the total table load difference metric to reflect the tablets that need to be moved.
  // We expect 2 adds to the new live tserver (from the tserver in z0) and 4 adds to the new read
  // replica.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto* master = external_mini_cluster()->GetLeaderMaster();
    auto num_adds = VERIFY_RESULT(master->GetMetric<uint32_t>(
        &METRIC_ENTITY_cluster, NULL, &METRIC_total_table_load_difference, "value"));
    LOG(INFO) << "Number of adds: " << num_adds;
    return num_adds == 6;
  }, 10s, "Total table load difference metric should reflect the tablets that need to be moved."));
}

// `yb-admin add_read_replica_placement_info` and the underlying YBA flow refuse to add a second
// read-replica cluster to a universe that already has one. The master's ChangeMasterClusterConfig
// RPC, however, has no such guard. This test bypasses the yb-admin guard by sending the RPC
// directly, then verifies that a universe configured with two read-replica clusters actually
// places and balances tablets across both clusters.
TEST_F(LoadBalancerReadReplicaPlacementPolicyYsqlTest, MultipleReadReplicaClustersViaRpc) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  const std::string kReadReplicaPlacementUuid2 = "read_replica_2";

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z4", ++num_tservers, kReadReplicaPlacementUuid2);
  ASSERT_EQ(num_tservers, 5);

  // Add the first read-replica cluster the normal way.
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3", 1 /* replication_factor */, kReadReplicaPlacementUuid));

  // Confirm yb-admin refuses to add a second read-replica cluster.
  Status add_second_via_yb_admin = yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z4", 1 /* replication_factor */, kReadReplicaPlacementUuid2);
  ASSERT_NOK(add_second_via_yb_admin);
  ASSERT_STR_CONTAINS(
      add_second_via_yb_admin.ToString(),
      "Already have a read replica placement, cannot add another");

  // Bypass the yb-admin guard: build a config with two read-replica clusters and push it via
  // ChangeMasterClusterConfig directly. The master accepts the config without complaint.
  master::MasterClusterClient cluster_client(
      GetMasterLeaderProxy<master::MasterClusterProxy>());
  auto cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 1);

  auto* second_rr = cluster_config.mutable_replication_info()->add_read_replicas();
  second_rr->set_placement_uuid(kReadReplicaPlacementUuid2);
  second_rr->set_num_replicas(1);
  auto* placement_block = second_rr->add_placement_blocks();
  auto* cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud("c");
  cloud_info->set_placement_region("r");
  cloud_info->set_placement_zone("z4");
  placement_block->set_min_num_replicas(1);

  ASSERT_OK(cluster_client.ChangeMasterClusterConfig(std::move(cluster_config)));

  // Confirm both read-replica clusters are now in the persisted config.
  auto updated_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(updated_config.replication_info().read_replicas_size(), 2);

  // Drop the YCQL table created by SetUp and create a YSQL table that picks up the new placement.
  DeleteTable();
  const std::string kDatabaseName = "yugabyte";
  const std::string kPgTable = "t";
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());
  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 4 TABLETS", kPgTable));

  WaitForLoadBalancerToBeIdle();

  // 4 tablets, RF=3 in the live cluster (z0, z1, z2 each have one tserver), and RF=1 in each of
  // the two read-replica clusters (z3 has one tserver in cluster "read_replica", z4 has one
  // tserver in cluster "read_replica_2"). Every tserver should hold all 4 tablets.
  const TableId table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));
  vector<int> counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  vector<int> expected_counts_per_ts = {4, 4, 4, 4, 4};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

// Removing a read-replica cluster and then adding it back (with the same placement_uuid and the
// same physical tserver) should leave the universe in a clean working state: the cluster config
// reflects each transition, and a freshly created table after the re-add gets placed on the
// read-replica tserver as expected.
TEST_F(LoadBalancerReadReplicaPlacementPolicyYsqlTest, RemoveAndReAddReadReplicaCluster) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_EQ(num_tservers, 4);

  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3", 1 /* replication_factor */, kReadReplicaPlacementUuid));

  master::MasterClusterClient cluster_client(
      GetMasterLeaderProxy<master::MasterClusterProxy>());
  auto cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 1);

  // Drop the YCQL table created by SetUp and create a YSQL table that picks up the read-replica
  // placement.
  DeleteTable();
  const std::string kDatabaseName = "yugabyte";
  const std::string kPgTable = "t";
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());
  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 4 TABLETS", kPgTable));
  WaitForLoadBalancerToBeIdle();

  // 4 tablets, RF=3 in the live cluster + RF=1 in the read-replica cluster: every tserver
  // (live and read-replica) should hold all 4 tablets.
  TableId table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));
  vector<int> counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  vector<int> expected_counts_per_ts = {4, 4, 4, 4};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  // Remove the read-replica cluster from the cluster config.
  ASSERT_OK(yb_admin_client_->DeleteReadReplicaPlacementInfo());
  cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 0);

  // Re-add the read-replica cluster with the same placement_uuid.
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3", 1 /* replication_factor */, kReadReplicaPlacementUuid));
  cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 1);
  ASSERT_EQ(
      cluster_config.replication_info().read_replicas(0).placement_uuid(),
      kReadReplicaPlacementUuid);

  // After dropping and recreating the table, the existing z3 tserver should be picked up by the
  // re-added read-replica cluster and again receive a copy of every tablet.
  ASSERT_OK(pg_conn.ExecuteFormat("DROP TABLE $0", kPgTable));
  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 4 TABLETS", kPgTable));
  WaitForLoadBalancerToBeIdle();

  table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));
  counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  expected_counts_per_ts = {4, 4, 4, 4};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

class TablespaceReadReplicaTest : public LoadBalancerReadReplicaPlacementPolicyYsqlTest {};

// Parameterized over whether the tablespace's read_replica_placement option specifies an
// explicit placement_uuid (true) or omits it and relies on the master to auto-populate the
// uuid from the cluster config (false).
class TablespaceReadReplicaUuidTest :
    public LoadBalancerReadReplicaPlacementPolicyYsqlTest,
    public ::testing::WithParamInterface<bool> {
 protected:
  // The placement_uuid to embed in the tablespace's read_replica_placement option: either the
  // matching cluster RR uuid (explicit) or the empty string (implicit, master auto-populates).
  std::string TablespaceRrUuid() const {
    return GetParam() ? kReadReplicaPlacementUuid : "";
  }

  // Verify the generated CREATE TABLESPACE command does/does not contain `placement_uuid`,
  // matching the current parameterized mode.
  void AssertCreateCmdHasPlacementUuid(const test::Tablespace& tablespace) const {
    ASSERT_STR_CONTAINS(tablespace.CreateCmd(), "read_replica_placement='{\"");
    ASSERT_STR_NOT_CONTAINS(tablespace.CreateCmd(), "read_replica_placement='[");
    if (GetParam()) {
      ASSERT_STR_CONTAINS(tablespace.CreateCmd(), "placement_uuid");
    } else {
      ASSERT_STR_NOT_CONTAINS(tablespace.CreateCmd(), "placement_uuid");
    }
  }
};

INSTANTIATE_TEST_SUITE_P(
    , TablespaceReadReplicaUuidTest, ::testing::Bool(),
    [](const ::testing::TestParamInfo<bool>& info) {
      return info.param ? "ExplicitUuid" : "ImplicitUuid";
    });

// Test creating a table in a tablespace that has a read replica. The tablespace's
// read_replica_placement option either embeds an explicit placement_uuid or omits it and lets
// the master auto-populate from the (single) cluster-level read-replica placement, depending on
// the test parameter.
TEST_P(TablespaceReadReplicaUuidTest, TestTablespaceReadReplicaBasic) {
  const auto kTablespaceName = "test_tablespace";
  const auto kDatabaseName = "yugabyte";
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z4", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3,c.r.z4", 2 /* replication_factor */, kReadReplicaPlacementUuid));

  DeleteTable();
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());

  const test::Tablespace tablespace(
      kTablespaceName,
      /* numReplicas = */ 3,
      {test::PlacementBlock("c", "r", "z0", 1), test::PlacementBlock("c", "r", "z1", 1),
       test::PlacementBlock("c", "r", "z2", 1)},
      {test::PlacementBlock("c", "r", "z4", 1)},
      /* read_replica_num_replicas = */ std::nullopt,
      /* read_replica_placement_uuid = */ TablespaceRrUuid());

  const std::string kPgTable = "t";

  AssertCreateCmdHasPlacementUuid(tablespace);

  ASSERT_OK(pg_conn.Execute(tablespace.CreateCmd()));

  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) TABLESPACE $1 SPLIT INTO 1 TABLETS",
      kPgTable,
      tablespace.name));

  WaitForLoadBalancerToBeIdle();

  const TableId table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));

  vector<int> counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));

  // We create read replicas in z3 and z4, but the tablespace specifies to only
  // place tablets on z4.
  vector<int> expected_counts_per_ts = {1, 1, 1, 0, 1};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

// Tablespaces with read-replica placement either embed an explicit placement_uuid or omit it
// (and the master auto-populates it from the cluster config), depending on the test parameter.
TEST_P(TablespaceReadReplicaUuidTest, TestTablespaceReadReplicaAlter) {
  const auto kTablespaceWithoutReadReplica = "ts_without_rr";
  const auto kTablespaceWithReadReplica = "ts_with_rr";
  const auto kTablespaceWithReadReplicaAlt = "ts_with_rr_alt";
  const auto kDatabaseName = "yugabyte";
  const auto kPgTable = "t";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z4", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3,c.r.z4", 2 /* replication_factor */, kReadReplicaPlacementUuid));

  DeleteTable();
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());

  const std::vector<test::PlacementBlock> live_placement_blocks = {
      test::PlacementBlock("c", "r", "z0", 1), test::PlacementBlock("c", "r", "z1", 1),
      test::PlacementBlock("c", "r", "z2", 1)};

  const test::Tablespace tablespace_without_read_replica(
      kTablespaceWithoutReadReplica,
      /* numReplicas = */ 3, live_placement_blocks);

  const test::Tablespace tablespace_with_read_replica(
      kTablespaceWithReadReplica,
      /* numReplicas = */ 3, live_placement_blocks, {test::PlacementBlock("c", "r", "z4", 1)},
      /* read_replica_num_replicas = */ std::nullopt,
      /* read_replica_placement_uuid = */ TablespaceRrUuid());

  const test::Tablespace tablespace_with_read_replica_alt(
      kTablespaceWithReadReplicaAlt,
      /* numReplicas = */ 3, live_placement_blocks, {test::PlacementBlock("c", "r", "z3", 1)},
      /* read_replica_num_replicas = */ std::nullopt,
      /* read_replica_placement_uuid = */ TablespaceRrUuid());

  AssertCreateCmdHasPlacementUuid(tablespace_with_read_replica);
  AssertCreateCmdHasPlacementUuid(tablespace_with_read_replica_alt);

  ASSERT_OK(pg_conn.Execute(tablespace_without_read_replica.CreateCmd()));
  ASSERT_OK(pg_conn.Execute(tablespace_with_read_replica.CreateCmd()));
  ASSERT_OK(pg_conn.Execute(tablespace_with_read_replica_alt.CreateCmd()));

  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) TABLESPACE $1 SPLIT INTO 1 TABLETS",
      kPgTable,
      tablespace_without_read_replica.name));

  WaitForLoadBalancerToBeIdle();

  const TableId table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));

  vector<int> counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  vector<int> expected_counts_per_ts = {1, 1, 1, 0, 0};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  ASSERT_OK(pg_conn.ExecuteFormat(
      "ALTER TABLE $0 SET TABLESPACE $1", kPgTable, tablespace_with_read_replica.name));
  WaitForLoadBalancer();
  counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  expected_counts_per_ts = {1, 1, 1, 0, 1};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  ASSERT_OK(pg_conn.ExecuteFormat(
      "ALTER TABLE $0 SET TABLESPACE $1", kPgTable, tablespace_with_read_replica_alt.name));
  WaitForLoadBalancer();
  counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  expected_counts_per_ts = {1, 1, 1, 1, 0};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

// Test that when a table with read replicas is altered to use a tablespace without read replicas,
// the read replicas are removed.
TEST_F(TablespaceReadReplicaTest, TestAlterTableToTablespaceWithoutReadReplica) {
  const auto kTablespaceNoReadReplica = "ts_no_rr";
  const auto kDatabaseName = "yugabyte";
  const auto kPgTableName = "test_pg_table";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z4", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3,c.r.z4", 2 /* replication_factor */, kReadReplicaPlacementUuid));

  DeleteTable();
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());

  // Create a table in the default tablespace. This table will inherit read replicas
  // from the cluster-level configuration.
  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 1 TABLETS", kPgTableName));

  WaitForLoadBalancerToBeIdle();

  const TableId table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTableName));

  vector<int> counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  vector<int> expected_counts_per_ts = {1, 1, 1, 1, 1};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  // Create a tablespace without read replicas
  const test::Tablespace tablespace_no_read_replica(
      kTablespaceNoReadReplica,
      /* numReplicas = */ 3,
      {test::PlacementBlock("c", "r", "z0", 1), test::PlacementBlock("c", "r", "z1", 1),
       test::PlacementBlock("c", "r", "z2", 1)});

  ASSERT_OK(pg_conn.Execute(tablespace_no_read_replica.CreateCmd()));

  ASSERT_OK(pg_conn.ExecuteFormat(
      "ALTER TABLE $0 SET TABLESPACE $1", kPgTableName, tablespace_no_read_replica.name));
  WaitForLoadBalancer();

  counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  expected_counts_per_ts = {1, 1, 1, 0, 0};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

// Test creating a table in a tablespace that has a read replica using a wildcard placement
// block. The tablespace either embeds an explicit placement_uuid or omits it (and the master
// auto-populates it from the cluster config), depending on the test parameter.
TEST_P(TablespaceReadReplicaUuidTest, TestTablespaceReadReplicaWildcard) {
  const auto kTablespaceName = "test_tablespace";
  const auto kDatabaseName = "yugabyte";
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z4", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3,c.r.z4", 2 /* replication_factor */, kReadReplicaPlacementUuid));

  DeleteTable();
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());

  const test::Tablespace tablespace(
      kTablespaceName,
      /* numReplicas = */ 3,
      {test::PlacementBlock("c", "r", "z0", 1), test::PlacementBlock("c", "r", "z1", 1),
       test::PlacementBlock("c", "r", "z2", 1)},
      {test::PlacementBlock("c", "*", "*", 2)},
      /* read_replica_num_replicas = */ std::nullopt,
      /* read_replica_placement_uuid = */ TablespaceRrUuid());

  const std::string kPgTable = "t";
  LOG(INFO) << "Creating tablespace: " << tablespace.CreateCmd();

  AssertCreateCmdHasPlacementUuid(tablespace);

  ASSERT_OK(pg_conn.Execute(tablespace.CreateCmd()));

  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) TABLESPACE $1 SPLIT INTO 1 TABLETS",
      kPgTable,
      tablespace.name));

  WaitForLoadBalancerToBeIdle();

  const TableId table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));

  vector<int> counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));

  // We expect the tablets to be spread across all of the read replicas,
  // since the wildcard placement block matches both z3 and z4.
  vector<int> expected_counts_per_ts = {1, 1, 1, 1, 1};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

// Creates a YSQL table in a tablespace whose read_replica_placement targets the RR
// cluster (with explicit or implicit placement_uuid per the test parameter), removes the
// cluster RR cluster and re-adds it with the same placement_uuid, then drops and recreates
// the table and verifies the new table picks up the re-added RR cluster as expected.
//
// The implicit-uuid mode is the more interesting case: the auto-populated placement_uuid in
// the master's in-memory tablespace map gets re-bound on the next refresh after the re-add.
// The explicit-uuid mode exercises the simpler hard-coded path.
TEST_P(TablespaceReadReplicaUuidTest, RemoveAndReAddReadReplicaCluster) {
  const auto kTablespaceName = "ts_with_rr";
  const auto kDatabaseName = "yugabyte";
  const auto kPgTable = "t";

  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  ASSERT_EQ(num_tservers, 4);

  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3", 1 /* replication_factor */, kReadReplicaPlacementUuid));

  master::MasterClusterClient cluster_client(
      GetMasterLeaderProxy<master::MasterClusterProxy>());
  auto cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 1);

  DeleteTable();
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());

  const test::Tablespace tablespace(
      kTablespaceName,
      /* numReplicas = */ 3,
      {test::PlacementBlock("c", "r", "z0", 1), test::PlacementBlock("c", "r", "z1", 1),
       test::PlacementBlock("c", "r", "z2", 1)},
      {test::PlacementBlock("c", "r", "z3", 1)},
      /* read_replica_num_replicas = */ std::nullopt,
      /* read_replica_placement_uuid = */ TablespaceRrUuid());

  AssertCreateCmdHasPlacementUuid(tablespace);

  ASSERT_OK(pg_conn.Execute(tablespace.CreateCmd()));

  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) TABLESPACE $1 SPLIT INTO 4 TABLETS",
      kPgTable, tablespace.name));
  WaitForLoadBalancerToBeIdle();

  // 4 tablets, RF=3 in the live cluster (z0,z1,z2) + RF=1 on the RR tserver (z3): every
  // tserver should hold all 4 tablets.
  TableId table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));
  vector<int> counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  vector<int> expected_counts_per_ts = {4, 4, 4, 4};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  // Remove the cluster's read-replica cluster from the cluster config.
  ASSERT_OK(yb_admin_client_->DeleteReadReplicaPlacementInfo());
  cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 0);

  // Re-add the read-replica cluster with the same placement_uuid.
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3", 1 /* replication_factor */, kReadReplicaPlacementUuid));
  cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 1);
  ASSERT_EQ(
      cluster_config.replication_info().read_replicas(0).placement_uuid(),
      kReadReplicaPlacementUuid);

  // After dropping and recreating the table in the same tablespace, the existing z3 tserver
  // should be picked up by the re-added read-replica cluster and again receive a copy of every
  // tablet. This exercises the tablespace-map refresh re-binding to the current cluster RR
  // (in implicit-uuid mode) or the unchanged hard-coded uuid still matching (in explicit-uuid
  // mode).
  ASSERT_OK(pg_conn.ExecuteFormat("DROP TABLE $0", kPgTable));
  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) TABLESPACE $1 SPLIT INTO 4 TABLETS",
      kPgTable, tablespace.name));
  WaitForLoadBalancerToBeIdle();

  table_id = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTable));
  counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id, num_tservers));
  expected_counts_per_ts = {4, 4, 4, 4};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

// Same setup as MultipleReadReplicaClustersViaRpc above, but verifies that a tablespace with a
// read-replica placement that omits placement_uuid is effectively invalid when multiple
// read-replica clusters exist (the master cannot auto-populate the UUID and marks the tablespace
// as invalid during background validation, so tables in it fall back to cluster-level placement).
// When the tablespace includes an explicit placement_uuid, it works correctly and directs read
// replicas to the targeted cluster.
TEST_F(TablespaceReadReplicaTest, MultipleReadReplicaClustersTablespace) {
  const auto kDatabaseName = "yugabyte";
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "c.r.z0,c.r.z1,c.r.z2", 3 /* replication_factor */, ""));

  const std::string kReadReplicaPlacementUuid2 = "read_replica_2";

  size_t num_tservers = num_tablet_servers();
  AddNewTserverToZone("z3", ++num_tservers, kReadReplicaPlacementUuid);
  AddNewTserverToZone("z4", ++num_tservers, kReadReplicaPlacementUuid2);
  ASSERT_EQ(num_tservers, 5);

  // Add the first read-replica cluster.
  ASSERT_OK(yb_admin_client_->AddReadReplicaPlacementInfo(
      "c.r.z3", 1 /* replication_factor */, kReadReplicaPlacementUuid));

  // Bypass yb-admin to add a second read-replica cluster via direct RPC.
  master::MasterClusterClient cluster_client(
      GetMasterLeaderProxy<master::MasterClusterProxy>());
  auto cluster_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(cluster_config.replication_info().read_replicas_size(), 1);

  auto* second_rr = cluster_config.mutable_replication_info()->add_read_replicas();
  second_rr->set_placement_uuid(kReadReplicaPlacementUuid2);
  second_rr->set_num_replicas(1);
  auto* placement_block = second_rr->add_placement_blocks();
  auto* cloud_info = placement_block->mutable_cloud_info();
  cloud_info->set_placement_cloud("c");
  cloud_info->set_placement_region("r");
  cloud_info->set_placement_zone("z4");
  placement_block->set_min_num_replicas(1);
  ASSERT_OK(cluster_client.ChangeMasterClusterConfig(std::move(cluster_config)));

  auto updated_config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  ASSERT_EQ(updated_config.replication_info().read_replicas_size(), 2);

  DeleteTable();
  auto pg_conn = ASSERT_RESULT(external_mini_cluster()->ConnectToDB());

  const std::vector<test::PlacementBlock> live_placement_blocks = {
      test::PlacementBlock("c", "r", "z0", 1), test::PlacementBlock("c", "r", "z1", 1),
      test::PlacementBlock("c", "r", "z2", 1)};

  // (1) A tablespace whose read-replica placement omits placement_uuid is created successfully
  // at the SQL level, but the master's background validation rejects it (because there are
  // multiple read-replica clusters and the UUID is ambiguous). Tables created in this tablespace
  // fall back to cluster-level placement, which includes both read-replica clusters.
  const test::Tablespace tablespace_no_uuid(
      "ts_multi_rr_no_uuid",
      /* numReplicas = */ 3, live_placement_blocks, {test::PlacementBlock("c", "r", "z3", 1)},
      /* read_replica_num_replicas = */ std::nullopt,
      /* read_replica_placement_uuid = */ "");
  ASSERT_OK(pg_conn.Execute(tablespace_no_uuid.CreateCmd()));

  const std::string kPgTableNoUuid = "t_no_uuid";
  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) TABLESPACE $1 SPLIT INTO 1 TABLETS",
      kPgTableNoUuid, tablespace_no_uuid.name));

  WaitForLoadBalancerToBeIdle();

  const TableId table_id_no_uuid = ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTableNoUuid));
  vector<int> counts_per_ts =
      ASSERT_RESULT(GetLoadOnTserversByTableId(table_id_no_uuid, num_tservers));
  // The tablespace wanted only z3, but because it was invalidated (no placement_uuid with
  // multiple RR clusters), the table falls back to cluster-level placement which has both RR
  // clusters. Every tserver gets a tablet.
  vector<int> expected_counts_per_ts = {1, 1, 1, 1, 1};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);

  // (2) A tablespace with an explicit placement_uuid works correctly and directs read replicas
  // to the targeted cluster only.
  const test::Tablespace tablespace_with_uuid(
      "ts_multi_rr_with_uuid",
      /* numReplicas = */ 3, live_placement_blocks, {test::PlacementBlock("c", "r", "z4", 1)},
      /* read_replica_num_replicas = */ std::nullopt,
      /* read_replica_placement_uuid = */ kReadReplicaPlacementUuid2);

  ASSERT_OK(pg_conn.Execute(tablespace_with_uuid.CreateCmd()));

  const std::string kPgTableWithUuid = "t_with_uuid";
  ASSERT_OK(pg_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) TABLESPACE $1 SPLIT INTO 1 TABLETS",
      kPgTableWithUuid, tablespace_with_uuid.name));

  WaitForLoadBalancerToBeIdle();

  const TableId table_id_with_uuid =
      ASSERT_RESULT(FindYsqlTableId(kDatabaseName, kPgTableWithUuid));
  counts_per_ts = ASSERT_RESULT(GetLoadOnTserversByTableId(table_id_with_uuid, num_tservers));
  // Live replicas on z0, z1, z2; read replica only on z4 (the targeted cluster).
  expected_counts_per_ts = {1, 1, 1, 0, 1};
  ASSERT_VECTORS_EQ(expected_counts_per_ts, counts_per_ts);
}

} // namespace integration_tests
} // namespace yb
