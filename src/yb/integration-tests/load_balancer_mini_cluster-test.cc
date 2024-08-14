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

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"

#include "yb/rocksdb/util/multi_drive_test_env.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_options.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/multi_drive_test_env.h"

METRIC_DECLARE_gauge_uint32(blacklisted_leaders);
METRIC_DECLARE_event_stats(load_balancer_duration);
METRIC_DECLARE_gauge_uint32(tablets_in_wrong_placement);
METRIC_DECLARE_gauge_uint32(total_table_load_difference);

DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(load_balancer_drive_aware);
DECLARE_int32(load_balancer_max_concurrent_moves);
DECLARE_int32(replication_factor);
DECLARE_int32(TEST_slowdown_master_async_rpc_tasks_by_ms);
DECLARE_int32(TEST_load_balancer_wait_ms);
DECLARE_int32(TEST_load_balancer_wait_after_count_pending_tasks_ms);
DECLARE_bool(tserver_heartbeat_metrics_add_drive_data);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_bool(TEST_fail_async_delete_replica_task);

using namespace std::literals;

namespace yb {
namespace integration_tests {

namespace {

const auto kDefaultTimeout = 30000ms;

class StatEmuEnv : public EnvWrapper {
 public:
  StatEmuEnv() : EnvWrapper(Env::Default()) { }

  virtual Result<FilesystemStats> GetFilesystemStatsBytes(const std::string& f) override {
    std::lock_guard lock(data_mutex_);
    auto i = stats_.find(f);
    if (i == stats_.end()) {
      return target()->GetFilesystemStatsBytes(f);
    }
    return i->second;
  }

  void AddPathStats(const std::string& path, const Env::FilesystemStats& stats) {
    std::lock_guard lock(data_mutex_);
    ASSERT_TRUE(stats_.emplace(path, stats).second);
  }

 private:
  std::unordered_map<std::string, Env::FilesystemStats> stats_ GUARDED_BY(data_mutex_);
  std::mutex data_mutex_;
};

void WaitForReplicaOnTS(yb::MiniCluster* mini_cluster,
                        const yb::client::YBTableName& table_name,
                        const std::string& ts_uuid,
                        int min_expected_count = 1) {

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto leader_mini_master = mini_cluster->GetLeaderMiniMaster();
    if (!leader_mini_master.ok()) {
      return false;
    }
    scoped_refptr<master::TableInfo> tbl_info =
      (*leader_mini_master)->catalog_manager().
          GetTableInfoFromNamespaceNameAndTableName(table_name.namespace_type(),
                                                    table_name.namespace_name(),
                                                    table_name.table_name());
    int count = min_expected_count;
    for (const auto& tablet : VERIFY_RESULT(tbl_info->GetTablets())) {
      auto replica_map = tablet->GetReplicaLocations();
      count -= replica_map->count(ts_uuid);
      if (count <= 0) {
        return true;
      }
    }
    return false;
  }, kDefaultTimeout, "WaitForAddTaskToBeProcessed"));
}

void WaitLoadBalancerActive(client::YBClient* client) {
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
    return !is_idle;
  },  kDefaultTimeout, "IsLoadBalancerActive"));
}

void WaitLoadBalancerIdle(client::YBClient* client) {
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return client->IsLoadBalancerIdle();
  },  kDefaultTimeout, "IsLoadBalancerIdle"));
}

typedef std::unordered_map<std::string,
                           std::pair<std::unordered_map<std::string, int>, int>> DriveStats;

