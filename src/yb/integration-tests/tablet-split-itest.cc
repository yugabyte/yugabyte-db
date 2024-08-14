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
#include <limits>
#include <thread>

#include <gtest/gtest.h>

#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb_test_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/util.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/create-table-itest-base.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/redis_table_test_base.h"
#include "yb/integration-tests/tablet-split-itest-base.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_error.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/ts_descriptor.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/table/block_based_table_reader.h"
#include "yb/rocksdb/table/index_reader.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/full_compaction_manager.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/tsan_util.h"

using std::string;
using std::vector;

using yb::test::Partitioning;
using yb::test::kPartitioningArray;

using namespace std::literals;  // NOLINT
using namespace yb::client::kv_table_test; // NOLINT

DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_maintenance_manager);
DECLARE_int32(load_balancer_max_concurrent_adds);
DECLARE_int32(load_balancer_max_concurrent_removals);
DECLARE_int32(load_balancer_max_concurrent_moves);
DECLARE_int32(maintenance_manager_polling_interval_ms);
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
DECLARE_uint64(tablet_split_limit_per_table);
DECLARE_bool(TEST_pause_before_full_compaction);
DECLARE_bool(TEST_pause_apply_tablet_split);
DECLARE_int32(TEST_slowdown_backfill_alter_table_rpcs_ms);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_int32(rocksdb_base_background_compactions);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(TEST_pause_rbs_before_download_wal);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_bool(TEST_validate_all_tablet_candidates);
DECLARE_uint64(outstanding_tablet_split_limit);
DECLARE_uint64(outstanding_tablet_split_limit_per_tserver);
DECLARE_double(TEST_fail_tablet_split_probability);
DECLARE_bool(TEST_skip_post_split_compaction);
DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_int32(replication_factor);
DECLARE_int32(txn_max_apply_batch_records);
DECLARE_int32(TEST_pause_and_skip_apply_intents_task_loop_ms);
DECLARE_bool(TEST_pause_tserver_get_split_key);
DECLARE_bool(TEST_reject_delete_not_serving_tablet_rpc);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int64(db_block_cache_size_bytes);
DECLARE_uint64(rocksdb_max_file_size_for_compaction);
DECLARE_uint64(prevent_split_for_ttl_tables_for_seconds);
DECLARE_bool(sort_automatic_tablet_splitting_candidates);
DECLARE_int32(intents_flush_max_delay_ms);
DECLARE_int32(index_block_restart_interval);
DECLARE_bool(TEST_error_after_registering_split_tablets);
DECLARE_bool(TEST_pause_before_send_hinted_election);
DECLARE_bool(TEST_skip_election_when_fail_detected);
DECLARE_int32(scheduled_full_compaction_frequency_hours);
DECLARE_int32(scheduled_full_compaction_jitter_factor_percentage);
DECLARE_bool(TEST_asyncrpc_finished_set_timedout);
DECLARE_bool(enable_copy_retryable_requests_from_parent);
DECLARE_bool(enable_flush_retryable_requests);
DECLARE_int32(max_create_tablets_per_ts);
DECLARE_double(tablet_split_min_size_ratio);

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

  bool wait_for_intents = GetParam() != NON_TRANSACTIONAL;
  const auto source_tablet_id =
      ASSERT_RESULT(CreateSingleTabletAndSplit(kNumRows, wait_for_intents));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = false;

  ASSERT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 2));

  ASSERT_OK(CheckRowsCount(kNumRows));

  ASSERT_OK(WriteRows(kNumRows, kNumRows + 1));

  ASSERT_OK(cluster_->RestartSync());

  // Wait previous writes to be replicated and LB to stabilize as we are going to check all peers.
  ASSERT_OK(cluster_->WaitForLoadBalancerToStabilize(
      RegularBuildVsDebugVsSanitizers(10s, 20s, 30s)));
  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows * 2));
}

TEST_F(TabletSplitITest, SplitTabletIsAsync) {
  constexpr auto kNumRows = kDefaultNumRows;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  for (auto peer : ASSERT_RESULT(ListTestTableActiveTabletPeers())) {
    EXPECT_FALSE(peer->tablet()->metadata()->parent_data_compacted());
  }
  std::this_thread::sleep_for(1s * kTimeMultiplier);
  for (auto peer : ASSERT_RESULT(ListTestTableActiveTabletPeers())) {
    EXPECT_FALSE(peer->tablet()->metadata()->parent_data_compacted());
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
  ASSERT_OK(WaitForTestTableTabletPeersPostSplitCompacted(15s * kTimeMultiplier));
}

TEST_F(TabletSplitITest, ParentTabletCleanup) {
  constexpr auto kNumRows = kDefaultNumRows;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  // This will make client first try to access deleted tablet and that should be handled correctly.
  ASSERT_OK(CheckRowsCount(kNumRows));
}

class TabletSplitNoBlockCacheITest : public TabletSplitITest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_cache_size_bytes) = -2;
    TabletSplitITest::SetUp();
  }
};

TEST_F_EX(TabletSplitITest, TestInitiatesCompactionAfterSplit, TabletSplitNoBlockCacheITest) {
  // This test is very tight to SST file byte size, so disable packed row to keep current checks.
  docdb::DisableYcqlPackedRow();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = 5;
  constexpr auto kNumRows = kDefaultNumRows;
  constexpr auto kNumPostSplitTablets = 2;

  CreateSingleTablet();
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kNumRows));
  ASSERT_OK(LoggedWaitFor(
      [this]() -> Result<bool> {
        const auto count = VERIFY_RESULT(NumTestTableTabletPeersPostSplitCompacted());
        return count >= kNumPostSplitTablets * ANNOTATE_UNPROTECTED_READ(FLAGS_replication_factor);
      },
      15s * kTimeMultiplier, "Waiting for post-split tablets to be post split compacted..."));

  // Get the sum of compaction bytes read by each child tablet replica grouped by peer uuid
  auto replicas = ListTableActiveTabletPeers(cluster_.get(), ASSERT_RESULT(GetTestTableId()));
  ASSERT_EQ(replicas.size(),
            kNumPostSplitTablets * ANNOTATE_UNPROTECTED_READ(FLAGS_replication_factor));
  std::unordered_map<std::string, uint64_t> child_replicas_bytes_read;
  for (const auto& replica : replicas) {
    auto replica_bytes = replica->tablet()->regulardb_statistics()->getTickerCount(
         rocksdb::Tickers::COMPACT_READ_BYTES);
    ASSERT_GT(replica_bytes, 0) << "Expected replica's read bytes to be greater than zero.";
    child_replicas_bytes_read[replica->permanent_uuid()] += replica_bytes;
  }

  // Get parent tablet's bytes written and also check value is the same for all replicas
  replicas = ASSERT_RESULT(ListSplitCompleteTabletPeers());
  ASSERT_EQ(replicas.size(), ANNOTATE_UNPROTECTED_READ(FLAGS_replication_factor));
  uint64_t pre_split_sst_files_size = 0;
  for (const auto& replica : replicas) {
    auto replica_bytes = replica->tablet()->GetCurrentVersionSstFilesSize();
    ASSERT_GT(replica_bytes, 0) << "Expected replica's SST file size to be greater than zero.";
    if (pre_split_sst_files_size == 0) {
      pre_split_sst_files_size = replica_bytes;
    } else {
      ASSERT_EQ(replica_bytes, pre_split_sst_files_size)
          << "Expected the number of SST files size at each replica to be the same.";
    }
  }

  // Make sure that during child tablets compaction we don't read the same row twice, in other words
  // we don't process parent tablet rows that are not served by child tablet. A specific scaling
  // factor is used to measure relation between child replicas bytes read and parent SST files
  // due to unpredictable space overhead for reading files and SST files sturcture after the split.
  constexpr double kScalingFactor = 1.05;
  const double child_replicas_bytes_read_upper_bound = pre_split_sst_files_size * kScalingFactor;
  uint64_t post_split_bytes_read = 0;
  for (const auto& replica_stat : child_replicas_bytes_read) {
    if (post_split_bytes_read == 0) {
      post_split_bytes_read = replica_stat.second;
    } else {
      ASSERT_EQ(replica_stat.second, post_split_bytes_read);
    }
    // There are two ways to resolve a failure at the point if happens. The first one is to increase
    // the value of kScalingFactor but this approach is not very accurate. The second way is to
    // use rocksdb::EventListener to retrieve CompactionJobInfo.info.stats.num_input_records and
    // to measure it with the number of records in regular DB via TableProperties::num_entries,
    // see VerifyTableProperties() for an example.
    ASSERT_LE(static_cast<double>(post_split_bytes_read), child_replicas_bytes_read_upper_bound);
  }
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/8295.
// Checks that slow post-split tablet compaction doesn't block that tablet's cleanup.
TEST_F(TabletSplitITest, PostSplitCompactionDoesntBlockTabletCleanup) {
  constexpr auto kNumRows = kDefaultNumRows;
  const MonoDelta kCleanupTimeout = 15s * kTimeMultiplier;

  // Keep tablets without compaction after split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));
  auto tablet_peers =
      ASSERT_RESULT(WaitForTableActiveTabletLeadersPeers(cluster_.get(), table_->id(), 2));
  const auto first_child_tablet = tablet_peers[0]->shared_tablet();
  ASSERT_OK(first_child_tablet->Flush(tablet::FlushMode::kSync));
  // Force compact on leader, so we can split first_child_tablet.
  ASSERT_OK(first_child_tablet->ForceManualRocksDBCompact());
  // Turn off split tablets cleanup in order to later turn it on during compaction of the
  // first_child_tablet to make sure manual compaction won't block tablet shutdown.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  ASSERT_OK(SplitTablet(ASSERT_RESULT(catalog_manager()), *first_child_tablet));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
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
              mini_ts->server()->tablet_manager()->GetTablet(first_child_tablet->tablet_id());
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;

  CreateSingleTablet();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kNumRows));

  auto test_table_id = ASSERT_RESULT(GetTestTableId());
  auto test_tablet_ids = ListTabletIdsForTable(cluster_.get(), test_table_id);

  // Verify that heartbeat contains flag should_disable_lb_move for all tablets of the test
  // table on each tserver to have after split.
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
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

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;

  ASSERT_OK(WaitForTestTableTabletPeersPostSplitCompacted(15s * kTimeMultiplier));

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

// Test for https://github.com/yugabyte/yugabyte-db/issues/4312 reproducing a deadlock
// between TSTabletManager::ApplyTabletSplit and Heartbeater::Thread::TryHeartbeat.
TEST_F(TabletSplitITest, SlowSplitSingleTablet) {
  const auto leader_failure_timeout = FLAGS_leader_failure_max_missed_heartbeat_periods *
        FLAGS_raft_heartbeat_interval_ms;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_apply_tablet_split_inject_delay_ms) = 200 * kTimeMultiplier;
  // We want heartbeater to be called during tablet split apply to reproduce deadlock bug.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) =
      FLAGS_TEST_apply_tablet_split_inject_delay_ms / 3;
  // We reduce FLAGS_leader_lease_duration_ms for ReplicaState::GetLeaderState to avoid always
  // reusing results from cache on heartbeat, otherwise it won't lock ReplicaState mutex.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_lease_duration_ms) =
      FLAGS_TEST_apply_tablet_split_inject_delay_ms / 2;
  // Reduce raft_heartbeat_interval_ms for leader lease to be reliably replicated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_raft_heartbeat_interval_ms) = FLAGS_leader_lease_duration_ms / 2;
  // Keep leader failure timeout the same to avoid flaky losses of leader with short heartbeats.
  FLAGS_leader_failure_max_missed_heartbeat_periods =
      leader_failure_timeout / FLAGS_raft_heartbeat_interval_ms;

  constexpr auto kNumRows = 50;

  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));
}

TEST_F(TabletSplitITest, SplitSystemTable) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());

  // Attempt splits on "sys.catalog" and "tables" system tables and verify that they fail.
  std::vector<master::TableInfoPtr> systables = {
      catalog_mgr->GetTableInfo("sys.catalog.uuid"),
      catalog_mgr->GetTableInfoFromNamespaceNameAndTableName(
          YQL_DATABASE_CQL, "system_schema", "tables")};

  for (const auto& systable : systables) {
    for (const auto& tablet : ASSERT_RESULT(systable->GetTablets())) {
      LOG(INFO) << "Splitting : " << systable->name() << " Tablet :" << tablet->id();
      auto s = catalog_mgr->TEST_SplitTablet(tablet, true /* is_manual_split */);
      LOG(INFO) << s.ToString();
      EXPECT_TRUE(s.IsNotSupported());
      LOG(INFO) << "Split of system table failed as expected";
    }
  }
}

TEST_F(TabletSplitITest, SplitTabletDuringReadWriteLoad) {
  constexpr auto kNumTablets = 3;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 100_KB;

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

  auto peers = ASSERT_RESULT(WaitForTableActiveTabletLeadersPeers(
      cluster_.get(), test_table_id, kNumTablets));

  LOG(INFO) << "Starting workload ...";
  workload.Start();

  for (const auto& peer : peers) {
    ASSERT_OK(LoggedWaitFor(
        [&peer] {
          const auto data_size =
              peer->tablet()->regular_db()->GetCurrentVersionSstFilesUncompressedSize();
          YB_LOG_EVERY_N_SECS(INFO, 5) << "Data written: " << data_size;
          size_t expected_size = (FLAGS_rocksdb_level0_file_num_compaction_trigger + 1) *
                                 FLAGS_db_write_buffer_size;
          return data_size > expected_size;
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

namespace {

void SetSmallDbBlockSize() {
  // Set data block size low enough, so we have enough data blocks for middle key
  // detection to work correctly.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 1_KB;
}

}

void TabletSplitITest::SplitClientRequestsIds(int split_depth) {
  SetSmallDbBlockSize();
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
      CHECK_OK(tablet->ForceManualRocksDBCompact());
      ASSERT_OK(SplitTablet(catalog_mgr, *tablet));
    }

    ASSERT_OK(WaitForTabletSplitCompletion(
        /* expected_non_split_tablets = */ 1 << (i + 1)));
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

class TabletSplitITestSlowMainenanceManager : public TabletSplitITest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_maintenance_manager_polling_interval_ms) = 60 * 1000;
    TabletSplitITest::SetUp();
  }
};

TEST_F_EX(TabletSplitITest, SplitClientRequestsClean, TabletSplitITestSlowMainenanceManager) {
  constexpr auto kSplitDepth = 3;
  constexpr auto kNumRows = 50 * (1 << kSplitDepth);
  constexpr auto kRetryableRequestTimeoutSecs = 1;
  SetSmallDbBlockSize();

  // Prevent periodic retryable requests cleanup by maintenance manager.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_maintenance_manager) = false;

  SetNumTablets(1);
  CreateTable();

  ASSERT_OK(WriteRows(kNumRows, 1));
  ASSERT_OK(CheckRowsCount(kNumRows));

  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  LOG(INFO) << "Creating new client, id: " << client->id();

  for (int i = 0; i < kSplitDepth; ++i) {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    ASSERT_EQ(peers.size(), 1 << i);
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
      ASSERT_OK(tablet->ForceManualRocksDBCompact());
      ASSERT_OK(SplitTablet(catalog_mgr, *tablet));
    }

    ASSERT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets =*/ 1 << (i + 1)));

    if (i == 0) {
      // This will set client's request_id_seq for tablets with split depth 1 to > 1^24 (see
      // YBClient::MaybeUpdateMinRunningRequestId) and update min_running_request_id for this
      // client at tserver side.
      auto session = client->NewSession(60s);
      ASSERT_OK(WriteRows(&this->table_, kNumRows, 1, session));
    }
  }

  auto leader_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  // Force cleaning retryable requests on leaders of active (split depth = kSplitDepth) tablets.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = kRetryableRequestTimeoutSecs;
  SleepFor(kRetryableRequestTimeoutSecs * 1s);
  for (int i = 0; i < 2; ++i) {
    for (auto& leader_peer : leader_peers) {
      LOG(INFO) << leader_peer->LogPrefix() << "MinRetryableRequestOpId(): "
                << AsString(
                       ASSERT_RESULT(leader_peer->GetRaftConsensus())->MinRetryableRequestOpId());
      // Delay to make RetryableRequests::CleanExpiredReplicatedAndGetMinOpId (called by
      // MinRetryableRequestOpId) do delayed cleanup.
      SleepFor(kRetryableRequestTimeoutSecs * 1s);
    }
  }

  auto session = client->NewSession(60s);
  // Since client doesn't know about tablets with split depth > 1, it will set request_id_seq for
  // active tablets based on min_running_request_id on leader, but on leader it has been cleaned up.
  // So, request_id_seq will be set to 0 + 1^24 that is less than min_running_request_id on the
  // follower (at which retryable requests is not yet cleaned up).
  // This will test how follower handles getting request_id less than min_running_request_id.
  LOG(INFO) << "Starting write to active tablets after retryable requests cleanup on leaders...";
  ASSERT_OK(WriteRows(&this->table_, kNumRows, 1, session));
  LOG(INFO) << "Write to active tablets completed.";
}


