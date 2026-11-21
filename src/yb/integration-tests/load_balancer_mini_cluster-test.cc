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

#include "yb/client/table_creator.h"

#include <rapidjson/document.h>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/master_test_api.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master_client.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"

#include "yb/rocksdb/util/multi_drive_test_env.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_options.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/server/server_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/multi_drive_test_env.h"
#include "yb/util/status_format.h"

METRIC_DECLARE_entity(cluster);
METRIC_DECLARE_gauge_uint32(blacklisted_leaders);
METRIC_DECLARE_event_stats(load_balancer_duration);
METRIC_DECLARE_gauge_uint32(tablets_in_wrong_placement);
METRIC_DECLARE_gauge_uint32(total_table_load_difference);

DECLARE_bool(load_balancer_drive_aware);
DECLARE_bool(tserver_heartbeat_metrics_add_drive_data);
DECLARE_int32(load_balancer_max_concurrent_adds);
DECLARE_int32(TEST_load_balancer_wait_ms);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(load_balancer_max_concurrent_moves);
DECLARE_int32(min_leader_stepdown_retry_interval_ms);
DECLARE_int32(replication_factor);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_string(instance_uuid_override);

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

// The master leader's locations of the table's tablets, by tablet id.
Result<std::map<TabletId, master::TabletLocationsPB>> GetTableLocations(
    MiniCluster* mini_cluster, const client::YBTableName& table_name) {
  MasterTestApi api(mini_cluster);
  auto table = VERIFY_RESULT(api.FindTable(table_name));
  std::map<TabletId, master::TabletLocationsPB> result;
  for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
    result[tablet->id()] = VERIFY_RESULT(api.GetTabletLocations(tablet->id()));
  }
  return result;
}

void WaitForReplicaOnTS(yb::MiniCluster* mini_cluster,
                        const yb::client::YBTableName& table_name,
                        const std::string& ts_uuid,
                        int min_expected_count = 1) {

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    int count = min_expected_count;
    for (const auto& [tablet_id, locations] :
         VERIFY_RESULT(GetTableLocations(mini_cluster, table_name))) {
      for (const auto& replica : locations.replicas()) {
        if (replica.ts_info().permanent_uuid() == ts_uuid && --count <= 0) {
          return true;
        }
      }
    }
    return false;
  }, kDefaultTimeout, "WaitForAddTaskToBeProcessed"));
}

void WaitLoadBalancerActive(
    client::YBClient* client, const std::string& msg = "IsLoadBalancerActive",
    const std::chrono::milliseconds timeout = kDefaultTimeout) {
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      timeout * kTimeMultiplier, msg));
}

void WaitLoadBalancerIdle(
    client::YBClient* client, const std::string& msg = "IsLoadBalancerIdle",
    const std::chrono::milliseconds timeout = kDefaultTimeout) {
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return client->IsLoadBalancerIdle(); }, timeout * kTimeMultiplier,
      msg));
}

struct TSDriveStats {
  std::unordered_map<std::string, std::vector<TabletId>> dir_tablets;
  int num_leaders = 0;
};

using DriveStats = std::unordered_map<TabletServerId, TSDriveStats>;

// The replicas the master leader locates, with the data directory each tserver holds its replica
// in: the directory the tserver reports, and the drive-aware balancer balances.
Result<DriveStats> GetTabletsDriveStats(
    MiniCluster* mini_cluster, const client::YBTableName& table_name) {
  DriveStats result;
  for (const auto& [tablet_id, locations] :
       VERIFY_RESULT(GetTableLocations(mini_cluster, table_name))) {
    for (const auto& replica : locations.replicas()) {
      const auto& ts_uuid = replica.ts_info().permanent_uuid();
      auto& ts = result[ts_uuid];
      if (replica.role() == PeerRole::LEADER) {
        ++ts.num_leaders;
      }
      auto* tserver = mini_cluster->find_tablet_server(ts_uuid);
      auto peer = tserver ? tserver->server()->tablet_manager()->LookupTablet(tablet_id) : nullptr;
      if (peer) {
        ts.dir_tablets[peer->tablet_metadata()->data_root_dir()].push_back(tablet_id);
      }
    }
  }
  return result;
}