Status GetTabletsDriveStats(DriveStats* stats,
                            yb::MiniCluster* mini_cluster,
                            const yb::client::YBTableName& table_name) {
  scoped_refptr<master::TableInfo> tbl_info =
    VERIFY_RESULT(mini_cluster->GetLeaderMiniMaster())->catalog_manager().
      GetTableInfoFromNamespaceNameAndTableName(table_name.namespace_type(),
                                                table_name.namespace_name(),
                                                table_name.table_name());
  for (const auto& tablet : VERIFY_RESULT(tbl_info->GetTablets())) {
    auto replica_map = tablet->GetReplicaLocations();
    for (const auto& replica : *replica_map.get()) {
      auto ts = stats->find(replica.first);
      if (ts == stats->end()) {
        ts = stats->insert({replica.first,
                           std::make_pair(std::unordered_map<std::string, int>(), 0)}).first;
      }
      if (replica.second.role == PeerRole::LEADER) {
        ++ts->second.second;
      }
      if (!replica.second.fs_data_dir.empty()) {
        auto& ts_map = ts->second.first;
        auto path = ts_map.find(replica.second.fs_data_dir);
        if (path == ts_map.end()) {
          ts_map.insert({replica.second.fs_data_dir, 1});
        } else {
          ++path->second;
        }
      }
    }
  }
  return Status::OK();
}

} // namespace


class LoadBalancerMiniClusterTestBase : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return false; }

  bool enable_ysql() override {
    // Do not create the transaction status table.
    return false;
  }
};

class LoadBalancerMiniClusterTestWithoutData : public LoadBalancerMiniClusterTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_add_drive_data) = false;
    YBTableTestBase::SetUp();
  }
};

class LoadBalancerMiniClusterTest : public LoadBalancerMiniClusterTestBase {
 protected:
  int num_drives() override {
    return 3;
  }

  int num_tablets() override {
    return 4;
  }

  Status AddTserverToBlacklist(size_t idx, bool leader_blacklist) {
    HostPort ts_hostport(mini_cluster()->mini_tablet_server(idx)->bound_rpc_addr());
    RETURN_NOT_OK(
        yb_admin_client_->ChangeBlacklist({ts_hostport}, true /* add */, leader_blacklist));
    return Status::OK();
  }

  Status RemoveTserverFromBlacklist(size_t idx, bool leader_blacklist) {
    HostPort ts_hostport(mini_cluster()->mini_tablet_server(idx)->bound_rpc_addr());
    RETURN_NOT_OK(
        yb_admin_client_->ChangeBlacklist({ts_hostport}, false /* add */, leader_blacklist));
    return Status::OK();
  }

  Status WaitForNoPendingDeletes() {
    auto ts_descriptors = VERIFY_RESULT(
        mini_cluster()->GetLeaderMiniMaster())->master()->ts_manager()->GetAllDescriptors();
    return WaitFor([&]() -> Result<bool> {
      for (const auto& ts : ts_descriptors) {
        if (ts->HasTabletDeletePending()) {
          return false;
        }
      }
      return true;
    }, kDefaultTimeout, "WaitForNoPendingDeletes");
  }

  void DeleteTableAsync() {
    if (table_exists_) {
      ASSERT_OK(client_->DeleteTable(table_name(), false /* wait */));
      table_exists_ = false;
    }
  }
};

class LoadBalancerMiniClusterRf3Test : public LoadBalancerMiniClusterTest {
 protected:
  size_t num_masters() override {
    return 3;
  }

  size_t num_tablet_servers() override {
    return 3;
  }
};

TEST_F(LoadBalancerMiniClusterRf3Test, DurationMetric) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_load_balancer_wait_ms) = 5;
  auto* mini_master = ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster());
  auto master_metric_entity = mini_master->master()->metric_entity();
  auto stats = master_metric_entity->FindOrCreateMetric<EventStats>(
      &METRIC_load_balancer_duration);
  // The metric should get a value.
  ASSERT_OK(WaitFor([&] {
    return stats->MeanValue() > 0;
  }, 10s, "load_balancer_duration gets a value"));

  ASSERT_OK(mini_cluster()->StepDownMasterLeader());

  // The metric quantiles should be reset by the stepdown.
  stats = master_metric_entity->FindOrCreateMetric<EventStats>(&METRIC_load_balancer_duration);
  ASSERT_OK(WaitFor([&] {
    return stats->MeanValue() == 0;
  }, 10s, "load_balancer_duration value resets"));
}

