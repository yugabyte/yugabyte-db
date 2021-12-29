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

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "yb/client/table.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_util.h"

#include "yb/docdb/doc_key.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/strings/join.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/redis_table_test_base.h"
#include "yb/integration-tests/tablet-split-itest-base.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_error.h"
#include "yb/master/master_heartbeat.pb.h"

#include "yb/rocksdb/db.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/format.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;  // NOLINT

DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_bool(enable_load_balancing);
DECLARE_int32(load_balancer_max_concurrent_adds);
DECLARE_int32(load_balancer_max_concurrent_removals);
DECLARE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_bool(TEST_do_not_start_election_test_only);
DECLARE_int32(TEST_apply_tablet_split_inject_delay_ms);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(leader_lease_duration_ms);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_bool(TEST_skip_deleting_split_tablets);
DECLARE_int32(tablet_split_limit_per_table);
DECLARE_bool(TEST_pause_before_post_split_compaction);
DECLARE_int32(TEST_slowdown_backfill_alter_table_rpcs_ms);
DECLARE_int32(rocksdb_base_background_compactions);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_bool(TEST_disable_split_tablet_candidate_processing);
DECLARE_int32(process_split_tablet_candidates_interval_msec);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_bool(TEST_validate_all_tablet_candidates);
DECLARE_bool(TEST_select_all_tablets_for_split);
DECLARE_int32(outstanding_tablet_split_limit);
DECLARE_double(TEST_fail_tablet_split_probability);
DECLARE_bool(TEST_skip_post_split_compaction);
DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_int32(replication_factor);

namespace yb {
class TabletSplitITestWithIsolationLevel : public TabletSplitITest,
                                           public testing::WithParamInterface<IsolationLevel> {
 public:
  void SetUp() override {
    SetIsolationLevel(GetParam());
    TabletSplitITest::SetUp();
  }
};

// Tests splitting of the single tablet in following steps:
// - Create single-tablet table and populates it with specified number of rows.
// - Do full scan using `select count(*)`.
// - Send SplitTablet RPC to the tablet leader.
// - After tablet split is completed - check that new tablets have exactly the same rows.
// - Check that source tablet is rejecting reads and writes.
// - Do full scan using `select count(*)`.
// - Restart cluster.
// - ClusterVerifier will check cluster integrity at the end of the test.

TEST_P(TabletSplitITestWithIsolationLevel, SplitSingleTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;

  // TODO(tsplit): add delay of applying part of intents after tablet is split.
  // TODO(tsplit): test split during long-running transactions.

  constexpr auto kNumRows = kDefaultNumRows;

  const auto source_tablet_id = ASSERT_RESULT(CreateSingleTabletAndSplit(kNumRows));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = false;

  ASSERT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2));

  ASSERT_OK(CheckRowsCount(kNumRows));

  ASSERT_OK(WriteRows(kNumRows, kNumRows + 1));

  ASSERT_OK(cluster_->RestartSync());

  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows * 2));
}

TEST_F(TabletSplitITest, SplitTabletIsAsync) {
  constexpr auto kNumRows = kDefaultNumRows;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = true;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  for (auto peer : ASSERT_RESULT(ListPostSplitChildrenTabletPeers())) {
    EXPECT_FALSE(peer->tablet()->metadata()->has_been_fully_compacted());
  }
  std::this_thread::sleep_for(1s * kTimeMultiplier);
  for (auto peer : ASSERT_RESULT(ListPostSplitChildrenTabletPeers())) {
    EXPECT_FALSE(peer->tablet()->metadata()->has_been_fully_compacted());
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = false;
  ASSERT_OK(WaitForTestTablePostSplitTabletsFullyCompacted(15s * kTimeMultiplier));
}

TEST_F(TabletSplitITest, ParentTabletCleanup) {
  constexpr auto kNumRows = kDefaultNumRows;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  // This will make client first try to access deleted tablet and that should be handled correctly.
  ASSERT_OK(CheckRowsCount(kNumRows));
}

TEST_F(TabletSplitITest, TestInitiatesCompactionAfterSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  constexpr auto kNumRows = kDefaultNumRows;

  CreateSingleTablet();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kNumRows));

  ASSERT_OK(LoggedWaitFor(
      [this] {
        const auto count = NumPostSplitTabletPeersFullyCompacted();
        constexpr auto kNumPostSplitTablets = 2;
        if (count.ok()) {
          return *count >= kNumPostSplitTablets * FLAGS_replication_factor;
        }
        LOG(WARNING) << count.status();
        return false;
      },
      15s * kTimeMultiplier, "Waiting for post-split tablets to be fully compacted..."));

  auto pre_split_bytes_written = ASSERT_RESULT(GetInactiveTabletsBytesWritten());
  auto post_split_bytes_read = ASSERT_RESULT((GetActiveTabletsBytesRead()));
  // Make sure that during child tablets compaction we don't read the same row twice, in other words
  // we don't process parent tablet rows that are not served by child tablet.
  ASSERT_GE(pre_split_bytes_written, post_split_bytes_read);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/8295.
// Checks that slow post-split tablet compaction doesn't block that tablet's cleanup.
TEST_F(TabletSplitITest, PostSplitCompactionDoesntBlockTabletCleanup) {
  constexpr auto kNumRows = kDefaultNumRows;
  const MonoDelta kCleanupTimeout = 15s * kTimeMultiplier;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  // Keep tablets without compaction after split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = true;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_do_not_start_election_test_only) = true;
  auto tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  ASSERT_EQ(tablet_peers.size(), 2);
  const auto first_child_tablet = tablet_peers[0]->shared_tablet();
  ASSERT_OK(first_child_tablet->Flush(tablet::FlushMode::kSync));
  // Force compact on leader, so we can split first_child_tablet.
  ASSERT_OK(first_child_tablet->ForceFullRocksDBCompact());
  // Turn off split tablets cleanup in order to later turn it on during compaction of the
  // first_child_tablet to make sure manual compaction won't block tablet shutdown.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  ASSERT_OK(SplitTablet(ASSERT_RESULT(catalog_manager()), *first_child_tablet));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_do_not_start_election_test_only) = false;
  ASSERT_OK(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets = */ 3, /* expected_split_tablets = */ 1));

  // Simulate slow compaction, so it takes at least kCleanupTimeout * 1.5 for first child tablet
  // followers.
  const auto original_compact_flush_rate_bytes_per_sec =
      FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec;
  SetCompactFlushRateLimitBytesPerSec(
      cluster_.get(),
      first_child_tablet->GetCurrentVersionSstFilesSize() / (kCleanupTimeout.ToSeconds() * 1.5));
  // Resume post-split compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = false;
  // Turn on split tablets cleanup.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = false;

  // Cleanup of first_child_tablet will shutdown that tablet after deletion and we check that
  // shutdown is not stuck due to its slow post-split compaction.
  const auto wait_message =
      Format("Waiting for tablet $0 cleanup", first_child_tablet->tablet_id());
  LOG(INFO) << wait_message << "...";
  std::vector<Result<tablet::TabletPeerPtr>> first_child_tablet_peer_results;
  const auto s = WaitFor(
      [this, &first_child_tablet, &first_child_tablet_peer_results] {
        first_child_tablet_peer_results.clear();
        for (auto mini_ts : cluster_->mini_tablet_servers()) {
          auto tablet_peer_result =
              mini_ts->server()->tablet_manager()->LookupTablet(first_child_tablet->tablet_id());
          if (tablet_peer_result.ok() || !tablet_peer_result.status().IsNotFound()) {
            first_child_tablet_peer_results.push_back(tablet_peer_result);
          }
        }
        return first_child_tablet_peer_results.empty();
      },
      kCleanupTimeout, wait_message);
  for (const auto& peer_result : first_child_tablet_peer_results) {
    LOG(INFO) << "Tablet peer not cleaned: "
              << (peer_result.ok() ? (*peer_result)->LogPrefix() : AsString(peer_result.status()));
  }
  ASSERT_OK(s);
  LOG(INFO) << wait_message << " - DONE";

  SetCompactFlushRateLimitBytesPerSec(cluster_.get(), original_compact_flush_rate_bytes_per_sec);
}

