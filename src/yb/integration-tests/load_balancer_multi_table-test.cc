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

#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/client/schema.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/gutil/strings/join.h"
#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_balancer_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master-test-util.h"
#include "yb/master/sys_catalog.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/util/monotime.h"

using namespace std::literals;

namespace yb {
namespace integration_tests {

const auto kDefaultTimeout = 30000ms;
constexpr int kNumTables = 3;
constexpr int kMovesPerTable = 1;

// We need multiple tables in order to test load_balancer_max_concurrent_moves_per_table.
class LoadBalancerMultiTableTest : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  bool enable_ysql() override {
    // Do not create the transaction status table.
    return false;
  }

  int num_tablets() override {
    return 5;
  }

  client::YBTableName table_name() override {
    return table_names_[0];
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--placement_cloud=c");
    opts->extra_tserver_flags.push_back("--placement_region=r");
    opts->extra_tserver_flags.push_back("--placement_zone=z${index}");
    opts->extra_master_flags.push_back("--load_balancer_skip_leader_as_remove_victim=false");
    opts->extra_master_flags.push_back("--load_balancer_max_concurrent_moves=10");
    opts->extra_master_flags.push_back("--load_balancer_max_concurrent_moves_per_table="
                                       + std::to_string(kMovesPerTable));
    opts->extra_master_flags.push_back("--enable_global_load_balancing=true");
  }

  void CreateTables() {
    for (int i = 1; i <= kNumTables; ++i) {
      table_names_.emplace_back(YQL_DATABASE_CQL,
                               "my_keyspace-" + std::to_string(i),
                               "kv-table-test-" + std::to_string(i));
    }

    for (const auto& tn : table_names_) {
      ASSERT_OK(client_->CreateNamespaceIfNotExists(tn.namespace_name(), tn.namespace_type()));

      client::YBSchemaBuilder b;
      b.AddColumn("k")->Type(BINARY)->NotNull()->HashPrimaryKey();
      b.AddColumn("v")->Type(BINARY)->NotNull();
      ASSERT_OK(b.Build(&schema_));

      ASSERT_OK(NewTableCreator()->table_name(tn).schema(&schema_).Create());
    }
  }

  void DeleteTables() {
    for (const auto& tn : table_names_) {
      ASSERT_OK(client_->DeleteTable(tn));
    }
    table_names_.clear();
  }

  void CreateTable() override {
    if (!table_exists_) {
      CreateTables();
      table_exists_ = true;
    }
  }

  void DeleteTable() override {
    if (table_exists_) {
      DeleteTables();
      table_exists_ = false;
    }
  }