TEST_F(LoadBalancerMiniClusterTest, TabletsInWrongPlacementMetric) {
  const int ts_idx = 0;
  auto* mini_master = ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster());
  auto cluster_metric_entity = mini_master->master()->metric_entity_cluster();
  auto tablets_in_wrong_placement =
      cluster_metric_entity->FindOrNull<AtomicGauge<uint32_t>>(METRIC_tablets_in_wrong_placement);
  ASSERT_EQ(tablets_in_wrong_placement->value(), 0);

  // Prevent moves so we can reliably read the metric and get a non-zero value.
  FLAGS_load_balancer_max_concurrent_adds = 0;

  unsigned int peers_on_ts = 0;
  auto ts_uuid = mini_cluster_->mini_tablet_server(ts_idx)->server()->permanent_uuid();
  for (const auto& peer : ListTabletPeers(mini_cluster(), ListPeersFilter::kAll)) {
    if (peer->permanent_uuid() == ts_uuid) {
      ++peers_on_ts;
    }
  }
  ASSERT_GT(peers_on_ts, 0);

  ASSERT_EQ(tablets_in_wrong_placement->value(), 0);

  // Blacklist first tserver.
  ASSERT_OK(AddTserverToBlacklist(ts_idx, false /* leader_blacklist */));
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * 2ms);
  ASSERT_EQ(tablets_in_wrong_placement->value(), peers_on_ts);

  // Change placement info to make first tserver invalid.
  // The invalid and blacklisted tablets should not be double-counted.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("cloud1.rack2.zone,cloud2.rack3.zone", 3, ""));
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * 2ms);
  ASSERT_EQ(tablets_in_wrong_placement->value(), peers_on_ts);

  // Unblacklist first tserver.
  // The tablets are still invalid, so the metric should not change.
  ASSERT_OK(RemoveTserverFromBlacklist(ts_idx, false /* leader_blacklist */));
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * 2ms);
  ASSERT_EQ(tablets_in_wrong_placement->value(), peers_on_ts);

  // Change placement info to make first tserver valid again.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "cloud1.rack1.zone,cloud1.rack2.zone,cloud2.rack3.zone", 3, ""));
  ASSERT_OK(WaitFor([&] {
    return tablets_in_wrong_placement->value() == 0;
  }, 5s, "Wait for tablets_in_wrong_placement to be 0"));
}

TEST_F(LoadBalancerMiniClusterTest, BlacklistedLeadersMetric) {
  auto* mini_master = ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster());
  auto cluster_metric_entity = mini_master->master()->metric_entity_cluster();
  auto blacklisted_leaders =
      cluster_metric_entity->FindOrNull<AtomicGauge<uint32_t>>(METRIC_blacklisted_leaders);
  ASSERT_EQ(blacklisted_leaders->value(), 0);

  // Prevent leader moves so we can reliably read the metric and get a non-zero value.
  FLAGS_load_balancer_max_concurrent_moves = 0;

  // Leader blacklist first tserver.
  ASSERT_OK(AddTserverToBlacklist(0 /* idx */, true /* leader_blacklist */));
  ASSERT_OK(WaitFor([&] {
    return blacklisted_leaders->value() > 0;
  }, 5s, "Wait for blacklisted_leaders to reflect blacklisted leader"));
}

