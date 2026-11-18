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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/gutil/algorithm.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_balancer_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/tsan_util.h"

using std::string;
using std::max;

DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(heartbeat_interval_ms);

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

  int num_drives() override {
    return 2;
  }

  client::YBTableName table_name() override {
    return table_names_[0];
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--placement_cloud=c");
    opts->extra_tserver_flags.push_back("--placement_region=r");
    opts->extra_tserver_flags.push_back("--placement_zone=z${index}");
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
      b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
      b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
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

  void SetFlagOnAllMasters(const string& flag, const string& value) {
    for (size_t i = 0; i < num_masters(); ++i) {
      ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->master(i), flag, value));
    }
  }
};

TEST_F(LoadBalancerMultiTableTest, MultipleLeaderTabletMovesPerTable) {
  // Disable load balancing until we restart ts-0 and want to balance leaders onto it.
  SetFlagOnAllMasters("enable_load_balancing", "false");

  auto ts0 = external_mini_cluster_->tablet_server(0);
  string ts0_uuid = ts0->instance_id().permanent_uuid();
  LOG(INFO) << "Shutting down ts-0. UUID: " << ts0_uuid;
  ts0->Shutdown();

  // Wait for leaders to be re-elected onto the other tservers.
  // Survivors detect the failure simultaneously, so a split vote plus another failure-detection
  // period is common; that exceeds two failure-detection periods.
  std::unordered_map<string, std::unordered_map<string, int>> initial_leader_counts;
  for (const auto& tn : table_names_) {
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      initial_leader_counts[tn.table_name()] = VERIFY_RESULT(yb_admin_client_->GetLeaderCounts(tn));
      int total_leaders = 0;
      for (const auto& lc : initial_leader_counts[tn.table_name()]) {
        if (lc.first == ts0_uuid) {
          // Not all leaders moved off of ts0.
          if (lc.second != 0) {
            return false;
          }
        } else {
          total_leaders += lc.second;
        }
      }
      // Wait for all leaders to be elected.
      return total_leaders == num_tablets();
    }, kDefaultTimeout, "Waiting for leader re-election."));
  }

  LOG(INFO) << "Restarting ts-0";
  ASSERT_OK(ts0->Restart());
  LOG(INFO) << "Done restarting ts-0";

  // Wait for one run of the load balancer to complete so we end up at the pause.
  SetFlagOnAllMasters("TEST_pause_catalog_manager_bg_loop_start", "true");
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms * 2));

  // Re-enable load balancing and continue from the pause, but stop after one iteration.
  SetFlagOnAllMasters("enable_load_balancing", "true");
  SetFlagOnAllMasters("TEST_pause_catalog_manager_bg_loop_end", "true");
  SetFlagOnAllMasters("TEST_pause_catalog_manager_bg_loop_start", "false");

  // Wait for the load balancer run to complete, and for tservers to heartbeat new leaders.
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms * 2 +
                                       FLAGS_heartbeat_interval_ms * 3 * kTimeMultiplier));

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
  SetFlagOnAllMasters("enable_global_load_balancing", "false");

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
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3 }));
  ASSERT_FALSE(AreLoadsBalanced(z0_tserver_loads));

  // Enable global load balancing.
  SetFlagOnAllMasters("enable_global_load_balancing", "true");

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Assert that each table is balanced, and that we are now globally balanced.
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3 }));
  ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));


  //// Three tservers:
  // Disable global load balancing.
  SetFlagOnAllMasters("enable_global_load_balancing", "false");

  // Add in a third tserver to zone 0.
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true, extra_opts));
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 2,
      kDefaultTimeout));

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Load will not be evenly spread across these tservers, each table will be (2, 2, 1), leading to
  // a global load of (6, 6, 3) in zone 0.

  // Assert that each table is balanced, and that we are not globally balanced.
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 3, 4 }));
  ASSERT_FALSE(AreLoadsBalanced(z0_tserver_loads));

  // Enable global load balancing.
  SetFlagOnAllMasters("enable_global_load_balancing", "true");

  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Assert that each table is balanced, and that we are now globally balanced.
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
  z0_tserver_loads = ASSERT_RESULT(GetTserverLoads({ 3, 4 }));
  ASSERT_TRUE(AreLoadsBalanced(z0_tserver_loads));
}