TEST_F(TabletSplitITest, SplitSingleTabletWithLimit) {
  SetSmallDbBlockSize();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  const auto kSplitDepth = 3;
  const auto kNumRows = 50 * (1 << kSplitDepth);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_limit_per_table) = (1 << kSplitDepth) - 1;

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
      ASSERT_OK(tablet->ForceManualRocksDBCompact());
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

TEST_F(TabletSplitITest, MaxCreateTabletsPerTs) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  SetNumTablets(3);
  CreateTable();

  auto& master = ASSERT_RESULT(GetLeaderMaster()).get();
  auto catalog_mgr = ASSERT_RESULT(catalog_manager());
  auto table = catalog_mgr->GetTableInfo(table_->id());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_create_tablets_per_ts) = 1;
  ASSERT_NOK(master.tablet_split_manager().ValidateSplitCandidateTable(table));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_create_tablets_per_ts) = 2;
  ASSERT_OK(master.tablet_split_manager().ValidateSplitCandidateTable(table));
}

TEST_F(TabletSplitITest, SplitDuringReplicaOffline) {
  constexpr auto kNumRows = kDefaultNumRows;

  SetNumTablets(1);
  CreateTable();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  auto rows_count = ASSERT_RESULT(CountRows(NewSession(), table_));
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

  rows_count = ASSERT_RESULT(CountRows(NewSession(), table_));
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

// During apply of tablet split op, each child tablet copies the log segments from the parent. For
// each segment, the copy could either be a hard link to the parent's copy or an actual duplication
// of the segment. The below test asserts that footer of the duplicated segments is rightly formed.
TEST_F(TabletSplitITest, TestLogCopySetsCloseTimestampInFooter) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;

  SetNumTablets(1);
  CreateTable();
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));

  const auto catalog_mgr = ASSERT_RESULT(catalog_manager());
  const auto tablet = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto tablet_id = tablet->id();
  LOG(INFO) << "Source tablet id " << tablet_id;

  size_t leader_idx;
  CHECK_NOTNULL(GetLeaderForTablet(cluster_.get(), tablet_id, &leader_idx));
  const auto follower1_idx = (leader_idx + 1) % 3;
  const auto follower1_id = cluster_->mini_tablet_server(follower1_idx)->server()->permanent_uuid();
  const auto follower2_idx = (leader_idx + 2) % 3;
  const auto follower2_id = cluster_->mini_tablet_server(follower2_idx)->server()->permanent_uuid();

  cluster_->mini_tablet_server(follower1_idx)->Shutdown();

  ASSERT_OK(catalog_mgr->TEST_SplitTablet(tablet, split_hash_code));
  ASSERT_OK(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets = */ 2, /* expected_split_tablets = */ 1,
      /* num_replicas_online = */ 2));

  // Initiate a leader change in order to append NO_OP entry to tablet's log from the new leader.
  // We do this hoping that it would trigger the duplication of log segment while creation of the
  // child tablets once the offline TS restarts, instead of hard linking to the parent's log
  // segment. An actual copy is triggered when the split op is in the segment but not the last op.
  const auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
  ASSERT_OK(StepDown(leader_peer, follower2_id, ForceStepDown::kTrue));
  // Wait for the new leader to replicate NO_OP.
  ASSERT_OK(WaitUntilTabletHasLeader(
      cluster_.get(), tablet_id, CoarseMonoClock::Now() + 5s, RequireLeaderIsReady::kTrue));
  // Now follower1_idx comes back alive, and receives the split op as well as the NO_OP, and applies
  // the split op while satisfying the condition that the split op isn't the last op in the segment.
  ASSERT_OK(cluster_->mini_tablet_server(follower1_idx)->Start());
  // Wait for the split op to be applied on the restarted TS.
  ASSERT_OK(WaitForTabletSplitCompletion(
      /* expected_non_split_tablets = */ 2, /* expected_split_tablets = */ 1,
      /* num_replicas_online = */ 3));

  auto peers = ListTabletPeers(
      cluster_.get(), ListPeersFilter::kAll, IncludeTransactionStatusTablets::kFalse);
  for (const auto& peer : peers) {
    log::SegmentSequence segments;
    ASSERT_OK(peer->log()->GetLogReader()->GetSegmentsSnapshot(&segments));
    for (const auto& segment : segments) {
      if (!segment->HasFooter()) {
        continue;
      }
      auto entries_copy_result = segment->ReadEntries();
      ASSERT_OK(entries_copy_result.status);
      bool has_replicated_entries = false;
      for (const auto& entry : entries_copy_result.entries) {
        if (entry->has_replicate()) {
          has_replicated_entries = true;
          break;
        }
      }
      ASSERT_EQ(has_replicated_entries, segment->footer().has_close_timestamp_micros())
          << "T " << peer->tablet_id() << " P " << peer->permanent_uuid()
          << ": Expected valid close timestamp for segment with replicated entries.";
    }
  }
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

  auto rows_count = ASSERT_RESULT(CountRows(NewSession(), table1));
  ASSERT_EQ(rows_count, kNumRows);

  rows_count = ASSERT_RESULT(CountRows(NewSession(), table2));
  ASSERT_EQ(rows_count, kNumRows);
}

TEST_F(TabletSplitITest, SplitSingleTabletLongTransactions) {
  constexpr auto kNumRows = 1000;
  constexpr auto kNumApplyLargeTxnBatches = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) =
      kNumRows / kNumApplyLargeTxnBatches;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_and_skip_apply_intents_task_loop_ms) = 1;

  // Write enough rows to trigger the large transaction apply path with kNumApplyLargeTxnBatches
  // batches. Wait for post split compaction and validate data before returning.
  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows, /* wait_for_intents */ false));

  // At this point, post split compaction has happened, and no apply intent task iterations have
  // run. If post-split compaction has improperly handled ApplyTransactionState present in
  // regulardb, e.g. by deleting it, then upon restart, one or both of the new child subtablets will
  // lose all unapplied data.
  ASSERT_OK(cluster_->RestartSync());

  // If we did not lose any large transaction apply data during post-split compaction, then we
  // should have all rows present in the database.
  EXPECT_OK(CheckRowsCount(kNumRows));
}

TEST_F(TabletSplitITest, StartHintedElectionForChildTablets) {
  const auto leader_failure_timeout = FLAGS_leader_failure_max_missed_heartbeat_periods *
      FLAGS_raft_heartbeat_interval_ms;
  CreateSingleTablet();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_election_when_fail_detected) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_send_hinted_election) = true;
  const auto hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kDefaultNumRows));
  const auto tablet_id = ASSERT_RESULT(SplitSingleTablet(hash_code));
  // Waiting for enough time for the leader failure.
  SleepFor(MonoDelta::FromMilliseconds(leader_failure_timeout) * kTimeMultiplier);
  // Child tablets shouldn't have leaders elected.
  ASSERT_EQ(ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id()).size(), 0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_send_hinted_election) = false;
  // Waiting for hinted election on 2 child tablets.
  ASSERT_OK(WaitForTableNumActiveLeadersPeers(/* expected_leaders = */ 2));
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
    EXPECT_NOK(s);
    EXPECT_TRUE(s.IsNotSupported()) << s.ToString();
  }
}

class AutomaticTabletSplitITest : public TabletSplitITest {
 public:
  void SetUp() override {
    // This value must be set before calling TabletSplitITest::SetUp(), since it is copied into a
    // variable in TServerMetricsHeartbeatDataProvider.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 100;
    TabletSplitITest::SetUp();

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = 0; // no limit
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit_per_tserver) = 0; // no limit
  }

 protected:
  Status FlushAllTabletReplicas(const TabletId& tablet_id, const TableId& table_id) {
    // Wait for the write transaction to move from intents db to regular db on each peer before
    // trying to flush.
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_id)) {
      if (peer->tablet_id() == tablet_id) {
        RETURN_NOT_OK(WaitFor([&]() {
          // This tablet might has been shut down or in the process of shutting down.
          // Thus, we need to check whether shared_tablet is nullptr or not
          // TEST_CountIntent return non ok status also means shutdown has started.
          const auto tablet = peer->shared_tablet();
          if (!tablet) {
            return true;
          }
          auto result = tablet->transaction_participant()->TEST_CountIntents();
          return !result.ok() || result->num_intents == 0;
        }, 30s, "Did not apply write transactions from intents db in time."));

        if (peer->IsShutdownStarted()) {
          return STATUS(NotFound, "The tablet has been shut down.");
        }
        RETURN_NOT_OK(peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
      }
    }
    return Status::OK();
  }

  Status CreateAndAutomaticallySplitSingleTablet(int num_rows_per_batch, int* key) {
    CreateSingleTablet();

    const auto threshold = static_cast<size_t>(
        ANNOTATE_UNPROTECTED_READ(FLAGS_tablet_split_low_phase_size_threshold_bytes));
    size_t current_size = 0;
    while (current_size <= threshold) {
      RETURN_NOT_OK(WriteRows(num_rows_per_batch, *key));
      *key += num_rows_per_batch;
      auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
      LOG(INFO) << "Active peers: " << peers.size();
      if (peers.size() == 2) {
        for (const auto& peer : peers) {
          const auto tablet = peer->shared_tablet();
          SCHECK_NOTNULL(tablet.get());

          // Since we've disabled compactions, each post-split subtablet should be larger than the
          // split size threshold.
          auto peer_size = tablet->GetCurrentVersionSstFilesSize();
          EXPECT_GE(peer_size, threshold);
        }
        break;
      } else if (peers.size() != 1) {
        return STATUS_FORMAT(IllegalState, "Expected number of peers: 1, actual: $0", peers.size());
      }
      const auto leader_peer = peers.at(0);

      // Flush all replicas of this shard to ensure that even if the leader changed we will be in a
      // state where yb-master should initiate a split.
      auto status = FlushAllTabletReplicas(leader_peer->tablet_id(), table_->id());
      if (status.IsNotFound()) {
        // The parent tablet has been shut down, which means tablet split is triggered.
        break;
      }
      RETURN_NOT_OK(status);

      // 1. If shared_tablet is nullptr, it means peer has shut down and split happened.
      // 2. If shared_tablet hasn't been reset, but RocksDB has been shut down,
      //    shared_tablet->GetCurrentVersionSstFilesSize() will return 0 as default.
      //    Then next loop of inserting and flush, the code will detect splitting
      //    happen and break the loop. Thus, we don't need to handle it here.
      const auto tablet = leader_peer->shared_tablet();
      if (tablet) {
        current_size = tablet->GetCurrentVersionSstFilesSize();
      } else {
        break;
      }
    }

    return WaitForTabletSplitCompletion(/* expected_non_split_tablets = */ 2);
  }

  Status CompactTablet(const string& tablet_id) {
    auto peers = ListTabletPeers(cluster_.get(), [&tablet_id](auto peer) {
      return peer->tablet_id() == tablet_id;
    });
    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      RETURN_NOT_OK(tablet->Flush(tablet::FlushMode::kSync));
      CHECK_OK(tablet->ForceManualRocksDBCompact());
    }
    return Status::OK();
  }

  void SleepForBgTaskIters(int num_iters) {
    std::this_thread::sleep_for(1ms *
        (FLAGS_catalog_manager_bg_task_wait_ms * num_iters +
         FLAGS_tserver_heartbeat_metrics_interval_ms));
    std::this_thread::sleep_for((FLAGS_catalog_manager_bg_task_wait_ms * num_iters +
                                FLAGS_tserver_heartbeat_metrics_interval_ms) * 1ms);
  }
};

class AutomaticTabletSplitExternalMiniClusterITest : public TabletSplitExternalMiniClusterITest {
 public:
  void SetFlags() override {
    TabletSplitITestBase<ExternalMiniCluster>::SetFlags();
    for (const auto& master_flag : {
              "--enable_automatic_tablet_splitting=true",
              "--outstanding_tablet_split_limit=5",
          }) {
      mini_cluster_opt_.extra_master_flags.push_back(master_flag);
    }
  }
};

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplitting) {
  constexpr int kNumRowsPerBatch = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;

  int key = 1;
  ASSERT_OK(CreateAndAutomaticallySplitSingleTablet(kNumRowsPerBatch, &key));

  // Since compaction is off, the tablets should not be further split since they won't have had
  // their post split compaction. Assert this is true by tripling the number of keys written and
  // seeing the number of tablets not grow.
  auto triple_keys = key * 2;
  while (key < triple_keys) {
    ASSERT_OK(WriteRows(kNumRowsPerBatch, key));
    key += kNumRowsPerBatch;
  }
  // TODO(#13426): We should find a faster way to check that splitting has not occurred. We
  // currently waste 40 * kTimeMultiplier for each of these checks, and we use these checks
  // frequently throughout our tests.
  ASSERT_NOK(WaitForTabletSplitCompletion(
      3,                  // expected_non_split_tablets
      0,                  // expected_split_tablets (default)
      0,                  // num_replicas_online (default)
      client::kTableName, // table (default)
      false));            // core_dump_on_failure
}