TEST_F(LoadBalancerMiniClusterTest, TableLoadDifferenceMetric) {
  const auto kNumTablets = static_cast<uint32_t>(num_tablets());
  auto* mini_master = ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster());
  auto cluster_metric_entity = mini_master->master()->metric_entity_cluster();
  auto load_difference_metric = cluster_metric_entity->FindOrNull<AtomicGauge<uint32_t>>(
      METRIC_total_table_load_difference);

  ASSERT_EQ(load_difference_metric->value(), 0);

  // Prevent moves temporarily so we can reliably read the metric and get a non-zero value.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_adds) = 0;

  // Add a new node and wait for load difference to equal kNumTablets (each existing node has
  // kNumTablets tablets, and the new node has 0).
  auto new_ts_index = mini_cluster()->num_tablet_servers();
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(new_ts_index + 1));
  ASSERT_OK(WaitFor([&] {
    return load_difference_metric->value() == kNumTablets;
  }, 5s, "load_difference reflects new node"));

  // Enable moves and verify that load_difference monotonically decreases to 0.
  auto load_difference_low_water_mark = load_difference_metric->value();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_adds) = 1;
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto load_difference = load_difference_metric->value();
    if (load_difference > load_difference_low_water_mark) {
      return STATUS_FORMAT(
          IllegalState, "load_difference unexpectedly increased from $0 to $1",
          load_difference_low_water_mark, load_difference);
    }
    load_difference_low_water_mark = load_difference;
    return load_difference == 0 && VERIFY_RESULT(client_->IsLoadBalancerIdle());
  }, 30s, "Wait for tablet moves after adding ts-3"));

  // Blacklist first tserver.
  // The replicas should move off the blacklisted tserver and load_difference should increase to
  // kNumTablets again.
  auto load_difference_high_water_mark = load_difference_metric->value();
  ASSERT_OK(AddTserverToBlacklist(0 /* idx */, false /* leader_blacklist */));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto load_difference = load_difference_metric->value();
    if (load_difference < load_difference_high_water_mark) {
      return STATUS_FORMAT(
          IllegalState, "load_difference unexpectedly decreased from $0 to $1",
          load_difference_high_water_mark, load_difference);
    }
    load_difference_high_water_mark = load_difference;
    return load_difference == kNumTablets && VERIFY_RESULT(client_->IsLoadBalancerIdle());
  }, 30s, "Wait for tablet moves after blacklisting ts-0"));
}