TEST_F(TabletSplitITest, TestLoadBalancerAndSplit) {
  constexpr auto kNumRows = kDefaultNumRows;

  // To speed up load balancing (it also processes transaction status tablets).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_adds) = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_removals) = 5;

  // Keep tablets without compaction after split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = true;

  CreateSingleTablet();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kNumRows));

  auto test_table_id = ASSERT_RESULT(GetTestTableId());
  auto test_tablet_ids = ListTabletIdsForTable(cluster_.get(), test_table_id);

  // Verify that heartbeat contains flag should_disable_lb_move for all tablets of the test
  // table on each tserver to have after split.
  for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto* ts_manager = cluster_->mini_tablet_server(i)->server()->tablet_manager();

    master::TabletReportPB report;
    ts_manager->GenerateTabletReport(&report);
    for (const auto& reported_tablet : report.updated_tablets()) {
      if (test_tablet_ids.count(reported_tablet.tablet_id()) == 0) {
        continue;
      }
      ASSERT_TRUE(reported_tablet.should_disable_lb_move());
    }
  }

  // Add new tserver in to force load balancer moves.
  auto new_ts = cluster_->num_tablet_servers();
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(new_ts + 1));
  const auto new_ts_uuid = cluster_->mini_tablet_server(new_ts)->server()->permanent_uuid();

  LOG(INFO) << "Added new tserver: " << new_ts_uuid;

  // Wait for the LB run.
  const auto lb_wait_period = MonoDelta::FromMilliseconds(
      FLAGS_catalog_manager_bg_task_wait_ms * 2 + FLAGS_raft_heartbeat_interval_ms * 2);
  SleepFor(lb_wait_period);

  // Verify none test tablet replica on the new tserver.
  for (const auto& tablet : ASSERT_RESULT(GetTabletInfosForTable(test_table_id))) {
    const auto replica_map = tablet->GetReplicaLocations();
    ASSERT_TRUE(replica_map->find(new_ts_uuid) == replica_map->end())
        << "Not expected tablet " << tablet->id()
        << " to be on newly added tserver: " << new_ts_uuid;
  }

  // Verify that custom placement info is honored when tablets are split.
  const auto& blacklisted_ts = *ASSERT_NOTNULL(cluster_->mini_tablet_server(1));
  const auto blacklisted_ts_uuid = blacklisted_ts.server()->permanent_uuid();
  ASSERT_OK(cluster_->AddTServerToBlacklist(blacklisted_ts));
  LOG(INFO) << "Blacklisted tserver: " << blacklisted_ts_uuid;
  std::vector<TabletId> on_blacklisted_ts;
  std::vector<TabletId> no_replicas_on_new_ts;
  auto s = LoggedWaitFor(
      [&] {
        auto tablet_infos = GetTabletInfosForTable(test_table_id);
        if (!tablet_infos.ok()) {
          return false;
        }
        on_blacklisted_ts.clear();
        no_replicas_on_new_ts.clear();
        for (const auto& tablet : *tablet_infos) {
          auto replica_map = tablet->GetReplicaLocations();
          if (replica_map->count(new_ts_uuid) == 0) {
            no_replicas_on_new_ts.push_back(tablet->id());
          }
          if (replica_map->count(blacklisted_ts_uuid) > 0) {
            on_blacklisted_ts.push_back(tablet->id());
          }
        }
        return on_blacklisted_ts.empty() && no_replicas_on_new_ts.empty();
      },
      60s * kTimeMultiplier,
      Format(
          "Wait for all test tablet replicas to be moved from tserver $0 to $1 on master",
          blacklisted_ts_uuid, new_ts_uuid));
  ASSERT_TRUE(s.ok()) << Format(
      "Replicas are still on blacklisted tserver $0: $1\nNo replicas for tablets on new tserver "
      "$2: $3",
      blacklisted_ts_uuid, on_blacklisted_ts, new_ts_uuid, no_replicas_on_new_ts);

  ASSERT_OK(cluster_->ClearBlacklist());
  // Wait for the LB run.
  SleepFor(lb_wait_period);

  // Test tablets should not move until compaction.
  for (const auto& tablet : ASSERT_RESULT(GetTabletInfosForTable(test_table_id))) {
    const auto replica_map = tablet->GetReplicaLocations();
    ASSERT_TRUE(replica_map->find(blacklisted_ts_uuid) == replica_map->end())
        << "Not expected tablet " << tablet->id() << " to be on tserver " << blacklisted_ts_uuid
        << " that moved out of blacklist before post-split compaction completed";
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = false;

  ASSERT_OK(WaitForTestTablePostSplitTabletsFullyCompacted(15s * kTimeMultiplier));

  ASSERT_OK(LoggedWaitFor(
      [&] {
        auto tablet_infos = GetTabletInfosForTable(test_table_id);
        if (!tablet_infos.ok()) {
          return false;
        }
        for (const auto& tablet : *tablet_infos) {
          auto replica_map = tablet->GetReplicaLocations();
          if (replica_map->find(blacklisted_ts_uuid) == replica_map->end()) {
            return true;
          }
        }
        return false;
      },
      60s * kTimeMultiplier,
      Format(
          "Wait for at least one test tablet replica on tserver that moved out of blacklist: $0",
          blacklisted_ts_uuid)));
}

// Start tablet split, create Index to start backfill while split operation in progress
// and check backfill state.
TEST_F(TabletSplitITest, TestBackfillDuringSplit) {
  constexpr auto kNumRows = 10000;
  FLAGS_TEST_apply_tablet_split_inject_delay_ms = 200 * kTimeMultiplier;

  CreateSingleTablet();
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());
  auto table = catalog_mgr->GetTableInfo(table_->id());
  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto source_tablet_id = source_tablet_info->id();

  // Send SplitTablet RPC to the tablet leader.
  ASSERT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));

  int indexed_column_index = 1;
  const client::YBTableName index_name(
        YQL_DATABASE_CQL, table_.name().namespace_name(),
        table_.name().table_name() + '_' +
          table_.schema().Column(indexed_column_index).name() + "_idx");
  // Create index while split operation in progress
  PrepareIndex(client::Transactional(GetIsolationLevel() != IsolationLevel::NON_TRANSACTIONAL),
               index_name, indexed_column_index);

  // Check that source table is not backfilling and wait for tablet split completion
  ASSERT_FALSE(table->IsBackfilling());
  ASSERT_OK(WaitForTabletSplitCompletion(2));
  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows));

  ASSERT_OK(index_.Open(index_name, client_.get()));
  ASSERT_OK(WaitFor([&] {
    auto rows_count = SelectRowsCount(NewSession(), index_);
    if (!rows_count.ok()) {
      return false;
    }
    return *rows_count == kNumRows;
  }, 30s * kTimeMultiplier, "Waiting for backfill index"));
}

// Create Index to start backfill, check split is not working while backfill in progress
// and check backfill state.
TEST_F(TabletSplitITest, TestSplitDuringBackfill) {
  constexpr auto kNumRows = 10000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms = 200 * kTimeMultiplier;

  CreateSingleTablet();
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  int indexed_column_index = 1;
  const client::YBTableName index_name(
        YQL_DATABASE_CQL, table_.name().namespace_name(),
        table_.name().table_name() + '_' +
          table_.schema().Column(indexed_column_index).name() + "_idx");
  // Create index and start backfill
  PrepareIndex(client::Transactional(GetIsolationLevel() != IsolationLevel::NON_TRANSACTIONAL),
               index_name, indexed_column_index);

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());
  auto table = catalog_mgr->GetTableInfo(table_->id());
  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto source_tablet_id = source_tablet_info->id();

  // Check that source table is backfilling
  ASSERT_OK(WaitFor([&] {
    return table->IsBackfilling();
  }, 30s * kTimeMultiplier, "Waiting for start backfill index"));

  // Send SplitTablet RPC to the tablet leader while backfill in progress
  ASSERT_NOK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));

  ASSERT_OK(WaitFor([&] {
    return !table->IsBackfilling();
  }, 30s * kTimeMultiplier, "Waiting for backfill index"));
  ASSERT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));
  ASSERT_OK(WaitForTabletSplitCompletion(2));
  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows));
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/4312 reproducing a deadlock
// between TSTabletManager::ApplyTabletSplit and Heartbeater::Thread::TryHeartbeat.
TEST_F(TabletSplitITest, SlowSplitSingleTablet) {
  const auto leader_failure_timeout = FLAGS_leader_failure_max_missed_heartbeat_periods *
        FLAGS_raft_heartbeat_interval_ms;

  FLAGS_TEST_apply_tablet_split_inject_delay_ms = 200 * kTimeMultiplier;
  // We want heartbeater to be called during tablet split apply to reproduce deadlock bug.
  FLAGS_heartbeat_interval_ms = FLAGS_TEST_apply_tablet_split_inject_delay_ms / 3;
  // We reduce FLAGS_leader_lease_duration_ms for ReplicaState::GetLeaderState to avoid always
  // reusing results from cache on heartbeat, otherwise it won't lock ReplicaState mutex.
  FLAGS_leader_lease_duration_ms = FLAGS_TEST_apply_tablet_split_inject_delay_ms / 2;
  // Reduce raft_heartbeat_interval_ms for leader lease to be reliably replicated.
  FLAGS_raft_heartbeat_interval_ms = FLAGS_leader_lease_duration_ms / 2;
  // Keep leader failure timeout the same to avoid flaky losses of leader with short heartbeats.
  FLAGS_leader_failure_max_missed_heartbeat_periods =
      leader_failure_timeout / FLAGS_raft_heartbeat_interval_ms;

  constexpr auto kNumRows = 50;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));
}