TEST_F(LoadBalancerMultiTableTest, TestDeadNodesLeaderBalancing) {
  static const int rf = 3;
  const auto& ts2_id = external_mini_cluster()->tablet_server(2)->uuid();
  const auto& ts1_id = external_mini_cluster()->tablet_server(1)->uuid();

  // Reduce the time after which a TS is marked DEAD.
  // Logically, after a tserver is killed, we are giving tablets whose leaders are present
  // on the dead tserver, ~3x time (3s*3).
  static const int tserver_unresponsive_timeout_ms = 10000*kTimeMultiplier;
  bool allow_dead_node_lb = true;
  SetFlagOnAllMasters("tserver_unresponsive_timeout_ms",
                      std::to_string(tserver_unresponsive_timeout_ms));
  SetFlagOnAllMasters("allow_leader_balancing_dead_node", std::to_string(allow_dead_node_lb));
  SetFlagOnAllMasters("min_leader_stepdown_retry_interval_ms", "3000");

  for (size_t i = 0; i < num_tablet_servers(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(external_mini_cluster_->tablet_server(i),
                                              "after_stepdown_delay_election_multiplier",
                                              "1"));
  }

  // Verify that the load is evenly distributed.
  ASSERT_TRUE(ASSERT_RESULT(client_->IsLoadBalanced(/* num_servers */ rf)));
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

  std::unordered_set<TabletServerId> zero_load_ts;
  zero_load_ts.insert(ts2_id);
  for (const auto& tn : table_names_) {
    const auto new_leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
    ASSERT_TRUE(AreLoadsAsExpected(new_leader_counts, zero_load_ts));
  }

  // Stop a TS and empty blacklist.
  LOG(INFO) << "Killing tablet server #" << 1;
  ASSERT_OK(external_mini_cluster()->tablet_server(1)->Pause());

  // Wait for the master leader to mark it dead.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return external_mini_cluster()->is_ts_stale(1);
  },
  MonoDelta::FromMilliseconds(2 * tserver_unresponsive_timeout_ms),
  "Is TS dead",
  MonoDelta::FromSeconds(1)));

  // All the leaders should now be on the third TS.
  zero_load_ts.insert(ts1_id);
  for (const auto& tn : table_names_) {
    const auto new_leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(tn));
    ASSERT_TRUE(AreLoadsAsExpected(new_leader_counts, zero_load_ts));
  }

  // Remove TS 2 from leader blacklist so that leader load gets transferred
  // to TS2 in the presence of a DEAD TS1.
  LOG(INFO) << "Clearing leader blacklist";
  ASSERT_OK(external_mini_cluster()->ClearBlacklist(external_mini_cluster()->master()));

  // Wait for load to be balanced and ts0 and ts2 to have the leaders.
  zero_load_ts.erase(ts2_id);
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    tserver_loads = VERIFY_RESULT(GetTserverLoads({ 0, 1, 2 }));
    if (tserver_loads[0] != 15 || tserver_loads[1] != 15 || tserver_loads[2] != 15) return false;
    for (const auto& tn : table_names_) {
      const auto new_leader_counts = VERIFY_RESULT(yb_admin_client_->GetLeaderCounts(tn));
      if (!AreLoadsAsExpected(new_leader_counts, zero_load_ts)) return false;
    }
    return true;
  }, MonoDelta::FromSeconds(30), "Wait for tserver loads to be balanced"));

  LOG(INFO) << "Resuming TS#1";
  ASSERT_OK(external_mini_cluster()->tablet_server(1)->Resume());
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers(),
                                                              kDefaultTimeout));

  WaitForLoadBalanceCompletion();
}