// See issue #6278. This test tests the segfault that used to occur during a rare race condition,
// where we would have an uninitialized TSDescriptor that we try to access.
// To trigger the race condition, we need a pending add task that gets completed after
// CountPendingTasksUnlocked, and for the tserver that the add is going to needs to be marked as
// not live before hitting AnalyzeTabletsUnlocked.
TEST_F(LoadBalancerMiniClusterTest, UninitializedTSDescriptorOnPendingAddTest) {
  const int test_bg_task_wait_ms = 5000;
  const int test_short_delay_ms = 100;
  // See MiniTabletServer::MiniTabletServer for default placement info.
  ASSERT_OK(yb_admin_client_->
      ModifyPlacementInfo("cloud1.rack1.zone,cloud1.rack2.zone,cloud2.rack3.zone", 3, ""));

  // Disable load balancing.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  // Increase the time between LB runs so we can better time things.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) = test_bg_task_wait_ms;
  // Set this to delay the add task so that we can have a pending add.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_slowdown_master_async_rpc_tasks_by_ms) =
      test_bg_task_wait_ms + test_short_delay_ms;
  // Insert a pause after finding pending tasks so that the race condition can be hit.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_load_balancer_wait_after_count_pending_tasks_ms) = 4000;
    // Don't allow for leader moves, only want add tasks.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_moves) = 0;

  // Add new tserver in to force load balancer moves.
  tserver::TabletServerOptions extra_opts =
      ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  // Important to set to cloud2 (see TEST_SetupConnectivity, we group servers in groups of two).
  extra_opts.SetPlacement("cloud2", "rack3", "zone");
  ASSERT_OK(mini_cluster()->AddTabletServer(extra_opts));
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 1));
  const auto ts3_uuid = mini_cluster_->mini_tablet_server(3)->server()->permanent_uuid();

  // Re-enable the load balancer.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = true;

  // LB RUN #1 : Start an add server task.

  // LB RUN #2 : Count the pending add task, and then pause for
  //             FLAGS_TEST_load_balancer_wait_after_count_pending_tasks_ms.

  // Wait for the add task to be processed (at least one replica reporting on the new tserver).
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto leader_mini_master = mini_cluster()->GetLeaderMiniMaster();
    if (!leader_mini_master.ok()) {
      return false;
    }
    scoped_refptr<master::TableInfo> tbl_info =
      (*leader_mini_master)->catalog_manager().
          GetTableInfoFromNamespaceNameAndTableName(table_name().namespace_type(),
                                                    table_name().namespace_name(),
                                                    table_name().table_name());
    for (const auto& tablet : VERIFY_RESULT(tbl_info->GetTablets())) {
      auto replica_map = tablet->GetReplicaLocations();
      if (replica_map->find(ts3_uuid) != replica_map->end()) {
        return true;
      }
    }
    return false;
  }, kDefaultTimeout, "WaitForAddTaskToBeProcessed"));

  // Modify GetAllReportedDescriptors so that it does not report the new tserver
  // (this could happen normally from a late heartbeat).
  master::TSDescriptorVector ts_descs;
  ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())
      ->ts_manager().GetAllReportedDescriptors(&ts_descs);
  auto ts3_it = std::find_if(
      ts_descs.cbegin(), ts_descs.cend(),
      [&ts3_uuid](const auto& ts_desc) -> bool {
        return ts_desc->id() == ts3_uuid; });
  ASSERT_NE(ts3_it, ts_descs.cend());
  {
    master::TSDescriptorPtr ts3_desc = *ts3_it;
    auto l = ts3_desc->LockForWrite();
    l.mutable_data()->pb.set_state(master::SysTServerEntryPB::REPLACED);
    l.Commit();
  }

  // LB run #2 will now continue, with GetAllReportedDescriptors not reporting this tserver, but
  // with GetReplicaLocations reporting it.

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
    return !is_idle;
  },  kDefaultTimeout * 2, "IsLoadBalancerActive"));

  // If the seg fault occurs, it will occur in the first 2 runs.
  SleepFor(MonoDelta::FromMilliseconds(3 * test_bg_task_wait_ms));

  // If it has yet to happen, bring back the tserver so that load balancing can complete.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_slowdown_master_async_rpc_tasks_by_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_load_balancer_wait_after_count_pending_tasks_ms) = 0;

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return client_->IsLoadBalancerIdle();
  },  kDefaultTimeout * 2, "IsLoadBalancerIdle"));
}

// Tests that load balancer shouldn't run for deleted/deleting tables.
TEST_F(LoadBalancerMiniClusterTest, NoLBOnDeletedTables) {
  // Delete the table.
  DeleteTable();

  LOG(INFO) << "Successfully sent Delete RPC.";
  // Wait for the table to be removed.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    // 1) Should not list it in ListTables.
    const auto tables = VERIFY_RESULT(client_->ListTables(/* filter */ "",
                                                /* exclude_ysql */ true));
    if (master::kNumSystemTables != tables.size()) {
      return false;
    }

    // 2) Should respond to GetTableSchema with a NotFound error.
    client::YBSchema schema;
    dockv::PartitionSchema partition_schema;
    Status s = client_->GetTableSchema(
        client::YBTableName(YQL_DATABASE_CQL,
                            table_name().namespace_name(),
                            table_name().table_name()),
        &schema, &partition_schema);
    if (!s.IsNotFound()) {
      return false;
    }

    return true;
  }, kDefaultTimeout, "HasTableBeenDeleted"));

  LOG(INFO) << "Table deleted successfully.";

  // We should be able to find the deleted table in the list of skipped tables now.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto leader_mini_master = mini_cluster()->GetLeaderMiniMaster();
        if (!leader_mini_master.ok()) {
          return false;
        }
        const auto tables = (*leader_mini_master)
                                ->catalog_manager()
                                .load_balancer()
                                ->GetAllTablesLoadBalancerSkipped();
        for (const auto& table : tables) {
          if (table->name() == table_name().table_name() &&
              table->namespace_name() == table_name().namespace_name()) {
            return true;
          }
        }
        return false;
      },
      kDefaultTimeout,
      "IsLBSkippingDeletedTables"));
}