TEST_F(TabletSplitITest, SplitTabletDuringReadWriteLoad) {
  constexpr auto kNumTablets = 3;

  FLAGS_db_write_buffer_size = 100_KB;

  TestWorkload workload(cluster_.get());
  workload.set_table_name(client::kTableName);
  workload.set_write_timeout_millis(MonoDelta(kRpcTimeout).ToMilliseconds());
  workload.set_num_tablets(kNumTablets);
  workload.set_num_read_threads(4);
  workload.set_num_write_threads(2);
  workload.set_write_batch_size(50);
  workload.set_payload_bytes(16);
  workload.set_sequential_write(true);
  workload.set_retry_on_restart_required_error(true);
  workload.set_read_only_written_keys(true);
  workload.Setup();

  const auto test_table_id = ASSERT_RESULT(GetTestTableId());

  std::vector<tablet::TabletPeerPtr> peers;
  ASSERT_OK(LoggedWaitFor([&] {
    peers = ListTableActiveTabletLeadersPeers(cluster_.get(), test_table_id);
    return peers.size() == kNumTablets;
  }, 30s * kTimeMultiplier, "Waiting for leaders ..."));

  LOG(INFO) << "Starting workload ...";
  workload.Start();

  for (const auto& peer : peers) {
    ASSERT_OK(LoggedWaitFor(
        [&peer] {
          const auto data_size =
              peer->tablet()->TEST_db()->GetCurrentVersionSstFilesUncompressedSize();
          YB_LOG_EVERY_N_SECS(INFO, 5) << "Data written: " << data_size;
          return data_size > (FLAGS_rocksdb_level0_file_num_compaction_trigger + 1) *
                                 FLAGS_db_write_buffer_size;
        },
        60s * kTimeMultiplier, Format("Writing data to split (tablet $0) ...", peer->tablet_id())));
  }

  DumpWorkloadStats(workload);

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());

  for (const auto& peer : peers) {
    const auto& source_tablet = *ASSERT_NOTNULL(peer->tablet());
    ASSERT_OK(SplitTablet(catalog_mgr, source_tablet));
  }

  ASSERT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ kNumTablets * 2));

  DumpTableLocations(catalog_mgr, client::kTableName);

  // Generate some more read/write traffic after tablets are split and after that we check data
  // for consistency and that failures rates are acceptable.
  std::this_thread::sleep_for(5s);

  LOG(INFO) << "Stopping workload ...";
  workload.StopAndJoin();

  DumpWorkloadStats(workload);

  ASSERT_NO_FATALS(CheckTableKeysInRange(workload.rows_inserted()));

  const auto insert_failure_rate = 1.0 * workload.rows_insert_failed() / workload.rows_inserted();
  const auto read_failure_rate = 1.0 * workload.rows_read_error() / workload.rows_read_ok();
  const auto read_try_again_rate = 1.0 * workload.rows_read_try_again() / workload.rows_read_ok();

  ASSERT_LT(insert_failure_rate, 0.01);
  ASSERT_LT(read_failure_rate, 0.01);
  // TODO(tsplit): lower this threshold as internal (without reaching client app) read retries
  //  implemented for split tablets.
  ASSERT_LT(read_try_again_rate, 0.1);
  ASSERT_EQ(workload.rows_read_empty(), 0);

  // TODO(tsplit): Check with different isolation levels.

  ASSERT_OK(cluster_->RestartSync());
}

void TabletSplitITest::SplitClientRequestsIds(int split_depth) {
  // Set data block size low enough, so we have enough data blocks for middle key
  // detection to work correctly.
  FLAGS_db_block_size_bytes = 1_KB;
  const auto kNumRows = 50 * (1 << split_depth);

  SetNumTablets(1);
  CreateTable();

  ASSERT_OK(WriteRows(kNumRows, 1));

  ASSERT_OK(CheckRowsCount(kNumRows));

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());

  for (int i = 0; i < split_depth; ++i) {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    ASSERT_EQ(peers.size(), 1 << i);
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
      tablet->ForceRocksDBCompactInTest();
      ASSERT_OK(SplitTablet(catalog_mgr, *tablet));
    }

    ASSERT_OK(WaitForTabletSplitCompletion(
        /* expected_non_split_tablets =*/ 1 << (i + 1)));
  }

  Status s;
  ASSERT_OK(WaitFor([&] {
    s = ResultToStatus(WriteRows(1, 1));
    return !s.IsTryAgain();
  }, 60s * kTimeMultiplier, "Waiting for successful write"));
  ASSERT_OK(s);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/5415.
// Client knows about split parent for final tablets.
TEST_F(TabletSplitITest, SplitClientRequestsIdsDepth1) {
  SplitClientRequestsIds(1);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/5415.
// Client doesn't know about split parent for final tablets.
TEST_F(TabletSplitITest, SplitClientRequestsIdsDepth2) {
  SplitClientRequestsIds(2);
}

TEST_F(TabletSplitITest, SplitSingleTabletWithLimit) {
  FLAGS_db_block_size_bytes = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  const auto kSplitDepth = 3;
  const auto kNumRows = 50 * (1 << kSplitDepth);
  FLAGS_tablet_split_limit_per_table = (1 << kSplitDepth) - 1;

  CreateSingleTablet();
  ASSERT_OK(WriteRows(kNumRows, 1));
  ASSERT_OK(CheckRowsCount(kNumRows));

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());

  master::TableIdentifierPB table_id_pb;
  table_id_pb.set_table_id(table_->id());
  bool reached_split_limit = false;

  for (int i = 0; i < kSplitDepth; ++i) {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    bool expect_split = false;
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
      tablet->ForceRocksDBCompactInTest();
      auto table_info = ASSERT_RESULT(catalog_mgr->FindTable(table_id_pb));

      expect_split = table_info->NumPartitions() < FLAGS_tablet_split_limit_per_table;

      if (expect_split) {
        ASSERT_OK(DoSplitTablet(catalog_mgr, *tablet));
      } else {
        const auto split_status = DoSplitTablet(catalog_mgr, *tablet);
        ASSERT_EQ(master::MasterError(split_status),
                  master::MasterErrorPB::REACHED_SPLIT_LIMIT);
        reached_split_limit = true;
      }
    }
    if (expect_split) {
      ASSERT_OK(WaitForTabletSplitCompletion(
          /* expected_non_split_tablets =*/1 << (i + 1)));
    }
  }

  ASSERT_TRUE(reached_split_limit);

  Status s;
  ASSERT_OK(WaitFor([&] {
    s = ResultToStatus(WriteRows(1, 1));
    return !s.IsTryAgain();
  }, 60s * kTimeMultiplier, "Waiting for successful write"));

  auto table_info = ASSERT_RESULT(catalog_mgr->FindTable(table_id_pb));
  ASSERT_EQ(table_info->NumPartitions(), FLAGS_tablet_split_limit_per_table);
}