// The leader's "cluster" metric entity, where it keeps the balancer's metrics.
Result<scoped_refptr<MetricEntity>> LeaderClusterMetrics(MiniCluster* mini_cluster) {
  auto* registry = VERIFY_RESULT(mini_cluster->GetLeaderMiniMaster())->server().metric_registry();
  return METRIC_ENTITY_cluster.Instantiate(registry, "yb.cluster");
}

// The leader's /api/v1/load-distribution page: the balancer's latest run.
Result<std::string> FetchLoadDistribution(MiniCluster* mini_cluster) {
  auto* leader = VERIFY_RESULT(mini_cluster->GetLeaderMiniMaster());
  EasyCurl curl;
  faststring body;
  RETURN_NOT_OK(curl.FetchURL(
      Format("http://$0/api/v1/load-distribution", leader->bound_http_addr()), &body));
  return body.ToString();
}

// The descriptions of the tasks of the balancer's latest run, one per task type and state.
Result<std::vector<std::string>> LatestBalancerTasks(MiniCluster* mini_cluster) {
  const auto json = VERIFY_RESULT(FetchLoadDistribution(mini_cluster));
  rapidjson::Document doc;
  doc.Parse(json.c_str());
  SCHECK(!doc.HasParseError() && doc.IsObject() && doc.HasMember("tasks_summary"), Corruption,
         "Unexpected JSON: " + json);
  std::vector<std::string> result;
  for (const auto& task : doc["tasks_summary"].GetArray()) {
    result.push_back(task["example_description"].GetString());
  }
  return result;
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
  auto master_metric_entity = mini_master->server().metric_entity();
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

TEST_F(LoadBalancerMiniClusterTest, TaskTracker) {
  auto kTimeout = 30s;
  // The latest run started exactly one task, and the description names its type.
  auto wait_for_task = [&](const std::string& type, const std::string& message) {
    return WaitFor([&]() -> Result<bool> {
      auto tasks = VERIFY_RESULT(LatestBalancerTasks(mini_cluster()));
      return tasks.size() == 1 && tasks[0].starts_with(type);
    }, kTimeout, message);
  };
  // Idle: the latest run started nothing, and no task it started earlier is still running.
  auto wait_for_idle = [&](const std::string& message) {
    return WaitFor([&]() -> Result<bool> {
      return VERIFY_RESULT(LatestBalancerTasks(mini_cluster())).empty() &&
             VERIFY_RESULT(client_->IsLoadBalancerIdle());
    }, kTimeout, message);
  };
  ASSERT_OK(wait_for_idle("Wait for the balancer to be idle"));

  // Add a tablet server. The load balancer should start an add server task.
  auto new_ts_index = mini_cluster()->num_tablet_servers();
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_OK(wait_for_task("AddServer ", "Wait for add server task to start"));
  ASSERT_OK(wait_for_idle("Wait for add server task to complete"));

  // Leader blacklist the new tablet server. The load balancer should start a stepdown task.
  ASSERT_OK(mini_cluster()->AddTServerToLeaderBlacklist(new_ts_index));
  ASSERT_OK(wait_for_task("Stepdown Leader ", "Wait for stepdown task to start"));
  ASSERT_OK(wait_for_idle("Wait for stepdown task to complete"));

  // Blacklist the new tablet server. The load balancer should start a remove server task.
  ASSERT_OK(mini_cluster()->AddTServerToBlacklist(new_ts_index));
  ASSERT_OK(wait_for_task("RemoveServer ", "Wait for remove server task to start"));
  ASSERT_OK(wait_for_idle("Wait for remove server task to complete"));
}

TEST_F(LoadBalancerMiniClusterTest, TabletsInWrongPlacementMetric) {
  const int ts_idx = 0;
  auto cluster_metric_entity = ASSERT_RESULT(LeaderClusterMetrics(mini_cluster()));
  auto tablets_in_wrong_placement =
      cluster_metric_entity->FindOrNull<AtomicGauge<uint32_t>>(METRIC_tablets_in_wrong_placement);
  ASSERT_EQ(tablets_in_wrong_placement->value(), 0);

  // Prevent moves so we can reliably read the metric and get a non-zero value.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_adds) = 0;

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
  ASSERT_OK(
      yb_admin_client_->ModifyPlacementInfo("cloud1.region2.zone,cloud2.region3.zone", 3, ""));
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * 2ms);
  ASSERT_EQ(tablets_in_wrong_placement->value(), peers_on_ts);

  // Unblacklist first tserver.
  // The tablets are still invalid, so the metric should not change.
  ASSERT_OK(RemoveTserverFromBlacklist(ts_idx, false /* leader_blacklist */));
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * 2ms);
  ASSERT_EQ(tablets_in_wrong_placement->value(), peers_on_ts);

  // Change placement info to make first tserver valid again.
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "cloud1.region1.zone,cloud1.region2.zone,cloud2.region3.zone", 3, ""));
  ASSERT_OK(WaitFor([&] {
    return tablets_in_wrong_placement->value() == 0;
  }, 5s, "Wait for tablets_in_wrong_placement to be 0"));
}