// Check flow tablet size data from tserver to master
TEST_F(LoadBalancerMiniClusterTest, CheckTabletSizeData) {
  auto num_peers = ListTabletPeers(mini_cluster(), ListPeersFilter::kAll).size();

  auto& catalog_manager =
      ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster())->catalog_manager();

  scoped_refptr<master::TableInfo> tbl_info = catalog_manager.
        GetTableInfoFromNamespaceNameAndTableName(table_name().namespace_type(),
                                                  table_name().namespace_name(),
                                                  table_name().table_name());
  int updated = 0;
  for (const auto& tablet : ASSERT_RESULT(tbl_info->GetTablets())) {
    auto replica_map = tablet->GetReplicaLocations();
    for (const auto& replica : *replica_map.get()) {
      if (!replica.second.fs_data_dir.empty()) {
        ++updated;
      }
    }
  }
  ASSERT_EQ(updated, num_peers);
}

TEST_F(LoadBalancerMiniClusterTest, CheckLoadBalanceDisabledDriveAware) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_drive_aware) = false;

  // Add new tserver to force load balancer moves.
  auto new_ts_index = mini_cluster()->num_tablet_servers();
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(new_ts_index + 1));
  auto new_ts_uuid = mini_cluster()->mini_tablet_server(new_ts_index)->server()->permanent_uuid();

  // Wait for the add task to be processed (at least one replica reporting on the new tserver).
  WaitForReplicaOnTS(mini_cluster(), table_name(), new_ts_uuid);
}

TEST_F_EX(LoadBalancerMiniClusterTest, CheckLoadBalanceWithoutDriveData,
          LoadBalancerMiniClusterTestWithoutData) {
  // Add new tserver to force load balancer moves.
  auto new_ts_index = mini_cluster()->num_tablet_servers();
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(new_ts_index + 1));
  auto new_ts_uuid = mini_cluster()->mini_tablet_server(new_ts_index)->server()->permanent_uuid();

  // Wait for the add task to be processed (at least one replica reporting on the new tserver).
  WaitForReplicaOnTS(mini_cluster(), table_name(), new_ts_uuid);

  // Drive data should be empty
  scoped_refptr<master::TableInfo> tbl_info =
    ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster())->catalog_manager().
        GetTableInfoFromNamespaceNameAndTableName(table_name().namespace_type(),
                                                  table_name().namespace_name(),
                                                  table_name().table_name());
  for (const auto& tablet : ASSERT_RESULT(tbl_info->GetTablets())) {
    auto replica_map = tablet->GetReplicaLocations();
    for (const auto& replica : *replica_map.get()) {
      ASSERT_EQ(replica.second.drive_info.sst_files_size, 0);
      ASSERT_EQ(replica.second.drive_info.wal_files_size, 0);
    }
  }
}