TEST_F(TabletSplitITest, SplitDuringReplicaOffline) {
  constexpr auto kNumRows = kDefaultNumRows;

  SetNumTablets(1);
  CreateTable();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  auto rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table_));
  ASSERT_EQ(rows_count, kNumRows);

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());

  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto source_tablet_id = source_tablet_info->id();

  cluster_->mini_tablet_server(0)->Shutdown();

  LOG(INFO) << "Stopped TS-1";

  ASSERT_OK(catalog_mgr->TEST_SplitTablet(source_tablet_info, split_hash_code));

  ASSERT_OK(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets =*/ 2, /* expected_split_tablets =*/ 1,
      /* num_replicas_online =*/ 2));

  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows, 2));

  ASSERT_OK(CheckSourceTabletAfterSplit(source_tablet_id));

  DumpTableLocations(catalog_mgr, client::kTableName);

  rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table_));
  ASSERT_EQ(rows_count, kNumRows);

  ASSERT_OK(WriteRows(kNumRows, kNumRows + 1));

  LOG(INFO) << "Starting TS-1";

  ASSERT_OK(cluster_->mini_tablet_server(0)->Start());

  // This time we expect all replicas to be online.
  ASSERT_OK(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets =*/ 2, /* expected_split_tablets =*/ 0));

  Status s;
  ASSERT_OK_PREPEND(LoggedWaitFor([&] {
      s = CheckPostSplitTabletReplicasData(kNumRows * 2);
      return s.IsOk();
    }, 30s * kTimeMultiplier, "Waiting for TS-1 to catch up ..."), AsString(s));
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/6890.
// Writes data to the tablet, splits it and then tries to do full scan with `select count(*)`
// using two different instances of YBTable one after another.
TEST_F(TabletSplitITest, DifferentYBTableInstances) {
  constexpr auto kNumRows = kDefaultNumRows;

  CreateSingleTablet();

  client::TableHandle table1, table2;
  for (auto* table : {&table1, &table2}) {
    ASSERT_OK(table->Open(client::kTableName, client_.get()));
  }

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  const auto source_tablet_id = ASSERT_RESULT(SplitTabletAndValidate(split_hash_code, kNumRows));

  ASSERT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2));

  auto rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table1));
  ASSERT_EQ(rows_count, kNumRows);

  rows_count = ASSERT_RESULT(SelectRowsCount(NewSession(), table2));
  ASSERT_EQ(rows_count, kNumRows);
}

class TabletSplitYedisTableTest : public integration_tests::RedisTableTestBase {
 protected:
  int num_tablets() override { return 1; }
};

TEST_F(TabletSplitYedisTableTest, BlockSplittingYedisTablet) {
  constexpr int kNumRows = 10000;

  for (int i = 0; i < kNumRows; ++i) {
    PutKeyValue(Format("$0", i), Format("$0", i));
  }

  for (const auto& peer : ListTableActiveTabletPeers(mini_cluster(), table_->id())) {
    ASSERT_OK(peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
  }

  for (const auto& peer : ListTableActiveTabletLeadersPeers(mini_cluster(), table_->id())) {
    auto catalog_manager = &CHECK_NOTNULL(
        ASSERT_RESULT(this->mini_cluster()->GetLeaderMiniMaster()))->catalog_manager();

    auto s = DoSplitTablet(catalog_manager, *peer->shared_tablet());
    EXPECT_NOT_OK(s);
    EXPECT_TRUE(s.IsNotSupported()) << s.ToString();
  }
}

class AutomaticTabletSplitITest : public TabletSplitITest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_process_split_tablet_candidates_interval_msec) = 1;
    TabletSplitITest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_select_all_tablets_for_split) = false;
  }

 protected:
  CHECKED_STATUS FlushAllTabletReplicas(const TabletId& tablet_id) {
    for (const auto& active_peer : ListTableActiveTabletPeers(cluster_.get(), table_->id())) {
      if (active_peer->tablet_id() == tablet_id) {
        RETURN_NOT_OK(active_peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
      }
    }
    return Status::OK();
  }

  CHECKED_STATUS AutomaticallySplitSingleTablet(
      const string& tablet_id, int num_rows_per_batch,
      int64_t threshold, int* key) {
    uint64_t current_size = 0;
    int cur_num_tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id()).size();
    while (current_size <= threshold) {
      RETURN_NOT_OK(WriteRows(num_rows_per_batch, *key));
      *key += num_rows_per_batch;
      auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
      LOG(INFO) << "Active peers: " << peers.size();
      if (peers.size() == cur_num_tablets + 1) {
        break;
      }
      if (peers.size() != cur_num_tablets) {
        return STATUS_FORMAT(IllegalState,
          "Expected number of peers: $0, actual: $1", cur_num_tablets, peers.size());
      }
      auto leader_peer = peers.at(0);
      for (auto peer : peers) {
        if (peer->tablet_id() == tablet_id) {
          leader_peer = peer;
          break;
        }
      }
      // Flush all replicas of this shard to ensure that even if the leader changed we will be in a
      // state where yb-master should initiate a split.
      RETURN_NOT_OK(FlushAllTabletReplicas(leader_peer->tablet_id()));
      current_size = leader_peer->shared_tablet()->GetCurrentVersionSstFilesSize();
    }
    RETURN_NOT_OK(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets =*/ cur_num_tablets + 1));
    return Status::OK();
  }

  CHECKED_STATUS CompactTablet(const string& tablet_id) {
    auto peers = ListTabletPeers(cluster_.get(), [&tablet_id](auto peer) {
      return peer->tablet_id() == tablet_id;
    });
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      RETURN_NOT_OK(tablet->Flush(tablet::FlushMode::kSync));
      tablet->ForceRocksDBCompactInTest();
    }
    return Status::OK();
  }
};

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplitting) {
  constexpr int kNumRowsPerBatch = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;

  int key = 1;
  CreateSingleTablet();
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(AutomaticallySplitSingleTablet(peers.at(0)->tablet_id(), kNumRowsPerBatch,
    FLAGS_tablet_split_low_phase_size_threshold_bytes, &key));

  // Since compaction is off, the tablets should not be further split since they won't have had
  // their post split compaction. Assert this is true by tripling the number of keys written and
  // seeing the number of tablets not grow.
  auto triple_keys = key * 2;
  while (key < triple_keys) {
    ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
    key += kNumRowsPerBatch;
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    EXPECT_EQ(peers.size(), 2);
  }
}


