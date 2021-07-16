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

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/cluster_verifier.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"
#include "yb/master/master-test-util.h"
#include "yb/master/master_fwd.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/master.proxy.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/tserver/tablet_server_options.h"
#include "yb/util/monotime.h"
#include "yb/gutil/dynamic_annotations.h"

DECLARE_bool(enable_load_balancing);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(TEST_slowdown_master_async_rpc_tasks_by_ms);
DECLARE_int32(TEST_load_balancer_wait_after_count_pending_tasks_ms);
DECLARE_int32(load_balancer_max_concurrent_moves);

using namespace std::literals;

namespace yb {
namespace integration_tests {

namespace {

class StatEmuEnv : public EnvWrapper {
 public:
  StatEmuEnv() : EnvWrapper(Env::Default()) { }

  virtual Result<FilesystemStats> GetFilesystemStatsBytes(const std::string& f) override {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto i = stats_.find(f);
    if (i == stats_.end()) {
      return target()->GetFilesystemStatsBytes(f);
    }
    return i->second;
  }

  void AddPathStats(const std::string& path, const Env::FilesystemStats& stats) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    ASSERT_TRUE(stats_.emplace(path, stats).second);
  }

 private:
  std::unordered_map<std::string, Env::FilesystemStats> stats_ GUARDED_BY(data_mutex_);
  std::mutex data_mutex_;
};

} // namespace

const auto kDefaultTimeout = 30000ms;

class LoadBalancerMiniClusterTest : public YBTableTestBase {
 protected:
  void SetUp() override {
    StatEmuEnv* env = new StatEmuEnv();
    ts_env_.reset(env);
    YBTableTestBase::SetUp();
    // ts1 (free, used, total)
    env->AddPathStats(mini_cluster()->GetTabletServerDrive(0, 0), { 50, 150, 200});
    env->AddPathStats(mini_cluster()->GetTabletServerDrive(0, 1), {100, 100, 200});
    env->AddPathStats(mini_cluster()->GetTabletServerDrive(0, 2), {150,  50, 200});
    // ts2 (free, used, total)
    env->AddPathStats(mini_cluster()->GetTabletServerDrive(1, 0), { 50, 150, 200});
    env->AddPathStats(mini_cluster()->GetTabletServerDrive(1, 1), {100, 100, 200});
    env->AddPathStats(mini_cluster()->GetTabletServerDrive(1, 2), {150,  50, 200});
  }

  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return false; }

  int num_drives() override {
    return 3;
  }

  int num_tablets() override {
    return 4;
  }

  bool enable_ysql() override {
    // Do not create the transaction status table.
    return false;
  }
};

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
      (*leader_mini_master)->master()->catalog_manager()->
          GetTableInfoFromNamespaceNameAndTableName(table_name().namespace_type(),
                                                    table_name().namespace_name(),
                                                    table_name().table_name());
    vector<scoped_refptr<master::TabletInfo>> tablets;
    tbl_info->GetAllTablets(&tablets);
    bool foundReplica = false;
    for (const auto& tablet : tablets) {
      auto replica_map = tablet->GetReplicaLocations();
      if (replica_map->find(ts3_uuid) != replica_map->end()) {
        foundReplica = true;
        break;
      }
    }
    return foundReplica;
  }, kDefaultTimeout, "WaitForAddTaskToBeProcessed"));

  // Modify GetAllReportedDescriptors so that it does not report the new tserver
  // (this could happen normally from a late heartbeat).
  master::TSDescriptorVector ts_descs;
  master::TSDescriptorPtr ts3_desc;
  ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())
      ->master()
      ->ts_manager()
      ->GetAllReportedDescriptors(&ts_descs);
  for (const auto& ts_desc : ts_descs) {
    if (ts_desc->permanent_uuid() == ts3_uuid) {
      ts_desc->SetRemoved();
      ts3_desc = ts_desc;
      break;
    }
  }
  CHECK_NOTNULL(ts3_desc);

  // LB run #2 will now continue, with GetAllReportedDescriptors not reporting this tserver, but
  // with GetReplicaLocations reporting it.

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
    return !is_idle;
  },  kDefaultTimeout * 2, "IsLoadBalancerActive"));

  // If the seg fault occurs, it will occur in the first 2 runs.
  SleepFor(MonoDelta::FromMilliseconds(3 * test_bg_task_wait_ms));

  // If it has yet to happen, bring back the tserver so that load balancing can complete.
  ts3_desc->SetRemoved(false);
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
    PartitionSchema partition_schema;
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
                                ->master()
                                ->catalog_manager()
                                ->load_balancer()
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
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto leader_mini_master = mini_cluster()->GetLeaderMiniMaster();
    if (!leader_mini_master.ok()) {
      return false;
    }
    scoped_refptr<master::TableInfo> tbl_info =
      (*leader_mini_master)->master()->catalog_manager()->
          GetTableInfoFromNamespaceNameAndTableName(table_name().namespace_type(),
                                                    table_name().namespace_name(),
                                                    table_name().table_name());
    vector<scoped_refptr<master::TabletInfo>> tablets;
    tbl_info->GetAllTablets(&tablets);

    for (const auto& tablet : tablets) {
      auto replica_map = tablet->GetReplicaLocations();
      for (const auto& replica : *replica_map.get()) {
        if (!replica.second.drive_info.ts_path.empty()) {
          return true;
        }
      }
    }
    return false;
  }, MonoDelta::FromMilliseconds(10000), "WaitForTabletDataSize"));
}

} // namespace integration_tests
} // namespace yb