TEST_F(LoadBalancerMultiTableTest, TestLBWithDeadBlacklistedTS) {
  auto num_ts = num_tablet_servers();

  // Reduce the time after which a TS is marked DEAD.
  int tserver_unresponsive_timeout_ms = 5000;
  SetFlagOnAllMasters("tserver_unresponsive_timeout_ms",
                      std::to_string(tserver_unresponsive_timeout_ms));

  // Add a TS and wait for LB to complete.
  LOG(INFO) << "Adding a TS";
  ASSERT_OK(external_mini_cluster()->AddTabletServer(true));
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));

  WaitForLoadBalanceCompletion();

  // Load should be balanced.
  std::vector<uint32_t> tserver_loads;
  tserver_loads = ASSERT_RESULT(GetTserverLoads({ 0, 1, 2, 3 }));
  ASSERT_TRUE(AreLoadsBalanced(tserver_loads));

  // Test 1: Test load movement with existing tservers.
  // Kill and blacklist a TS.
  LOG(INFO) << "Killing tablet server #" << 2;
  external_mini_cluster()->tablet_server(2)->Shutdown();

  // Wait for the master leader to mark it dead.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return external_mini_cluster()->is_ts_stale(2);
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
    return external_mini_cluster()->is_ts_stale(3);
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
  auto num_ts = num_tablet_servers();

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return client_->IsLoadBalancerIdle();
  }, kDefaultTimeout, "IsLoadBalancerIdle"));
  ASSERT_OK(external_mini_cluster()->SetFlagOnMasters("enable_load_balancing", "false"));

  // Add a couple of TServers so that each node has 1 leader tablet per table.
  LOG(INFO) << "Adding 2 tservers";
  ASSERT_OK(external_mini_cluster()->AddTabletServer());
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->AddTabletServer());
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));

  ASSERT_OK(external_mini_cluster()->SetFlagOnMasters("enable_load_balancing", "true"));
  // Wait for load balancing to complete.
  WaitForLoadBalanceCompletion();

  // Now add a new TS. Per table there won't be any leader transfer
  // as each node has 1 leader/table.
  // Total leader loads without global leader balancing will be:
  // 3, 3, 3, 3, 3, 0.
  // Global leader balancing should kick in and make it
  // 3, 3, 3, 2, 2, 2.
  LOG(INFO) << "Adding another tserver on which global leader load should be transferred";
  ASSERT_OK(external_mini_cluster()->AddTabletServer());
  ++num_ts;
  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_ts, kDefaultTimeout));
  string new_ts = external_mini_cluster()->tablet_server(num_ts - 1)->uuid();
  ASSERT_FALSE(new_ts.empty());
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
  // TODO: replace with AreLoadsAsExpected(per_ts_leader_loads) after
  // https://github.com/yugabyte/yugabyte-db/issues/10002 is fixed.
  ASSERT_GT(per_ts_leader_loads[new_ts], 0);
}