TEST_F(AutomaticTabletSplitITest, TabletSplitHasClusterReplicationInfo) {
  constexpr int kNumRowsPerBatch = 1000;
  // This test relies on the fact that the high_phase_size_threshold > force_split_threshold
  // to ensure that without the placement code the test will fail
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 50_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 50_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_post_split_compaction) = true;
  // Disable automatic compactions
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_base_background_compactions) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 0;
  // Disable manual compations from flushes
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_nodes_per_cloud) = 1;

  std::vector<string> clouds = {"cloud1_split", "cloud2_split", "cloud3_split", "cloud4_split"};
  std::vector<string> regions = {"rack1_split", "rack2_split", "rack3_split", "rack4_split"};
  std::vector<string> zones = {"zone1_split", "zone2_split", "zone3_split", "zone4_split"};

  // Create 4 tservers with the placement info from above
  for (int i = 0; i < clouds.size(); i++) {
    tserver::TabletServerOptions extra_opts =
      ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
    extra_opts.SetPlacement(clouds.at(i), regions.at(i), zones.at(i));
    auto new_ts = cluster_->num_tablet_servers();
    ASSERT_OK(cluster_->AddTabletServer(extra_opts));
    ASSERT_OK(cluster_->WaitForTabletServerCount(new_ts + 1));
  }

  // Set cluster level placement information using only the first 3 clouds/regions/zones
  master::ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(clouds.size() - 1);
  for (int i = 0; i < clouds.size() - 1; i++) {
    auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
    auto* cloud_info = placement_block->mutable_cloud_info();
    cloud_info->set_placement_cloud(clouds.at(i));
    cloud_info->set_placement_region(regions.at(i));
    cloud_info->set_placement_zone(zones.at(i));
    placement_block->set_min_num_replicas(1);
  }
  ASSERT_OK(client_->SetReplicationInfo(replication_info));

  // Create and split single tablet into 2 partitions
  // The split should happen at the high threshold
  int key = 1;
  CreateSingleTablet();
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(AutomaticallySplitSingleTablet(peers.at(0)->tablet_id(), kNumRowsPerBatch,
    FLAGS_tablet_split_high_phase_size_threshold_bytes, &key));

  peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  ASSERT_EQ(peers.size(), 2);
  auto tablet_id_to_split = peers.at(0)->tablet_id();

  // Split one of the 2 tablets to get 3 partitions
  // The split should happen at the high threshhold
  ASSERT_OK(CompactTablet(tablet_id_to_split));
  ASSERT_OK(AutomaticallySplitSingleTablet(tablet_id_to_split, kNumRowsPerBatch,
    FLAGS_tablet_split_high_phase_size_threshold_bytes, &key));

  // Split one of the 3 remaining tablets to get 4 partitions
  // The split should happen at the force split threshold
  // We set the high phase > force split to ensure that we split at the force split level
  // given the custom placement information
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 300_KB;
  tablet_id_to_split = peers.at(1)->tablet_id();
  ASSERT_OK(CompactTablet(tablet_id_to_split));
  ASSERT_OK(AutomaticallySplitSingleTablet(tablet_id_to_split, kNumRowsPerBatch,
    FLAGS_tablet_force_split_threshold_bytes, &key));
}

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplittingWaitsForAllPeersCompacted) {
  constexpr auto kNumRowsPerBatch = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 100_KB;
  // Disable post split compaction
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_post_split_compaction) = true;
  // Disable automatic compactions
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_base_background_compactions) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 0;
  // Disable manual compations from flushes
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;

  int key = 1;
  CreateSingleTablet();
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(AutomaticallySplitSingleTablet(peers.at(0)->tablet_id(), kNumRowsPerBatch,
    FLAGS_tablet_split_low_phase_size_threshold_bytes, &key));

  std::unordered_set<string> tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_->id());
  ASSERT_EQ(tablet_ids.size(), 2);
  auto expected_num_tablets = 2;

  // Compact peers one by one and ensure a tablet is not split until all peers are compacted
  for (const auto& tablet_id : tablet_ids) {
    auto peers = ListTabletPeers(cluster_.get(), [&tablet_id](auto peer) {
      return peer->tablet_id() == tablet_id;
    });
    ASSERT_EQ(peers.size(), FLAGS_replication_factor);
    for (const auto& peer : peers) {
      // We shouldn't have split this tablet yet since not all peers are compacted yet
      EXPECT_EQ(
        ListTableActiveTabletPeers(cluster_.get(), table_->id()).size(),
        expected_num_tablets * FLAGS_replication_factor);

      // Force a manual rocksdb compaction on the peer tablet and wait for it to complete
      const auto tablet = peer->shared_tablet();
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
      tablet->ForceRocksDBCompactInTest();
      ASSERT_OK(LoggedWaitFor(
        [peer]() -> Result<bool> {
          return peer->tablet_metadata()->has_been_fully_compacted();
        },
        15s * kTimeMultiplier,
        "Wait for post tablet split compaction to be completed for peer: " + peer->tablet_id()));

      // Write enough data to get the tablet into a state where it's large enough for a split
      uint64_t current_size = 0;
      while (current_size <= FLAGS_tablet_split_low_phase_size_threshold_bytes) {
        ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
        key += kNumRowsPerBatch;
        ASSERT_OK(FlushAllTabletReplicas(tablet_id));
        auto current_size_res = GetMinSstFileSizeAmongAllReplicas(tablet_id);
        if (!current_size_res.ok()) {
          break;
        }
        current_size = current_size_res.get();
      }

      // Wait for a potential split to get triggered
      std::this_thread::sleep_for(
        2 * (FLAGS_catalog_manager_bg_task_wait_ms * 2ms + FLAGS_raft_heartbeat_interval_ms * 2ms));
    }

    // Now that all peers have been compacted, we expect this tablet to get split.
    ASSERT_OK(
      WaitForTabletSplitCompletion(++expected_num_tablets));
  }
}


TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplittingMovesToNextPhase) {
  constexpr int kNumRowsPerBatch = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 50_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;

  const auto this_phase_tablet_lower_limit = FLAGS_tablet_split_low_phase_shard_count_per_node
      * cluster_->num_tablet_servers();
  const auto this_phase_tablet_upper_limit = FLAGS_tablet_split_high_phase_shard_count_per_node
      * cluster_->num_tablet_servers();

  auto num_tablets = this_phase_tablet_lower_limit;
  SetNumTablets(num_tablets);
  CreateTable();

  auto key = 1;
  while (num_tablets < this_phase_tablet_upper_limit) {
    ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
    key += kNumRowsPerBatch;
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    num_tablets = peers.size();
    for (const auto& peer : peers) {
      // Flush other replicas of this shard to ensure that even if the leader changed we will be in
      // a state where yb-master should initiate a split.
      ASSERT_OK(FlushAllTabletReplicas(peer->tablet_id()));
      auto size = peer->shared_tablet()->GetCurrentVersionSstFilesSize();
      if (size > FLAGS_tablet_split_high_phase_size_threshold_bytes) {
        num_tablets++;
        LOG(INFO) << "Waiting for tablet number " << num_tablets
            << " with id " << peer->tablet_id()
            << " and size " << size
            << " bytes and leader status " << peer->consensus()->GetLeaderStatus()
            << " to split.";
        ASSERT_OK(WaitForTabletSplitCompletion(num_tablets));
      }
    }
  }
  EXPECT_EQ(num_tablets, this_phase_tablet_upper_limit);
}

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplittingMultiPhase) {
  constexpr int kNumRowsPerBatch = RegularBuildVsSanitizers(5000, 1000);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 20_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 2;
  // Disable automatic compactions, but continue to allow manual compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_base_background_compactions) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 0;

  SetNumTablets(1);
  CreateTable();

  int key = 1;
  auto num_peers = 1;
  const auto num_tservers = cluster_->num_tablet_servers();

  auto test_phase = [&key, &num_peers, this](int tablet_count_limit, int split_threshold_bytes) {
    while (num_peers < tablet_count_limit) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = true;
      ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
      key += kNumRowsPerBatch;
      auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
      if (peers.size() > num_peers) {
        // If a new tablet was formed, it means one of the tablets from the last iteration was
        // split. In that case, verify that some peer has greater than the current split threshold
        // bytes on disk. Note that it would not have compacted away the post split orphaned bytes
        // since automatic compactions are off, so we expect this verification to pass with
        // certainty.
        num_peers = peers.size();

        auto found_large_tablet = false;
        for (const auto& peer : peers) {
          auto peer_size = peer->shared_tablet()->GetCurrentVersionSstFilesSize();
          // Since we've disabled compactions, each post-split subtablet should be larger than the
          // split size threshold.
          if (peer_size > split_threshold_bytes) {
            found_large_tablet = true;
          }
        }
        ASSERT_TRUE(found_large_tablet)
            << "We have split a tablet but upon inspection none of them were large enough to "
            << "split.";
      }

      for (const auto& peer : peers) {
        ASSERT_OK(peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
        // Compact each tablet to remove the orphaned post-split data so that it can be split again.
        ASSERT_OK(peer->shared_tablet()->ForceFullRocksDBCompact());
      }
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = false;
      // Wait for two rounds of split tablet processing intervals to ensure any split candidates
      // have had time to be processed and drained from the queue of TabletSplitManager.
      auto sleep_time =
          FLAGS_process_split_tablet_candidates_interval_msec * 2ms * kTimeMultiplier;
      std::this_thread::sleep_for(sleep_time);
    }
  };
  test_phase(
    FLAGS_tablet_split_low_phase_shard_count_per_node * num_tservers,
    FLAGS_tablet_split_low_phase_size_threshold_bytes);
  test_phase(
    FLAGS_tablet_split_high_phase_shard_count_per_node * num_tservers,
    FLAGS_tablet_split_high_phase_size_threshold_bytes);
}

TEST_F(AutomaticTabletSplitITest, LimitNumberOfOutstandingTabletSplits) {
  constexpr int kNumRowsPerBatch = 1000;
  constexpr int kTabletSplitLimit = 3;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 0;

  // Limit the number of tablet splits.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = kTabletSplitLimit;
  // Start with candidate processing off.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = true;

  // Randomly fail a percentage of tablet splits to ensure that failed splits get removed.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_tablet_split_probability) = IsTsan() ? 0.1 : 0.2;

  // Create a table with kTabletSplitLimit tablets.
  int num_tablets = kTabletSplitLimit;
  SetNumTablets(num_tablets);
  CreateTable();
  // Add some data.
  ASSERT_OK(WriteRows(kNumRowsPerBatch, 1));

  // Main test loop:
  // Each loop we will split kTabletSplitLimit tablets with post split compactions disabled.
  // We will then wait until we have that many tablets split, at which point we will reenable post
  // split compactions.
  // We will then wait until the post split compactions are done, then repeat.
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  for (int split_round = 0; split_round < 3; ++split_round) {
    for (const auto& peer : peers) {
      // Flush other replicas of this shard to ensure that even if the leader changed we will be in
      // a state where yb-master should initiate a split.
      ASSERT_OK(FlushAllTabletReplicas(peer->tablet_id()));
    }

    // Keep tablets without compaction after split.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = true;
    // Enable splitting.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = false;

    ASSERT_OK(WaitForTabletSplitCompletion(num_tablets + kTabletSplitLimit));
    // Ensure that we don't split any more tablets.
    ASSERT_NOK(WaitForTabletSplitCompletion(
        num_tablets + kTabletSplitLimit + 1,  // expected_non_split_tablets
        0,                                    // expected_split_tablets (default)
        0,                                    // num_replicas_online (default)
        client::kTableName,                   // table (default)
        false));                              // core_dump_on_failure

    // Pause any more tablet splits.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = true;
    // Reenable post split compaction, wait for this to complete so next tablets can be split.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_post_split_compaction) = false;

    ASSERT_OK(WaitForTestTablePostSplitTabletsFullyCompacted(15s * kTimeMultiplier));

    peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    num_tablets = peers.size();

    // There should be kTabletSplitLimit intial tablets + kTabletSplitLimit new tablets per loop.
    EXPECT_EQ(num_tablets, (split_round + 2) * kTabletSplitLimit);
  }

  // TODO (jhe) For now we need to manually delete the cluster otherwise the cluster verifier can
  // get stuck waiting for tablets that got registered but whose tablet split got cancelled by
  // FLAGS_TEST_fail_tablet_split_probability.
  // We should either have a way to wait for these tablets to get split, or have a way to delete
  // these tablets in case a tablet split fails.
  cluster_->Shutdown();
}