  Result<bool> is_ts_stale(int ts_idx) {
    std::shared_ptr<master::MasterServiceProxy> proxy = external_mini_cluster()->master_proxy();
    std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
    master::ListTabletServersRequestPB req;
    master::ListTabletServersResponsePB resp;
    controller->Reset();

    proxy->ListTabletServers(req, &resp, controller.get());

    bool is_stale = false, is_ts_found = false;
    for (int i = 0; i < resp.servers_size(); i++) {
      if (!resp.servers(i).has_instance_id()) {
        return STATUS_SUBSTITUTE(
          Uninitialized,
          "ListTabletServers RPC returned a TS with uninitialized instance id."
        );
      }

      if (!resp.servers(i).instance_id().has_permanent_uuid()) {
        return STATUS_SUBSTITUTE(
          Uninitialized,
          "ListTabletServers RPC returned a TS with uninitialized UUIDs."
        );
      }

      if (resp.servers(i).instance_id().permanent_uuid() ==
                      external_mini_cluster()->tablet_server(ts_idx)->uuid()) {
        is_ts_found = true;
        is_stale = !resp.servers(i).alive();
      }
    }

    if (!is_ts_found) {
      return STATUS_SUBSTITUTE(
          NotFound,
          "Given TS not found in ListTabletServers RPC."
      );
    }
    return is_stale;
  }
};

TEST_F(LoadBalancerMultiTableTest, MultipleLeaderTabletMovesPerTable) {
  const int test_bg_task_wait_ms = 5000;

  // Start with 3 tables each with 5 tablets on 3 servers.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Disable leader balancing.
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(
      external_mini_cluster_->master(i), "load_balancer_max_concurrent_moves", "0"));
    // Don't remove leaders to ensure that we still have a leader move for each table later on.
    ASSERT_OK(external_mini_cluster_->SetFlag(
      external_mini_cluster_->master(i), "load_balancer_skip_leader_as_remove_victim", "true"));
  }

  // Add new tserver.
  std::vector<std::string> extra_opts;
  extra_opts.push_back("--placement_cloud=c");
  extra_opts.push_back("--placement_region=r");
  extra_opts.push_back("--placement_zone=z1");
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 1,
      kDefaultTimeout));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Get current leader counts.
  unordered_map<string, unordered_map<string, int>> initial_leader_counts;
  for (const auto& tn : table_names_) {
    initial_leader_counts[tn.table_name()] = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
  }

  // Enable leader balancing and also increase LB run delay.
  LOG(INFO) << "Re-enabling leader balancing.";
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "load_balancer_max_concurrent_moves", "10"));
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "catalog_manager_bg_task_wait_ms",
                                              std::to_string(test_bg_task_wait_ms)));
  }

  // Wait for one run of the load balancer to complete
  SleepFor(MonoDelta::FromMilliseconds(test_bg_task_wait_ms));

  // Check new leader counts.
  int num_leader_moves = 0;
  for (const auto& tn : table_names_) {
    const auto new_leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
    // Only count increases in leaders
    for (const auto& lc : new_leader_counts) {
      num_leader_moves += max(0, lc.second - initial_leader_counts[tn.table_name()][lc.first]);
    }
  }

  // Ensure that we moved one run's worth of leaders (should be one leader move per table).
  LOG(INFO) << "Moved " << num_leader_moves << " leaders in total.";
  ASSERT_EQ(num_leader_moves, kMovesPerTable * kNumTables);
}

TEST_F(LoadBalancerMultiTableTest, GlobalLoadBalancing) {
  const int rf = 3;
  std::vector<uint32_t> z0_tserver_loads;
  // Start with 3 tables with 5 tablets.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", rf, ""));

  // Disable global load balancing.
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "enable_global_load_balancing", "false"));
  }

  //// Two tservers:
  // Add a new tserver to c.r.z0.
  // This zone will then have 15 tablets on the old ts and 0 on the new one.
  std::vector<std::string> extra_opts;
  extra_opts.push_back("--placement_cloud=c");
  extra_opts.push_back("--placement_region=r");
  extra_opts.push_back("--placement_zone=z0");
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 1,
      kDefaultTimeout));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Zone 0 should have 9 tablets on the old ts and 6 on the new ts, since each table will be
  // balanced with 3 tablets on the old ts and 2 on the new one. This results in each table having
  // a balanced load, but that the global load is skewed.

  // Assert that each table is balanced, but we are not globally balanced.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3 }));
  ASSERT_FALSE(AreLoadsBalanced(z0_tserver_loads));

  // Enable global load balancing.
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "enable_global_load_balancing", "true"));
  }

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Assert that each table is balanced, and that we are now globally balanced.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3 }));
  ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));


  //// Three tservers:
  // Disable global load balancing.
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "enable_global_load_balancing", "false"));
  }

  // Add in a third tserver to zone 0.
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 2,
      kDefaultTimeout));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Load will not be evenly spread across these tservers, each table will be (2, 2, 1), leading to
  // a global load of (6, 6, 3) in zone 0.

  // Assert that each table is balanced, and that we are not globally balanced.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3, 4 }));
  ASSERT_FALSE(AreLoadsBalanced(z0_tserver_loads));

  // Enable global load balancing.
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "enable_global_load_balancing", "true"));
  }

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Assert that each table is balanced, and that we are now globally balanced.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3, 4 }));
  ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));
  // Each node should have exactly 5 tablets on it.
  ASSERT_EQ(z0_tserver_loads[0], 5);
  ASSERT_EQ(z0_tserver_loads[1], 5);
  ASSERT_EQ(z0_tserver_loads[2], 5);
}