// A warning for a per-table move that stays impossible until a tserver is added or the placement
// changes, so it repeats every run, must not block global leader balancing. The warning comes from
// a single-tablet table whose placement wants a replica in a zone that has no tservers: every run
// finds nowhere to add that replica and nowhere to move the replica that sits outside the
// placement, and records both. The imbalance is one only global balancing can fix: every tablet has
// a replica on every tserver, so after leader-blacklisting one tserver and clearing the blacklist,
// per-table leader balancing stops at 2 2 1 leaders for each of the original tables, and only
// global leader balancing evens out the totals.
TEST_F(LoadBalancerMultiTableTest, GlobalLeaderBalancingWithPersistentWarning) {
  const auto kWaitTimeout = MonoDelta::FromSeconds(60 * kTimeMultiplier);
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return client_->IsLoadBalancerIdle();
  }, kWaitTimeout, "IsLoadBalancerIdle"));
  SetFlagOnAllMasters("load_balancer_allow_global_balancing_with_warnings", "false");
  SetFlagOnAllMasters("min_leader_stepdown_retry_interval_ms", "3000");
  for (size_t i = 0; i < num_tablet_servers(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(
        external_mini_cluster_->tablet_server(i), "after_stepdown_delay_election_multiplier", "1"));
  }

  const client::YBTableName stuck_table_name(
      YQL_DATABASE_CQL, table_names_[0].namespace_name(), "stuck-table");
  ASSERT_OK(client_->NewTableCreator()->table_name(stuck_table_name).schema(&schema_)
                .num_tablets(1).Create());
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
      stuck_table_name, "c.r.z0:1,c.r.z1:1,c.r.zmissing:1", 3 /* replication_factor */,
      "" /* optional_uuid */));
  // From now on every run records a warning, so the balancer never reports idle.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return !VERIFY_RESULT(client_->IsLoadBalancerIdle());
  }, kWaitTimeout, "warning keeps the balancer active"));

  // Leaders of all tables, per tserver index. Global balancing counts the stuck table's leader
  // too, so it must be included for the final spread to come out as the balancer sees it.
  auto counted_tables = table_names_;
  counted_tables.push_back(stuck_table_name);
  const auto leader_counts = [&]() -> Result<std::vector<int>> {
    std::vector<int> counts(num_tablet_servers(), 0);
    for (const auto& tn : counted_tables) {
      const auto table_counts = VERIFY_RESULT(yb_admin_client_->GetLeaderCounts(tn));
      for (size_t i = 0; i < num_tablet_servers(); ++i) {
        const auto it = table_counts.find(external_mini_cluster()->tablet_server(i)->uuid());
        if (it != table_counts.end()) {
          counts[i] += it->second;
        }
      }
    }
    return counts;
  };

  // Drain the leaders off ts2, then let them come back.
  ASSERT_OK(external_mini_cluster()->AddTServerToLeaderBlacklist(
      external_mini_cluster()->master(), external_mini_cluster()->tablet_server(2)));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(leader_counts())[2] == 0;
  }, kWaitTimeout, "leaders drained off ts2"));
  ASSERT_OK(external_mini_cluster()->ClearBlacklist(external_mini_cluster()->master()));

  // Per-table leader balancing gives ts2 one leader per original table and stops, as no table
  // has a per-table variance of 2 left. The stuck table's single leader lands on one of the two
  // other tservers, since ts2 had just been drained. While warnings block global balancing, that
  // is final.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(leader_counts())[2] == kNumTables;
  }, kWaitTimeout, "per-table leader balancing done"));
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier));
  ASSERT_EQ(ASSERT_RESULT(leader_counts())[2], kNumTables);

  // Once warnings no longer block global balancing, the totals even out.
  SetFlagOnAllMasters("load_balancer_allow_global_balancing_with_warnings", "true");
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    const auto counts = VERIFY_RESULT(leader_counts());
    return *std::max_element(counts.begin(), counts.end()) -
           *std::min_element(counts.begin(), counts.end()) <= 1;
  }, kWaitTimeout, "global leader balancing"));
  // The warning never went away.
  ASSERT_FALSE(ASSERT_RESULT(client_->IsLoadBalancerIdle()));

  ASSERT_OK(client_->DeleteTable(stuck_table_name));
}