class TabletSplitSingleServerITest : public TabletSplitITest {
 protected:
  int64_t GetRF() override { return 1; }

  Result<tablet::TabletPeerPtr> GetSingleTabletLeaderPeer() {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    SCHECK_EQ(peers.size(), 1, IllegalState, "Expected only a single tablet leader.");
    return peers.at(0);
  }
};

TEST_F(TabletSplitSingleServerITest, TabletServerGetSplitKey) {
  constexpr auto kNumRows = kDefaultNumRows;
  // Setup table with rows.
  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndGetMiddleHashCode(kNumRows));
  const auto source_tablet_id =
      ASSERT_RESULT(GetSingleTestTabletInfo(ASSERT_RESULT(catalog_manager())))->id();

  // Flush tablet and directly compute expected middle key.
  auto tablet_peer = ASSERT_RESULT(GetSingleTabletLeaderPeer());
  ASSERT_OK(tablet_peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
  auto middle_key = ASSERT_RESULT(tablet_peer->shared_tablet()->GetEncodedMiddleSplitKey());
  auto expected_middle_key_hash = CHECK_RESULT(docdb::DocKey::DecodeHash(middle_key));

  // Send RPC.
  auto resp = ASSERT_RESULT(GetSplitKey(source_tablet_id));

  // Validate response.
  CHECK(!resp.has_error()) << resp.error().DebugString();
  auto decoded_split_key_hash = CHECK_RESULT(docdb::DocKey::DecodeHash(resp.split_encoded_key()));
  CHECK_EQ(decoded_split_key_hash, expected_middle_key_hash);
  auto decoded_partition_key_hash = PartitionSchema::DecodeMultiColumnHashValue(
      resp.split_partition_key());
  CHECK_EQ(decoded_partition_key_hash, expected_middle_key_hash);
}

TEST_F(TabletSplitSingleServerITest, TabletServerOrphanedPostSplitData) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  constexpr auto kNumRows = 2000;

  auto source_tablet_id = CreateSingleTabletAndSplit(kNumRows);

  // Try to call GetSplitKey RPC on each child tablet that resulted from the split above
  const auto& peers = ListTableActiveTabletPeers(cluster_.get(), table_->id());
  ASSERT_EQ(peers.size(), 2);

  for (const auto& peer : peers) {
      // Send RPC to child tablet.
      auto resp = ASSERT_RESULT(GetSplitKey(peer->tablet_id()));

      // Validate response
      EXPECT_TRUE(resp.has_error());
      EXPECT_TRUE(resp.error().has_status());
      EXPECT_TRUE(resp.error().status().has_message());
      EXPECT_EQ(resp.error().status().code(),
                yb::AppStatusPB::ErrorCode::AppStatusPB_ErrorCode_ILLEGAL_STATE);
      EXPECT_EQ(resp.error().status().message(), "Tablet has orphaned post-split data");
  }
}

TEST_F(TabletSplitSingleServerITest, TabletServerSplitAlreadySplitTablet) {
  constexpr auto kNumRows = 2000;

  CreateSingleTablet();
  auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  auto tablet_peer = ASSERT_RESULT(GetSingleTabletLeaderPeer());
  const auto tserver_uuid = tablet_peer->permanent_uuid();

  SetAtomicFlag(true, &FLAGS_TEST_skip_deleting_split_tablets);
  const auto source_tablet_id = ASSERT_RESULT(SplitSingleTablet(split_hash_code));
  ASSERT_OK(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets =*/ 2, /* expected_split_tablets = */ 1));

  auto send_split_request = [this, &tserver_uuid, &source_tablet_id]()
      -> Result<tserver::SplitTabletResponsePB> {
    auto tserver = cluster_->mini_tablet_server(0);
    auto ts_admin_service_proxy = std::make_unique<tserver::TabletServerAdminServiceProxy>(
      proxy_cache_.get(), HostPort::FromBoundEndpoint(tserver->bound_rpc_addr()));
    tablet::SplitTabletRequestPB req;
    req.set_dest_uuid(tserver_uuid);
    req.set_tablet_id(source_tablet_id);
    req.set_new_tablet1_id(Format("$0$1", source_tablet_id, "1"));
    req.set_new_tablet2_id(Format("$0$1", source_tablet_id, "2"));
    req.set_split_partition_key("abc");
    req.set_split_encoded_key("def");
    rpc::RpcController controller;
    controller.set_timeout(kRpcTimeout);
    tserver::SplitTabletResponsePB resp;
    RETURN_NOT_OK(ts_admin_service_proxy->SplitTablet(req, &resp, &controller));
    return resp;
  };

  // If the parent tablet is still around, this should trigger an AlreadyPresent error
  auto resp = ASSERT_RESULT(send_split_request());
  EXPECT_TRUE(resp.has_error());
  EXPECT_TRUE(StatusFromPB(resp.error().status()).IsAlreadyPresent()) << resp.error().DebugString();

  SetAtomicFlag(false, &FLAGS_TEST_skip_deleting_split_tablets);
  ASSERT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2));

  // If the parent tablet has been cleaned up, this should trigger a Not Found error.
  resp = ASSERT_RESULT(send_split_request());
  EXPECT_TRUE(resp.has_error());
  EXPECT_TRUE(
      StatusFromPB(resp.error().status()).IsNotFound() ||
      resp.error().code() == tserver::TabletServerErrorPB::TABLET_NOT_FOUND)
      << resp.error().DebugString();
}

TEST_F(TabletSplitExternalMiniClusterITest, Simple) {
  CreateSingleTablet();
  CHECK_OK(WriteRowsAndFlush());
  auto tablet_id = CHECK_RESULT(GetOnlyTestTabletId());
  CHECK_OK(SplitTablet(tablet_id));
  ASSERT_OK(WaitForTablets(3));
}

TEST_F(TabletSplitExternalMiniClusterITest, CrashMasterDuringSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) =
    FLAGS_heartbeat_interval_ms + 1000;

  ASSERT_OK(SplitTabletCrashMaster(false, nullptr));
}

TEST_F(TabletSplitExternalMiniClusterITest, CrashMasterCheckConsistentPartitionKeys) {
  // Tests that when master crashes during a split and a new split key is used
  // we will revert to an older boundary used by the inital split
  // Used to validate the fix for: https://github.com/yugabyte/yugabyte-db/issues/8148
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) =
    FLAGS_heartbeat_interval_ms + 1000;

  string split_partition_key;
  ASSERT_OK(SplitTabletCrashMaster(true, &split_partition_key));

  auto tablets = CHECK_RESULT(ListTablets());
  ASSERT_EQ(tablets.size(), 2);
  auto part = tablets.at(0).tablet_status().partition();
  auto part2 = tablets.at(1).tablet_status().partition();

  // check that both partitions have the same boundary
  if (part.partition_key_end() == part2.partition_key_start() && part.partition_key_end() != "") {
    ASSERT_EQ(part.partition_key_end(), split_partition_key);
  } else {
    ASSERT_EQ(part.partition_key_start(), split_partition_key);
    ASSERT_EQ(part2.partition_key_end(), split_partition_key);
  }
}