TEST_F(LoadBalancerMultiTableTest, GlobalLoadBalancingWithBlacklist) {
  const int rf = 3;
  std::vector<uint32_t> z0_tserver_loads;
  // Start with 3 tables with 5 tablets.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0,c.r.z1,c.r.z2", rf, ""));

  // Add two tservers to z0 and wait for everything to be balanced (globally and per table).
  std::vector<std::string> extra_opts;
  extra_opts.push_back("--placement_cloud=c");
  extra_opts.push_back("--placement_region=r");
  extra_opts.push_back("--placement_zone=z0");
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 2,
      kDefaultTimeout));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Assert that each table is balanced, and that we are globally balanced.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3, 4 }));
  ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));
  // Each node should have exactly 5 tablets on it.
  ASSERT_EQ(z0_tserver_loads[0], 5);
  ASSERT_EQ(z0_tserver_loads[1], 5);
  ASSERT_EQ(z0_tserver_loads[2], 5);

  // Blacklist one tserver in z0.
  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
      external_mini_cluster()->master(),
      external_mini_cluster()->tablet_server(0)));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Assert that the blacklisted tserver has no load.
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0 }));
  ASSERT_EQ(z0_tserver_loads[0], 0);

  // Assert that each table is balanced, and that we are globally balanced amongst the other nodes.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 3, 4 }));
  ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));
}

