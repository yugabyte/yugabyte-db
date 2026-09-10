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

#include "yb/fs/fs_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/tsan_util.h"

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"

#include "yb/dockv/partition.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_metrics_heartbeat_data_provider.h"
#include "yb/tserver/ysql_advisory_lock_table.h"

DECLARE_int32(scheduled_full_compaction_frequency_hours);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_bool(TEST_pause_before_full_compaction);

namespace yb {
namespace tserver {

class TServerMetricsHeartbeatDataProviderITest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    cluster_.reset(new MiniCluster(MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(CreateClient());
  }

  void DoTearDown() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
    MiniClusterTestWithClient::DoTearDown();
  }

  Result<bool> AddDataSatisfies(
      std::function<Result<bool>(const master::TSHeartbeatRequestPB&)> metrics_check) {
    for (const auto& tablet_server : cluster_->mini_tablet_servers()) {
      auto metrics_heartbeat_provider =
          TServerMetricsHeartbeatDataProvider(tablet_server->server());
      const master::TSHeartbeatResponsePB resp;
      master::TSHeartbeatRequestPB req;

      metrics_heartbeat_provider.AddData(resp, &req);

      if (!VERIFY_RESULT(metrics_check(req))) {
        return false;
      }
    }
    return true;
  }
};

// Verify that the heartbeat carries a storage_tier label on every path_metric
// entry.  In a default mini-cluster (no labeled --fs_data_dirs), all paths
// should report "ssd" (kDefaultStorageTier).
TEST_F(TServerMetricsHeartbeatDataProviderITest, PathMetricsCarryStorageTier) {
  ASSERT_TRUE(ASSERT_RESULT(AddDataSatisfies(
      [](const master::TSHeartbeatRequestPB& req) -> Result<bool> {
        if (!req.has_metrics()) {
          return false;
        }
        const auto& metrics = req.metrics();
        // At least one path metric must be present.
        if (metrics.path_metrics_size() == 0) {
          return false;
        }
        for (const auto& pm : metrics.path_metrics()) {
          // Every path metric must carry a storage_tier.
          if (!pm.has_storage_tier()) {
            return false;
          }
          // Default cluster: no labels set, so all tiers should equal the default.
          if (pm.storage_tier() != FsManager::kDefaultStorageTier) {
            return false;
          }
        }
        return true;
      })));
}

class TServerFullCompactionStatusMetricsHeartbeatDataProviderITest
    : public TServerMetricsHeartbeatDataProviderITest {
  void SetUp() override {
    TServerMetricsHeartbeatDataProviderITest::SetUp();
    // Disable automatic compactions.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;

    const client::YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "test-table");
    ASSERT_OK(client_->CreateNamespaceIfNotExists(
        kTableName.namespace_name(), kTableName.namespace_type()));

    std::shared_ptr<client::YBTable> table;
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    const auto schema = itest::SimpleIntKeyYBSchema();
    ASSERT_OK(table_creator->table_name(kTableName)
                  .schema(&schema)
                  .hash_schema(dockv::YBHashSchema::kMultiColumnHash)
                  .Create());
    ASSERT_OK(client_->OpenTable(kTableName, &table));
    test_table_id_ = table->id();

    // Wait for the master's background creation of the pg_advisory_locks system table to finish
    // before pausing heartbeats. Otherwise the table will be stuck in "is being created" state
    // because tablet assignment requires live tserver heartbeats.
    ASSERT_OK(client_->WaitForCreateTableToFinish(
        client::YBTableName(
            YQL_DATABASE_CQL, master::kSystemNamespaceName,
            std::string(kPgAdvisoryLocksTableName)),
        CoarseMonoClock::Now() + 60s * kTimeMultiplier));

    // Also wait until the master has no pending tablet assignments for any table, so that pausing
    // heartbeats here won't strand any other lazy system table in a half-created state.
    ASSERT_OK(WaitForNoPendingTabletAssignments(60s * kTimeMultiplier));

    PauseAllTserverHeartbeats(true);
  }