TEST_F(AutomaticTabletSplitITest, IsTabletSplittingComplete) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;

  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndFlush(1000));

  auto master_admin_proxy = std::make_unique<master::MasterAdminProxy>(
      proxy_cache_.get(), client_->GetMasterLeaderAddress());

  // No splits at the beginning.
  ASSERT_TRUE(ASSERT_RESULT(IsSplittingComplete(master_admin_proxy.get(),
                                                false /* wait_for_parent_deletion */)));

  // Create a split task by pausing when trying to get split key. IsTabletSplittingComplete should
  // include this ongoing task.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_tserver_get_split_key) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
  std::this_thread::sleep_for(FLAGS_catalog_manager_bg_task_wait_ms * 2ms);
  ASSERT_FALSE(ASSERT_RESULT(IsSplittingComplete(master_admin_proxy.get(),
                                                 false /* wait_for_parent_deletion */)));

  // Now let the split occur on master but not tserver.
  // IsTabletSplittingComplete should include splits that are only complete on master.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_tablet_split_probability) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_tserver_get_split_key) = false;
  ASSERT_FALSE(ASSERT_RESULT(IsSplittingComplete(master_admin_proxy.get(),
                                                 false /* wait_for_parent_deletion */)));

  // Verify that the split finishes, and that IsTabletSplittingComplete returns true (even though
  // compactions are not done) if wait_for_parent_deletion is false .
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_tablet_split_probability) = 0;
  ASSERT_OK(WaitForTabletSplitCompletion(2 /* expected_non_split_tablets */,
                                         1 /* expected_split_tablets */));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(IsSplittingComplete(master_admin_proxy.get(),
                                             false /* wait_for_parent_deletion */));
  }, MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms * 2),
    "IsTabletSplittingComplete did not return true."));

  // Re-enable deletion of children and check that IsTabletSplittingComplete returns true if
  // wait_for_parent_deletion is true.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = false;
  ASSERT_OK(WaitForTabletSplitCompletion(2));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(IsSplittingComplete(master_admin_proxy.get(),
                                             true /* wait_for_parent_deletion */));
  }, MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms * 2),
    "IsTabletSplittingComplete did not return true."));
}

// This test tests both FLAGS_enable_automatic_tablet_splitting and the DisableTabletSplitting API
// (which temporarily disables splitting).
TEST_F(AutomaticTabletSplitITest, DisableTabletSplitting) {
  // Must disable splitting for at least as long as we wait in WaitForTabletSplitCompletion.
  const auto kExtraSleepDuration = 5s * kTimeMultiplier;
  const auto kDisableDuration = split_completion_timeout_sec_ + kExtraSleepDuration;
  const std::string kSplitDisableFeatureName = "DisableTabletSplittingTest";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndFlush(1000));

  // Splitting should fail while FLAGS_enable_automatic_tablet_splitting is false.
  ASSERT_NOK(WaitForTabletSplitCompletion(
      2,                  // expected_non_split_tablets
      0,                  // expected_split_tablets (default)
      0,                  // num_replicas_online (default)
      client::kTableName, // table (default)
      false));            // core_dump_on_failure

  auto master_admin_proxy = std::make_unique<master::MasterAdminProxy>(
      proxy_cache_.get(), client_->GetMasterLeaderAddress());
  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);

  master::DisableTabletSplittingRequestPB disable_req;
  disable_req.set_disable_duration_ms(kDisableDuration.ToMilliseconds());
  disable_req.set_feature_name(kSplitDisableFeatureName);
  master::DisableTabletSplittingResponsePB disable_resp;
  ASSERT_OK(master_admin_proxy->DisableTabletSplitting(disable_req, &disable_resp, &controller));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
  // Splitting should not occur while it is temporarily disabled.
  ASSERT_NOK(WaitForTabletSplitCompletion(
      2,                  // expected_non_split_tablets
      0,                  // expected_split_tablets (default)
      0,                  // num_replicas_online (default)
      client::kTableName, // table (default)
      false));            // core_dump_on_failure

  // Sleep until the splitting is no longer disabled (we already waited for
  // split_completion_timeout_sec_ seconds in the previous step).
  std::this_thread::sleep_for(kExtraSleepDuration);
  // Splitting should succeed once the delay has expired and FLAGS_enable_automatic_tablet_splitting
  // is true.
  ASSERT_OK(WaitForTabletSplitCompletion(2));
}

// TODO(tsplit): AutomaticTabletSplitITest.TabletSplitHasClusterReplicationInfo was removed because
// there's no guarantee that required functionality is covered by this test. A separate issue #14801
// as created to track a new unit test for ShouldSplitValidCandidate() only.
// This comment should be removed in the context of that new issue.

TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplittingWaitsForAllPeersCompacted) {
  constexpr auto kNumRowsPerBatch = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 100_KB;
  // Disable post split compaction
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_post_split_compaction) = true;
  // Disable automatic compactions
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
      std::numeric_limits<int32>::max();

  int key = 1;
  ASSERT_OK(CreateAndAutomaticallySplitSingleTablet(kNumRowsPerBatch, &key));

  const auto tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_->id());
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
      ASSERT_OK(tablet->ForceManualRocksDBCompact());
      ASSERT_OK(LoggedWaitFor(
        [peer]() -> Result<bool> {
          return peer->tablet_metadata()->parent_data_compacted();
        },
        15s * kTimeMultiplier,
        "Wait for post tablet split compaction to be completed for peer: " + peer->tablet_id()));

      // Write enough data to get the tablet into a state where it's large enough for a split
      int64_t current_size = 0;
      while (current_size <= FLAGS_tablet_split_low_phase_size_threshold_bytes) {
        ASSERT_OK(WriteRowsAndFlush(kNumRowsPerBatch, key));
        key += kNumRowsPerBatch;
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


TEST_F(AutomaticTabletSplitITest, PrioritizeLargeTablets) {
  constexpr int kNumRowsBase = 3000;
  constexpr int kNumExtraRowsPerTable = 100;
  constexpr int kNumTables = 5;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  // Disable automatic compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
      std::numeric_limits<int32>::max();
  // Disable post split compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_post_split_compaction) = true;

  // Disable splitting until all data has been written.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = 1;

  // Create tables with 1 tablet each.
  client::TableHandle tables[kNumTables];
  auto catalog_mgr = ASSERT_RESULT(catalog_manager());
  for (int i = 0; i < kNumTables; ++i) {
    auto table_name = client::YBTableName(YQL_DATABASE_CQL,
                                          "my_keyspace",
                                          "table_" + std::to_string(i));
    client::kv_table_test::CreateTable(
        client::Transactional(true), 1 /* num_tablets */, client_.get(), &tables[i], table_name);
    ASSERT_OK(WriteRowsAndFlush(&tables[i], kNumRowsBase + i * kNumExtraRowsPerTable));

    const auto peers = ListTableActiveTabletPeers(cluster_.get(), tables[i]->id());
    ASSERT_EQ(peers.size(), 3);

    // Wait for SST file sizes to be updated on the master (via a metrics heartbeat) before enabling
    // splitting (otherwise we might split a tablet which is not the largest tablet).
    auto tablet = ASSERT_RESULT(catalog_mgr->GetTabletInfo(peers[0]->tablet_id()));
    ASSERT_OK(WaitFor([&]() {
      auto drive_info = tablet->GetLeaderReplicaDriveInfo();
      if (!drive_info.ok()) {
        return false;
      }
      if (drive_info->sst_files_size > 0) {
        LOG(INFO) << Format("Peer size: $0. Tablet id: $1.",
            drive_info->sst_files_size, tablet->tablet_id());
        return true;
      }
      return false;
    }, 10s, "Wait for tablet heartbeat."));
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
  for (int i = kNumTables - 1; i >= 0; --i) {
    ASSERT_OK(WaitForTabletSplitCompletion(
        2,                   // expected_non_split_tablets
        0,                   // expected_split_tablets (default)
        0,                   // num_replicas_online (default)
        tables[i]->name())); // table (default)
    // Compact so that other splits can proceed.
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), tables[i]->id())) {
      ASSERT_OK(peer->shared_tablet()->ForceManualRocksDBCompact());
    }
  }
}

// This test verifies that a tablet only splits if it has at least much SST data as the split
// threshold for the current phase.
TEST_F(AutomaticTabletSplitITest, AutomaticTabletSplittingMultiPhase) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 10_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 20_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 30_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 2;
  // Disable automatic compactions, but continue to allow manual compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
      std::numeric_limits<int32>::max();
  // Disable post split compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_post_split_compaction) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto master_admin_proxy = std::make_unique<master::MasterAdminProxy>(
      proxy_cache_.get(), client_->GetMasterLeaderAddress());

  SetNumTablets(1);
  CreateTable();

  int key = 1;
  size_t num_tablets = 0;
  const auto num_tservers = cluster_->num_tablet_servers();

  auto enable_splitting_and_wait_for_completion = [&]() -> Status {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    SleepForBgTaskIters(2);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    LOG(INFO) << "Waiting for tablet splitting";
    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      return IsSplittingComplete(master_admin_proxy.get(), false /* wait_for_parent_deletion */);
    }, 30s * kTimeMultiplier, "Wait for IsTabletSplittingComplete"));
    LOG(INFO) << "Tablet splitting is complete";
    return Status::OK();
  };

  auto test_phase = [&](
      int num_rows_per_batch, size_t tablet_leaders_per_node_limit, uint64_t split_threshold_bytes,
      uint64_t next_threshold_bytes) {
    // At this point, no tablet should have enough data to split since each tablet should have
    // ~split_threshold_bytes of data from the previous round (or 0 bytes for the low phase), which
    // is less than this round's split_threshold_bytes. Verify that they do not split.
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_->id())) {
      ASSERT_LT(peer->shared_tablet()->GetCurrentVersionSstFilesSize(), split_threshold_bytes);
    }
    ASSERT_OK(enable_splitting_and_wait_for_completion());
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_->id())) {
      ASSERT_FALSE(peer->shared_tablet()->MayHaveOrphanedPostSplitData());
    }

    // Write data until we hit tablet_count_per_node_limit tablet leaders per node. We should not
    // have more than next_threshold_bytes in any tablet at any time in this phase.
    do {
      ASSERT_OK(WriteRowsAndFlush(num_rows_per_batch, key));
      key += num_rows_per_batch;

      // Give splitting a chance to run, then wait for any splits to complete.
      ASSERT_OK(enable_splitting_and_wait_for_completion());

      vector<tablet::TabletPeerPtr> split_peers;
      std::unordered_map<TabletId, uint64_t> max_split_peer_size;
      for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_->id())) {
        auto peer_size = peer->shared_tablet()->GetCurrentVersionSstFilesSize();
        auto tablet_id = peer->tablet_id();
        LOG(INFO) << Format("Tablet peer: $0 on tserver $1 with size $2 (before compaction).",
            tablet_id, peer->permanent_uuid(), peer_size);

        ASSERT_LT(peer_size, next_threshold_bytes);
        if (peer->shared_tablet()->MayHaveOrphanedPostSplitData()) {
          max_split_peer_size[tablet_id] = std::max(max_split_peer_size[tablet_id], peer_size);
          split_peers.push_back(peer);
        }
      }

      // Verify that any tablet that split has at least one peer with greater than
      // split_threshold_bytes of SST files. Not all peers are necessarily larger than
      // split_threshold_bytes, only the leader at the time of the split.
      // Note that the tablet would not have compacted away the parent's data since automatic
      // compactions are off, so we expect this verification to pass with certainty.
      for (const auto& [tablet_id, peer_size] : max_split_peer_size) {
        ASSERT_GE(peer_size, split_threshold_bytes)
            << Format("No peer of tablet $0 has enough data to split.", tablet_id);
      }

      // Compact the tablets that split so they can split again in the next iteration.
      for (const auto& peer : split_peers) {
        ASSERT_OK(peer->shared_tablet()->ForceManualRocksDBCompact());
      }

      // Return once we hit the tablet leader count limit for this phase.
      num_tablets = ListActiveTabletIdsForTable(cluster_.get(), table_->id()).size();
      LOG(INFO) << "Num tablets: " << num_tablets;
    } while (num_tablets / num_tservers < tablet_leaders_per_node_limit);
  };

  LOG(INFO) << "Starting low phase test.";
  test_phase(
    /* kNumRowsPerBatch */ 100,
    /* tablet_count_limit */ FLAGS_tablet_split_low_phase_shard_count_per_node,
    /* split_threshold_bytes */ FLAGS_tablet_split_low_phase_size_threshold_bytes,
    /* next_threshold_bytes */ FLAGS_tablet_split_high_phase_size_threshold_bytes);

  // Assert that we are now in the high phase.
  ASSERT_EQ(num_tablets / num_tservers, FLAGS_tablet_split_low_phase_shard_count_per_node);

  // Using a higher number of rows per batch now that we have more tablets to speed up the test.
  LOG(INFO) << "Starting high phase test.";
  test_phase(
    /* kNumRowsPerBatch */ 200,
    /* tablet_count_limit */ FLAGS_tablet_split_high_phase_shard_count_per_node,
    /* split_threshold_bytes */ FLAGS_tablet_split_high_phase_size_threshold_bytes,
    /* next_threshold_bytes */ FLAGS_tablet_force_split_threshold_bytes);

  // Assert that we are now in the force-split phase.
  ASSERT_EQ(num_tablets / num_tservers, FLAGS_tablet_split_high_phase_shard_count_per_node);

  LOG(INFO) << "Starting force-split phase test.";
  test_phase(
    /* kNumRowsPerBatch */ 500,
    /* tablet_count_limit */ FLAGS_tablet_split_high_phase_shard_count_per_node + 1,
    /* split_threshold_bytes */ FLAGS_tablet_force_split_threshold_bytes,
    /* next_threshold_bytes */ FLAGS_tablet_force_split_threshold_bytes + 30_KB);
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

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
      ASSERT_OK(FlushAllTabletReplicas(peer->tablet_id(), table_->id()));
    }

    // Keep tablets without compaction after split.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;
    // Enable splitting.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

    ASSERT_OK(WaitForTabletSplitCompletion(num_tablets + kTabletSplitLimit));
    // Ensure that we don't split any more tablets.
    SleepForBgTaskIters(2);
    ASSERT_NOK(WaitForTabletSplitCompletion(
        num_tablets + kTabletSplitLimit + 1,  // expected_non_split_tablets
        0,                                    // expected_split_tablets (default)
        0,                                    // num_replicas_online (default)
        client::kTableName,                   // table (default)
        false));                              // core_dump_on_failure

    // Pause any more tablet splits.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    // Reenable post split compaction, wait for this to complete so next tablets can be split.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;

    ASSERT_OK(WaitForTestTableTabletPeersPostSplitCompacted(15s * kTimeMultiplier));

    peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    num_tablets = narrow_cast<int32_t>(peers.size());

    // There should be kTabletSplitLimit initial tablets + kTabletSplitLimit new tablets per loop.
    EXPECT_EQ(num_tablets, (split_round + 2) * kTabletSplitLimit);
  }

  // TODO (jhe) For now we need to manually delete the cluster otherwise the cluster verifier can
  // get stuck waiting for tablets that got registered but whose tablet split got cancelled by
  // FLAGS_TEST_fail_tablet_split_probability.
  // We should either have a way to wait for these tablets to get split, or have a way to delete
  // these tablets in case a tablet split fails.
  cluster_->Shutdown();
}