TEST_F(LoadBalancerMultiTableTest, TestDeadNodesLeaderBalancing) {
  const int rf = 3;
  const auto& ts2_id = external_mini_cluster()->tablet_server(2)->uuid();
  const auto& ts1_id = external_mini_cluster()->tablet_server(1)->uuid();

  // Reduce the time after which a TS is marked DEAD.
  int tserver_unresponsive_timeout_ms = 15000;
  bool allow_dead_node_lb = true;
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "tserver_unresponsive_timeout_ms",
                                              std::to_string(tserver_unresponsive_timeout_ms)));
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "allow_leader_balancing_dead_node",
                                              std::to_string(allow_dead_node_lb)));
  }

  // Verify that the load is evenly distributed.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));

  std::vector<uint32_t> tserver_loads;
  tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 1, 2 }));
  ASSERT_TRUE(AreLoadsBalanced(tserver_loads));

  // Leader blacklist a TS.
  LOG(INFO) << "Blacklisting node#2 for leaders";

  ASSERT_OK(external_mini_cluster()->AddTServerToLeaderBlacklist(
      external_mini_cluster()->master(),
      external_mini_cluster()->tablet_server(2)));

  // Wait for LB to finish and then verify leaders and load evenly distributed.
  WaitForLoadBalanceCompletion();

  tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 1, 2 }));
  ASSERT_TRUE(AreLoadsBalanced(tserver_loads));
  ASSERT_EQ(tserver_loads[0], 15);
  ASSERT_EQ(tserver_loads[1], 15);
  ASSERT_EQ(tserver_loads[2], 15);

  std::vector<uint32_t> leader_tserver_loads;
  int total_leaders;
  for (const auto& tn : table_names_) {
    const auto new_leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
    leader_tserver_loads.clear();
    total_leaders = 0;

    for (const auto& lc : new_leader_counts) {
      if (lc.first == ts2_id) {
        ASSERT_EQ(lc.second, 0);
      } else {
        leader_tserver_loads.push_back(lc.second);
        total_leaders += lc.second;
      }

    }
    ASSERT_TRUE(AreLoadsBalanced(leader_tserver_loads));
    ASSERT_EQ(total_leaders, 5);
  }

  // Stop a TS and empty blacklist.
  LOG(INFO) << "Killing tablet server #" << 1;
  ASSERT_OK(external_mini_cluster()->tablet_server(1)->Pause());

  WaitForLoadBalanceCompletion();

  // Wait for the master leader to mark it dead.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return is_ts_stale(1);
  },
  MonoDelta::FromMilliseconds(2 * tserver_unresponsive_timeout_ms),
  "Is TS dead",
  MonoDelta::FromSeconds(1)));

  // All the leaders should now be on the first TS.
  for (const auto& tn : table_names_) {
    const auto new_leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
    leader_tserver_loads.clear();
    total_leaders = 0;

    for (const auto& lc : new_leader_counts) {
      if (lc.first == ts1_id || lc.first == ts2_id) {
        ASSERT_EQ(lc.second, 0);
      } else {
        leader_tserver_loads.push_back(lc.second);
        total_leaders += lc.second;
      }

    }
    ASSERT_TRUE(AreLoadsBalanced(leader_tserver_loads));
    ASSERT_EQ(total_leaders, 5);
  }

  // Remove TS 2 from leader blacklist so that leader load gets transferred
  // to TS2 in the presenece of a DEAD TS1.
  LOG(INFO) << "Emptying blacklist";
  ASSERT_OK(external_mini_cluster()->EmptyBlacklist(
      external_mini_cluster()->master()));

  WaitForLoadBalanceCompletion();

  // Verify loads and leader loads.
  tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 1, 2 }));
  ASSERT_TRUE(AreLoadsBalanced(tserver_loads));
  ASSERT_EQ(tserver_loads[0], 15);
  ASSERT_EQ(tserver_loads[1], 15);
  ASSERT_EQ(tserver_loads[2], 15);

  // Check new leader counts. TS 0 and 2 should contain all the leaders.
  for (const auto& tn : table_names_) {
    const auto new_leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
    leader_tserver_loads.clear();
    total_leaders = 0;

    for (const auto& lc : new_leader_counts) {
      if (lc.first == ts1_id) {
        ASSERT_EQ(lc.second, 0);
      } else {
        leader_tserver_loads.push_back(lc.second);
        total_leaders += lc.second;
      }

    }
    ASSERT_TRUE(AreLoadsBalanced(leader_tserver_loads));
    ASSERT_EQ(total_leaders, 5);
  }

  ASSERT_OK(external_mini_cluster()->tablet_server(1)->Resume());

  WaitForLoadBalanceCompletion();
}