  Status WaitForNoPendingTabletAssignments(MonoDelta timeout) {
    return WaitFor(
        [this]() -> Result<bool> {
          auto* leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
          auto& catalog_manager = leader_master->catalog_manager();
          for (const auto& table : catalog_manager.GetTables(master::GetTablesMode::kAll)) {
            auto tablets = VERIFY_RESULT(table->GetTablets());
            for (const auto& tablet : tablets) {
              auto lock = tablet->LockForRead();
              if (!lock->is_running() && !lock->is_deleted()) {
                return false;
              }
            }
          }
          return true;
        },
        timeout, "Waiting for master to have no pending tablet assignments");
  }

  void DoBeforeTearDown() override {
    // Unpause heartbeats to avoid anything to be stuck because of paused heartbeats.
    if (cluster_) {
      PauseAllTserverHeartbeats(false);
    }
    TServerMetricsHeartbeatDataProviderITest::DoBeforeTearDown();
  }

 protected:
  std::function<Result<bool>(const master::TSHeartbeatRequestPB&)> GetMetricCheck(
      std::function<bool(const master::FullCompactionStatusPB&)> full_compaction_status_check) {
    return [this,
            full_compaction_status_check](const master::TSHeartbeatRequestPB& req) -> Result<bool> {
      auto& catalog_manager = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
      for (const auto& full_compaction_status : req.full_compaction_statuses()) {
        if (!full_compaction_status.has_tablet_id()) {
          return false;
        }

        const auto tablet_info =
            VERIFY_RESULT(catalog_manager.GetTabletInfo(full_compaction_status.tablet_id()));
        if (tablet_info->table()->id() != test_table_id_) {
          continue;
        }

        if (!full_compaction_status_check(full_compaction_status)) {
          return false;
        }
      }
      return true;
    };
  }

  // should_wait determines whether the function is asynchronous or synchronous.
  void TriggerAdminCompactions(ShouldWait should_wait) {
    for (const auto& tablet_server : cluster_->mini_tablet_servers()) {
      auto* ts_tablet_manager = tablet_server->server()->tablet_manager();
      const auto tablet_peers = ts_tablet_manager->GetTabletPeersWithTableId(test_table_id_);
      TSTabletManager::TabletPtrs test_tablet_ptrs;
      for (const auto& tablet_peer : tablet_peers) {
        test_tablet_ptrs.push_back(tablet_peer->shared_tablet_maybe_null());
      }
      ASSERT_OK(ts_tablet_manager->TriggerAdminCompaction(
          test_tablet_ptrs, AdminCompactionOptions { should_wait }));
    }
  }

  void PauseAllTserverHeartbeats(bool pause) {
    for (const auto& tablet_server : cluster_->mini_tablet_servers()) {
      tablet_server->FailHeartbeats(pause);
    }
  }

  TableId test_table_id_;
};

TEST_F(
    TServerFullCompactionStatusMetricsHeartbeatDataProviderITest,
    MetricsReportIdleCompactionState) {
  TriggerAdminCompactions(ShouldWait::kTrue);
  ASSERT_TRUE(ASSERT_RESULT(AddDataSatisfies(
      GetMetricCheck([](const master::FullCompactionStatusPB& full_compaction_status) {
        return full_compaction_status.has_full_compaction_state() &&
               full_compaction_status.full_compaction_state() == tablet::IDLE &&
               full_compaction_status.has_last_full_compaction_time() &&
               full_compaction_status.last_full_compaction_time() > 0;
      }))));
}

TEST_F(
    TServerFullCompactionStatusMetricsHeartbeatDataProviderITest,
    MetricsReportCompactingCompactionState) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;
  TriggerAdminCompactions(ShouldWait::kFalse);
  ASSERT_OK(WaitFor(
      [&]() {
        return AddDataSatisfies(
            GetMetricCheck([](const master::FullCompactionStatusPB& full_compaction_status) {
              return full_compaction_status.has_full_compaction_state() &&
                     full_compaction_status.full_compaction_state() == tablet::COMPACTING &&
                     full_compaction_status.has_last_full_compaction_time() &&
                     full_compaction_status.last_full_compaction_time() == 0;
            }));
      },
      30s /* timeout */,
      "Waiting for all full compaction states to be reported as COMPACTING"));
}

}  // namespace tserver
}  // namespace yb