TEST_F(AutomaticTabletSplitITest, LimitNumberOfOutstandingTabletSplitsPerTserver) {
  constexpr int kNumRowsPerBatch = 2000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit_per_tserver) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 10000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_tserver_get_split_key) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_tablet_split_probability) = 1.0;
  // Need to disable load balancing until the first tablet is split, otherwise it might end up
  // being overreplicated on 4 tservers when we split, resulting in the split children being on 4
  // tservers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(5));

  SetNumTablets(2);
  CreateTable();

  auto catalog_mgr = ASSERT_RESULT(catalog_manager());
  auto table_info = catalog_mgr->GetTableInfo(table_->id());

  // Flush to ensure an SST file is generated so splitting can occur.
  // One of the tablets (call it A) should be automatically split after the flush. Since RF=3 and we
  // have 5 tservers, the other tablet (B) must share at least one tserver with A. Since we limit
  // the number of outstanding splits on a tserver to 1, B should not be split (since that would
  // result in two outstanding splits on the tserver that hosted a replica of A and B).
  ASSERT_OK(WriteRowsAndFlush(kNumRowsPerBatch));

  // Check that no more than 1 split task is created (the split task should be counted as an
  // ongoing split).
  SleepForBgTaskIters(4);
  int num_split_tasks = 0;
  for (const auto& task : table_info->GetTasks()) {
    // These tasks will retry automatically until they succeed or fail.
    if (task->type() == server::MonitoredTaskType::kGetTabletSplitKey ||
        task->type() == server::MonitoredTaskType::kSplitTablet) {
      ++num_split_tasks;
    }
  }
  ASSERT_EQ(num_split_tasks, 1);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_tserver_get_split_key) = false;

  // Check that non-running child tablets count against the per-tserver split limit, and so only
  // one split is triggered.
  SleepForBgTaskIters(4);
  ASSERT_EQ(table_info->TabletCount(), 3);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_tablet_split_probability) = 0.0;

  ASSERT_OK(WaitForTabletSplitCompletion(3));
  ASSERT_NOK(WaitForTabletSplitCompletion(
      4,                                    // expected_non_split_tablets
      0,                                    // expected_split_tablets (default)
      0,                                    // num_replicas_online (default)
      client::kTableName,                   // table (default)
      false));                              // core_dump_on_failure

  // Add a 6th tserver. Tablet B should be load balanced onto the three tservers that do not have
  // replicas of tablet A, and should subsequently split. Note that the children of A should remain
  // on the same 3 tservers as A, since we don't move compacting tablets (this is important to
  // ensure that there are no ongoing splits on the 3 tservers that B is on).
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(6));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = true;
  ASSERT_OK(WaitFor([&]() {
    std::unordered_set<std::string> seen_tservers;
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_->id())) {
      seen_tservers.insert(peer->permanent_uuid());
    }
    LOG(INFO) << "seen_tservers.size(): " <<  seen_tservers.size();
    return seen_tservers.size() == 6;
  }, 30s * kTimeMultiplier, "Did not load balance in time."));

  ASSERT_OK(WaitForTabletSplitCompletion(4));
}

TEST_F(AutomaticTabletSplitITest, SizeRatio) {
  // Test for FLAGS_tablet_split_min_size_ratio.

  // Create 4 tables (1 RF-1 tablet each), so one tserver has 2 tablets and the other two have 1.
  // Write 4k and 2k rows to the tablets on the same tserver, and k rows to one of the other
  // tablets. After the largest tablet begins splitting:
  // - With the flag set to 1, the only split candidate we should consider is the 2k rows tablet,
  //   but it should not split because there is already an outstanding split on that tserver.
  // - With the flag set to <= 0.5, the 1k rows tablet should be able to split since:
  //    1. It is on a different tserver from the splitting tablet
  //    2. The ratio between the size of that tablet and the largest candidate is 1k / 2k = 0.5
  constexpr int kNumRowsPerBatch = 1000;
  constexpr int kNumInitialTablets = 1;
  constexpr int kNumTables = 4;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit_per_tserver) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 10000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  // This flag prevents split children from finishing compaction and thus becoming split candidates.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  // Only split the largest tablet to start.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_min_size_ratio) = 1;
  // Disable tablet splitting until we load the data.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  // Create the 4 tablets with 1 RF-1 tablet each.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 1;
  std::vector<client::TableHandle> table_handles;
  std::vector<client::YBTableName> table_names;
  for (int i = 0; i < kNumTables; ++i) {
    client::TableHandle table_handle;
    auto table_name =
        client::YBTableName(YQL_DATABASE_CQL, "my_keyspace", "test_table" + std::to_string(i));
    client::kv_table_test::CreateTable(
        client::Transactional::kTrue, kNumInitialTablets, client_.get(), &table_handle, table_name);

    table_handles.push_back(std::move(table_handle));
    table_names.push_back(std::move(table_name));
  }

  // Find which tservers have which tablets.
  auto catalog_mgr = ASSERT_RESULT(catalog_manager());
  std::unordered_map<TabletServerId, std::vector<int>> ts_to_table_indexes;
  for (int i = 0; i < kNumTables; ++i) {
    auto tablets = ASSERT_RESULT(catalog_mgr->GetTableInfo(table_handles[i]->id())->GetTablets());
    auto replica_map = tablets[0]->GetReplicaLocations();
    ts_to_table_indexes[replica_map->begin()->first].push_back(i);
  }

  // splitting_tablet will be the largest tablet, candidate1 will be the tablet on the same tserver
  // as the largest tablet, and candidate2 will be a tablet on another tserver.
  int splitting_table_idx  = -1;
  int candidate1_table_idx = -1;
  int candidate2_table_idx = -1;
  for (auto& [_, table_indexes] : ts_to_table_indexes) {
    if (table_indexes.size() == 2) {
      splitting_table_idx  = table_indexes[0];
      candidate1_table_idx = table_indexes[1];
    } else {
      candidate2_table_idx = table_indexes[0];
    }
  }
  // Sanity check to make sure all indexes have been set.
  ASSERT_GE(splitting_table_idx, 0);
  ASSERT_LT(splitting_table_idx, kNumTables);
  ASSERT_GE(candidate1_table_idx, 0);
  ASSERT_LT(candidate1_table_idx, kNumTables);
  ASSERT_GE(candidate2_table_idx, 0);
  ASSERT_LT(candidate2_table_idx, kNumTables);

  LOG(INFO) << "Splitting tablet table id: "       << table_handles[splitting_table_idx]->id()
            << ", same tserver tablet table id: "  << table_handles[candidate1_table_idx]->id()
            << ", other tserver tablet table id: " << table_handles[candidate2_table_idx]->id();

  // Write rows.
  ASSERT_OK(WriteRowsAndFlush(&table_handles[splitting_table_idx],  4 * kNumRowsPerBatch));
  ASSERT_OK(WriteRowsAndFlush(&table_handles[candidate1_table_idx], 2 * kNumRowsPerBatch));
  ASSERT_OK(WriteRowsAndFlush(&table_handles[candidate2_table_idx], kNumRowsPerBatch));

  // Wait for the SST sizes to be reported, then enable tablet splitting.
  SleepFor(FLAGS_tserver_heartbeat_metrics_interval_ms * 2ms);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

  // Wait for biggest tablet to start splitting.
  ASSERT_OK(WaitForTabletSplitCompletion(
      kNumInitialTablets + 1,             // expected_non_split_tablets
      0,                                  // expected_split_tablets (default)
      0,                                  // num_replicas_online (default)
      table_names[splitting_table_idx])); // table

  // Check that neither candidate splits. candidate1 cannot split without exceeding
  // FLAGS_outstanding_tablet_split_limit_per_tserver, and candidate2 is too small because
  // FLAGS_tablet_split_min_size_ratio = 1.
  SleepForBgTaskIters(2);
  table_handles[candidate1_table_idx]->RefreshPartitions(client_.get(), DoNothingStatusCB);
  table_handles[candidate2_table_idx]->RefreshPartitions(client_.get(), DoNothingStatusCB);
  ASSERT_EQ(table_handles[candidate1_table_idx]->GetPartitionCount(), 1);
  ASSERT_EQ(table_handles[candidate2_table_idx]->GetPartitionCount(), 1);

  // Lower the split size ratio so candidate 2 can split. The expected ratio between candidate1 and
  // candidate2 is actually 0.5, but set the value a bit lower here for test stability).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_min_size_ratio) = 0.3;
  ASSERT_OK(WaitForTabletSplitCompletion(
      kNumInitialTablets + 1,              // expected_non_split_tablets
      0,                                   // expected_split_tablets (default)
      0,                                   // num_replicas_online (default)
      table_names[candidate2_table_idx])); // table

  // TODO(asrivastava): Investigate why cluster verification and the MetaCache destructor sometimes
  // fails with a segfault in release mode without this sleep.
  SleepFor(2s);
}

TEST_F(AutomaticTabletSplitITest, DroppedTablesExcludedFromOutstandingSplitLimit) {
  constexpr int kNumRowsPerBatch = 1000;
  constexpr int kTabletSplitLimit = 1;
  constexpr int kNumInitialTablets = 1;

  // Limit the number of tablet splits.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = kTabletSplitLimit;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;

  SetNumTablets(kNumInitialTablets);
  CreateTable();
  ASSERT_OK(WriteRowsAndFlush(kNumRowsPerBatch));
  ASSERT_OK(WaitForTabletSplitCompletion(kNumInitialTablets + 1));

  client::TableHandle table2;
  auto table2_name = client::YBTableName(YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table_2");
  client::kv_table_test::CreateTable(
      client::Transactional(true), kNumInitialTablets, client_.get(), &table2, table2_name);
  ASSERT_OK(WriteRowsAndFlush(&table2, kNumRowsPerBatch));

  // The tablet should not split while the split for the first table is outstanding.
  SleepForBgTaskIters(2);
  ASSERT_NOK(WaitForTabletSplitCompletion(
      kNumInitialTablets + 1, // expected_non_split_tablets
      0,                      // expected_split_tablets (default)
      0,                      // num_replicas_online (default)
      table2_name,            // table
      false));                // core_dump_on_failure

  // After deleting the first table, its split should no longer be counted for an ongoing split, so
  // the second table's tablet should split.
  ASSERT_OK(client_->DeleteTable(client::kTableName));
  ASSERT_OK(WaitForTabletSplitCompletion(
      kNumInitialTablets + 1, // expected_non_split_tablets
      0,                      // expected_split_tablets (default)
      0,                      // num_replicas_online (default)
      table2_name));          // table
}

TEST_F(AutomaticTabletSplitITest, IncludeTasksInOutstandingSplits) {
  constexpr int kNumRowsPerBatch = 1000;
  constexpr int kInitialNumTablets = 2;

  // Only allow one tablet split to start.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 100;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_tserver_get_split_key) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_reject_delete_not_serving_tablet_rpc) = true;

  // Start with two tablets. Only one should be chosen for splitting, and it should stall (but be
  // counted as outstanding) until the pause is removed.
  SetNumTablets(kInitialNumTablets);
  CreateTable();
  ASSERT_OK(WriteRowsAndFlush(kNumRowsPerBatch));

  // Assert that the other tablet does not get split.
  SleepForBgTaskIters(2);
  ASSERT_NOK(WaitForTabletSplitCompletion(
      kInitialNumTablets + 1,               // expected_non_split_tablets
      1,                                    // expected_split_tablets
      0,                                    // num_replicas_online (default)
      client::kTableName,                   // table (default)
      false));                              // core_dump_on_failure

  // Allow no new splits. The stalled split task should resume after the pause is removed.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  auto& master = ASSERT_RESULT(GetLeaderMaster()).get();
  ASSERT_OK(master.tablet_split_manager().WaitUntilIdle());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_tserver_get_split_key) = false;
  ASSERT_OK(WaitForTabletSplitCompletion(kInitialNumTablets + 1, /* expected_non_split_tablets */
                                         1 /* expected_split_tablets */));
  SleepForBgTaskIters(2);
  ASSERT_NOK(WaitForTabletSplitCompletion(
      kInitialNumTablets + 2,               // expected_non_split_tablets
      1,                                    // expected_split_tablets
      0,                                    // num_replicas_online (default)
      client::kTableName,                   // table (default)
      false));                              // core_dump_on_failure
}

TEST_F(AutomaticTabletSplitITest, FailedSplitIsRestarted) {
  constexpr int kNumRowsPerBatch = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  // Fail the split on the tserver.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_tablet_split_probability) = 1;

  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndFlush(kNumRowsPerBatch));

  // The split should fail because of the test flag.
  SleepForBgTaskIters(2);
  ASSERT_NOK(WaitForTabletSplitCompletion(
      2,                                    // expected_non_split_tablets
      0,                                    // expected_split_tablets (default)
      0,                                    // num_replicas_online (default)
      client::kTableName,                   // table (default)
      false));                              // core_dump_on_failure

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_tablet_split_probability) = 0;
  ASSERT_OK(WaitForTabletSplitCompletion(2));
}

// Similar to the FailedSplitIsRestarted test, but crash instead.
TEST_F(AutomaticTabletSplitExternalMiniClusterITest, CrashedSplitIsRestarted) {
  constexpr int kNumRows = 1000;

  ASSERT_OK(cluster_->SetFlagOnMasters("tablet_split_low_phase_shard_count_per_node", "1"));
  ASSERT_OK(cluster_->SetFlagOnMasters("tablet_split_low_phase_size_threshold_bytes", "0"));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_fault_crash_after_registering_split_children", "1.0"));
  ASSERT_OK(cluster_->SetFlagOnTServers("rocksdb_disable_compactions", "true"));

  CreateSingleTablet();
  const TabletId tablet_id = ASSERT_RESULT(GetOnlyTestTabletId());
  ExternalMaster* master_leader = cluster_->GetLeaderMaster();
  ASSERT_OK(WriteRows(kNumRows, 1));

  // Sleep to wait for the transaction to be applied from intents.
  std::this_thread::sleep_for(2s);
  // Flush to ensure SST files are generated so splitting can occur.
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(i),
                                                    {tablet_id},
                                                    tserver::FlushTabletsRequestPB::FLUSH));
  }

  const auto kCrashTime = 10s;
  const auto sleep_time = (FLAGS_catalog_manager_bg_task_wait_ms +
                     FLAGS_tserver_heartbeat_metrics_interval_ms) * 2ms + kCrashTime;
  ASSERT_OK(WaitFor([&]() {
    return !master_leader->IsProcessAlive();
  }, sleep_time * kTimeMultiplier, "Waiting for master leader to crash after trying to split."));

  ASSERT_OK(master_leader->Restart());
  ASSERT_OK(WaitFor([&]() {
    return master_leader->IsProcessAlive();
  }, 30s * kTimeMultiplier, "Waiting for master leader to restart."));

  // These flags get cleared when we restart, so we need to set them again.
  ASSERT_OK(cluster_->SetFlag(master_leader, "tablet_split_low_phase_shard_count_per_node", "1"));
  ASSERT_OK(cluster_->SetFlag(master_leader, "tablet_split_low_phase_size_threshold_bytes", "0"));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto tablets = CHECK_RESULT(cluster_->ListTablets(cluster_->tablet_server(0)));
    int running_tablets = 0;
    for (const auto& tablet : tablets.status_and_schema()) {
      const auto& status = tablet.tablet_status();
      if (status.state() == tablet::RaftGroupStatePB::RUNNING &&
          status.table_id() == table_->id() &&
          status.tablet_data_state() == tablet::TABLET_DATA_READY) {
        ++running_tablets;
      }
    }
    return running_tablets == 2;
  }, split_completion_timeout_sec_, "Waiting for split children to be running."));
}

class  AutomaticTabletSplitAddServerITest: public AutomaticTabletSplitITest {
 public:
  void SetUp() override {
    AutomaticTabletSplitITest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
    // Skip post split compaction to protect child tablets from being split.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_post_split_compaction) = true;
  }

