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
#include "yb/util/storage_tier.h"
#include "yb/util/tsan_util.h"

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ql_protocol_util.h"

#include "yb/consensus/raft_consensus.h"

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
DECLARE_bool(enable_tiered_storage);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_bool(tserver_heartbeat_metrics_send_storage_metadata_on_change);

namespace yb {
namespace tserver {

class TServerMetricsHeartbeatDataProviderITest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tiered_storage) = EnableTieredStorage();
    cluster_.reset(new MiniCluster(MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(CreateClient());
  }

  void DoTearDown() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
    MiniClusterTestWithClient::DoTearDown();
  }

  virtual bool EnableTieredStorage() const { return true; }

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
          if (pm.storage_tier() != kDefaultStorageTier) {
            return false;
          }
        }
        return true;
      })));
}

class TServerMetricsHeartbeatTieredStorageDisabledITest
    : public TServerMetricsHeartbeatDataProviderITest {
 public:
  bool EnableTieredStorage() const override { return false; }
};

// With tiered storage disabled (the default), path metrics carry no storage_tier at all, so the
// master-side API looks exactly as it did before the feature.
TEST_F(TServerMetricsHeartbeatTieredStorageDisabledITest, PathMetricsOmitStorageTier) {
  ASSERT_TRUE(ASSERT_RESULT(AddDataSatisfies(
      [](const master::TSHeartbeatRequestPB& req) -> Result<bool> {
        if (!req.has_metrics() || req.metrics().path_metrics_size() == 0) {
          return false;
        }
        for (const auto& pm : req.metrics().path_metrics()) {
          if (pm.has_storage_tier()) {
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

class TServerLeaderInfoHeartbeatDataProviderITest
    : public TServerMetricsHeartbeatDataProviderITest {
 public:
  void SetUp() override {
    TServerMetricsHeartbeatDataProviderITest::SetUp();
    const client::YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "leader-info-table");
    ASSERT_OK(client_->CreateNamespaceIfNotExists(
        kTableName.namespace_name(), kTableName.namespace_type()));
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    const auto schema = itest::SimpleIntKeyYBSchema();
    ASSERT_OK(table_creator->table_name(kTableName)
                  .schema(&schema)
                  .hash_schema(dockv::YBHashSchema::kMultiColumnHash)
                  .num_tablets(kNumTablets)
                  .Create());
    std::shared_ptr<client::YBTable> table;
    ASSERT_OK(client_->OpenTable(kTableName, &table));
    test_table_id_ = table->id();
  }

 protected:
  static constexpr int kNumTablets = 3;

  master::TSHeartbeatRequestPB BuildRequest(MiniTabletServer* tablet_server) {
    const master::TSHeartbeatResponsePB resp;
    master::TSHeartbeatRequestPB req;
    TServerMetricsHeartbeatDataProvider(tablet_server->server()).AddData(resp, &req);
    return req;
  }

  TableId test_table_id_;
};

// A tserver reports leader_info for exactly the tablets it leads, each with its lease, and
// nothing for the tablets it follows.
TEST_F(TServerLeaderInfoHeartbeatDataProviderITest, LeaderInfoOnlyForLedTablets) {
  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> {
        size_t leaders = 0;
        for (const auto& tablet_server : cluster_->mini_tablet_servers()) {
          std::set<TabletId> hosted;
          std::set<TabletId> led;
          for (const auto& peer :
               tablet_server->server()->tablet_manager()->GetTabletPeersWithTableId(
                   test_table_id_)) {
            hosted.insert(peer->tablet_id());
            if (VERIFY_RESULT(peer->GetRaftConsensus())->role() == PeerRole::LEADER) {
              led.insert(peer->tablet_id());
            }
          }
          std::set<TabletId> reported;
          for (const auto& info : BuildRequest(tablet_server.get()).leader_info()) {
            if (!hosted.contains(info.tablet_id())) {
              continue;
            }
            if (info.leader_lease_status() != consensus::LeaderLeaseStatus::HAS_LEASE ||
                info.ht_lease_expiration() == 0) {
              return false;
            }
            reported.insert(info.tablet_id());
          }
          if (reported != led) {
            return false;
          }
          leaders += led.size();
        }
        return leaders == kNumTablets;
      },
      30s * kTimeMultiplier, "Waiting for every tablet leader to report its lease"));
}

class TServerStorageMetadataHeartbeatDataProviderITest
    : public TServerMetricsHeartbeatDataProviderITest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 1;
    TServerMetricsHeartbeatDataProviderITest::SetUp();
    const client::YBTableName kTableName(
        YQL_DATABASE_CQL, "my_keyspace", "storage-metadata-table");
    ASSERT_OK(client_->CreateNamespaceIfNotExists(
        kTableName.namespace_name(), kTableName.namespace_type()));
    // A hash key, so the table really gets kNumTablets and a row lands in exactly one of them.
    client::YBSchema schema;
    client::YBSchemaBuilder builder;
    builder.AddColumn("key")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    ASSERT_OK(builder.Build(&schema));
    ASSERT_OK(table_.Create(kTableName, kNumTablets, schema, client_.get()));
    // One provider per tserver, kept across heartbeats like the real heartbeater does.
    for (const auto& tablet_server : cluster_->mini_tablet_servers()) {
      providers_.push_back(
          std::make_unique<TServerMetricsHeartbeatDataProvider>(tablet_server->server()));
    }
  }

 protected:
  static constexpr int kNumTablets = 3;
  using TabletIds = std::set<TabletId>;

  static master::TSHeartbeatResponsePB Unacked() { return {}; }

  static master::TSHeartbeatResponsePB Acked() {
    master::TSHeartbeatResponsePB resp;
    resp.set_tablet_metadata_processed(true);
    return resp;
  }

  static master::TSHeartbeatResponsePB FullReportRequested() {
    master::TSHeartbeatResponsePB resp;
    resp.set_needs_full_tablet_report(true);
    return resp;
  }

  // One metrics heartbeat from every tserver: build the request after last_resp, then hand the
  // provider this heartbeat's response. Returns the storage_metadata tablet ids each tserver sent.
  std::vector<TabletIds> Heartbeat(
      const master::TSHeartbeatResponsePB& last_resp, const master::TSHeartbeatResponsePB& resp) {
    std::vector<TabletIds> sent;
    for (auto& provider : providers_) {
      SleepFor(5ms);  // Let the metrics period elapse.
      master::TSHeartbeatRequestPB req;
      // The tablet report provider answers a full-report request with a non-incremental report.
      if (last_resp.needs_full_tablet_report()) {
        req.mutable_tablet_report()->set_is_incremental(false);
      }
      provider->AddData(last_resp, &req);
      TabletIds ids;
      for (const auto& metadata : req.storage_metadata()) {
        ids.insert(metadata.tablet_id());
      }
      sent.push_back(std::move(ids));
      provider->OnHeartbeatResponse(resp);
    }
    return sent;
  }

  static bool AllEmpty(const std::vector<TabletIds>& sent) {
    return std::all_of(sent.begin(), sent.end(), [](const auto& ids) { return ids.empty(); });
  }

  TabletIds TestTablets(size_t ts_idx) {
    TabletIds ids;
    for (const auto& peer : cluster_->mini_tablet_server(ts_idx)->server()->tablet_manager()
                                ->GetTabletPeersWithTableId(table_->id())) {
      ids.insert(peer->tablet_id());
    }
    return ids;
  }

  // Every tserver reports every tablet it hosts, the test table's among them.
  void AssertAllTablets(const std::vector<TabletIds>& sent) {
    ASSERT_EQ(sent.size(), providers_.size());
    TabletIds all_test_tablets;
    for (size_t i = 0; i < sent.size(); ++i) {
      auto test_tablets = TestTablets(i);
      ASSERT_TRUE(std::includes(
          sent[i].begin(), sent[i].end(), test_tablets.begin(), test_tablets.end()));
      all_test_tablets.insert(test_tablets.begin(), test_tablets.end());
    }
    ASSERT_EQ(all_test_tablets.size(), static_cast<size_t>(kNumTablets));
  }

  void InsertRow(int32_t key) {
    auto session = client_->NewSession(30s);
    auto insert = table_.NewInsertOp(session->arena());
    QLAddInt32HashValue(insert->mutable_request(), key);
    ASSERT_OK(session->TEST_ApplyAndFlush(insert));
  }

  client::TableHandle table_;
  std::vector<std::unique_ptr<TServerMetricsHeartbeatDataProvider>> providers_;
};

TEST_F(TServerStorageMetadataHeartbeatDataProviderITest, SentUntilAcked) {
  auto all = Heartbeat(Unacked(), Unacked());
  ASSERT_NO_FATALS(AssertAllTablets(all));
  // Not confirmed by the master, so sent again; this response confirms it.
  ASSERT_EQ(Heartbeat(Unacked(), Acked()), all);
  ASSERT_TRUE(AllEmpty(Heartbeat(Acked(), Acked())));
  ASSERT_TRUE(AllEmpty(Heartbeat(Acked(), Acked())));
}

TEST_F(TServerStorageMetadataHeartbeatDataProviderITest, ResentOnlyForChangedTablets) {
  auto all = Heartbeat(Unacked(), Acked());
  ASSERT_NO_FATALS(AssertAllTablets(all));
  ASSERT_TRUE(AllEmpty(Heartbeat(Acked(), Acked())));

  // One row grows the WAL and total size by far less than the change threshold.
  ASSERT_NO_FATALS(InsertRow(1));
  ASSERT_TRUE(AllEmpty(Heartbeat(Acked(), Acked())));

  // Flushing turns the row into an SST file, an exact-compare field.
  ASSERT_OK(cluster_->FlushTablets());
  auto changed = Heartbeat(Acked(), Acked());
  TabletIds changed_test_tablets;
  for (size_t i = 0; i < changed.size(); ++i) {
    ASSERT_LT(changed[i].size(), all[i].size());
    ASSERT_TRUE(std::includes(all[i].begin(), all[i].end(), changed[i].begin(), changed[i].end()));
    auto test_tablets = TestTablets(i);
    std::set_intersection(
        changed[i].begin(), changed[i].end(), test_tablets.begin(), test_tablets.end(),
        std::inserter(changed_test_tablets, changed_test_tablets.begin()));
  }
  // The row landed in exactly one tablet, and only that tablet gained an SST file.
  ASSERT_EQ(changed_test_tablets.size(), 1);
  ASSERT_TRUE(AllEmpty(Heartbeat(Acked(), Acked())));
}

TEST_F(TServerStorageMetadataHeartbeatDataProviderITest, ResentAfterFullReport) {
  auto all = Heartbeat(Unacked(), Acked());
  ASSERT_NO_FATALS(AssertAllTablets(all));
  // A full tablet report carries no storage metadata and forgets what the master confirmed.
  ASSERT_TRUE(AllEmpty(Heartbeat(FullReportRequested(), Acked())));
  ASSERT_EQ(Heartbeat(Acked(), Acked()), all);
  ASSERT_TRUE(AllEmpty(Heartbeat(Acked(), Acked())));
}

TEST_F(TServerStorageMetadataHeartbeatDataProviderITest, AlwaysSentWhenDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_send_storage_metadata_on_change) =
      false;
  auto all = Heartbeat(Unacked(), Acked());
  ASSERT_NO_FATALS(AssertAllTablets(all));
  ASSERT_EQ(Heartbeat(Acked(), Acked()), all);
}

}  // namespace tserver
}  // namespace yb