TEST_F(TabletSplitExternalMiniClusterITest, FaultedSplitNodeRejectsRemoteBootstrap) {
  constexpr int kTabletSplitInjectDelayMs = 20000 * kTimeMultiplier;
  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndFlush());
  const auto tablet_id = CHECK_RESULT(GetOnlyTestTabletId());

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto healthy_follower_idx = (leader_idx + 1) % 3;
  const auto faulted_follower_idx = (leader_idx + 2) % 3;

  auto faulted_follower = cluster_->tablet_server(faulted_follower_idx);
  ASSERT_OK(cluster_->SetFlag(
      faulted_follower, "TEST_crash_before_apply_tablet_split_op", "true"));

  ASSERT_OK(SplitTablet(tablet_id));
  ASSERT_OK(cluster_->WaitForTSToCrash(faulted_follower));

  ASSERT_OK(faulted_follower->Restart(ExternalMiniClusterOptions::kDefaultStartCqlProxy, {
    std::make_pair(
        "TEST_apply_tablet_split_inject_delay_ms", Format("$0", kTabletSplitInjectDelayMs))
  }));

  consensus::StartRemoteBootstrapRequestPB req;
  req.set_split_parent_tablet_id(tablet_id);
  req.set_dest_uuid(faulted_follower->uuid());
  // We put some bogus values for these next two required fields.
  req.set_tablet_id("::std::string &&value");
  req.set_bootstrap_peer_uuid("abcdefg");
  consensus::StartRemoteBootstrapResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  auto s = cluster_->GetConsensusProxy(faulted_follower).StartRemoteBootstrap(req, &resp, &rpc);
  EXPECT_OK(s);
  EXPECT_TRUE(resp.has_error());
  EXPECT_EQ(resp.error().code(), tserver::TabletServerErrorPB::TABLET_SPLIT_PARENT_STILL_LIVE);

  SleepFor(1ms * kTabletSplitInjectDelayMs);
  EXPECT_OK(WaitForTablets(2));
  EXPECT_OK(WaitForTablets(2, faulted_follower_idx));

  // By shutting down the healthy follower and writing rows to the table, we ensure the faulted
  // follower is eventually able to rejoin the raft group.
  auto healthy_follower = cluster_->tablet_server(healthy_follower_idx);
  healthy_follower->Shutdown();
  EXPECT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows(10).ok();
  }, 20s * kTimeMultiplier, "Write rows after requiring faulted follower."));

  ASSERT_OK(healthy_follower->Restart());
}

TEST_F(TabletSplitExternalMiniClusterITest, CrashesAfterChildLogCopy) {
  ASSERT_OK(cluster_->SetFlagOnMasters("unresponsive_ts_rpc_retry_limit", "0"));

  CreateSingleTablet();
  CHECK_OK(WriteRowsAndFlush());
  const auto tablet_id = CHECK_RESULT(GetOnlyTestTabletId());

  // We will fault one of the non-leader servers after it performs a WAL Log copy from parent to
  // the first child, but before it can mark the child as TABLET_DATA_READY.
  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto faulted_follower_idx = (leader_idx + 2) % 3;
  const auto non_faulted_follower_idx = (leader_idx + 1) % 3;

  auto faulted_follower = cluster_->tablet_server(faulted_follower_idx);
  CHECK_OK(cluster_->SetFlag(
      faulted_follower, "TEST_fault_crash_in_split_after_log_copied", "1.0"));

  CHECK_OK(SplitTablet(tablet_id));
  CHECK_OK(cluster_->WaitForTSToCrash(faulted_follower));

  CHECK_OK(faulted_follower->Restart());

  ASSERT_OK(cluster_->WaitForTabletsRunning(faulted_follower, 20s * kTimeMultiplier));
  ASSERT_OK(WaitForTablets(3));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after faulted follower resurrection."));

  auto non_faulted_follower = cluster_->tablet_server(non_faulted_follower_idx);
  non_faulted_follower->Shutdown();
  CHECK_OK(cluster_->WaitForTSToCrash(non_faulted_follower));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after requiring bootstraped node consensus."));

  CHECK_OK(non_faulted_follower->Restart());
}

class TabletSplitRemoteBootstrapEnabledTest : public TabletSplitExternalMiniClusterITest {
 protected:
  void SetFlags() override {
    TabletSplitExternalMiniClusterITest::SetFlags();
    mini_cluster_opt_.extra_tserver_flags.push_back(
        "--TEST_disable_post_split_tablet_rbs_check=true");
  }
};

TEST_F(TabletSplitRemoteBootstrapEnabledTest, TestSplitAfterFailedRbsCreatesDirectories) {
  const auto kApplyTabletSplitDelay = 15s * kTimeMultiplier;

  const auto get_tablet_meta_dirs =
      [this](ExternalTabletServer* node) -> Result<std::vector<string>> {
    auto tablet_meta_dirs = VERIFY_RESULT(env_->GetChildren(
        JoinPathSegments(node->GetRootDir(), "yb-data", "tserver", "tablet-meta")));
    std::sort(tablet_meta_dirs.begin(), tablet_meta_dirs.end());
    return tablet_meta_dirs;
  };

  const auto wait_for_same_tablet_metas =
      [&get_tablet_meta_dirs]
      (ExternalTabletServer* node_1, ExternalTabletServer* node_2) -> Status {
    return WaitFor([node_1, node_2, &get_tablet_meta_dirs]() -> Result<bool> {
      auto node_1_metas = VERIFY_RESULT(get_tablet_meta_dirs(node_1));
      auto node_2_metas = VERIFY_RESULT(get_tablet_meta_dirs(node_2));
      if (node_1_metas.size() != node_2_metas.size()) {
        return false;
      }
      for (int i = 0; i < node_2_metas.size(); ++i) {
        if (node_1_metas.at(i) != node_2_metas.at(i)) {
          return false;
        }
      }
      return true;
    }, 5s * kTimeMultiplier, "Waiting for nodes to have same set of tablet metas.");
  };

  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndFlush());
  const auto tablet_id = CHECK_RESULT(GetOnlyTestTabletId());

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto leader = cluster_->tablet_server(leader_idx);
  const auto healthy_follower_idx = (leader_idx + 1) % 3;
  const auto healthy_follower = cluster_->tablet_server(healthy_follower_idx);
  const auto faulted_follower_idx = (leader_idx + 2) % 3;
  const auto faulted_follower = cluster_->tablet_server(faulted_follower_idx);

  // Make one node fail on tablet split, and ensure the leader does not remote bootstrap to it at
  // first.
  ASSERT_OK(cluster_->SetFlag(
      faulted_follower, "TEST_crash_before_apply_tablet_split_op", "true"));
  ASSERT_OK(cluster_->SetFlag(leader, "TEST_enable_remote_bootstrap", "false"));
  ASSERT_OK(SplitTablet(tablet_id));
  ASSERT_OK(cluster_->WaitForTSToCrash(faulted_follower));

  // Once split is applied on two nodes, re-enable remote bootstrap before restarting the faulted
  // node. Ensure that remote bootstrap requests can be retried until the faulted node is up.
  ASSERT_OK(WaitForTablets(3, leader_idx));
  ASSERT_OK(WaitForTablets(3, healthy_follower_idx));
  ASSERT_OK(wait_for_same_tablet_metas(leader, healthy_follower));
  ASSERT_OK(cluster_->SetFlag(leader, "unresponsive_ts_rpc_retry_limit", "100"));
  ASSERT_OK(cluster_->SetFlag(leader, "TEST_enable_remote_bootstrap", "true"));

  // Restart the faulted node. Ensure it waits a long time in ApplyTabletSplit to allow a remote
  // bootstrap request to come in and create a directory for the subtablets before returning error.
  ASSERT_OK(faulted_follower->Restart(ExternalMiniClusterOptions::kDefaultStartCqlProxy, {
    std::make_pair(
        "TEST_apply_tablet_split_inject_delay_ms",
        Format("$0", MonoDelta(kApplyTabletSplitDelay).ToMilliseconds())),
    std::make_pair("TEST_simulate_already_present_in_remote_bootstrap", "true"),
    std::make_pair("TEST_crash_before_apply_tablet_split_op", "false"),
  }));

  // Once the faulted node has the same tablet metas written to disk as the leader, disable remote
  // bootstrap to avoid registering transition status for the subtablets.
  ASSERT_OK(wait_for_same_tablet_metas(leader, faulted_follower));
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_enable_remote_bootstrap", "false"));

  // Sleep some time to allow the ApplyTabletSplit pause to run out, and then ensure we have healthy
  // subtablets at the formerly faulted follower node.
  std::this_thread::sleep_for(kApplyTabletSplitDelay);
  EXPECT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 10s * kTimeMultiplier, "Write rows after faulted follower resurrection."));
  cluster_->Shutdown();
}