  void BuildTServerMap() {
    master::MasterClusterProxy master_proxy(
        proxy_cache_.get(), cluster_->mini_master()->bound_rpc_addr());
    ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, proxy_cache_.get()));
  }

  void AddTabletToNewTServer(const TabletId& tablet_id,
                         const std::string& leader_id,
                         consensus::PeerMemberType peer_type) {
    const auto new_ts = cluster_->num_tablet_servers();
    ASSERT_OK(cluster_->AddTabletServer());
    ASSERT_OK(cluster_->WaitForTabletServerCount(new_ts + 1));
    const auto new_tserver = cluster_->mini_tablet_server(new_ts)->server();
    const auto new_ts_id = new_tserver->permanent_uuid();
    LOG(INFO) << "Added new tserver: " << new_ts_id;

    auto* const catalog_mgr = ASSERT_RESULT(catalog_manager());
    const auto tablet = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
    BuildTServerMap();
    const auto leader = ts_map_[leader_id].get();

    // Replicate to the new tserver.
    ASSERT_OK(itest::AddServer(
        leader, tablet_id, ts_map_[new_ts_id].get(), peer_type, boost::none, kRpcTimeout));

    // Wait for config change reported to master.
    ASSERT_OK(itest::WaitForTabletConfigChange(tablet, new_ts_id, consensus::ADD_SERVER));

    // Wait until replicated to new tserver.
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return itest::GetNumTabletsOfTableOnTS(new_tserver, table_->id()) == 1;
    }, 20s * kTimeMultiplier, "Waiting for new tserver having one tablet."));
  }

  Result<size_t> GetLeaderIdx(const master::TabletInfoPtr tablet) {
    const auto leader_ts_desc = CHECK_RESULT(tablet->GetLeader());
    for (size_t idx = 0; idx < cluster_->num_tablet_servers(); ++idx) {
      const auto tserver = cluster_->mini_tablet_server(idx)->server();
      if (tserver->permanent_uuid() == leader_ts_desc->permanent_uuid()) {
        return idx;
      }
    }
    return STATUS(NotFound, Format("No tserver hosts leader of tablet $0", tablet->id()));
  }

  itest::TabletServerMap ts_map_;
};

TEST_F(AutomaticTabletSplitAddServerITest, DoNotSplitTabletDoingRBS) {
  // Test should not schedule automatic tablet split on tablet in progress of remote bootstrap.

  CreateSingleTablet();
  ASSERT_OK(WriteRows());

  BuildTServerMap();

  const auto catalog_mgr = ASSERT_RESULT(catalog_manager());
  const auto tablet = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto tablet_id = tablet->id();
  const auto leader_idx = ASSERT_RESULT(GetLeaderIdx(tablet));
  const auto leader_ts = cluster_->mini_tablet_server(leader_idx)->server();
  const auto leader_id = leader_ts->permanent_uuid();
  const auto follower_idx = (leader_idx + 1) % 3;
  const auto follower_id = cluster_->mini_tablet_server(follower_idx)->server()->permanent_uuid();
  LOG(INFO) << "Source tablet id " << tablet_id;

  // Remove tablet from follower to let tablet live replicas == table replication factor
  // after adding a new tserver.
  ASSERT_OK(itest::RemoveServer(
      ts_map_[leader_id].get(), tablet_id, ts_map_[follower_id].get(),
      boost::none, kRpcTimeout));
  ASSERT_OK(itest::WaitForTabletConfigChange(tablet, follower_id, consensus::REMOVE_SERVER));

  // Start rbs on it but pause before downloading wal.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_rbs_before_download_wal) = true;

  // Create a new tserver and add tablet to it. By adding it as PRE_VOTER, it will be promoted
  // to be VOTER and will participate in raft consensus.
  AddTabletToNewTServer(tablet_id, leader_id, consensus::PeerMemberType::PRE_VOTER);

  const auto new_ts_idx = cluster_->num_tablet_servers() - 1;
  const auto new_tserver = cluster_->mini_tablet_server(new_ts_idx);
  const auto new_ts_id = new_tserver->server()->permanent_uuid();

  // Wait for the first heartbeat from new tserver to update the replica state to NOT_STARTED.
  ASSERT_OK(itest::WaitUntilTabletInState(tablet, new_ts_id, tablet::NOT_STARTED));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

  // Fail to split because tablet is doing RBS and under replication.
  ASSERT_NOK(master::CheckLiveReplicasForSplit(
      tablet_id, *tablet->GetReplicaLocations(), FLAGS_replication_factor));
  ASSERT_EQ(itest::GetNumTabletsOfTableOnTS(leader_ts, table_->id()), 1);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_rbs_before_download_wal) = false;

  // Should succeed to split since RBS is done.
  ASSERT_OK(WaitForTabletSplitCompletion(2));
}

TEST_F(AutomaticTabletSplitAddServerITest, DoNotSplitOverReplicatedTablet) {
  // Test should not schedule automatic tablet split on over-replicated tablet.

  CreateSingleTablet();
  ASSERT_OK(WriteRows());

  auto* const catalog_mgr = ASSERT_RESULT(catalog_manager());
  const auto tablet = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto tablet_id = tablet->id();
  const auto leader_idx = ASSERT_RESULT(GetLeaderIdx(tablet));
  const auto leader_ts = cluster_->mini_tablet_server(leader_idx)->server();
  const auto leader_id = leader_ts->permanent_uuid();
  const auto follower_idx = (leader_idx + 1) % 3;
  const auto follower_id = cluster_->mini_tablet_server(follower_idx)->server()->permanent_uuid();
  LOG(INFO) << "Source tablet id " << tablet_id;

  // Create a new tserver and add tablet to it. By adding it as PRE_VOTER, it will be promoted
  // to be VOTER and will participate in raft consensus.
  AddTabletToNewTServer(tablet_id, leader_id, consensus::PeerMemberType::PRE_VOTER);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

  // Should fail to split because of over replication.
  ASSERT_NOK(master::CheckLiveReplicasForSplit(
      tablet_id, *tablet->GetReplicaLocations(), FLAGS_replication_factor));
  ASSERT_EQ(itest::GetNumTabletsOfTableOnTS(leader_ts, table_->id()), 1);

  // Remove tablet from follower to let tablet live replicas == table replication factor.
  ASSERT_OK(itest::RemoveServer(
      ts_map_[leader_id].get(), tablet_id,
      ts_map_[follower_id].get(), boost::none, kRpcTimeout));
  ASSERT_OK(itest::WaitForTabletConfigChange(tablet, follower_id, consensus::REMOVE_SERVER));

  // Should succeed to split since live replicas = 3 after RemoveServer.
  ASSERT_OK(WaitForTabletSplitCompletion(2));
}

TEST_F(AutomaticTabletSplitAddServerITest, SplitTabletWithReadReplica) {
  // Test split on tablet with 3 live replicas and one read replica,
  // it should not be treated as over-replicated and succeed.

  CreateSingleTablet();
  ASSERT_OK(WriteRows());

  const auto catalog_mgr = ASSERT_RESULT(catalog_manager());
  const auto tablet = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));
  const auto tablet_id = tablet->id();
  const auto leader_idx = ASSERT_RESULT(GetLeaderIdx(tablet));
  const auto leader_ts = cluster_->mini_tablet_server(leader_idx)->server();
  const auto leader_id = leader_ts->permanent_uuid();
  LOG(INFO) << "Source tablet id " << tablet_id;

  // Create a new tserver and add tablet to it By adding it as PRE_OBSERVER,
  // it will be promoted to be OBSERVER after RBS is done and will be a read replica.
  AddTabletToNewTServer(tablet_id, leader_id, consensus::PeerMemberType::PRE_OBSERVER);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

  // Should succeed to split because read replica should not cause over replication.
  ASSERT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets = */ 2,
                                         /* expected_split_tablets = */ 0,
                                         /* num_replicas_online = */ 4));
}

class TabletSplitSingleServerITest : public TabletSplitITest {
 protected:
  int64_t GetRF() override { return 1; }

  Result<tablet::TabletPeerPtr> GetSingleTabletLeaderPeer() {
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected only a single tablet leader.");
    return peers.at(0);
  }

  Status AlterTableSetDefaultTTL(int ttl_sec) {
    const auto table_name = table_.name();
    auto alterer = client_->NewTableAlterer(table_name);
    alterer->wait(true);
    TableProperties table_properties;
    table_properties.SetDefaultTimeToLive(ttl_sec * MonoTime::kMillisecondsPerSecond);
    alterer->SetTableProperties(table_properties);
    return alterer->Alter();
  }

  Status TestSplitBeforeParentDeletion(bool hide_only);

  void TestRetryableWrite();
};

// Start tablet split, create Index to start backfill while split operation in progress
// and check backfill state.
TEST_F(TabletSplitSingleServerITest, TestBackfillDuringSplit) {
  // TODO(#11695) -- Switch this back to a TabletServerITest
  constexpr auto kNumRows = 10000UL;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_apply_tablet_split_inject_delay_ms) = 200 * kTimeMultiplier;

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
    auto rows_count = CountRows(NewSession(), index_);
    if (!rows_count.ok()) {
      return false;
    }
    return *rows_count == kNumRows;
  }, 30s * kTimeMultiplier, "Waiting for backfill index"));
}

// Create Index to start backfill, check split is not working while backfill in progress
// and check backfill state.
TEST_F(TabletSplitSingleServerITest, TestSplitDuringBackfill) {
  // TODO(#11695) -- Switch this back to a TabletServerITest
  constexpr auto kNumRows = 10000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_slowdown_backfill_alter_table_rpcs_ms) = 200 * kTimeMultiplier;

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
  auto expected_middle_key_hash = CHECK_RESULT(dockv::DocKey::DecodeHash(middle_key));

  // Send RPC.
  auto resp = ASSERT_RESULT(SendTServerRpcSyncGetSplitKey(source_tablet_id));

  // Validate response.
  CHECK(!resp.has_error()) << resp.error().DebugString();
  auto decoded_split_key_hash = CHECK_RESULT(dockv::DocKey::DecodeHash(resp.split_encoded_key()));
  CHECK_EQ(decoded_split_key_hash, expected_middle_key_hash);
  auto decoded_partition_key_hash = dockv::PartitionSchema::DecodeMultiColumnHashValue(
      resp.split_partition_key());
  CHECK_EQ(decoded_partition_key_hash, expected_middle_key_hash);
}

TEST_F(TabletSplitSingleServerITest, SplitKeyNotSupportedForTTLTablets) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 100_KB;

  constexpr auto kNumRows = kDefaultNumRows;

  CreateSingleTablet();
  ASSERT_RESULT(WriteRowsAndFlush(kNumRows));

  const auto source_tablet_id =
      ASSERT_RESULT(GetSingleTestTabletInfo(ASSERT_RESULT(catalog_manager())))->id();

  // Flush tablet and directly compute expected middle key.
  auto tablet_peer = ASSERT_RESULT(GetSingleTabletLeaderPeer());
  auto tablet = tablet_peer->shared_tablet();
  ASSERT_OK(tablet_peer->shared_tablet()->Flush(tablet::FlushMode::kSync));
  ASSERT_OK(tablet->ForceManualRocksDBCompact());

  auto resp = ASSERT_RESULT(SendTServerRpcSyncGetSplitKey(source_tablet_id));
  EXPECT_FALSE(resp.has_error());

  // Alter the table with a table TTL, and call GetSplitKey RPC, expecting a
  // "not supported" response.
  // Amount of time for the TTL is irrelevant, so long as it's larger than 0.
  ASSERT_OK(AlterTableSetDefaultTTL(1));

  resp = ASSERT_RESULT(SendTServerRpcSyncGetSplitKey(source_tablet_id));

  // Validate response
  EXPECT_TRUE(resp.has_error());
  EXPECT_EQ(resp.error().code(),
      tserver::TabletServerErrorPB_Code::TabletServerErrorPB_Code_TABLET_SPLIT_DISABLED_TTL_EXPIRY);
  EXPECT_TRUE(resp.error().has_status());
  EXPECT_TRUE(resp.error().status().has_message());
  EXPECT_EQ(resp.error().status().code(),
            yb::AppStatusPB::ErrorCode::AppStatusPB_ErrorCode_NOT_SUPPORTED);
}

TEST_F(TabletSplitSingleServerITest, MaxFileSizeTTLTabletOnlyValidForManualSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 100_KB;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;

  constexpr auto kNumRows = kDefaultNumRows;

  CreateSingleTablet();
  ASSERT_RESULT(WriteRowsAndFlush(kNumRows));

  auto& master = ASSERT_RESULT(GetLeaderMaster()).get();
  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());
  auto* split_manager = &master.tablet_split_manager();
  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(catalog_mgr));

  // Requires a metrics heartbeat to get max_file_size_for_compaction flag to master.
  auto ts_desc = ASSERT_RESULT(source_tablet_info->GetLeader());
  EXPECT_OK(WaitFor([&]() -> Result<bool> {
      return ts_desc->uptime_seconds() > 0 && ts_desc->get_disable_tablet_split_if_default_ttl();
    }, 10s * kTimeMultiplier, "Wait for TServer to report metrics."));

  // Candidate tablet should still be valid since default TTL not enabled.
  ASSERT_OK(split_manager->ValidateSplitCandidateTablet(*source_tablet_info,
                                                        nullptr /* parent */));

  // Alter the table with a table TTL, at which point tablet should no longer be valid
  // for tablet splitting.
  // Amount of time for the TTL is irrelevant, so long as it's larger than 0.
  ASSERT_OK(AlterTableSetDefaultTTL(1));
  ASSERT_NOK(split_manager->ValidateSplitCandidateTablet(*source_tablet_info,
                                                        nullptr /* parent */));

  // Tablet should still be a valid candidate if ignore_ttl_validation is set to true
  // (e.g. for manual tablet splitting).
  ASSERT_OK(split_manager->ValidateSplitCandidateTablet(
      *source_tablet_info,
      nullptr, /* parent */
      master::IgnoreTtlValidation::kTrue,
      master::IgnoreDisabledList::kTrue));
}

TEST_F(TabletSplitSingleServerITest, AutoSplitNotValidOnceCheckedForTtl) {
  const auto kSecondsBetweenChecks = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_prevent_split_for_ttl_tables_for_seconds)
      = kSecondsBetweenChecks;

  constexpr auto kNumRows = kDefaultNumRows;

  CreateSingleTablet();
  ASSERT_RESULT(WriteRowsAndFlush(kNumRows));

  auto& master = ASSERT_RESULT(GetLeaderMaster()).get();
  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());
  auto* split_manager = &master.tablet_split_manager();
  auto table_info = ASSERT_NOTNULL(catalog_mgr->GetTableInfo(table_->id()));

  // Candidate table should start as a valid split candidate.
  ASSERT_OK(split_manager->ValidateSplitCandidateTable(table_info));

  // State that table should not be split for the next 1 second.
  // Candidate table should no longer be valid.
  split_manager->DisableSplittingForTtlTable(table_->id());
  ASSERT_NOK(split_manager->ValidateSplitCandidateTable(table_info));

  // After 2 seconds, table is a valid split candidate again.
  SleepFor(kSecondsBetweenChecks * 2s);
  ASSERT_OK(split_manager->ValidateSplitCandidateTable(table_info));

  // State again that table should not be split for the next 1 second.
  // Candidate table should still be a valid candidate if ignore_disabled_list
  // is true (e.g. in the case of manual tablet splitting).
  split_manager->DisableSplittingForTtlTable(table_->id());
  ASSERT_OK(split_manager->ValidateSplitCandidateTable(table_info,
      master::IgnoreDisabledList::kTrue));
}