TEST_F(LoadBalancerMiniClusterTest, BlacklistedLeadersMetric) {
  auto cluster_metric_entity = ASSERT_RESULT(LeaderClusterMetrics(mini_cluster()));
  auto blacklisted_leaders =
      cluster_metric_entity->FindOrNull<AtomicGauge<uint32_t>>(METRIC_blacklisted_leaders);
  ASSERT_EQ(blacklisted_leaders->value(), 0);

  // Prevent leader moves so we can reliably read the metric and get a non-zero value.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_moves) = 0;

  // Leader blacklist first tserver.
  ASSERT_OK(AddTserverToBlacklist(0 /* idx */, true /* leader_blacklist */));
  ASSERT_OK(WaitFor([&] {
    return blacklisted_leaders->value() > 0;
  }, 5s, "Wait for blacklisted_leaders to reflect blacklisted leader"));
}

TEST_F(LoadBalancerMiniClusterTest, TableLoadDifferenceMetric) {
  const auto kNumTablets = static_cast<uint32_t>(num_tablets());
  auto cluster_metric_entity = ASSERT_RESULT(LeaderClusterMetrics(mini_cluster()));
  auto load_difference_metric = cluster_metric_entity->FindOrNull<AtomicGauge<uint32_t>>(
      METRIC_total_table_load_difference);

  ASSERT_EQ(load_difference_metric->value(), 0);

  // Prevent moves temporarily so we can reliably read the metric and get a non-zero value.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_adds) = 0;

  // Add a new node and wait for load difference to equal num_tablets * RF / num_tservers (we need
  // to move at least that many replicas onto the new node to balance the load).
  auto new_ts_index = mini_cluster()->num_tablet_servers();
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(new_ts_index + 1));
  const auto expected_load_difference = (kNumTablets * 3) / mini_cluster_->num_tablet_servers();
  ASSERT_OK(WaitFor([&] {
    return load_difference_metric->value() == expected_load_difference;
  }, 5s, Format("load_difference should be $0", expected_load_difference)));

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

  // The balancer leaves the deleted table's replicas alone: it goes idle with nothing to do.
  WaitLoadBalancerIdle(client_.get(), "IsLBSkippingDeletedTables");
  ASSERT_TRUE(ASSERT_RESULT(LatestBalancerTasks(mini_cluster())).empty());
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

  // Drive data should be empty: the master shows the sizes its tablet leaders report.
  const auto locations = ASSERT_RESULT(GetTableLocations(mini_cluster(), table_name()));
  master::GetTabletsMetadataRequestPB req;
  master::GetTabletsMetadataResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(kDefaultTimeout);
  auto proxy = ASSERT_RESULT(mini_cluster()->GetLeaderMasterProxy<master::MasterClientProxy>());
  ASSERT_OK(proxy.GetTabletsMetadata(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.error().ShortDebugString();
  size_t found = 0;
  for (const auto& tablet : resp.tablet_metadatas()) {
    if (locations.contains(tablet.tablet_id())) {
      ++found;
      ASSERT_EQ(tablet.sst_files_disk_size(), 0);
      ASSERT_EQ(tablet.wal_files_disk_size(), 0);
    }
  }
  ASSERT_EQ(found, locations.size());
}

TEST_F(LoadBalancerMiniClusterTest, CheckLoadBalanceDriveAware) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_min_leader_stepdown_retry_interval_ms) = 0;

  // Wait LB to move leaders
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms * 2));
  WaitLoadBalancerIdle(client_.get());

  auto before = ASSERT_RESULT(GetTabletsDriveStats(mini_cluster(), table_name()));

  // Add new tserver to force load balancer moves.
  auto new_ts_index = mini_cluster()->num_tablet_servers();
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(new_ts_index + 1));
  auto new_ts_uuid = mini_cluster()->mini_tablet_server(new_ts_index)->server()->permanent_uuid();

  // Wait LB to move tablets to new ts
  WaitLoadBalancerActive(client_.get());
  WaitLoadBalancerIdle(client_.get());

  auto after = ASSERT_RESULT(GetTabletsDriveStats(mini_cluster(), table_name()));

  int balanced_servers = 0;
  for (size_t ts_index = 0; ts_index < new_ts_index; ++ts_index) {
    const auto ts_uuid = mini_cluster()->mini_tablet_server(ts_index)->server()->permanent_uuid();
    std::vector<std::string> drives;
    auto& ts_before = before[ts_uuid];
    auto& ts_after = after[ts_uuid];
    for (const auto& drive : ts_before.dir_tablets) {
      drives.emplace_back(drive.first);
    }
    ASSERT_FALSE(drives.empty());
    std::sort(drives.begin(), drives.end());
    LOG(INFO) << "P " << ts_uuid;
    LOG(INFO) << "Leaders before: " << ts_before.num_leaders << " after: " << ts_after.num_leaders;

    size_t tablets = ts_after.dir_tablets[drives.front()].size();
    bool drives_balanced = true;
    for (const auto& drive : drives) {
      LOG(INFO) << drive << " before: " << AsString(ts_before.dir_tablets[drive]) <<
                   " after: " << AsString(ts_after.dir_tablets[drive]);
      if (ts_after.dir_tablets[drive].size() != tablets) {
        drives_balanced = false;
      }
    }
    if (drives_balanced) {
      ++balanced_servers;
    }
  }

  // There are 4 tablets in this test and each tserver has 3 disks.
  // The goal of the test is to get 1 tablet on each disk after adding a new tablet server.
  // Before adding a tserver we have 2 tablets on some disk for all tservers, and 1 tablet on the
  // other disks. It could happen that the same two tablets share a disk on all tservers.
  // In this case we cannot guarantee even disk load, but at least 2 original tservers should have
  // a balanced disks after load balancing.
  ASSERT_GE(balanced_servers, 2);
}

