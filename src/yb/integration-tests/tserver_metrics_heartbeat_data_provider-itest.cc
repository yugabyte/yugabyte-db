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

#include "yb/util/backoff_waiter.h"

#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"

#include "yb/dockv/partition.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_metrics_heartbeat_data_provider.h"

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

    PauseAllTserverHeartbeats(true);
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
  void TriggerAdminCompactions(bool should_wait) {
    for (const auto& tablet_server : cluster_->mini_tablet_servers()) {
      auto* ts_tablet_manager = tablet_server->server()->tablet_manager();
      const auto tablet_peers = ts_tablet_manager->GetTabletPeersWithTableId(test_table_id_);
      TSTabletManager::TabletPtrs test_tablet_ptrs;
      for (const auto& tablet_peer : tablet_peers) {
        test_tablet_ptrs.push_back(tablet_peer->shared_tablet());
      }
      ASSERT_OK(ts_tablet_manager->TriggerAdminCompaction(
          test_tablet_ptrs, should_wait /* should_wait */));
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
  TriggerAdminCompactions(true /* should_wait */);
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
  TriggerAdminCompactions(false /* should_wait */);
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