TEST_F(TabletSplitSingleServerITest, ScheduledFullCompactionsDoNotBlockSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  const HybridTime now = clock_->Now();
  constexpr auto kNumRows = kDefaultNumRows;
  auto tserver = cluster_->mini_tablet_server(0)->server();
  auto compact_manager = tserver->tablet_manager()->full_compaction_manager();
  // Set compaction frequency to > 0.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) = 24;
  // Hold up full compaction before split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;

  // Schedule full compaction on a single tablet.
  CreateSingleTablet();
  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  compact_manager->ScheduleFullCompactions();
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 1);

  ASSERT_OK(SplitTabletAndValidate(split_hash_code, kNumRows));

  // Split and post-split compactions have finished.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
  ASSERT_OK(WaitForTestTableTabletPeersPostSplitCompacted(15s * kTimeMultiplier));

  // Check that the metadata for all tablets got updated.
  for (auto peer : ASSERT_RESULT(ListTestTableActiveTabletPeers())) {
    EXPECT_GE(
        HybridTime(peer->shared_tablet()->metadata()->last_full_compaction_time()), now);
  }
}

TEST_F(TabletSplitSingleServerITest, PostSplitCompactionsBlockScheduledFullCompactions) {
  constexpr auto kNumRows = kDefaultNumRows;
  auto tserver = cluster_->mini_tablet_server(0)->server();
  auto compact_manager = tserver->tablet_manager()->full_compaction_manager();
  // Set compaction frequency to > 0.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) = 24;

  // Hold up post-split compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;
  ASSERT_OK(CreateSingleTabletAndSplit(kNumRows));

  // Attempt to schedule full compactions, but don't expect any to be scheduled.
  compact_manager->ScheduleFullCompactions();
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 0);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
  ASSERT_OK(WaitForTestTableTabletPeersPostSplitCompacted(15s * kTimeMultiplier));

  // Try to schedule full compactions again, but shouldn't actually schedule any because
  // the post-split compactions finished too recently.
  compact_manager->ScheduleFullCompactions();
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 0);

  // Change the compaction frequency to seconds, and try to schedule compactions again.
  const auto short_frequency = MonoDelta::FromSeconds(1);
  SleepFor(short_frequency);
  compact_manager->TEST_DoScheduleFullCompactionsWithManualValues(short_frequency, 0);
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 2);
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
      auto resp = ASSERT_RESULT(SendTServerRpcSyncGetSplitKey(peer->tablet_id()));

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

  // If the parent tablet has been cleaned up or is still being cleaned up, this should trigger
  // a Not Found error or a Not Running error correspondingly.
  resp = ASSERT_RESULT(send_split_request());
  EXPECT_TRUE(resp.has_error());
  EXPECT_TRUE(
      StatusFromPB(resp.error().status()).IsNotFound() ||
      resp.error().code() == tserver::TabletServerErrorPB::TABLET_NOT_FOUND ||
      resp.error().code() == tserver::TabletServerErrorPB::TABLET_NOT_RUNNING)
      << resp.error().DebugString();
}

Status TabletSplitSingleServerITest::TestSplitBeforeParentDeletion(bool hide_only) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_validate_all_tablet_candidates) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  const int kNumRows = 1000;
  CreateSingleTablet();
  if (hide_only) {
    auto snapshot_util = std::make_unique<client::SnapshotTestUtil>();
    snapshot_util->SetProxy(&client_->proxy_cache());
    snapshot_util->SetCluster(cluster_.get());
    VERIFY_RESULT(
      snapshot_util->CreateSchedule(table_, kTableName.namespace_type(),
                                    kTableName.namespace_name()));
}

  const auto split_hash_code = VERIFY_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));
  const TabletId parent_id = VERIFY_RESULT(SplitTabletAndValidate(split_hash_code, kNumRows));
  auto child_ids = ListActiveTabletIdsForTable(cluster_.get(), table_->id());

  auto resp = VERIFY_RESULT(SendMasterRpcSyncSplitTablet(*child_ids.begin()));
  SCHECK(resp.has_error(), RuntimeError,
         "Splitting should fail while parent tablet is not hidden / deleted.");

  // Allow parent tablet to be deleted, and verify that the child tablet can be split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = false;
  auto catalog_mgr = VERIFY_RESULT(catalog_manager());
  RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
    auto parent = catalog_mgr->GetTabletInfo(parent_id);
    if (!parent.ok()) {
      if (parent.status().IsNotFound()) {
        return true;
    }
      return parent.status();
  }
    auto parent_lock = parent.get()->LockForRead();
    return hide_only ? parent_lock->is_hidden() : parent_lock->is_deleted();
  }, 10s * kTimeMultiplier, "Wait for parent to be hidden / deleted."));
  resp = VERIFY_RESULT(SendMasterRpcSyncSplitTablet(*child_ids.begin()));
  SCHECK(!resp.has_error(), RuntimeError,
         "Splitting should succeed once parent tablet is hidden / deleted.");
  return Status::OK();
  }

TEST_F(TabletSplitSingleServerITest, SplitBeforeParentDeleted) {
  ASSERT_OK(TestSplitBeforeParentDeletion(false /* hide_only */));
}

TEST_F(TabletSplitSingleServerITest, SplitBeforeParentHidden) {
  ASSERT_OK(TestSplitBeforeParentDeletion(true /* hide_only */));
}

// Test scenario of GH issue: https://github.com/yugabyte/yugabyte-db/issues/14005
// 1. Parent tablet leader received and replicated the WRITE_OP 1
//    but the client didn't get response.
// 2. The client retried WRITE_OP 1 and gets TABLET_SPLIT.
// 3. The client prepared WRITE_OP 2 to the child tablet and cause duplication.
void TabletSplitSingleServerITest::TestRetryableWrite() {
  auto kNumRows = 2;
  CreateSingleTablet();

  const auto split_hash_code = ASSERT_RESULT(WriteRowsAndGetMiddleHashCode(kNumRows));

  auto peer = ASSERT_RESULT(GetSingleTabletLeaderPeer());
  ASSERT_OK(peer->log()->AllocateSegmentAndRollOver());

  if (GetAtomicFlag(&FLAGS_enable_flush_retryable_requests)) {
    // Wait retryable requests flushed to disk.
    ASSERT_OK(WaitFor([&] {
      return peer->TEST_HasBootstrapStateOnDisk();
    }, 10s, "retryable requests flushed to disk"));
  }

#ifndef NDEBUG
  SyncPoint::GetInstance()->LoadDependency({
      {"AsyncRpc::Finished:SetTimedOut:1",
       "TabletSplitSingleServerITest::TestRetryableWrite:WaitForSetTimedOut"},
      {"TabletSplitSingleServerITest::TestRetryableWrite:RowDeleted",
       "AsyncRpc::Finished:SetTimedOut:2"}
  });

  SyncPoint::GetInstance()->EnableProcessing();
#endif

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = true;
  // Start a new thread for writing a new row.
  std::thread th([&] {
    CHECK_OK(WriteRow(NewSession(), kNumRows + 1, kNumRows + 1));
  });

  // Wait for 1.4 is replicated.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(peer->GetRaftConsensus())->GetLastCommittedOpId().index ==
               kNumRows + 2;
      },
      10s, "the third row is replicated"));

  TEST_SYNC_POINT("TabletSplitSingleServerITest::TestRetryableWrite:WaitForSetTimedOut");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = false;

  ASSERT_OK(SplitSingleTablet(split_hash_code));
  ASSERT_OK(WaitForTabletSplitCompletion(2, 0, 1));

  // Delete the new row, if retryable request causes duplication, will write it back.
  ASSERT_OK(DeleteRow(NewSession(), kNumRows + 1));

  TEST_SYNC_POINT("TabletSplitSingleServerITest::TestRetryableWrite:RowDeleted");

  th.join();

  // The new row should be deleted.
  ASSERT_OK(CheckRowsCount(kNumRows));

#ifndef NDEBUG
     SyncPoint::GetInstance()->DisableProcessing();
     SyncPoint::GetInstance()->ClearTrace();
#endif // NDEBUG
}

TEST_F(TabletSplitSingleServerITest, TestRetryableWrite) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_copy_retryable_requests_from_parent) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = false;
  TestRetryableWrite();
}

TEST_F(TabletSplitSingleServerITest, TestRetryableWriteWithCopyingFromParent) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_copy_retryable_requests_from_parent) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = false;
  TestRetryableWrite();
}

TEST_F(TabletSplitSingleServerITest, TestRetryableWriteWithPersistedStructure) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_copy_retryable_requests_from_parent) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
  TestRetryableWrite();
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
  req.set_bootstrap_source_peer_uuid("abcdefg");
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

class TabletSplitExternalMiniClusterCrashITest :
    public TabletSplitExternalMiniClusterITest,
    public testing::WithParamInterface<std::string> {
 public:
  void TestCrashServer(ExternalTabletServer *const server_to_crash, const TabletId& tablet_id) {
    ASSERT_OK(cluster_->SetFlag(
        server_to_crash, GetParam(), "true"));

    CHECK_OK(SplitTablet(tablet_id));
    CHECK_OK(cluster_->WaitForTSToCrash(server_to_crash));
  }
};

TEST_P(TabletSplitExternalMiniClusterCrashITest, CrashLeaderTest) {
  // Test crashing tserver hosting the tablet leader at different point
  // during tablet splitting and restart.
  CreateSingleTablet();
  CHECK_OK(WriteRowsAndFlush());

  const auto tablet_id = CHECK_RESULT(GetOnlyTestTabletId());
  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  auto* const leader = cluster_->tablet_server(leader_idx);

  TestCrashServer(leader, tablet_id);

  CHECK_OK(leader->Restart());
  ASSERT_OK(cluster_->WaitForTabletsRunning(leader, 20s * kTimeMultiplier));

  ASSERT_OK(WaitForTabletsExcept(2, leader_idx, tablet_id));

  // Wait for both child tablets have leaders elected.
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(
      cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>(), &cluster_->proxy_cache()));
  auto tablet_ids = CHECK_RESULT(GetTestTableTabletIds());
  for (const auto& id : tablet_ids) {
    if (id != tablet_id) {
      itest::TServerDetails *leader_ts = nullptr;
      ASSERT_OK(itest::FindTabletLeader(ts_map, id, 20s * kTimeMultiplier, &leader_ts));
    }
  }

  // Check number of rows is correct after recovery.
  ASSERT_OK(CheckRowsCount(kDefaultNumRows));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows(kDefaultNumRows, kDefaultNumRows + 1).ok();
  }, 20s * kTimeMultiplier, "Write rows after faulted leader resurrection."));

  ASSERT_OK(CheckRowsCount(kDefaultNumRows * 2));
  CheckTableKeysInRange(kDefaultNumRows * 2);
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
      for (size_t i = 0; i < node_2_metas.size(); ++i) {
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
  // TODO: Wait for the tablet to be replicated before we send a split request.
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
  ASSERT_OK(cluster_->SetFlagOnMasters("unresponsive_ts_rpc_retry_limit", "100"));
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

class TabletSplitRbsTest : public TabletSplitExternalMiniClusterITest {
 protected:
  void SetFlags() override {
    TabletSplitExternalMiniClusterITest::SetFlags();

    // Disable leader moves.
    mini_cluster_opt_.extra_master_flags.push_back("--load_balancer_max_concurrent_moves=0");
  }
};

// TODO(tsplit): should be RemoteBootstrapsFromNodeWithNotAppliedSplitOp, but not renaming now to
// have common test results history.
TEST_F_EX(
    TabletSplitExternalMiniClusterITest, RemoteBootstrapsFromNodeWithUncommittedSplitOp,
    TabletSplitRbsTest) {
  // If a new tablet is created and split with one node completely uninvolved, then when that node
  // rejoins it will have to do a remote bootstrap.

  const auto kWaitForTabletsRunningTimeout = 20s * kTimeMultiplier;
  const auto server_to_bootstrap_idx = 0;

  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(
      cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>(), &cluster_->proxy_cache()));

  CreateSingleTablet();
  const auto source_tablet_id = CHECK_RESULT(GetOnlyTestTabletId());
  LOG(INFO) << "Source tablet ID: " << source_tablet_id;

  // Delete tablet on server_to_bootstrap and shutdown server, so it will be RBSed after SPLIT_OP
  // is Raft-committed on the leader.
  auto* const server_to_bootstrap = cluster_->tablet_server(server_to_bootstrap_idx);
  auto* ts_details_to_bootstrap = ts_map[server_to_bootstrap->uuid()].get();
  ASSERT_OK(
      itest::WaitUntilTabletRunning(ts_details_to_bootstrap, source_tablet_id, kRpcTimeout));
  // We might need to retry attempt to delete tablet if tablet state transition is not yet
  // completed even after it is running.
  ASSERT_OK(WaitFor(
      [&source_tablet_id, ts_details_to_bootstrap]() -> Result<bool> {
        const auto s = itest::DeleteTablet(
            ts_details_to_bootstrap, source_tablet_id, tablet::TABLET_DATA_TOMBSTONED, boost::none,
            kRpcTimeout);
        if (s.ok()) {
          return true;
        }
        if (s.IsAlreadyPresent()) {
          return false;
        }
        return s;
      },
      10s * kTimeMultiplier, Format("Delete parent tablet on $0", server_to_bootstrap->uuid())));
  server_to_bootstrap->Shutdown();
  ASSERT_OK(cluster_->WaitForTSToCrash(server_to_bootstrap));

  ASSERT_OK(WriteRows());

  // With enable_leader_failure_detection=false when creating child tablets on leader and
  // other_follower, Only child tablet peer on server_to_bootstrap can detect leader failure.
  // If hinted leader is not disabled, it's possible that:
  // 1) child tablet leader is elected on other_follower
  // 2) other_follower crash
  // 3) server_to_bootstrap detects leader failure, but its log is behind old leader's log,
  //    so we can't elect a leader for child tablet.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->GetLeaderMaster(), "use_create_table_leader_hint", "false"));

  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    auto* ts = cluster_->tablet_server(i);
    if (i != server_to_bootstrap_idx) {
      ASSERT_OK(cluster_->WaitForAllIntentsApplied(ts, 15s * kTimeMultiplier));
      ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(
          ts, {source_tablet_id}, tserver::FlushTabletsRequestPB::FLUSH));
      // Prevent leader changes.
      ASSERT_OK(cluster_->SetFlag(ts, "enable_leader_failure_detection", "false"));
    }
  }

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(source_tablet_id));

  const auto other_follower_idx = 3 - leader_idx - server_to_bootstrap_idx;
  auto* const other_follower = cluster_->tablet_server(other_follower_idx);

  auto* const leader = cluster_->tablet_server(leader_idx);

  // We need to pause leader on UpdateMajorityReplicated for SPLIT_OP, not for previous OPs, so
  // wait for tablet to be quiet.
  ASSERT_OK(WaitTServerToBeQuietOnTablet(ts_map[leader->uuid()].get(), source_tablet_id));

  // We want the leader to not apply the split operation for now, but commit it, so RBSed node
  // replays it.
  ASSERT_OK(cluster_->SetFlag(
      leader, "TEST_pause_update_majority_replicated", "true"));

  ASSERT_OK(SplitTablet(source_tablet_id));

  LOG(INFO) << "Restarting server to bootstrap: " << server_to_bootstrap->uuid();
  // Delaying RBS WAL downloading to start after leader marked SPLIT_OP as Raft-committed.
  // By the time RBS starts to download WAL, RocksDB is already downloaded, so flushed op ID will
  // be less than SPLIT_OP ID, so it will be replayed by server_to_bootstrap.
  LogWaiter log_waiter(
      server_to_bootstrap,
      source_tablet_id + ": Pausing due to flag TEST_pause_rbs_before_download_wal");
  ASSERT_OK(server_to_bootstrap->Restart(
      ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      {std::make_pair("TEST_pause_rbs_before_download_wal", "true")}));
  ASSERT_OK(log_waiter.WaitFor(30s * kTimeMultiplier));

  // Resume leader to mark SPLIT_OP as Raft-committed.
  ASSERT_OK(cluster_->SetFlag(
      leader, "TEST_pause_update_majority_replicated", "false"));

  // Wait for SPLIT_OP to apply at leader.
  ASSERT_OK(WaitForTabletsExcept(2, leader_idx, source_tablet_id));

  // Resume RBS.
  ASSERT_OK(cluster_->SetFlag(
      server_to_bootstrap, "TEST_pause_rbs_before_download_wal", "false"));

  // Wait until RBS replays SPLIT_OP.
  ASSERT_OK(WaitForTabletsExcept(2, server_to_bootstrap_idx, source_tablet_id));
  ASSERT_OK(cluster_->WaitForTabletsRunning(server_to_bootstrap, kWaitForTabletsRunningTimeout));

  ASSERT_OK(cluster_->WaitForTabletsRunning(leader, kWaitForTabletsRunningTimeout));

  ASSERT_OK(WaitForTabletsExcept(2, other_follower_idx, source_tablet_id));
  ASSERT_OK(cluster_->WaitForTabletsRunning(other_follower, kWaitForTabletsRunningTimeout));

  ASSERT_OK(cluster_->SetFlagOnTServers("enable_leader_failure_detection", "true"));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after split."));

  other_follower->Shutdown();
  ASSERT_OK(cluster_->WaitForTSToCrash(other_follower));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return WriteRows().ok();
  }, 20s * kTimeMultiplier, "Write rows after requiring bootstraped node consensus."));

  ASSERT_OK(other_follower->Restart());
}