// The same, with the warning from a table the balancer skips every run, as when its placement
// cannot be looked up (here through TEST_load_balancer_skip_table_id). A skipped table's tablets
// are missing from the global load counts. That widens the minimum variance for a global tablet
// move but not for a global leader move, which is cheap to reverse, so global leader balancing
// evens out the original tables from 6 6 3 to 5 5 5 as if nothing were skipped.
TEST_F(LoadBalancerMultiTableTest, GlobalLeaderBalancingWithSkippedTable) {
  const auto kWaitTimeout = MonoDelta::FromSeconds(60 * kTimeMultiplier);
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return client_->IsLoadBalancerIdle();
  }, kWaitTimeout, "IsLoadBalancerIdle"));
  SetFlagOnAllMasters("load_balancer_allow_global_balancing_with_warnings", "false");
  SetFlagOnAllMasters("min_leader_stepdown_retry_interval_ms", "3000");
  for (size_t i = 0; i < num_tablet_servers(); ++i) {
    ASSERT_OK(external_mini_cluster_->SetFlag(
        external_mini_cluster_->tablet_server(i), "after_stepdown_delay_election_multiplier", "1"));
  }

  const client::YBTableName skipped_table_name(
      YQL_DATABASE_CQL, table_names_[0].namespace_name(), "skipped-table");
  ASSERT_OK(client_->NewTableCreator()->table_name(skipped_table_name).schema(&schema_)
                .num_tablets(1).Create());
  std::shared_ptr<client::YBTable> skipped_table;
  ASSERT_OK(client_->OpenTable(skipped_table_name, &skipped_table));
  SetFlagOnAllMasters("TEST_load_balancer_skip_table_id", skipped_table->id());
  // From now on every run records a warning, so the balancer never reports idle.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return !VERIFY_RESULT(client_->IsLoadBalancerIdle());
  }, kWaitTimeout, "warning keeps the balancer active"));

  // Leaders of the original tables, per tserver index. The skipped table is left out, as it is
  // from the balancer's global counts; being skipped, its leader is never moved either.
  const auto leader_counts = [&]() -> Result<std::vector<int>> {
    std::vector<int> counts(num_tablet_servers(), 0);
    for (const auto& tn : table_names_) {
      const auto table_counts = VERIFY_RESULT(yb_admin_client_->GetLeaderCounts(tn));
      for (size_t i = 0; i < num_tablet_servers(); ++i) {
        const auto it = table_counts.find(external_mini_cluster()->tablet_server(i)->uuid());
        if (it != table_counts.end()) {
          counts[i] += it->second;
        }
      }
    }
    return counts;
  };
  const auto spread = [](const std::vector<int>& counts) {
    return *std::max_element(counts.begin(), counts.end()) -
           *std::min_element(counts.begin(), counts.end());
  };

  // Drain the leaders off ts2, then let them come back.
  ASSERT_OK(external_mini_cluster()->AddTServerToLeaderBlacklist(
      external_mini_cluster()->master(), external_mini_cluster()->tablet_server(2)));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(leader_counts())[2] == 0;
  }, kWaitTimeout, "leaders drained off ts2"));
  ASSERT_OK(external_mini_cluster()->ClearBlacklist(external_mini_cluster()->master()));

  // Per-table leader balancing gives ts2 one leader per table and stops: 6 6 3. While warnings
  // block global balancing, that is final.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(leader_counts())[2] == kNumTables;
  }, kWaitTimeout, "per-table leader balancing done"));
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier));
  ASSERT_EQ(ASSERT_RESULT(leader_counts())[2], kNumTables);

  // Once warnings no longer block global balancing, the totals even out. The skipped tablet does
  // not widen the bar for leader moves, so balancing does not stop at 5 6 4 with a spread of 2.
  SetFlagOnAllMasters("load_balancer_allow_global_balancing_with_warnings", "true");
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return spread(VERIFY_RESULT(leader_counts())) <= 1;
  }, kWaitTimeout, "global leader balancing"));
  // The warning never went away.
  ASSERT_FALSE(ASSERT_RESULT(client_->IsLoadBalancerIdle()));

  SetFlagOnAllMasters("TEST_load_balancer_skip_table_id", "");
  ASSERT_OK(client_->DeleteTable(skipped_table_name));
}

} // namespace integration_tests
} // namespace yb