TEST_F(LoadBalancerMiniClusterTest, LeaderMovesWithGeopartitionedTables) {
  ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
      "cloud1.region1.zone,cloud1.region2.zone,cloud2.region3.zone", 3, ""));

  // Add new set of tservers.
  for (const auto& [cloud, region, uuid] : {
           std::tuple("cloud2", "region4", "fffffffffffffffffffffffffffffffc"),
           std::tuple("cloud3", "region5", "fffffffffffffffffffffffffffffffd"),
           std::tuple("cloud3", "region6", "fffffffffffffffffffffffffffffffe"),
       }) {
    // Set large instance uuids to make sure these tservers are sorted last when breaking ties.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_instance_uuid_override) = uuid;
    tserver::TabletServerOptions extra_opts =
        ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
    extra_opts.SetPlacement(cloud, region, "zone");
    ASSERT_OK(mini_cluster()->AddTabletServer(extra_opts));
  }
  ASSERT_OK(mini_cluster()->WaitForTabletServerCount(num_tablet_servers() + 3));

  // Create two new tables.
  for (int i = 2; i <= 3; ++i) {
    client::YBTableName tn(
        YQL_DATABASE_CQL, table_name().namespace_name(), "kv-table-test-" + std::to_string(i));
    client::YBSchemaBuilder b;
    b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
    b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
    ASSERT_OK(b.Build(&schema_));

    ASSERT_OK(NewTableCreator()->table_name(tn).schema(&schema_).Create());
  }

  // The load balancer processes tables in table id order; take the first of this keyspace's.
  auto catalog = ASSERT_RESULT(MasterTestApi(mini_cluster()).Catalog());
  const auto keyspace = ASSERT_RESULT(catalog.FindNamespace(
      YQLDatabase::YQL_DATABASE_CQL, table_name().namespace_name()));
  master::TableInfoPtr first_table;
  for (const auto& table : catalog.GetTables()) {
    if (table->namespace_id() == keyspace->id() && !table->is_deleted() &&
        (!first_table || table->id() < first_table->id())) {
      first_table = table;
    }
  }
  ASSERT_NE(first_table, nullptr);

  // Select the first table to be processed by the load balancer, and move it to the other region.
  client::YBTableName first_table_name(
      YQLDatabase::YQL_DATABASE_CQL, table_name().namespace_name(), first_table->name());
  ASSERT_OK(yb_admin_client_->ModifyTablePlacementInfo(
      first_table_name, "cloud2.region4.zone,cloud3.region5.zone,cloud3.region6.zone", 3, ""));

  // Wait for the load balancer to finish.
  WaitLoadBalancerActive(
      client_.get(), "Waiting for LB to begin after changing placement of first table");
  // Full move of a table may take a bit longer, so wait longer for idle.
  WaitLoadBalancerIdle(
      client_.get(), "Waiting for load to settle after changing placement of first table",
      kDefaultTimeout * 2);

  auto leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(first_table_name));
  LOG(INFO) << "Leader counts before blacklist: " << ToString(leader_counts);

  // Now leader blacklist one of the new tservers. Ensure that the load balancer is able to move
  // leaders off of it.
  ASSERT_OK(AddTserverToBlacklist(4, true /* leader_blacklist */));
  WaitLoadBalancerActive(client_.get(), "Waiting for LB to begin after blacklisting tserver");
  WaitLoadBalancerIdle(client_.get(), "Waiting for load to settle after blacklisting tserver");

  // Assert that the leaders have been moved off of the leader blacklisted tserver.
  leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(first_table_name));
  LOG(INFO) << "Leader counts after blacklist: " << ToString(leader_counts);
  ASSERT_EQ(leader_counts.at(mini_cluster_->mini_tablet_server(4)->server()->permanent_uuid()), 0);

  // Finally, remove the tserver from the blacklist.
  ASSERT_OK(RemoveTserverFromBlacklist(4, true /* leader_blacklist */));
  WaitLoadBalancerActive(client_.get(), "Waiting for LB to begin after unblacklisting tserver");
  WaitLoadBalancerIdle(client_.get(), "Waiting for load to settle after unblacklisting tserver");

  // Ensure that even though we picked the first table to move, the load balancer is able to move
  // leaders based on the correct overall global load, and not just the global load at the time of
  // AnalyzeTablets.
  leader_counts = ASSERT_RESULT(yb_admin_client_->GetLeaderCounts(first_table_name));
  LOG(INFO) << "Leader counts after unblacklist: " << ToString(leader_counts);
  ASSERT_GT(leader_counts.at(mini_cluster_->mini_tablet_server(4)->server()->permanent_uuid()), 0);
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

  const auto ts1_uuid = mini_cluster_->mini_tablet_server(0)->server()->permanent_uuid();
  for (const auto& [tablet_id, locations] :
       ASSERT_RESULT(GetTableLocations(mini_cluster(), table_name()))) {
    for (const auto& replica : locations.replicas()) {
      EXPECT_NE(ts1_uuid, replica.ts_info().permanent_uuid()) << tablet_id;
    }
  }
}

} // namespace integration_tests
} // namespace yb