class TabletSplitReplaceNodeITest : public TabletSplitExternalMiniClusterITest {
 protected:
  void SetFlags() override {
    TabletSplitExternalMiniClusterITest::SetFlags();

    for (const auto& tserver_flag : std::initializer_list<std::string>{
        // We want to test behavior of the source tablet, so setting up to skip deleting it.
        "--TEST_skip_deleting_split_tablets=true",
        // Reduce follower_unavailable_considered_failed_sec, so offline tserver is evicted
        // from Raft group faster.
        Format("--follower_unavailable_considered_failed_sec=$0", 5 * kTimeMultiplier)
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
        // To speed up test in case of intermittent failures due to leader re-elections.
        "--retrying_ts_rpc_max_delay_ms=1000",
      }) {
      mini_cluster_opt_.extra_master_flags.push_back(master_flag);
    }
  }
};

TEST_F_EX(
    TabletSplitExternalMiniClusterITest, ReplaceNodeForParentTablet, TabletSplitReplaceNodeITest) {
  constexpr auto kReplicationFactor = 3;
  constexpr auto kNumRows = kDefaultNumRows;

  CreateSingleTablet();
  ASSERT_OK(WriteRows(kNumRows));
  const auto source_tablet_id = ASSERT_RESULT(GetOnlyTestTabletId());
  LOG(INFO) << "Source tablet ID: " << source_tablet_id;

  // Wait until WRITE_OP is replicated across all peers.
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  ASSERT_OK(itest::WaitForServerToBeQuiet(10s * kTimeMultiplier, ts_map, source_tablet_id,
      /* last_logged_opid = */ nullptr, itest::MustBeCommitted::kTrue));

  constexpr auto offline_ts_idx = 0;
  auto* offline_ts = cluster_->tablet_server(offline_ts_idx);
  offline_ts->Shutdown();
  LOG(INFO) << "Shutdown completed for tserver: " << offline_ts->uuid();
  const auto offline_ts_id = offline_ts->uuid();

  for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    auto* ts = cluster_->tablet_server(ts_idx);
    if (ts->IsProcessAlive()) {
      ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(
          ts, {source_tablet_id}, tserver::FlushTabletsRequestPB::FLUSH));
      ASSERT_OK(WaitForAnySstFiles(*ts, source_tablet_id));
    }
  }
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
        if (source_tablet_replicas.size() != kReplicationFactor) {
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

  LOG(INFO) << "Waiting for parent + child tablets on all online tservers...";

  // Wait for the split to be completed on all online tservers.
  for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    if (cluster_->tablet_server(ts_idx)->IsProcessAlive()) {
      ASSERT_OK(WaitForTablets(3, ts_idx));
    }
  }

  // Restarting offline_ts, because ClusterVerifier requires all tservers to be online.
  ASSERT_OK(offline_ts->Start());
  // Wait for tablet moves to be completed and have 1 parent + 2 child tablets with
  // kReplicationFactor replicas each.
  const auto expected_tablet_replicas = (1 + 2) * kReplicationFactor;
  std::vector<std::set<TabletId>> test_tablets_by_tserver;
  size_t num_test_tablet_replicas = 0;
  auto status = LoggedWaitFor(
      [&]() -> Result<bool> {
        test_tablets_by_tserver.clear();
        num_test_tablet_replicas = 0;
        for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
          auto tablets = VERIFY_RESULT(GetTestTableTabletIds(ts_idx));
          num_test_tablet_replicas += tablets.size();
          test_tablets_by_tserver.emplace_back(std::move(tablets));
        }
        return num_test_tablet_replicas == expected_tablet_replicas;
      },
      30s * kTimeMultiplier,
      Format(
          "Waiting for all tablet servers to have $0 total tablet replicas",
          expected_tablet_replicas));
  LOG(INFO) << "Test tablets replicas (" << num_test_tablet_replicas
            << "): " << AsString(test_tablets_by_tserver);
  ASSERT_OK(status);
}

// Make sure RBS of split parent and replay of SPLIT_OP don't delete child tablets data.
TEST_F(TabletSplitITest, ParentRemoteBootstrapAfterWritesToChildren) {
  constexpr auto kNumRows = kDefaultNumRows;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;
  // Disable leader moves.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_moves) = 0;

  auto source_tablet_id = ASSERT_RESULT(CreateSingleTabletAndSplit(kNumRows));

  LOG(INFO) << "Source tablet ID: " << source_tablet_id;

  // Write more rows into child tablets.
  ASSERT_OK(WriteRows(kNumRows, /* start_key = */ kNumRows + 1));
  ASSERT_OK(CheckRowsCount(kNumRows * 2));

  // Trigger and wait for RBS to complete on the followers of split parent tablet.
  for (auto& ts : cluster_->mini_tablet_servers()) {
    const auto* tablet_manager = ts->server()->tablet_manager();
    const auto peer = ASSERT_RESULT(tablet_manager->GetTablet(source_tablet_id));
    if (ASSERT_RESULT(peer->GetConsensus())->GetLeaderStatus() !=
        consensus::LeaderStatus::NOT_LEADER) {
      continue;
    }
    LOG(INFO) << Format(
        "Triggering RBS for tablet $0 peer $1", peer->tablet_id(), peer->permanent_uuid());
    peer->SetFailed(STATUS(InternalError, "Set to failed to initiate RBS for test"));

    ASSERT_OK(LoggedWaitFor(
        [&] { return peer->state() == tablet::RaftGroupStatePB::SHUTDOWN; }, 30s * kTimeMultiplier,
        Format(
            "Waiting for tablet $0 peer $1 shutdown", peer->tablet_id(), peer->permanent_uuid())));

    ASSERT_OK(LoggedWaitFor(
        [&] {
          const auto result = tablet_manager->GetTablet(source_tablet_id);
          if (!result.ok()) {
            return false;
          }
          if ((*result)->state() != tablet::RaftGroupStatePB::RUNNING) {
            return false;
          }
          const auto tablet = (*result)->shared_tablet();
          if (!tablet) {
            return false;
          }
          return tablet->metadata()->tablet_data_state() ==
                 tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED;
        },
        30s * kTimeMultiplier,
        Format(
            "Waiting for tablet $0 peer $1 remote bootstrap completed", peer->tablet_id(),
            peer->permanent_uuid())));
  }

  // Make sure all child tablet replicas are not affected by split parent RBS that replayed
  // SPLIT_OP (should be idempotent).
  ASSERT_OK(CheckPostSplitTabletReplicasData(kNumRows * 2));

  // Write more data and make sure it is there.
  ASSERT_OK(WriteRows(kNumRows, /* start_key = */ kNumRows * 2 + 1));
  ASSERT_OK(CheckRowsCount(kNumRows * 3));
}

class TabletSplitSingleServerITestWithPartition :
    public TabletSplitSingleServerITest,
    public testing::WithParamInterface<Partitioning> {
};

TEST_P(TabletSplitSingleServerITestWithPartition, TestSplitEncodedKeyAfterBreakInTheMiddleOfSplit) {
  // Make catalog manager to do only Upsert in order to emulate master error/crash behaviour in the
  // middle of split. Restart is required to be sure the flags change is seen at the master's side.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_error_after_registering_split_tablets) = true;
  ASSERT_OK(cluster_->RestartSync());
  SetNumTablets(1);

  Schema schema;
  BuildSchema(GetParam(), &schema);
  ASSERT_OK(CreateTable(schema));

  ASSERT_RESULT(WriteRowsAndFlush(2000));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(WaitForAnySstFiles(peers.front()));

  // Keep keys to compare later, and split
  const auto source_tablet_id =
      ASSERT_RESULT(GetSingleTestTabletInfo(ASSERT_RESULT(catalog_manager())))->id();
  const auto key_response = ASSERT_RESULT(SendTServerRpcSyncGetSplitKey(source_tablet_id));
  ASSERT_FALSE(ASSERT_RESULT(SendMasterRpcSyncSplitTablet(source_tablet_id)).has_error());

  // Split should return an OK status, so, let's try to wait for child tablets. The wait must fail
  // as master error has been simulated in the middle.
  const auto status = WaitForTabletSplitCompletion(
      /* expected_non_split_tablets = */2,
      /* expected_split_tablets = */ 0,
      /* num_replicas_online = */ 0,
      client::kTableName,
      /* core_dump_on_failure = */ false);
  ASSERT_NOK(status) << "Corresponding split is expected to fail!";

  // Reset flag to emulate partitions re-calculation.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_error_after_registering_split_tablets) = false;
  ASSERT_OK(cluster_->RestartSync());

  // Split should pass without any error.
  const auto response = ASSERT_RESULT(SendMasterRpcSyncSplitTablet(source_tablet_id));
  ASSERT_FALSE(response.has_error()) << response.error().ShortDebugString();
  ASSERT_OK(WaitForTabletSplitCompletion(2 /* expected_non_split_tablets */));

  // Investigate child tablets to make sure keys are expected.
  peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_->id());
  ASSERT_EQ(peers.size(), 2);
  for (const auto& peer : peers) {
    const auto& key_bounds = peer->tablet()->key_bounds();
    ASSERT_TRUE(key_bounds.IsInitialized());

    const auto partition_end = peer->tablet_metadata()->partition()->partition_key_end();
    const auto partition = peer->tablet_metadata()->partition()->partition_key_start();
    if (partition.empty()) {
      // First child
      ASSERT_EQ(partition_end, key_response.split_partition_key());
      ASSERT_TRUE(key_bounds.lower.empty())
          << "peer lower bound = " << FormatSliceAsStr(key_bounds.lower.AsSlice());
      ASSERT_EQ(key_bounds.upper.ToStringBuffer(), key_response.split_encoded_key());
    } else {
      // Second child
      ASSERT_EQ(partition, key_response.split_partition_key());
      ASSERT_TRUE(partition_end.empty())
          << "peer partition end = " << FormatBytesAsStr(partition_end);
      ASSERT_EQ(key_bounds.lower.ToStringBuffer(), key_response.split_encoded_key());
      ASSERT_TRUE(key_bounds.upper.empty())
          << "peer upper bound = " << FormatSliceAsStr(key_bounds.upper.AsSlice());
    }
  }
}

class TabletSplitSystemRecordsITest :
    public TabletSplitSingleServerITest,
    public testing::WithParamInterface<Partitioning> {
 protected:
  void SetUp() override {
    TabletSplitSingleServerITest::SetUp();
    SetNumTablets(1);

    // Disable automatic tablet splitting.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    // Disable automatic compactions.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
        std::numeric_limits<int32>::max();
  }

  Status VerifySplitKeyError(yb::tablet::TabletPtr tablet) {
    SCHECK_NOTNULL(tablet.get());

    // Get middle key directly from in-memory tablet and check correct message has been returned.
    auto middle_key = tablet->GetEncodedMiddleSplitKey();
    SCHECK_EQ(middle_key.ok(), false, IllegalState, "Valid split key is not expected.");
    const auto key_message = middle_key.status().message();
    const auto is_expected_key_message =
        strnstr(key_message.cdata(), "got internal record", key_message.size()) != nullptr;
    SCHECK_EQ(is_expected_key_message, true, IllegalState,
              Format("Unexepected error message: $0", middle_key.status().ToString()));
    LOG(INFO) << "System record middle key result: " << middle_key.status().ToString();

    // Test that tablet GetSplitKey RPC returns the same message.
    auto response = VERIFY_RESULT(SendTServerRpcSyncGetSplitKey(tablet->tablet_id()));
    SCHECK_EQ(response.has_error(), true, IllegalState,
              "GetSplitKey RPC unexpectedly succeeded.");
    const Status op_status = StatusFromPB(response.error().status());
    SCHECK_EQ(op_status.ToString(), middle_key.status().ToString(), IllegalState,
              Format("Unexpected error message: $0", op_status.ToString()));
    LOG(INFO) << "System record get split key result: " << op_status.ToString();

    return Status::OK();
  }
};