TEST_F(LoadBalancerMiniClusterTest, CheckLoadBalanceDriveAware) {
  // Wait LB to move leaders
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms * 2));
  WaitLoadBalancerIdle(client_.get());

  DriveStats before;
  ASSERT_OK(GetTabletsDriveStats(&before, mini_cluster(), table_name()));

  // Add new tserver to force load balancer moves.
  auto new_ts_index = mini_cluster()->num_tablet_servers();
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(new_ts_index + 1));
  auto new_ts_uuid = mini_cluster()->mini_tablet_server(new_ts_index)->server()->permanent_uuid();

  // Wait LB to move tablets to new ts
  WaitLoadBalancerActive(client_.get());
  WaitLoadBalancerIdle(client_.get());

  DriveStats after;
  ASSERT_OK(GetTabletsDriveStats(&after, mini_cluster(), table_name()));

  bool found = false;
  for (size_t ts_index = 0; ts_index < new_ts_index; ++ts_index) {
    const auto ts_uuid = mini_cluster()->mini_tablet_server(ts_index)->server()->permanent_uuid();
    std::vector<std::string> drives;
    auto& ts_before = before[ts_uuid];
    auto& ts_after = after[ts_uuid];
    for (const auto& drive : ts_before.first) {
      drives.emplace_back(drive.first);
    }
    ASSERT_FALSE(drives.empty());
    std::sort(drives.begin(), drives.end());
    LOG(INFO) << "P " << ts_uuid;
    LOG(INFO) << "Leaders before: " << ts_before.second << " after: " << ts_after.second;

    int tablets = ts_after.first[drives.front()];
    bool expected_move = true;
    for (const auto& drive : drives) {
      if (ts_after.first[drive] != tablets) {
        expected_move = false;
      }
      LOG(INFO) << drive << " before: " << ts_before.first[drive] <<
                   " after: " << ts_after.first[drive];
    }
    if (expected_move) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST_F(LoadBalancerMiniClusterTest, ClearPendingDeletesOnFailure) {
  // Make sure that deleting tablet task is failed/times out.
  FLAGS_TEST_fail_async_delete_replica_task = true;
  // Delete the table, async. The sync call will fail since the table will not get deleted.
  DeleteTableAsync();
  // This sleep currently functions to wait until all DeleteTablet RPCs have been sent out and have
  // failed. Need a better condition here.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto& catalog_manager =
        VERIFY_RESULT(mini_cluster()->GetLeaderMiniMaster())->catalog_manager();
    auto recent_tasks = catalog_manager.GetRecentTasks();
    auto num_delete_tasks = 0;
    // Make sure that all deleted tasks triggered have failed and at least 1 delete has been
    // triggered.
    for (const auto& task : recent_tasks) {
      if (task->type() == server::MonitoredTaskType::kDeleteReplica) {
        if (task->state() != server::MonitoredTaskState::kFailed) {
          return false;
        }
        num_delete_tasks++;
      }
    }
    return num_delete_tasks > 0;
  }, kDefaultTimeout, "deleted tasks failed"));
  ASSERT_OK(WaitForNoPendingDeletes());
}


class LoadBalancerFailedDrive : public LoadBalancerMiniClusterTestBase {
 protected:
  void SetUp() override {
    ts_env_.reset(new MultiDriveTestEnv());
    ts_rocksdb_env_.reset(new rocksdb::MultiDriveTestEnv());
    YBTableTestBase::SetUp();
  }

  void BeforeStartCluster() override {
    auto ts1_drive0 = mini_cluster()->GetTabletServerDrive(0, 0);
    dynamic_cast<MultiDriveTestEnv*>(ts_env_.get())->AddFailedPath(ts1_drive0);
    dynamic_cast<rocksdb::MultiDriveTestEnv*>(ts_rocksdb_env_.get())->AddFailedPath(ts1_drive0);
  }

  int num_drives() override {
    return 3;
  }

  int num_tablets() override {
    return 4;
  }

  size_t num_tablet_servers() override {
    return 4;
  }
};

TEST_F(LoadBalancerFailedDrive, CheckTabletSizeData) {
  WaitLoadBalancerActive(client_.get());
  WaitLoadBalancerIdle(client_.get());

  auto& catalog_manager =
      ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster())->catalog_manager();

  scoped_refptr<master::TableInfo> tbl_info = catalog_manager.
      GetTableInfoFromNamespaceNameAndTableName(table_name().namespace_type(),
                                                table_name().namespace_name(),
                                                table_name().table_name());
  const auto ts1_uuid = mini_cluster_->mini_tablet_server(0)->server()->permanent_uuid();
  for (const auto& tablet : ASSERT_RESULT(tbl_info->GetTablets())) {
    auto replica_map = tablet->GetReplicaLocations();
    for (const auto& replica : *replica_map.get()) {
      EXPECT_NE(ts1_uuid, replica.first);
    }
  }
}

} // namespace integration_tests
} // namespace yb