TEST_F(TabletSplitExternalMiniClusterITest, RemoteBootstrapsFromNodeWithUncommittedSplitOp) {
  // If a new tablet is created and split with one node completely uninvolved, then when that node
  // rejoins it will have to do a remote bootstrap.

  const auto server_to_bootstrap_idx = 0;
  std::vector<int> other_servers;
  for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
    if (i != server_to_bootstrap_idx) {
      other_servers.push_back(i);
    }
  }

  ASSERT_OK(cluster_->SetFlagOnMasters("unresponsive_ts_rpc_retry_limit", "0"));

  auto server_to_bootstrap = cluster_->tablet_server(server_to_bootstrap_idx);
  server_to_bootstrap->Shutdown();
  CHECK_OK(cluster_->WaitForTSToCrash(server_to_bootstrap));

  CreateSingleTablet();
  const auto other_server_idx = *other_servers.begin();
  const auto tablet_id = CHECK_RESULT(GetOnlyTestTabletId(other_server_idx));

  CHECK_OK(WriteRows());
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    if (i != server_to_bootstrap_idx) {
      ASSERT_OK(FlushTabletsOnSingleTServer(i, {tablet_id}, false));
    }
  }

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  const auto server_to_kill_idx = 3 - leader_idx - server_to_bootstrap_idx;
  auto server_to_kill = cluster_->tablet_server(server_to_kill_idx);

  auto leader = cluster_->tablet_server(leader_idx);
  CHECK_OK(cluster_->SetFlag(
      server_to_kill, "TEST_fault_crash_in_split_before_log_flushed", "1.0"));
  CHECK_OK(cluster_->SetFlag(leader, "TEST_fault_crash_in_split_before_log_flushed", "1.0"));
  CHECK_OK(SplitTablet(tablet_id));

  // The leader is guaranteed to attempt to apply the split operation and crash.
  CHECK_OK(cluster_->WaitForTSToCrash(leader));
  // The other follower may or may not attempt to apply the split operation. We shut it down here so
  // that it cannot be used for remote bootstrap.
  server_to_kill->Shutdown();

  CHECK_OK(leader->Restart());
  CHECK_OK(server_to_bootstrap->Restart());

  ASSERT_OK(cluster_->WaitForTabletsRunning(leader, 20s * kTimeMultiplier));
  ASSERT_OK(cluster_->WaitForTabletsRunning(server_to_bootstrap, 20s * kTimeMultiplier));
  CHECK_OK(server_to_kill->Restart());
  ASSERT_OK(WaitForTabletsExcept(2, server_to_bootstrap_idx, tablet_id));
  ASSERT_OK(WaitForTabletsExcept(2, leader_idx, tablet_id));
  ASSERT_OK(WaitForTabletsExcept(2, server_to_kill_idx, tablet_id));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after split."));

  server_to_kill->Shutdown();
  CHECK_OK(cluster_->WaitForTSToCrash(server_to_kill));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after requiring bootstraped node consensus."));

  CHECK_OK(server_to_kill->Restart());
}

class TabletSplitReplaceNodeITest : public TabletSplitExternalMiniClusterITest {
 protected:
  void SetFlags() override {
    TabletSplitExternalMiniClusterITest::SetFlags();

    for (const auto& tserver_flag : {
        // We want to test behavior of the source tablet, so setting up to skip deleting it.
        "--TEST_skip_deleting_split_tablets=true",
        // Reduce follower_unavailable_considered_failed_sec, so offline tserver is evicted
        // from Raft group faster.
        "--follower_unavailable_considered_failed_sec=5",
      }) {
      mini_cluster_opt_.extra_tserver_flags.push_back(tserver_flag);
    }

    for (const auto& master_flag : {
        // Should be less than follower_unavailable_considered_failed_sec, so load balancer
        // doesn't go into infinite loop trying to add failed follower back.
        "--tserver_unresponsive_timeout_ms=3000",
        // To speed up load balancing:
        // - Allow more concurrent adds/removes, so we deal with transaction status tablets
        // faster.
        "--load_balancer_max_concurrent_adds=10", "--load_balancer_max_concurrent_removals=10",
        // - Allow more over replicated tablets, so temporary child tablets over replication
        // doesn't block parent tablet move.
        "--load_balancer_max_over_replicated_tablets=5",
        // TODO: should be default behaviour after
        // https://github.com/yugabyte/yugabyte-db/issues/10301 is fixed.
        "--TEST_load_balancer_skip_inactive_tablets=false",
      }) {
      mini_cluster_opt_.extra_master_flags.push_back(master_flag);
    }
  }
};

TEST_F_EX(
    TabletSplitExternalMiniClusterITest, ReplaceNodeForParentTablet, TabletSplitReplaceNodeITest) {
  constexpr auto kNumRows = kDefaultNumRows;

  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndFlush(kNumRows));
  const auto source_tablet_id = ASSERT_RESULT(GetOnlyTestTabletId());
  LOG(INFO) << "Source tablet ID: " << source_tablet_id;

  auto* offline_ts = cluster_->tablet_server(0);
  offline_ts->Shutdown();
  LOG(INFO) << "Shutdown completed for tserver: " << offline_ts->uuid();
  const auto offline_ts_id = offline_ts->uuid();

  ASSERT_OK(SplitTablet(source_tablet_id));
  ASSERT_OK(WaitForTablets(3));

  ASSERT_OK(cluster_->AddTabletServer());
  const auto new_ts_id = cluster_->tablet_server(3)->uuid();
  LOG(INFO) << "Started new tserver: " << new_ts_id;

  ASSERT_OK(cluster_->WaitForTabletServerCount(4, 20s));
  LOG(INFO) << "New tserver has been added: " << new_ts_id;

  const auto deadline = CoarseMonoClock::Now() + 30s * kTimeMultiplier;
  std::set<TabletServerId> source_tablet_replicas;
  auto s = LoggedWait(
      [this, &deadline, &source_tablet_id, &offline_ts_id, &new_ts_id, &source_tablet_replicas] {
        const MonoDelta remaining_timeout = deadline - CoarseMonoClock::Now();
        if (remaining_timeout.IsNegative()) {
          return false;
        }
        master::TabletLocationsPB resp;
        const auto s = itest::GetTabletLocations(
            cluster_.get(), source_tablet_id, remaining_timeout, &resp);
        if (!s.ok()) {
          return false;
        }
        source_tablet_replicas.clear();
        for (auto& replica : resp.replicas()) {
          source_tablet_replicas.insert(replica.ts_info().permanent_uuid());
        }
        if (source_tablet_replicas.size() != 3) {
          return false;
        }
        if (source_tablet_replicas.count(offline_ts_id) > 0) {
          // We don't expect source tablet to have replica on offline tserver.
          return false;
        }
        return source_tablet_replicas.count(new_ts_id) > 0;
      },
      deadline,
      Format("Waiting for source tablet $0 to be moved to ts-4 ($1)", source_tablet_id, new_ts_id));

  ASSERT_TRUE(s.ok()) << s << ". Source tablet replicas: " << AsString(source_tablet_replicas);

  // Wait for the split to be completed on all online tservers.
  for (auto ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    if (ts_idx == 3) {
      // Skip new TS, because of https://github.com/yugabyte/yugabyte-db/issues/10301.
      // TODO(tsplit): remove after it is fixed.
      continue;
    }
    if (cluster_->tablet_server(ts_idx)->IsProcessAlive()) {
      ASSERT_OK(WaitForTablets(3, ts_idx));
    }
  }

  // Restarting offline_ts, because ClusterVerifier requires all tservers to be online.
  ASSERT_OK(offline_ts->Start());

  // TODO(tsplit): remove after https://github.com/yugabyte/yugabyte-db/issues/10301 is fixed.
  DontVerifyClusterBeforeNextTearDown();
}

namespace {

PB_ENUM_FORMATTERS(IsolationLevel);

std::string TestParamToString(const testing::TestParamInfo<IsolationLevel>& isolation_level) {
  return ToString(isolation_level.param);
}

} // namespace

INSTANTIATE_TEST_CASE_P(
    TabletSplitITest,
    TabletSplitITestWithIsolationLevel,
    ::testing::ValuesIn(GetAllPbEnumValues<IsolationLevel>()),
    TestParamToString);

}  // namespace yb