TEST_P(TabletSplitSystemRecordsITest, GetSplitKey) {
  // The idea of the test is to generate data with kNumTxns ApplyTransactionState records following
  // by 2 * kNumRows user records (very small number). This can be achieved by the following steps:
  //   1) pause ApplyIntentsTasks to keep ApplyTransactionState records
  //   2) run kNumTxns transaction with the same keys
  //   3) run manual compaction to collapse all user records to the latest transaciton content
  //   4) at this step there are kNumTxns internal records followed by 2 * kNumRows user records

  // Selecting a small period for history cutoff to force compacting records with the same keys.
  constexpr auto kHistoryRetentionSec = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec)
      = kHistoryRetentionSec;

  // This flag shoudn't be less than 2, setting it to 1 may cause RemoveIntentsTask to become stuck.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) = 2;

  // Force intents to not apply in ApplyIntentsTask.
  // Note: transactions are still partly applied via tablet::UpdateTxnOperation::DoReplicated(),
  // but ApplyTransactionState will not be removed from SST during compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_and_skip_apply_intents_task_loop_ms) = 1;

  // This combination of txns number and user rows is enough to generate suitable number of
  // internal records to make middle key point to one of the ApplyTransactionState records.
  // Also this will prevent spawning logs with long operation timeout warnings.
  constexpr auto kNumTxns = 50;
  constexpr auto kNumRows = 3;

  Schema schema;
  BuildSchema(GetParam(), &schema);
  ASSERT_OK(CreateTable(schema));
  auto peer = ASSERT_RESULT(GetSingleTabletLeaderPeer());
  auto tablet = peer->shared_tablet();
  auto partition_schema = tablet->metadata()->partition_schema();
  LOG(INFO) << "System records partitioning: "
            << "hash = "  << partition_schema->IsHashPartitioning() << ", "
            << "range = " << partition_schema->IsRangePartitioning();

  for (auto i = 0; i < kNumTxns; ++i) {
    ASSERT_OK(WriteRows(&table_, kNumRows, 1, kNumRows * i));
  }

  // Sleep for kHistoryRetentionSec + delta to make sure all the records with the same keys
  // will be compacted. Taking into account FLAGS_TEST_pause_and_skip_apply_intents_task_loop_ms
  // is set, this leads to a warning for too long ScopedRWOperation (see ProcessApply routine).
  std::this_thread::sleep_for(std::chrono::seconds(kHistoryRetentionSec + 1));

  // Force manual compaction
  ASSERT_OK(FlushTestTable());
  ASSERT_OK(tablet->ForceManualRocksDBCompact());
  ASSERT_OK(LoggedWaitFor(
      [peer]() -> Result<bool> {
        return peer->tablet_metadata()->parent_data_compacted();
      },
      15s * kTimeMultiplier,
      "Wait for tablet manual compaction to be completed for peer: " + peer->tablet_id()));

  ASSERT_OK(VerifySplitKeyError(tablet));
}

TEST_F_EX(TabletSplitITest, SplitOpApplyAfterLeaderChange, TabletSplitExternalMiniClusterITest) {
  constexpr auto kNumRows = kDefaultNumRows;

  ASSERT_OK(cluster_->SetFlagOnMasters("enable_load_balancing", "false"));

  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(
      cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>(), &cluster_->proxy_cache()));

  CreateSingleTablet();
  ASSERT_OK(WriteRowsAndFlush(kNumRows));
  const auto source_tablet_id = ASSERT_RESULT(GetOnlyTestTabletId());
  LOG(INFO) << "Source tablet ID: " << source_tablet_id;

  // Select tserver to pause making sure it is not leader for source tablet (just for master to not
  // wait for re-election and GetSplitKey timeout before proceeding with the split).
  const auto paused_ts_idx =
      (ASSERT_RESULT(cluster_->GetTabletLeaderIndex(source_tablet_id)) + 1) %
      cluster_->num_tablet_servers();
  auto* paused_ts = cluster_->tablet_server(paused_ts_idx);

  // We want to avoid leader changes in child tablets for this test.
  // Disabling leader failure detection for paused_ts now before pausing it.
  // And will disable it for other tservers after child tablets are created and their leaders are
  // elected.
  ASSERT_OK(cluster_->SetFlag(paused_ts, "enable_leader_failure_detection", "false"));

  const auto paused_ts_id = paused_ts->uuid();
  LOG(INFO) << Format("Pausing ts-$0: $1", paused_ts_idx + 1, paused_ts_id);
  ASSERT_OK(paused_ts->Pause());

  ASSERT_OK(SplitTablet(source_tablet_id));
  // Wait for split to compete on all non-paused replicas.
  for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    if (ts_idx != paused_ts_idx) {
      ASSERT_OK(WaitForTablets(3, ts_idx));
    }
  }

  // Make sure all test tablets have leaders.
  const auto tablet_ids = ASSERT_RESULT(GetTestTableTabletIds());
  for (auto& tablet_id : tablet_ids) {
    itest::TServerDetails* leader;
    ASSERT_OK(FindTabletLeader(ts_map, tablet_id, kRpcTimeout, &leader));
  }

  // We want to avoid leader changes in child tablets for this test.
  for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    if (ts_idx != paused_ts_idx) {
      ASSERT_OK(cluster_->SetFlag(
          cluster_->tablet_server(ts_idx), "enable_leader_failure_detection", "false"));
    }
  }

  struct TabletLeaderInfo {
    PeerId peer_id;
    int64_t term;
  };

  auto get_leader_info = [&](const TabletId& tablet_id) -> Result<TabletLeaderInfo> {
    const auto leader_idx = VERIFY_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
    const auto leader_peer_id = cluster_->tablet_server(leader_idx)->uuid();
    LOG(INFO) << "Tablet " << tablet_id << " leader: " << leader_peer_id << " idx: " << leader_idx;
    consensus::ConsensusStatePB cstate;
    RETURN_NOT_OK(itest::GetConsensusState(
        ts_map[leader_peer_id].get(), tablet_id, consensus::CONSENSUS_CONFIG_ACTIVE, kRpcTimeout,
        &cstate));
    LOG(INFO) << "Tablet " << tablet_id << " cstate: " << cstate.DebugString();
    return TabletLeaderInfo {
        .peer_id = leader_peer_id,
        .term = cstate.current_term(),
    };
  };

  std::unordered_map<TabletId, TabletLeaderInfo> leader_info;
  int64_t max_leader_term = 0;
  for (auto& tablet_id : tablet_ids) {
    auto info = ASSERT_RESULT(get_leader_info(tablet_id));
    max_leader_term = std::max(max_leader_term, info.term);
    leader_info[tablet_id] = info;
  }
  LOG(INFO) << "Max leader term: " << max_leader_term;

  // Make source tablet to advance term to larger than max_leader_term, so resumed replica will
  // apply split in later term than child leader replicas have.
  while (leader_info[source_tablet_id].term <= max_leader_term) {
    ASSERT_OK(itest::LeaderStepDown(
        ts_map[leader_info[source_tablet_id].peer_id].get(), source_tablet_id, nullptr,
        kRpcTimeout));

    const auto source_leader_term = leader_info[source_tablet_id].term;
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          auto result = get_leader_info(source_tablet_id);
          if (!result.ok()) {
            return false;
          }
          leader_info[source_tablet_id] = *result;
          return result->term > source_leader_term;
        },
        30s * kTimeMultiplier,
        Format("Waiting for term >$0 on source tablet ...", source_leader_term)));
  }

  ASSERT_OK(paused_ts->Resume());
  // To avoid term changes.
  ASSERT_OK(cluster_->SetFlag(paused_ts, "enable_leader_failure_detection", "false"));

  // Wait for all replicas to have only 2 child tablets (parent tablet will be deleted).
  for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    ASSERT_OK(WaitForTablets(2, ts_idx));
  }

  ASSERT_OK(cluster_->WaitForTabletsRunning(paused_ts, 20s * kTimeMultiplier));

  // Make sure resumed replicas are not ahead of leader.
  const auto child_tablet_ids = ASSERT_RESULT(GetTestTableTabletIds());
  for (auto& child_tablet_id : child_tablet_ids) {
    consensus::ConsensusStatePB cstate;
    ASSERT_OK(itest::GetConsensusState(
        ts_map[paused_ts_id].get(), child_tablet_id, consensus::CONSENSUS_CONFIG_ACTIVE,
        kRpcTimeout, &cstate));
    LOG(INFO) << "Child tablet " << child_tablet_id
              << " resumed replica cstate: " << cstate.DebugString();

    ASSERT_LE(cstate.current_term(), leader_info[child_tablet_id].term);
  }
}

class TabletSplitSingleBlockITest :
    public TabletSplitSingleServerITest,
    public testing::WithParamInterface<Partitioning> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 2_KB;
    TabletSplitSingleServerITest::SetUp();
    SetNumTablets(1);
  }

  Status DoSplitSingleBlock(const Partitioning partitioning, const uint32_t num_rows,
      std::function<Status(yb::tablet::Tablet *tablet)> rows_written_callback) {
    // Setup table with rows.
    Schema schema;
    BuildSchema(partitioning, &schema);
    RETURN_NOT_OK(CreateTable(schema));

    const auto source_tablet_id =
        VERIFY_RESULT(GetSingleTestTabletInfo(VERIFY_RESULT(catalog_manager())))->id();

    // Check empty case.
    LOG(INFO) << "Sending GetSplitKey Rpc";
    auto key_resp = VERIFY_RESULT(SendTServerRpcSyncGetSplitKey(source_tablet_id));
    SCHECK(key_resp.has_error(), IllegalState, "Error is expected");

    // Write a few records.
    RETURN_NOT_OK(WriteRows(num_rows));
    auto tablet_peer = VERIFY_RESULT(GetSingleTabletLeaderPeer());
    RETURN_NOT_OK(tablet_peer->tablet()->Flush(tablet::FlushMode::kSync));

    // Wait for SST files appear on disc
    RETURN_NOT_OK(WaitFor([&] {
      return tablet_peer->tablet()->regular_db()->GetCurrentVersionNumSSTFiles() > 0;
    }, 5s * kTimeMultiplier, "Waiting for successful write", MonoDelta::FromSeconds(1)));
    RETURN_NOT_OK(rows_written_callback(tablet_peer->tablet()));

    // Send RPC for tablet splitting and validate resposnse.
    LOG(INFO) << "Sending sync SPLIT Rpc";
    auto resp = VERIFY_RESULT(SendMasterRpcSyncSplitTablet(source_tablet_id));
    SCHECK(!resp.has_error(), IllegalState, resp.error().DebugString());

    RETURN_NOT_OK(WaitForTabletSplitCompletion(/* expected_non_split_tablets = */ 2));
    return CheckPostSplitTabletReplicasData(num_rows);
  }

  static Result<uint32_t> GetFirstDataBlockRestartPointsNumber(rocksdb::DB* regular_db) {
    SCHECK_NOTNULL(regular_db);

    auto table_reader = dynamic_cast<rocksdb::BlockBasedTable*>(
        VERIFY_RESULT(regular_db->TEST_GetLargestSstTableReader()));
    SCHECK_NOTNULL(table_reader);

    auto index_reader_base = VERIFY_RESULT(table_reader->TEST_GetIndexReader());
    auto index_reader = dynamic_cast<rocksdb::MultiLevelIndexReader*>(index_reader_base.get());
    SCHECK_NOTNULL(index_reader);

    // Due to test intention this method is supported only for multi-index with 1 level.
    if (index_reader->TEST_GetNumLevels() > 1) {
      return STATUS(NotSupported, "It is expected to have only one level for the index.");
    }

    std::unique_ptr<rocksdb::InternalIterator> index_iter(
        table_reader->NewIndexIterator(rocksdb::ReadOptions::kDefault));
    index_iter->SeekToFirst();
    RETURN_NOT_OK(index_iter->status());
    if (!VERIFY_RESULT(index_iter->CheckedValid())) {
      return STATUS(Incomplete, "Empty or too small SST.");
    }

    return table_reader->TEST_GetBlockNumRestarts(
        rocksdb::ReadOptions::kDefault, index_iter->value(), rocksdb::BlockType::kData);
  }
};

TEST_P(TabletSplitSingleBlockITest, SplitSingleDataBlockTablet) {
  ASSERT_OK(DoSplitSingleBlock(GetParam(), /* num_rows = */ 18,
      [](yb::tablet::Tablet *tablet) -> Status {
    const auto num_restarts =
        VERIFY_RESULT(GetFirstDataBlockRestartPointsNumber(tablet->regular_db()));
    if (num_restarts <= 1) {
      return STATUS(IllegalState,
          "RocksDB records structure might be changed, "
          "try to adjust rows number to have more than one restart point.");
    }
    return Status::OK();
  }));
}

TEST_P(TabletSplitSingleBlockITest, SplitSingleDataBlockOneRestartTablet) {
  ASSERT_OK(DoSplitSingleBlock(GetParam(), /* num_rows = */ 6,
      [](yb::tablet::Tablet *tablet) -> Status {
    const auto num_restarts =
        VERIFY_RESULT(GetFirstDataBlockRestartPointsNumber(tablet->regular_db()));
    if (num_restarts > 1) {
      return STATUS(IllegalState,
          "RocksDB records structure might be changed, "
          "try to adjust rows number to have exactly one restart point.");
    }
    return Status::OK();
  }));
}

TEST_P(TabletSplitSingleBlockITest, SplitSingleDataBlockMultiLevelTablet) {
  docdb::DisableYcqlPackedRow();
  // Required to simulate a case with num levels > 1 and top block restarts num == 1.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_index_block_restart_interval) = 4;

  ASSERT_OK(DoSplitSingleBlock(GetParam(), /* num_rows = */ 4000,
      [](yb::tablet::Tablet *tablet) -> Status {
    auto table_reader = dynamic_cast<rocksdb::BlockBasedTable*>(
        VERIFY_RESULT(tablet->regular_db()->TEST_GetLargestSstTableReader()));
    SCHECK_NOTNULL(table_reader);

    auto index_reader_base = VERIFY_RESULT(table_reader->TEST_GetIndexReader());
    auto index_reader = dynamic_cast<rocksdb::MultiLevelIndexReader*>(index_reader_base.get());
    SCHECK_NOTNULL(index_reader);

    if ((index_reader->TEST_GetNumLevels() == 1) ||
        (index_reader->TEST_GetTopLevelBlockNumRestarts() > 1)) {
      return STATUS(IllegalState,
          Format(
              "Num level = $0, num top level restarts = $1. RocksDB records structure "
              "might be changed, try to adjust rows number to have num levels > 1 "
              "and num top level restarts == 1 for a top level index block.",
              index_reader->TEST_GetNumLevels(), index_reader->TEST_GetTopLevelBlockNumRestarts()));
    }
    return Status::OK();
  }));
}

namespace {

PB_ENUM_FORMATTERS(IsolationLevel);

template <typename T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
}

} // namespace

INSTANTIATE_TEST_CASE_P(
    TabletSplitITest,
    TabletSplitITestWithIsolationLevel,
    ::testing::ValuesIn(GetAllPbEnumValues<IsolationLevel>()),
    TestParamToString<IsolationLevel>);

INSTANTIATE_TEST_CASE_P(
    TabletSplitSingleServerITest,
    TabletSplitSingleServerITestWithPartition,
    ::testing::ValuesIn(kPartitioningArray),
    TestParamToString<Partitioning>);

INSTANTIATE_TEST_CASE_P(
    TabletSplitSingleServerITest,
    TabletSplitSystemRecordsITest,
    ::testing::ValuesIn(kPartitioningArray),
    TestParamToString<Partitioning>);

INSTANTIATE_TEST_CASE_P(
    TabletSplitSingleServerITest,
    TabletSplitSingleBlockITest,
    ::testing::ValuesIn(kPartitioningArray),
    TestParamToString<Partitioning>);

INSTANTIATE_TEST_CASE_P(
    TabletSplitExternalMiniClusterITest,
    TabletSplitExternalMiniClusterCrashITest,
    ::testing::Values(
        "TEST_crash_before_apply_tablet_split_op",
        "TEST_crash_before_source_tablet_mark_split_done",
        "TEST_crash_after_tablet_split_completed"));

}  // namespace yb