TEST_F(LoadBalancerMultiTableTest, TestLBWithDeadBlacklistedTS) {
  const int rf = 3;
  int num_ts = num_tablet_servers();

  // Reduce the time after which a TS is marked DEAD.
  int tserver_unresponsive_timeout_ms = 5000;
  for (int i = 0; i < num_masters(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i),
                                              "tserver_unresponsive_timeout_ms",
                                              std::to_string(tserver_unresponsive_timeout_ms)));
  }

  // Add a TS and wait for LB to complete.
  LOG(INFO) << "Adding a TS";
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true));
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));

  WaitForLoadBalanceCompletion();

  // Load should be balanced.
  ASSERT_OK(client_->IsLoadBalanced(kNumTables * num_tablets() * rf));

  std::vector<uint32_t> tserver_loads;
  tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 1, 2, 3 }));
  ASSERT_TRUE(AreLoadsBalanced(tserver_loads));

  // Test 1: Test load movement with existing tservers.
  // Kill and blacklist a TS.
  LOG(INFO) << "Killing tablet server #" << 2;
  external_mini_cluster()->tablet_server(2)->Shutdown();

  // Wait for the master leader to mark it dead.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return is_ts_stale(2);
  },
  MonoDelta::FromMilliseconds(2 * tserver_unresponsive_timeout_ms),
  "Is TS dead",
  MonoDelta::FromSeconds(1)));

  LOG(INFO) << "Node #2 dead. Blacklisting it.";

  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
      external_mini_cluster()->master(),
      external_mini_cluster()->tablet_server(2)));

  // Wait for LB to become idle.
  WaitForLoadBalanceCompletion();

  tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 1, 2, 3 }));

  // Each node should have exactly 15 tablets except node 2.
  ASSERT_EQ(tserver_loads[0], 15);
  ASSERT_EQ(tserver_loads[1], 15);
  ASSERT_EQ(tserver_loads[2], 0);
  ASSERT_EQ(tserver_loads[3], 15);

  // Test 2: Test adding a new node while blacklist+dead is in progress.
  // Kill and blacklist a TS.
  LOG(INFO) << "Killing tablet server #" << 3;
  external_mini_cluster()->tablet_server(3)->Shutdown();

  // Wait for the master leader to mark it dead.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return is_ts_stale(3);
  },
  MonoDelta::FromMilliseconds(2 * tserver_unresponsive_timeout_ms),
  "Is TS dead",
  MonoDelta::FromSeconds(1)));

  LOG(INFO) << "Node #3 dead. Blacklisting it.";

  ASSERT_OK(external_mini_cluster()->AddTServerToBlacklist(
      external_mini_cluster()->master(),
      external_mini_cluster()->tablet_server(3)));

  // Add a TS now and check if load is transferred.
  LOG(INFO) << "Adding a TS";
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true));
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));

  // Wait for LB to become idle.
  WaitForLoadBalanceCompletion();

  tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 1, 2, 3, 4 }));

  // Each node should have exactly 15 tablets on it except node 2 and 3.
  ASSERT_EQ(tserver_loads[0], 15);
  ASSERT_EQ(tserver_loads[1], 15);
  ASSERT_EQ(tserver_loads[2], 0);
  ASSERT_EQ(tserver_loads[3], 0);
  ASSERT_EQ(tserver_loads[4], 15);
}

TEST_F(LoadBalancerMultiTableTest, GlobalLeaderBalancing) {
  int num_ts = num_tablet_servers();

  // Increase the time after which raft would start a leader change
  // if heartbeats are missed.
  int max_heartbeat_missed_periods = 50;
  ASSERT_OK(external_mini_cluster()->SetFlagOnMasters(
                                      "leader_failure_max_missed_heartbeat_periods",
                                      std::to_string(max_heartbeat_missed_periods)));

  ASSERT_OK(external_mini_cluster()->SetFlagOnTServers(
                                      "leader_failure_max_missed_heartbeat_periods",
                                      std::to_string(max_heartbeat_missed_periods)));

  // Add a couple of TServers so that each node has 1 leader tablet per table.
  LOG(INFO) << "Adding 2 tservers";
  std::vector<std::string> extra_opts;
  extra_opts.push_back(strings::Substitute("--leader_failure_max_missed_heartbeat_periods=$0",
                                  max_heartbeat_missed_periods));
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));

  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Now add a new TS. Per table there won't be any leader transfer
  // as each node has 1 leader/table.
  // Total leader loads without global leader balancing will be:
  // 3, 3, 3, 3, 3, 0.
  // Global leader balancing should kick in and make it
  // 3, 3, 3, 2, 2, 2.
  LOG(INFO) << "Adding another tserver on which global leader load should be transferred";
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));

  WaitForLoadBalanceCompletion();

  // Check for leader loads.
  std::vector<uint32_t> leader_tserver_loads;
  std::unordered_map<TabletServerId, int> per_ts_leader_loads;
  int total_leaders = 0;
  for (const auto& tn : table_names_) {
    const auto new_leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
    for (const auto& lc : new_leader_counts) {
      per_ts_leader_loads[lc.first] += lc.second;
      total_leaders += lc.second;
    }
  }

  ASSERT_EQ(total_leaders, 15);

  for (const auto& ll : per_ts_leader_loads) {
    LOG(INFO) << "TS Id: " << ll.first << ", leader count: " << ll.second;
    leader_tserver_loads.push_back(ll.second);
  }

  ASSERT_TRUE(AreLoadsBalanced(leader_tserver_loads));
}

} // namespace integration_tests
} // namespace yb
