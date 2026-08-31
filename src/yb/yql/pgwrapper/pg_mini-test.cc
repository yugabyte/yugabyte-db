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

#include <atomic>
#include <fstream>
#include <limits>
#include <optional>
#include <thread>
#include <string_view>

#include <boost/preprocessor/seq/for_each.hpp>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/docdb/doc_read_context.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/ts_manager.h"
#include "yb/rocksdb/db.h"

#include "yb/server/skewed_clock.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/tablet_server.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/gutil/casts.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/random_util.h"
#include "yb/util/range.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/rpc/rpc_context.h"

#include "yb/yql/pggate/pggate_flags.h"

#include "yb/yql/pgwrapper/libpq_test_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

#include "yb/common/advisory_locks_error.h"

using std::string;

using namespace std::literals;

DECLARE_bool(TEST_disable_flush_on_shutdown);
DECLARE_bool(TEST_enable_pg_client_mock);
DECLARE_bool(TEST_fail_batcher_rpc);
DECLARE_bool(TEST_force_master_leader_resolution);
DECLARE_bool(TEST_no_schedule_remove_intents);
DECLARE_bool(TEST_request_unknown_tables_during_perform);
DECLARE_bool(TEST_skip_process_apply);
DECLARE_bool(TEST_tablet_pause_apply_write_ops);
DECLARE_bool(delete_intents_sst_files);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_colocated_table_tombstone_cache);
DECLARE_bool(enable_tracing);
DECLARE_bool(enable_wait_queues);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_bool(use_bootstrap_intent_ht_filter);
DECLARE_bool(ysql_allow_duplicating_repeatable_read_queries);
DECLARE_bool(ysql_yb_enable_ash);
DECLARE_bool(ysql_yb_enable_replica_identity);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_concurrent_ddl);

DECLARE_double(TEST_respond_write_failed_probability);
DECLARE_double(TEST_transaction_ignore_applying_probability);

DECLARE_int32(TEST_inject_mvcc_delay_add_leader_pending_ms);
DECLARE_int32(TEST_txn_participant_inject_latency_on_apply_update_txn_ms);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(gzip_stream_compression_level);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(sampled_trace_1_in_n);
DECLARE_int32(pg_client_extra_timeout_ms);
DECLARE_int32(stream_compression_algo);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_int32(tracing_level);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(tablet_creation_timeout_ms);
DECLARE_int32(txn_max_apply_batch_records);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_int32(ysql_yb_ash_sample_size);
DECLARE_int32(ysql_ddl_rpc_timeout_sec);
DECLARE_uint32(yb_max_recursion_depth);

DECLARE_int64(TEST_inject_random_delay_on_txn_status_response_ms);
DECLARE_int64(apply_intents_task_injected_delay_ms);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_filter_block_size_bytes);
DECLARE_int64(db_index_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);

DECLARE_string(time_source);
DECLARE_string(ysql_yb_default_replica_identity);

DECLARE_uint64(consensus_max_batch_size_bytes);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_uint64(pg_client_heartbeat_interval_ms);
DECLARE_uint64(pg_client_session_expiration_ms);
DECLARE_uint64(rpc_max_message_size);
DECLARE_bool(ysql_enable_relcache_init_optimization);

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_gauge_uint64(aborted_transactions_pending_cleanup);
METRIC_DECLARE_histogram(handler_latency_outbound_transfer);
METRIC_DECLARE_gauge_int64(rpc_busy_reactors);
METRIC_DECLARE_gauge_uint64(wal_replayable_applied_transactions);

namespace yb::pgwrapper {
namespace {

Result<bool> IsCatalogVersionChangedDuringDdl(PGConn* conn, const std::string& ddl_query) {
  auto version_getter =
      [conn]() { return GetCatalogVersion(conn); };
  const auto initial_version = VERIFY_RESULT(version_getter());
  RETURN_NOT_OK(conn->Execute(ddl_query));
  return initial_version != VERIFY_RESULT(version_getter());
}

Status IsReplicaIdentityPopulatedInTabletPeers(
    PgReplicaIdentity expected_replica_identity,
    const std::vector<tablet::TabletPeerPtr>& tablet_peers, const std::string& table_id) {
  for (const auto& peer : tablet_peers) {
    auto replica_identity =
        peer->tablet_metadata()->schema(table_id)->table_properties().replica_identity();
    EXPECT_EQ(replica_identity, expected_replica_identity);
  }
  return Status::OK();
}

} // namespace

class PgMiniTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_max_recursion_depth) = 1500; // for RegexRecursionLimit
    PgMiniTestBase::SetUp();
  }

  // Have several threads doing updates and several threads doing large scans in parallel.
  // If deferrable is true, then the scans are in deferrable transactions, so no read restarts are
  // expected.
  // Otherwise, the scans are in transactions with snapshot isolation, but we still don't expect any
  // read restarts to be observed because they should be transparently handled on the postgres side.
  void TestReadRestart(bool deferrable = true);

  void TestForeignKey(IsolationLevel isolation);

  void TestBigInsert(bool restart);

  void TestAnalyze(int row_width);

  void DestroyTable(const std::string& table_name);

  void TestConcurrentDeleteRowAndUpdateColumn(bool select_before_update);

  void CreateDBWithTablegroupAndTables(
      const std::string& database_name, const std::string& tablegroup_name, size_t num_tables,
      size_t keys, PGConn* conn);

  void VerifyFileSizeAfterCompaction(PGConn* conn, size_t num_tables);

  void RunManyConcurrentReadersTest();

  void ValidateAbortedTxnMetric();

  int64_t GetBloomFilterCheckedMetric();

  PgSchemaName GetPgSchema(const string& tbl_name);
};

class PgMiniTestSingleNode : public PgMiniTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    PgMiniTest::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

class PgMiniTestFailOnConflict : public PgMiniTest {
 protected:
  void SetUp() override {
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    EnableFailOnConflict();
    PgMiniTest::SetUp();
  }
};

class PgMiniPgClientServiceCleanupTest : public PgMiniTestSingleNode {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_session_expiration_ms) = 5000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_heartbeat_interval_ms) = 2000;
    PgMiniTestBase::SetUp();
  }
};

TEST_F_EX(PgMiniTest, VerifyPgClientServiceCleanupQueue, PgMiniPgClientServiceCleanupTest) {
  constexpr size_t kTotalConnections = 30;
  constexpr size_t kAshConnection = 1;
  std::vector<PGConn> connections;
  connections.reserve(kTotalConnections);
  for (size_t i = 0; i < kTotalConnections; ++i) {
    connections.push_back(ASSERT_RESULT(Connect()));
  }
  auto* client_service =
      cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientService();
  // The first Connect() to a fresh DB spawns an internal libpq backend
  // (TriggerRelcacheInitConnection) whose session lingers in sessions_
  // for the platform's ListenConnectionShutdown delay (up to 250ms on
  // macOS, 1s under sanitizers). Poll until it drains instead of
  // asserting immediately.
  ASSERT_OK(WaitFor([client_service, expected_count = connections.size() + kAshConnection]() {
    return client_service->TEST_SessionsCount() == expected_count;
  }, 5s, "relcache-init session cleanup"));

  connections.erase(connections.begin() + connections.size() / 2, connections.end());
  ASSERT_OK(WaitFor([client_service, expected_count = connections.size() + kAshConnection]() {
    return client_service->TEST_SessionsCount() == expected_count;
  }, 4 * FLAGS_pg_client_session_expiration_ms * 1ms, "client session cleanup", 1s));
}

// Try to change this to test follower reads.
TEST_F(PgMiniTest, FollowerReads) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (key int PRIMARY KEY, word TEXT, phrase TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 (key, word, phrase) VALUES (1, 'old', 'old is gold')"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 (key, word, phrase) VALUES (2, 'NEW', 'NEW is fine')"));

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'old')"));

  ASSERT_OK(conn.Execute("SET yb_debug_log_docdb_requests = false"));

  for (bool expect_old_behavior_before_20482 : {false, true}) {
    ASSERT_OK(conn.ExecuteFormat("SET yb_read_from_followers = true"));
    // Try to set a value < 2 * max_clock_skew (500ms) should fail.
    ASSERT_NOK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", 400)));
    ASSERT_NOK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", 999)));
    // Setting a value > 2 * max_clock_skew should work.
    ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", 1001)));

    ASSERT_OK(conn.ExecuteFormat("SET yb_read_from_followers = false"));
    if (expect_old_behavior_before_20482) {
      ASSERT_OK(conn.ExecuteFormat("SET yb_follower_reads_behavior_before_fixing_20482 = true"));
      // The old behavior was to check the limits only when follower reads are enabled.
      ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", 999)));
      // However, the limits are checked when follower reads is enabled.
      ASSERT_NOK(conn.ExecuteFormat("SET yb_read_from_followers = true"));
      ASSERT_OK(conn.ExecuteFormat("SET yb_follower_reads_behavior_before_fixing_20482 = false"));
    } else {
      // The new behavior is to check the limits whenever the staleness is updated.
      ASSERT_NOK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", 999)));
    }
  }

  // Setting staleness to what we require for the test.
  // Sleep and then perform an update, such that follower reads should see the old value.
  // But current reads will see the new/updated value.
  constexpr int32_t kStalenessMs = 4000 * kTimeMultiplier;
  LOG(INFO) << "Sleeping for " << kStalenessMs << " ms";
  SleepFor(MonoDelta::FromMilliseconds(kStalenessMs));
  ASSERT_OK(conn.Execute("UPDATE t SET value = 'NEW' WHERE key = 1"));
  auto kUpdateTime = MonoTime::Now();
  ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", kStalenessMs)));
  ASSERT_OK(conn.ExecuteFormat("SET yb_read_from_followers = true"));
  ASSERT_OK(
        conn.Execute("CREATE FUNCTION func() RETURNS text AS"
                     " $$ SELECT value FROM t WHERE key = 1 $$ LANGUAGE SQL"));

  // Follower reads will not be enabled unless a transaction block is marked read-only.
  for (bool read_only : {true, false}) {
    ASSERT_OK(conn.Execute(yb::Format("BEGIN TRANSACTION $0", read_only ? "READ ONLY" : "")));
    auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
    ASSERT_EQ(value, read_only ? "old" : "NEW");
    // Test with function
    value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT func()"));
    ASSERT_EQ(value, read_only ? "old" : "NEW");
    // Test with join
    value = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT phrase FROM t, t2 WHERE t.value = t2.word"));
    ASSERT_EQ(value, read_only ? "old is gold" : "NEW is fine");
    ASSERT_OK(conn.Execute("COMMIT"));
  }

  ASSERT_OK(conn.Execute("SET yb_read_from_followers = true"));
  // Follower reads will not be enabled unless the session or statement is marked read-only.
  for (bool read_only : {true, false}) {
    ASSERT_OK(conn.Execute(yb::Format("SET default_transaction_read_only = $0", read_only)));
    auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
    ASSERT_EQ(value, read_only ? "old" : "NEW");
    // Test with function
    value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT func()"));
    ASSERT_EQ(value, read_only ? "old" : "NEW");
    // Test with join
    value = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT phrase FROM t, t2 WHERE t.value = t2.word"));
    ASSERT_EQ(value, read_only ? "old is gold" : "NEW is fine");
  }

  const std::vector<std::string> kIsolationLevels{
      "SERIALIZABLE", "REPEATABLE READ", "READ COMMITTED", "READ UNCOMMITTED"};
  for (bool expect_old_behavior_before_20482 : {false, true}) {
    ASSERT_OK(conn.ExecuteFormat(
        "SET yb_follower_reads_behavior_before_fixing_20482 = $0",
        expect_old_behavior_before_20482));
    for (const auto& isolation_level : kIsolationLevels) {
      for (bool in_subtransaction : {true, false}) {
        ASSERT_OK(conn.Execute("SET yb_read_from_followers = false"));

        LOG(INFO) << "Isolation level " << isolation_level << " in_subtransaction "
                  << in_subtransaction;
        ASSERT_OK(
            conn.Execute(yb::Format("BEGIN TRANSACTION ISOLATION LEVEL $0", isolation_level)));
        ASSERT_OK(conn.Execute("SAVEPOINT a"));
        ASSERT_OK(conn.Execute("SET transaction_read_only = true"));
        if (!in_subtransaction) {
          ASSERT_OK(conn.Execute("RELEASE SAVEPOINT a"));
          ASSERT_OK(conn.Execute("SET yb_read_from_followers = true"));
          auto value =
              ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
          ASSERT_EQ(value, expect_old_behavior_before_20482 ? "NEW" : "old");
        } else {
          // We don't allow changing follower read settings in a sub-transaction.
          auto s = conn.Execute("SET yb_read_from_followers = true");
          ASSERT_EQ(s.ok(), expect_old_behavior_before_20482);
          if (!expect_old_behavior_before_20482) {
            ASSERT_TRUE(
                s.ToString(false, false)
                    .find("ERROR:  SET yb_read_from_followers must not be called in a "
                          "subtransaction") != std::string::npos);
          }
        }
        ASSERT_OK(conn.Execute("COMMIT"));
      }  // in_subtransaction

      // Test the ability to SET follower reads within a txn. with and without LOCAL being
      // specified.
      for (bool local : {true, false}) {
        LOG(INFO) << "Isolation level " << isolation_level << " local " << local;
        ASSERT_OK(conn.Execute("SET yb_read_from_followers = false"));

        ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", kStalenessMs)));
        ASSERT_OK(conn.Execute(
            yb::Format("BEGIN TRANSACTION READ ONLY ISOLATION LEVEL $0", isolation_level)));

        ASSERT_OK(
            conn.Execute(yb::Format("SET $0 yb_read_from_followers = true", local ? "LOCAL" : "")));
        ASSERT_OK(conn.Execute(Format(
            "SET $0 yb_follower_read_staleness_ms = $1", (local ? "LOCAL" : ""),
            kStalenessMs + 1)));
        auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
        ASSERT_EQ(value, expect_old_behavior_before_20482 ? "NEW" : "old");
        ASSERT_OK(conn.Execute("COMMIT"));

        value = ASSERT_RESULT(conn.FetchRow<std::string>("SHOW yb_read_from_followers"));
        ASSERT_EQ(value, local ? "off" : "on");
        value = ASSERT_RESULT(conn.FetchRow<std::string>("SHOW yb_follower_read_staleness_ms"));
        ASSERT_EQ(value, yb::ToString(local ? kStalenessMs : kStalenessMs + 1));

        // If the setting was updated using `local` then it should not have any effect outside the
        // transaction block. However, if `local` is not used, the setting should reflect
        // the changes even after the txn block is committed.
        ASSERT_OK(conn.Execute(
            yb::Format("BEGIN TRANSACTION READ ONLY ISOLATION LEVEL $0", isolation_level)));
        value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
        // If for some reason things are slow, follower reads may still see the NEW value.
        const auto time_delta_ms = MonoTime::Now().GetDeltaSince(kUpdateTime).ToMilliseconds();
        if (time_delta_ms < kStalenessMs) {
          ASSERT_EQ(value, local ? "NEW" : "old");
        }
        ASSERT_OK(conn.Execute("COMMIT"));
      }  // local

      // Test that we are able to disable the follower read settings within a txn.
      ASSERT_OK(conn.Execute("SET yb_read_from_followers = false"));
      ASSERT_OK(conn.Execute(
          yb::Format("BEGIN TRANSACTION READ ONLY ISOLATION LEVEL $0", isolation_level)));
      ASSERT_OK(conn.Execute("SET local yb_read_from_followers = true"));
      ASSERT_OK(conn.Execute("SET local yb_read_from_followers = false"));
      auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
      ASSERT_EQ(value, "NEW");
      ASSERT_OK(conn.Execute("COMMIT"));

      // Test the ability to SET yb_follower_read_staless_ms within a txn.
      constexpr int32_t kShortStalenessMs = 1001;
      auto kWaitUntil = kUpdateTime + MonoDelta::FromMilliseconds(2 * kShortStalenessMs);
      LOG(INFO) << "Last update was done at " << kUpdateTime.ToString() << " waiting until "
                << kWaitUntil.ToString();
      SleepUntil(kWaitUntil);
      LOG(INFO) << "Done waiting";
      ASSERT_GE(MonoTime::Now(), kWaitUntil);

      for (bool short_staleness : {true, false}) {
        LOG(INFO) << "Isolation level " << isolation_level << " short_staleness "
                  << short_staleness;
        ASSERT_OK(conn.Execute("SET yb_read_from_followers = false"));
        ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", kStalenessMs)));

        LOG(INFO) << "Isolation level " << isolation_level;
        ASSERT_OK(conn.Execute(
            yb::Format("BEGIN TRANSACTION READ ONLY ISOLATION LEVEL $0", isolation_level)));
        ASSERT_OK(conn.Execute("SET LOCAL yb_read_from_followers = true"));
        auto staleness_ms = short_staleness ? kShortStalenessMs : kStalenessMs;
        ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", staleness_ms)));
        auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
        ASSERT_EQ(value, (short_staleness || expect_old_behavior_before_20482) ? "NEW" : "old");
        ASSERT_OK(conn.Execute("COMMIT"));
      }  // short_staleness

      // Test joins/functions with follower reads inside a txn block.
      ASSERT_OK(conn.Execute("SET yb_read_from_followers = false"));
      ASSERT_OK(conn.Execute(
          yb::Format("BEGIN TRANSACTION READ ONLY ISOLATION LEVEL $0", isolation_level)));
      ASSERT_OK(conn.Execute("SET yb_read_from_followers = true"));
      value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
      ASSERT_EQ(value, expect_old_behavior_before_20482 ? "NEW" : "old");
      // Test with function
      value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT func()"));
      ASSERT_EQ(value, expect_old_behavior_before_20482 ? "NEW" : "old");
      // Test with join
      value = ASSERT_RESULT(
          conn.FetchRow<std::string>("SELECT phrase FROM t, t2 WHERE t.value = t2.word"));
      ASSERT_EQ(value, expect_old_behavior_before_20482 ? "NEW is fine" : "old is gold");
      auto s = conn.Execute("SET yb_read_from_followers = false");
      ASSERT_EQ(s.ok(), expect_old_behavior_before_20482);
      ASSERT_OK(conn.Execute("ABORT"));
    }  // isolation_level
  }    // expect_old_behavior_before_20482

  // After sufficient time has passed, even "follower reads" should see the newer value.
  {
    const auto kWaitUntil = kUpdateTime + MonoDelta::FromMilliseconds(kStalenessMs);
    LOG(INFO) << "Sleeping until we are past " << kWaitUntil.ToString();
    SleepUntil(kWaitUntil);

    ASSERT_OK(conn.Execute("SET default_transaction_read_only = false"));
    auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
    ASSERT_EQ(value, "NEW");
    // Test with function
    value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT func()"));
    ASSERT_EQ(value, "NEW");
    // Test with join
    value = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT phrase FROM t, t2 WHERE t.value = t2.word"));
    ASSERT_EQ(value, "NEW is fine");

    ASSERT_OK(conn.Execute("SET default_transaction_read_only = true"));
    value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
    ASSERT_EQ(value, "NEW");
    // Test with function
    value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT func()"));
    ASSERT_EQ(value, "NEW");
    // Test with join
    value = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT phrase FROM t, t2 WHERE t.value = t2.word"));
    ASSERT_EQ(value, "NEW is fine");
  }
}

TEST_F(PgMiniTest, MultiColFollowerReads) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k int PRIMARY KEY, c1 TEXT, c2 TEXT)"));
  ASSERT_OK(conn.Execute("SET yb_debug_log_docdb_requests = true"));
  ASSERT_OK(conn.Execute("SET yb_read_from_followers = true"));

  constexpr int32_t kSleepTimeMs = 1200 * kTimeMultiplier;

  ASSERT_OK(conn.Execute("INSERT INTO t (k, c1, c2) VALUES (1, 'old', 'old')"));
  auto kUpdateTime0 = MonoTime::Now();

  SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));

  ASSERT_OK(conn.Execute("UPDATE t SET c1 = 'NEW' WHERE k = 1"));
  auto kUpdateTime1 = MonoTime::Now();

  SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));

  ASSERT_OK(conn.Execute("UPDATE t SET c2 = 'NEW' WHERE k = 1"));
  auto kUpdateTime2 = MonoTime::Now();

  ASSERT_OK(conn.Execute("SET default_transaction_read_only = false"));
  auto row = ASSERT_RESULT((conn.FetchRow<int32_t, std::string, std::string>(
      "SELECT * FROM t WHERE k = 1")));
  ASSERT_EQ(row, (decltype(row){1, "NEW", "NEW"}));

  // Set default_transaction_read_only to true for the rest of the statements
  ASSERT_OK(conn.Execute("SET default_transaction_read_only = true"));

  const int32_t kOpDurationMs = 10;
  auto staleness_ms = (MonoTime::Now() - kUpdateTime0).ToMilliseconds() - kOpDurationMs;
  ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", staleness_ms)));
  ASSERT_OK(conn.Execute("SET default_transaction_read_only = true"));
  row = ASSERT_RESULT((conn.FetchRow<int32_t, std::string, std::string>(
      "SELECT * FROM t WHERE k = 1")));
  ASSERT_EQ(row, (decltype(row){1, "old", "old"}));

  staleness_ms = (MonoTime::Now() - kUpdateTime1).ToMilliseconds() - kOpDurationMs;
  ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", staleness_ms)));
  row = ASSERT_RESULT((conn.FetchRow<int32_t, std::string, std::string>(
      "SELECT * FROM t WHERE k = 1")));
  ASSERT_EQ(row, (decltype(row){1, "NEW", "old"}));

  SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));

  staleness_ms = (MonoTime::Now() - kUpdateTime2).ToMilliseconds();
  ASSERT_OK(conn.Execute(Format("SET yb_follower_read_staleness_ms = $0", staleness_ms)));
  row = ASSERT_RESULT((conn.FetchRow<int32_t, std::string, std::string>(
      "SELECT * FROM t WHERE k = 1")));
  ASSERT_EQ(row, (decltype(row){1, "NEW", "NEW"}));
}

TEST_F(PgMiniTest, Simple) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  ASSERT_EQ(value, "hello");
}

class PgMiniTestTracing : public PgMiniTest, public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tracing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tracing_level) = 1;
    // Disable probabilistic tracing. Otherwise a sampled trace of an unrelated background RPC
    // (e.g. a slow remote bootstrap) is dumped into the log and counted by the test's log sink.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_sampled_trace_1_in_n) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_use_shared_memory) = GetParam();
    // Disable auto analyze because it introduces flakiness for query plans and metrics.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    PgMiniTest::SetUp();
  }
};

TEST_P(PgMiniTestTracing, Tracing) {
  class TraceLogSink : public google::LogSink {
   public:
    void send(
        google::LogSeverity severity, const char* full_filename, const char* base_filename,
        int line, const struct ::tm* tm_time, const char* message, size_t message_len) {
      // Count only traces of PG session RPCs. Traces of unrelated background RPCs (e.g. a slow
      // remote bootstrap of a system tablet) are dumped to the same log and would otherwise be
      // attributed to the queries below.
      if (strcmp(base_filename, "trace.cc") == 0 &&
          std::string_view(message, message_len).find("pg_client_session.cc") !=
              std::string_view::npos) {
        last_logged_bytes_ = message_len;
      }
    }

    size_t get_last_logged_bytes_and_reset() {
      return last_logged_bytes_.exchange(0);
    }

   private:
    std::atomic<size_t> last_logged_bytes_{0};
  };


  TraceLogSink trace_log_sink;
  google::AddLogSink(&trace_log_sink);
  size_t last_logged_trace_size;

  // Wait for all tablet servers to be registered at the master.
  ASSERT_OK(cluster_->WaitForTabletServerCount(cluster_->num_tablet_servers()));

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT, value2 TEXT)"));

  LOG(INFO) << "Doing Insert";
  trace_log_sink.get_last_logged_bytes_and_reset();
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value, value2) VALUES (0, 'zero', 'zero')"));
  ASSERT_OK(conn.Execute("COMMIT"));
  SleepFor(1s);
  // We do not expect the transaction to be logged unless we set the tracing flag.
  EXPECT_EQ(trace_log_sink.get_last_logged_bytes_and_reset(), 0);

  LOG(INFO) << "Setting yb_enable_docdb_tracing";
  ASSERT_OK(conn.Execute("SET yb_enable_docdb_tracing = true"));

  LOG(INFO) << "Doing Insert";
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value, value2) VALUES (1, 'hello', 'world')"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  // 1975 is size of the current trace for insert when using Rpc.
  // The trace is about 1787 when using shared memory. But we only care that
  // something got printed, so we are not checking the exact size.
  EXPECT_GE(last_logged_trace_size, 1000);
  LOG(INFO) << "Done Insert";

  // 1884 is size of the current trace for select.
  // being a little conservative for changes in ports/ip addr etc.
  constexpr size_t kSingleSelectTraceSizeBound = 1600;
  LOG(INFO) << "Doing Select";
  auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  EXPECT_GE(last_logged_trace_size, kSingleSelectTraceSizeBound);
  // ASSERT_EQ(value, "hello");
  LOG(INFO) << "Done Select";

  LOG(INFO) << "Doing block transaction for inserts";
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION"));
  value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  EXPECT_GE(last_logged_trace_size, kSingleSelectTraceSizeBound);

  ASSERT_OK(conn.Execute("INSERT INTO t (key, value, value2) VALUES (2, 'good', 'morning')"));
  EXPECT_EQ(trace_log_sink.get_last_logged_bytes_and_reset(), 0);

  ASSERT_OK(conn.Execute("INSERT INTO t (key, value, value2) VALUES (3, 'good', 'morning')"));
  EXPECT_EQ(trace_log_sink.get_last_logged_bytes_and_reset(), 0);

  value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 2"));
  EXPECT_EQ(trace_log_sink.get_last_logged_bytes_and_reset(), 0);

  ASSERT_OK(conn.Execute("COMMIT"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  // 5446 is size of the current trace for the transaction block.
  // being a little conservative for changes in ports/ip addr etc.
  EXPECT_GE(last_logged_trace_size, 5200);
  LOG(INFO) << "Done block transaction for inserts";

  LOG(INFO) << "Doing block READ ONLY transaction for selects";
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION READ ONLY"));
  value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  EXPECT_GE(last_logged_trace_size, kSingleSelectTraceSizeBound);
  value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 2"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  EXPECT_GE(last_logged_trace_size, kSingleSelectTraceSizeBound);
  value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 3"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  EXPECT_GE(last_logged_trace_size, kSingleSelectTraceSizeBound);
  ASSERT_OK(conn.Execute("COMMIT"));
  SleepFor(1s);
  last_logged_trace_size = trace_log_sink.get_last_logged_bytes_and_reset();
  LOG(INFO) << "Logged " << last_logged_trace_size << " bytes";
  // 5446 is size of the current trace for the transaction block.
  // being a little conservative for changes in ports/ip addr etc.
  EXPECT_EQ(last_logged_trace_size, 0);
  LOG(INFO) << "Done block READ ONLY transaction for selects";

  google::RemoveLogSink(&trace_log_sink);
  ValidateAbortedTxnMetric();
}

INSTANTIATE_TEST_SUITE_P(PgMiniTestTracing, PgMiniTestTracing, ::testing::Bool(),
    [](const ::testing::TestParamInfo<bool>& info) {
        return info.param ? "PgClientSharedMem" : "PgClientRpc";
    });

TEST_F(PgMiniTest, TracingSushant) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tracing) = false;
  auto conn = ASSERT_RESULT(Connect());

  LOG(INFO) << "Setting yb_enable_docdb_tracing";
  ASSERT_OK(conn.Execute("SET yb_enable_docdb_tracing = true"));
  LOG(INFO) << "Doing Create";
  ASSERT_OK(conn.Execute("create table t (h int, r int, v int, primary key (h, r));"));
  LOG(INFO) << "Done Create";
  LOG(INFO) << "Doing Insert";
  ASSERT_OK(conn.Execute("insert into t  values (1,3,1), (1,4,1);"));
  LOG(INFO) << "Done Insert";
}

TEST_F(PgMiniTest, WriteRetry) {
  constexpr int kKeys = 100;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_respond_write_failed_probability) = 0.25;

  LOG(INFO) << "Insert " << kKeys << " keys";
  for (int key = 0; key != kKeys; ++key) {
    auto status = conn.ExecuteFormat("INSERT INTO t (key) VALUES ($0)", key);
    ASSERT_TRUE(status.ok() || PgsqlError(status) == YBPgErrorCode::YB_PG_UNIQUE_VIOLATION ||
                status.ToString().find("Duplicate request") != std::string::npos) << status;
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_respond_write_failed_probability) = 0;

  auto result = ASSERT_RESULT(conn.FetchMatrix("SELECT * FROM t ORDER BY key", kKeys, 1));
  for (int key = 0; key != kKeys; ++key) {
    auto fetched_key = ASSERT_RESULT(GetValue<int32_t>(result.get(), key, 0));
    ASSERT_EQ(fetched_key, key);
  }

  LOG(INFO) << "Insert duplicate key";
  auto status = conn.Execute("INSERT INTO t (key) VALUES (1)");
  ASSERT_EQ(PgsqlError(status), YBPgErrorCode::YB_PG_UNIQUE_VIOLATION) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "duplicate key value violates unique constraint");
}

TEST_F(PgMiniTest, With) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (k int PRIMARY KEY, v int)"));

  ASSERT_OK(conn.Execute(
      "WITH test2 AS (UPDATE test SET v = 2 WHERE k = 1) "
      "UPDATE test SET v = 3 WHERE k = 1"));
}

void PgMiniTest::DestroyTable(const std::string& table_name) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
}

void PgMiniTest::TestReadRestart(const bool deferrable) {
  constexpr CoarseDuration kWaitTime = 60s;
  constexpr int kKeys = 100;
  constexpr int kNumReadThreads = 8;
  constexpr int kNumUpdateThreads = 8;
  constexpr int kRequiredNumReads = 500;
  constexpr std::chrono::milliseconds kClockSkew = -100ms;
  std::atomic<int> num_read_restarts(0);
  std::atomic<int> num_read_successes(0);
  TestThreadHolder thread_holder;

  // Set up table
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value INT)"));
  for (int key = 0; key != kKeys; ++key) {
    ASSERT_OK(setup_conn.Execute(Format("INSERT INTO t (key, value) VALUES ($0, 0)", key)));
  }

  // Introduce clock skew
  auto delta_changers = SkewClocks(cluster_.get(), kClockSkew);

  // Start read threads
  for (int i = 0; i < kNumReadThreads; ++i) {
    thread_holder.AddThreadFunctor([this, deferrable, &num_read_restarts, &num_read_successes,
                                    &stop = thread_holder.stop_flag()] {
      auto read_conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        if (deferrable) {
          ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY, "
                                      "DEFERRABLE"));
        } else {
          ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
        }
        auto result = read_conn.FetchMatrix("SELECT * FROM t", kKeys, 2);
        if (!result.ok()) {
          ASSERT_TRUE(result.status().IsNetworkError()) << result.status();
          ASSERT_EQ(PgsqlError(result.status()), YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE)
              << result.status();
          ASSERT_STR_CONTAINS(result.status().ToString(), "Restart read");
          ++num_read_restarts;
          ASSERT_OK(read_conn.Execute("ABORT"));
          break;
        } else {
          ASSERT_OK(read_conn.Execute("COMMIT"));
          ++num_read_successes;
        }
      }
    });
  }

  // Start update threads
  for (int i = 0; i < kNumUpdateThreads; ++i) {
    thread_holder.AddThreadFunctor([this, i, &stop = thread_holder.stop_flag()] {
      auto update_conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        for (int key = i; key < kKeys; key += kNumUpdateThreads) {
          ASSERT_OK(update_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
          ASSERT_OK(update_conn.Execute(
              Format("UPDATE t SET value = value + 1 WHERE key = $0", key)));
          ASSERT_OK(update_conn.Execute("COMMIT"));
        }
      }
    });
  }

  // Stop threads after a while
  thread_holder.WaitAndStop(kWaitTime);

  // Count successful reads
  int num_reads = (num_read_restarts.load(std::memory_order_acquire)
                   + num_read_successes.load(std::memory_order_acquire));
  LOG(INFO) << "Successful reads: " << num_read_successes.load(std::memory_order_acquire) << "/"
      << num_reads;
  ASSERT_EQ(num_read_restarts.load(std::memory_order_acquire), 0);
  ASSERT_GT(num_read_successes.load(std::memory_order_acquire), kRequiredNumReads);
  ValidateAbortedTxnMetric();
}

class PgMiniLargeClockSkewTest : public PgMiniTest {
 public:
  void SetUp() override {
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_clock_skew_usec) = 250000ULL;
    PgMiniTestBase::SetUp();
  }
};

TEST_F_EX(PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(ReadRestartSerializableDeferrable),
          PgMiniLargeClockSkewTest) {
  TestReadRestart(true /* deferrable */);
}

TEST_F_EX(PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(ReadRestartSnapshot),
          PgMiniLargeClockSkewTest) {
  TestReadRestart(false /* deferrable */);
}

class PgMiniSingleTserverTest : public PgMiniTest {
 public:
  size_t NumTabletServers() override {
    return 1;
  }
};

// Reproduces https://github.com/yugabyte/yugabyte-db/issues/33107.
// When the read time is picked on the tserver and the read is restarted in place because of an
// intent of a transaction committed above the picked read time, the retried read must not be
// performed until the tablet safe time reaches the restart read time.
// Otherwise it can miss a write that is replicated but not yet applied, even though this write
// has a hybrid time below the restart read time. The restart read time is returned as
// used_read_time and becomes the transaction read point, so the next statement of the same
// transaction sees this write appear at the same read point - a repeatable read violation.
TEST_F_EX(PgMiniTest, ReadRestartWaitsForSafeTime, PgMiniSingleTserverTest) {
  auto txn_conn = ASSERT_RESULT(Connect());
  auto write_conn = ASSERT_RESULT(Connect());
  auto read_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS"));
  // Warm up catalog caches, so the read below does not need any writes.
  ASSERT_RESULT((read_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t")));

  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(txn_conn.Execute("INSERT INTO t VALUES (1, 1)"));

  // Keep intents of the committed transaction in intents db, so the read below resolves its
  // commit time via the transaction status path and gets restarted.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_process_apply) = true;
  // Pause write apply after replication, pinning the tablet safe time below the write hybrid
  // time.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tablet_pause_apply_write_ops) = true;

  TestThreadHolder thread_holder;
  StringWaiterLogSink pause_log_sink("Pausing due to flag TEST_tablet_pause_apply_write_ops");
  thread_holder.AddThreadFunctor([&write_conn] {
    ASSERT_OK(write_conn.Execute("INSERT INTO t VALUES (2, 2)"));
  });
  ASSERT_OK(pause_log_sink.WaitFor(60s * kTimeMultiplier));

  // The paused write already has a hybrid time, so the transaction commit time is above it.
  ASSERT_OK(txn_conn.CommitTransaction());

  // Keep the apply paused long enough for the read below to pick its read time and restart
  // while the safe time is still pinned below the paused write.
  thread_holder.AddThreadFunctor([] {
    SleepFor(3s * kTimeMultiplier);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tablet_pause_apply_write_ops) = false;
  });

  // The read picks read time = safe time, which is below the hybrid time of the paused write,
  // then hits the intent committed above it and restarts in place at the commit time.
  // Run it as the first statement of a repeatable read transaction, so the restart read time
  // becomes the transaction read point.
  ASSERT_OK(read_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto rows = ASSERT_RESULT(
      (read_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t ORDER BY k")));
  // Wait for the paused write to complete.
  thread_holder.JoinAll();
  // Re-read at the same read point, the result must be the same.
  const auto reread_rows = ASSERT_RESULT(
      (read_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t ORDER BY k")));
  ASSERT_OK(read_conn.CommitTransaction());

  ASSERT_EQ(rows, reread_rows);
}

TEST_F_EX(PgMiniTest, SerializableReadOnly, PgMiniTestFailOnConflict) {
  PGConn read_conn = ASSERT_RESULT(Connect());
  PGConn setup_conn = ASSERT_RESULT(Connect());
  PGConn write_conn = ASSERT_RESULT(Connect());

  // Set up table
  ASSERT_OK(setup_conn.Execute("CREATE TABLE t (i INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO t (i) VALUES (0)"));

  // SERIALIZABLE, READ ONLY should use snapshot isolation
  ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY"));
  ASSERT_OK(write_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ WRITE"));
  ASSERT_OK(write_conn.Execute("UPDATE t SET i = i + 1"));
  ASSERT_OK(read_conn.Fetch("SELECT * FROM t"));
  ASSERT_OK(read_conn.Execute("COMMIT"));
  ASSERT_OK(write_conn.Execute("COMMIT"));

  // READ ONLY, SERIALIZABLE should use snapshot isolation
  ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION READ ONLY, ISOLATION LEVEL SERIALIZABLE"));
  ASSERT_OK(write_conn.Execute("BEGIN TRANSACTION READ WRITE, ISOLATION LEVEL SERIALIZABLE"));
  ASSERT_OK(read_conn.Fetch("SELECT * FROM t"));
  ASSERT_OK(write_conn.Execute("UPDATE t SET i = i + 1"));
  ASSERT_OK(read_conn.Execute("COMMIT"));
  ASSERT_OK(write_conn.Execute("COMMIT"));

  // SHOW for READ ONLY should show serializable
  ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY"));
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRow<std::string>("SHOW transaction_isolation")),
            "serializable");
  ASSERT_OK(read_conn.Execute("COMMIT"));

  // SHOW for READ WRITE to READ ONLY should show serializable and read_only
  ASSERT_OK(write_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ WRITE"));
  ASSERT_OK(write_conn.Execute("SET TRANSACTION READ ONLY"));
  ASSERT_EQ(ASSERT_RESULT(write_conn.FetchRow<std::string>("SHOW transaction_isolation")),
            "serializable");
  ASSERT_EQ(ASSERT_RESULT(write_conn.FetchRow<std::string>("SHOW transaction_read_only")), "on");
  ASSERT_OK(write_conn.Execute("COMMIT"));

  // SERIALIZABLE, READ ONLY to READ WRITE should not use snapshot isolation
  ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY"));
  ASSERT_OK(write_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ WRITE"));
  ASSERT_OK(read_conn.Execute("SET TRANSACTION READ WRITE"));
  ASSERT_OK(write_conn.Execute("UPDATE t SET i = i + 1"));
  // The result of the following statement is probabilistic.  If it does not fail now, then it
  // should fail during COMMIT.
  auto s = ResultToStatus(read_conn.Fetch("SELECT * FROM t"));
  if (s.ok()) {
    ASSERT_OK(read_conn.Execute("COMMIT"));
    Status status = write_conn.Execute("COMMIT");
    ASSERT_NOK(status);
    ASSERT_TRUE(status.IsNetworkError()) << status;
    ASSERT_EQ(PgsqlError(status), YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE) << status;
  } else {
    ASSERT_TRUE(s.IsNetworkError()) << s;
    ASSERT_TRUE(IsSerializeAccessError(s) || IsAbortError(s)) << s;
    ASSERT_STR_CONTAINS(s.ToString(), "conflicts with higher priority transaction");
  }
}

void AssertAborted(const Status& status) {
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "aborted");
}

TEST_F_EX(PgMiniTest, SelectModifySelect, PgMiniTestFailOnConflict) {
  {
    auto read_conn = ASSERT_RESULT(Connect());
    auto write_conn = ASSERT_RESULT(Connect());

    ASSERT_OK(read_conn.Execute("CREATE TABLE t (i INT)"));
    ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
    ASSERT_RESULT(read_conn.FetchMatrix("SELECT * FROM t", 0, 1));
    ASSERT_OK(write_conn.Execute("INSERT INTO t VALUES (1)"));
    ASSERT_NO_FATALS(AssertAborted(ResultToStatus(read_conn.Fetch("SELECT * FROM t"))));
  }
  {
    auto read_conn = ASSERT_RESULT(Connect());
    auto write_conn = ASSERT_RESULT(Connect());

    ASSERT_OK(read_conn.Execute("CREATE TABLE t2 (i INT PRIMARY KEY)"));
    ASSERT_OK(read_conn.Execute("INSERT INTO t2 VALUES (1)"));

    ASSERT_OK(read_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
    ASSERT_RESULT(read_conn.FetchMatrix("SELECT * FROM t2", 1, 1));
    ASSERT_OK(write_conn.Execute("DELETE FROM t2 WHERE i = 1"));
    ASSERT_NO_FATALS(AssertAborted(ResultToStatus(read_conn.Fetch("SELECT * FROM t2"))));
  }
}

class PgMiniSmallWriteBufferTest : public PgMiniTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 256_KB;
    PgMiniTest::SetUp();
  }
};

TEST_F(PgMiniTest, TruncateColocatedBigTable) {
  // Simulate truncating a big colocated table with multiple sst files flushed to disk.
  // To repro issue https://github.com/yugabyte/yugabyte-db/issues/15206
  // When using bloom filter, it might fail to find the table tombstone because it's stored in
  // a different sst file than the key is currently seeking.

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("create tablegroup tg1"));
  ASSERT_OK(conn.Execute("create table t1(k int primary key) tablegroup tg1"));
  const auto& peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  tablet::TabletPeerPtr tablet_peer = nullptr;
  tablet::TabletPtr tablet = nullptr;
  for (auto peer : peers) {
    tablet = ASSERT_RESULT(peer->shared_tablet());
    if (tablet->regular_db()) {
      tablet_peer = peer;
      break;
    }
  }
  ASSERT_NE(tablet_peer, nullptr);

  // Insert 2 rows, and flush.
  ASSERT_OK(conn.Execute("insert into t1 values (1)"));
  ASSERT_OK(conn.Execute("insert into t1 values (2)"));
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  // Truncate the table, and flush. Tabletombstone should be in a seperate sst file.
  ASSERT_OK(conn.Execute("TRUNCATE t1"));
  SleepFor(1s);
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  // Check if the row still visible.
  ASSERT_OK(conn.FetchMatrix("select k from t1 where k = 1", 0, 1));

  // Check if hit dup key error.
  ASSERT_OK(conn.Execute("insert into t1 values (1)"));
}

TEST_F_EX(PgMiniTest, BulkCopyWithRestart, PgMiniSmallWriteBufferTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_allow_duplicating_repeatable_read_queries) = true;

  const std::string kTableName = "key_value";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (key INTEGER NOT NULL PRIMARY KEY, value VARCHAR)",
      kTableName));

  TestThreadHolder thread_holder;
  constexpr int kTotalBatches = RegularBuildVsSanitizers(50, 5);
  constexpr int kBatchSize = 1000;
  constexpr int kValueSize = 128;

  std::atomic<int> key(0);

  thread_holder.AddThreadFunctor([this, &kTableName, &stop = thread_holder.stop_flag(), &key] {
    SetFlagOnExit set_flag(&stop);
    auto connection = ASSERT_RESULT(Connect());

    auto se = ScopeExit([&key] {
      LOG(INFO) << "Total keys: " << key;
    });

    while (!stop.load(std::memory_order_acquire) && key < kBatchSize * kTotalBatches) {
      ASSERT_OK(connection.CopyFromStdin(
          kTableName,
          [&key](PGConn::RowMaker<int32_t, std::string_view>& row) {
            for (int j = 0; j != kBatchSize; ++j) {
              row(++key, RandomHumanReadableString(kValueSize));
            }
          }));
    }
  });

  thread_holder.AddThread(RestartsThread(cluster_.get(), 5s, &thread_holder.stop_flag()));

  thread_holder.WaitAndStop(120s); // Actually will stop when enough batches were copied

  ASSERT_EQ(key.load(std::memory_order_relaxed), kTotalBatches * kBatchSize);

  LOG(INFO) << "Restarting cluster";
  ASSERT_OK(RestartCluster());

  ASSERT_OK(WaitFor([this, &key, &kTableName] {
    auto intents_count = CountIntents(cluster_.get());
    LOG(INFO) << "Intents count: " << intents_count;

    if (intents_count <= 5000) {
      return true;
    }

    // We cleanup only transactions that were completely aborted/applied before last replication
    // happens.
    // So we could get into situation when intents of the last transactions are not cleaned.
    // To avoid such scenario in this test we write one more row to allow cleanup.
    // As the previous connection might have been dead (from the cluster restart), do the insert
    // from a new connection.
    auto new_conn = EXPECT_RESULT(Connect());
    EXPECT_OK(new_conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, '$2')",
              kTableName, ++key, RandomHumanReadableString(kValueSize)));
    return false;
  }, 10s * kTimeMultiplier, "Intents cleanup", 200ms));
}

TEST_F_EX(PgMiniTest, SmallParallelScan, PgMiniTestSingleNode) {
  const std::string kDatabaseName = "testdb";
  constexpr auto kNumRows = 100;

  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocation=true", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  ASSERT_OK(conn.Execute("CREATE TABLE t (k int, primary key(k ASC)) with (colocation=true)"));

  LOG(INFO) << "Loading data";

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO t SELECT i FROM generate_series(1, $0) i", kNumRows));

  ASSERT_OK(conn.Execute("SET yb_parallel_range_rows to 10"));
  ASSERT_OK(conn.Execute("SET yb_enable_base_scans_cost_model to true"));
  ASSERT_OK(conn.Execute("SET force_parallel_mode = TRUE"));

  LOG(INFO) << "Starting scan";
  auto res = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM t"));
  ASSERT_EQ(res, kNumRows);

  LOG(INFO) << "Starting transaction";
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  res = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM t"));
  ASSERT_EQ(res, kNumRows);

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO t SELECT i FROM generate_series($0, $1) i",
                               kNumRows + 1, kNumRows * 2));
  res = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM t"));
  ASSERT_EQ(res, kNumRows * 2);
  ASSERT_OK(conn.CommitTransaction());
}

void PgMiniTest::TestForeignKey(IsolationLevel isolation_level) {
  const std::string kDataTable = "data";
  const std::string kReferenceTable = "reference";
  constexpr int kRows = 10;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (id int NOT NULL, name VARCHAR, PRIMARY KEY (id))",
      kReferenceTable));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (ref_id INTEGER, data_id INTEGER, name VARCHAR, "
          "PRIMARY KEY (ref_id, data_id))",
      kDataTable));
  ASSERT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0 ADD CONSTRAINT fk FOREIGN KEY(ref_id) REFERENCES $1(id) "
          "ON DELETE CASCADE",
      kDataTable, kReferenceTable));

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES ($1, 'reference_$1')", kReferenceTable, 1));

  for (int i = 1; i <= kRows; ++i) {
    ASSERT_OK(conn.StartTransaction(isolation_level));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES ($1, $2, 'data_$2')", kDataTable, 1, i));
    ASSERT_OK(conn.CommitTransaction());
  }

  ASSERT_OK(WaitFor([this] {
    return CountIntents(cluster_.get()) == 0;
  }, 15s, "Intents cleanup"));
}

TEST_F(PgMiniTest, ForeignKeySerializable) {
  TestForeignKey(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F(PgMiniTest, ForeignKeySnapshot) {
  TestForeignKey(IsolationLevel::SNAPSHOT_ISOLATION);
}

TEST_F(PgMiniTest, ConcurrentSingleRowUpdate) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT PRIMARY KEY, counter INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1, 0)"));
  const size_t thread_count = 10;
  const size_t increment_per_thread = 5;
  {
    CountDownLatch latch(thread_count);
    TestThreadHolder thread_holder;
    for (size_t i = 0; i < thread_count; ++i) {
      thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag(), &latch] {
        auto thread_conn = ASSERT_RESULT(Connect());
        latch.CountDown();
        latch.Wait();
        for (size_t j = 0; j < increment_per_thread; ++j) {
          ASSERT_OK(thread_conn.Execute("UPDATE t SET counter = counter + 1 WHERE k = 1"));
        }
      });
    }
  }
  auto counter = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT counter FROM t WHERE k = 1"));
  ASSERT_EQ(thread_count * increment_per_thread, counter);
}

TEST_F(PgMiniTest, DropDBUpdateSysTablet) {
  const std::string kDatabaseName = "testdb";
  PGConn conn = ASSERT_RESULT(Connect());
  std::array<int, 4> num_tables;

  auto* catalog_manager = &ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto sys_tablet = ASSERT_RESULT(catalog_manager->GetTabletInfo(master::kSysCatalogTabletId));
  {
    auto tablet_lock = sys_tablet->LockForWrite();
    num_tables[0] = tablet_lock->pb.table_ids_size();
  }
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  {
    auto tablet_lock = sys_tablet->LockForWrite();
    num_tables[1] = tablet_lock->pb.table_ids_size();
  }
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  {
    auto tablet_lock = sys_tablet->LockForWrite();
    num_tables[2] = tablet_lock->pb.table_ids_size();
  }
  // Make sure that the system catalog tablet table_ids is persisted.
  ASSERT_OK(RestartCluster());
  catalog_manager = &ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  sys_tablet = ASSERT_RESULT(catalog_manager->GetTabletInfo(master::kSysCatalogTabletId));
  {
    auto tablet_lock = sys_tablet->LockForWrite();
    num_tables[3] = tablet_lock->pb.table_ids_size();
  }
  ASSERT_LT(num_tables[0], num_tables[1]);
  ASSERT_EQ(num_tables[0], num_tables[2]);
  ASSERT_EQ(num_tables[0], num_tables[3]);
}

TEST_F(PgMiniTest, DropDBMarkDeleted) {
  const std::string kDatabaseName = "testdb";
  constexpr auto kSleepTime = 500ms;
  constexpr int kMaxNumSleeps = 20;
  auto *catalog_manager = &ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  PGConn conn = ASSERT_RESULT(Connect());

  ASSERT_FALSE(catalog_manager->AreTablesDeletingOrHiding());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  // System tables should be deleting then deleted.
  int num_sleeps = 0;
  while (catalog_manager->AreTablesDeletingOrHiding() && (num_sleeps++ != kMaxNumSleeps)) {
    LOG(INFO) << "Tables are deleting...";
    std::this_thread::sleep_for(kSleepTime);
  }
  ASSERT_FALSE(catalog_manager->AreTablesDeletingOrHiding())
      << "Tables should have finished deleting";
  // Make sure that the table deletions are persisted.
  ASSERT_OK(RestartCluster());
  // Refresh stale local variable after RestartSync.
  catalog_manager = &ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  ASSERT_FALSE(catalog_manager->AreTablesDeletingOrHiding());
}

TEST_F(PgMiniTest, DropDBWithTables) {
  const std::string kDatabaseName = "testdb";
  const std::string kTablePrefix = "testt";
  constexpr auto kSleepTime = 500ms;
  constexpr int kMaxNumSleeps = 20;
  int num_tables_before, num_tables_after;
  auto *catalog_manager = &ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  PGConn conn = ASSERT_RESULT(Connect());
  auto sys_tablet = ASSERT_RESULT(catalog_manager->GetTabletInfo(master::kSysCatalogTabletId));

  {
    auto tablet_lock = sys_tablet->LockForWrite();
    num_tables_before = tablet_lock->pb.table_ids_size();
  }
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  {
    PGConn conn_new = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    for (int i = 0; i < 10; ++i) {
      ASSERT_OK(conn_new.ExecuteFormat("CREATE TABLE $0$1 (i int)", kTablePrefix, i));
    }
    ASSERT_OK(conn_new.ExecuteFormat("INSERT INTO $0$1 (i) VALUES (1), (2), (3)", kTablePrefix, 5));
  }
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  // User and system tables should be deleting then deleted.
  int num_sleeps = 0;
  while (catalog_manager->AreTablesDeletingOrHiding() && (num_sleeps++ != kMaxNumSleeps)) {
    LOG(INFO) << "Tables are deleting...";
    std::this_thread::sleep_for(kSleepTime);
  }
  ASSERT_FALSE(catalog_manager->AreTablesDeletingOrHiding())
      << "Tables should have finished deleting";
  // Make sure that the table deletions are persisted.
  ASSERT_OK(RestartCluster());
  catalog_manager = &ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  sys_tablet = ASSERT_RESULT(catalog_manager->GetTabletInfo(master::kSysCatalogTabletId));
  ASSERT_FALSE(catalog_manager->AreTablesDeletingOrHiding());
  {
    auto tablet_lock = sys_tablet->LockForWrite();
    num_tables_after = tablet_lock->pb.table_ids_size();
  }
  ASSERT_EQ(num_tables_before, num_tables_after);
}

TEST_F(PgMiniTest, BigSelect) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));

  constexpr size_t kRows = 400;
  constexpr size_t kValueSize = RegularBuildVsSanitizers(256_KB, 4_KB);

  for (size_t i = 0; i != kRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t VALUES ($0, '$1')", i, RandomHumanReadableString(kValueSize)));
  }

  auto start = MonoTime::Now();
  auto res = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(DISTINCT(value)) FROM t"));
  auto finish = MonoTime::Now();
  LOG(INFO) << "Time: " << finish - start;
  ASSERT_EQ(res, kRows);
}

TEST_F(PgMiniTest, MoveMaster) {
  for (;;) {
    client::YBTableName transactions_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    auto result = client_->GetYBTableInfo(transactions_table_name);
    if (result.ok()) {
      LOG(INFO) << "Transactions table info: " << result->table_id;
      break;
    }
    LOG(INFO) << "Waiting for transactions table";
    std::this_thread::sleep_for(1s);
  }
  ShutdownAllMasters(cluster_.get());
  cluster_->mini_master(0)->set_pass_master_addresses(false);
  ASSERT_OK(StartAllMasters(cluster_.get()));

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(WaitFor([&conn] {
    auto status = conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)");
    WARN_NOT_OK(status, "Failed to create table");
    return status.ok();
  }, 15s * kTimeMultiplier, "Create table"));
}

TEST_F(PgMiniTest, DDLWithRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_force_master_leader_resolution) = true;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn.Execute("CREATE TABLE t (a int PRIMARY KEY)"));
  ASSERT_OK(conn.CommitTransaction());

  ShutdownAllMasters(cluster_.get());

  LOG(INFO) << "Start masters";
  ASSERT_OK(StartAllMasters(cluster_.get()));

  auto res = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM t"));
  ASSERT_EQ(res, 0);
}

TEST_F(PgMiniTest, CreateDatabase) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;
  auto conn = ASSERT_RESULT(Connect());
  const std::string kDatabaseName = "testdb";
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  ASSERT_OK(RestartCluster());
}

void PgMiniTest::TestBigInsert(bool restart) {
  constexpr int64_t kNumRows = RegularBuildVsSanitizers(100000, 10000);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) = kNumRows / 10;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (a int PRIMARY KEY) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (0)"));

  TestThreadHolder thread_holder;

  std::atomic<int> post_insert_reads{0};
  std::atomic<bool> restarted{false};
  thread_holder.AddThreadFunctor(
      [this, &stop = thread_holder.stop_flag(), &post_insert_reads, &restarted] {
    auto connection = ASSERT_RESULT(Connect());
    while (!stop.load(std::memory_order_acquire)) {
      auto res = connection.FetchRow<PGUint64>("SELECT SUM(a) FROM t");
      if (!res.ok()) {
        auto msg = res.status().message().ToBuffer();
        // With object locking enabled the tserver shuts its object lock manager down before
        // stopping PG, so an in-flight query may be rejected before the connection is closed.
        ASSERT_TRUE(msg.find("server closed the connection unexpectedly") != std::string::npos ||
                    msg.find("Object Lock Manager Shutdown") != std::string::npos)
            << res.status();
        while (!restarted.load() && !stop.load()) {
          std::this_thread::sleep_for(10ms);
        }
        std::this_thread::sleep_for(1s);
        LOG(INFO) << "Establishing new connection";
        connection = ASSERT_RESULT(Connect());
        restarted = false;
        continue;
      }

      // We should see zero or full sum only.
      if (*res) {
        ASSERT_EQ(*res, kNumRows * (kNumRows + 1) / 2);
        ++post_insert_reads;
      }
    }
  });

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t SELECT generate_series(1, $0)", kNumRows));

  if (restart) {
    LOG(INFO) << "Restart cluster";
    ASSERT_OK(RestartCluster());
    restarted = true;
  }

  ASSERT_OK(WaitFor([this, &post_insert_reads] {
    auto intents_count = CountIntents(cluster_.get());
    LOG(INFO) << "Intents count: " << intents_count;

    return intents_count == 0 && post_insert_reads.load(std::memory_order_acquire) > 0;
  }, 60s * kTimeMultiplier, "Intents cleanup", 200ms));

  thread_holder.Stop();

  FlushAndCompactTablets();

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      continue;
    }
    auto db = tablet->regular_db();
    if (!db) {
      continue;
    }
    rocksdb::ReadOptions read_opts;
    read_opts.query_id = rocksdb::kDefaultQueryId;
    std::unique_ptr<rocksdb::Iterator> iter(db->NewIterator(read_opts));

    for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
      Slice key = iter->key();
      ASSERT_FALSE(key.TryConsumeByte(dockv::KeyEntryTypeAsChar::kTransactionApplyState))
          << "Key: " << iter->key().ToDebugString() << ", value: " << iter->value().ToDebugString();
    }
  }
}

TEST_F(PgMiniTest, BigInsert) {
  TestBigInsert(/* restart= */ false);
}

TEST_F(PgMiniTest, BigInsertWithRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_apply_intents_task_injected_delay_ms) = 200;
  TestBigInsert(/* restart= */ true);
}

TEST_F(PgMiniTest, BigInsertWithDropTable) {
  constexpr int kNumRows = 10000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) = kNumRows / 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_apply_intents_task_injected_delay_ms) = 200;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(id int) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t SELECT generate_series(1, $0)", kNumRows));
  ASSERT_OK(conn.Execute("DROP TABLE t"));
}

void PgMiniTest::TestConcurrentDeleteRowAndUpdateColumn(bool select_before_update) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE t (i INT PRIMARY KEY, j INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO t VALUES (1, 10), (2, 20), (3, 30)"));
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  if (select_before_update) {
    ASSERT_OK(conn1.Fetch("SELECT * FROM t"));
  }
  ASSERT_OK(conn2.Execute("DELETE FROM t WHERE i = 2"));
  auto status = conn1.Execute("UPDATE t SET j = 21 WHERE i = 2");
  if (select_before_update) {
    ASSERT_TRUE(IsSerializeAccessError(status)) << status;
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), "Value write after transaction start");
    return;
  }
  ASSERT_OK(status);
  ASSERT_OK(conn1.CommitTransaction());
  const auto rows = ASSERT_RESULT((conn1.FetchRows<int32_t, int32_t>(
      "SELECT * FROM t ORDER BY i")));
  const decltype(rows) expected_rows = {{1, 10}, {3, 30}};
  ASSERT_EQ(rows, expected_rows);
}

TEST_F(PgMiniTest, ConcurrentDeleteRowAndUpdateColumn) {
  TestConcurrentDeleteRowAndUpdateColumn(/* select_before_update= */ false);
}

TEST_F(PgMiniTest, ConcurrentDeleteRowAndUpdateColumnWithSelect) {
  TestConcurrentDeleteRowAndUpdateColumn(/* select_before_update= */ true);
}

// The test checks catalog version is updated only in case of changes in sys catalog.
TEST_F(PgMiniTest, CatalogVersionUpdateIfNeeded) {
  auto conn = ASSERT_RESULT(Connect());
  const auto schema_ddl = "CREATE SCHEMA IF NOT EXISTS test";
  const auto first_create_schema = ASSERT_RESULT(
      IsCatalogVersionChangedDuringDdl(&conn, schema_ddl));
  ASSERT_TRUE(first_create_schema);
  const auto second_create_schema = ASSERT_RESULT(
      IsCatalogVersionChangedDuringDdl(&conn, schema_ddl));
  ASSERT_FALSE(second_create_schema);
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
  const auto add_column_ddl = "ALTER TABLE t ADD COLUMN IF NOT EXISTS v INT";
  const auto first_add_column = ASSERT_RESULT(
      IsCatalogVersionChangedDuringDdl(&conn, add_column_ddl));
  ASSERT_TRUE(first_add_column);
  const auto second_add_column = ASSERT_RESULT(
      IsCatalogVersionChangedDuringDdl(&conn, add_column_ddl));
  ASSERT_FALSE(second_add_column);
}

// Test that we don't sequential restart read on the same table if intents were written
// after the first read. GH #6972.
TEST_F(PgMiniTest, NoRestartSecondRead) {
  // Create an initial first connection without max_clock_skew_usec set. Postgres crashes otherwise.
  ASSERT_RESULT(Connect());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_clock_skew_usec) = 1000000000LL * kTimeMultiplier;
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE t (a int PRIMARY KEY, b int) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn1.Execute("INSERT INTO t VALUES (1, 1), (2, 1), (3, 1)"));
  auto start_time = MonoTime::Now();
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  LOG(INFO) << "Select1";
  auto res = ASSERT_RESULT(conn1.FetchRow<int32_t>("SELECT b FROM t WHERE a = 1"));
  ASSERT_EQ(res, 1);
  LOG(INFO) << "Update";
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.Execute("UPDATE t SET b = 2 WHERE a = 2"));
  ASSERT_OK(conn2.CommitTransaction());
  auto update_time = MonoTime::Now();
  ASSERT_LE(update_time, start_time + FLAGS_max_clock_skew_usec * 1us);
  LOG(INFO) << "Select2";
  res = ASSERT_RESULT(conn1.FetchRow<int32_t>("SELECT b FROM t WHERE a = 2"));
  ASSERT_EQ(res, 1);
  ASSERT_OK(conn1.CommitTransaction());
}

TEST_F(PgMiniTest, AlterTableWithReplicaIdentity) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("set yb_enable_replica_identity = true"));
  ASSERT_OK(conn.Execute("CREATE TABLE t (a int PRIMARY KEY, b int) SPLIT INTO 3 TABLETS"));

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));
  auto tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);

  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(CHANGE, tablet_peers, table_id));

  ASSERT_OK(conn.Execute("ALTER TABLE t REPLICA IDENTITY FULL"));
  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(FULL, tablet_peers, table_id));

  ASSERT_OK(conn.Execute("ALTER TABLE t REPLICA IDENTITY CHANGE"));
  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(CHANGE, tablet_peers, table_id));

  ASSERT_OK(conn.Execute("ALTER TABLE t REPLICA IDENTITY DEFAULT"));
  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(DEFAULT, tablet_peers, table_id));

  ASSERT_OK(conn.Execute("ALTER TABLE t REPLICA IDENTITY NOTHING"));
  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(NOTHING, tablet_peers, table_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_default_replica_identity) = "FULL";
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (a int primary key)"));
  table_id = ASSERT_RESULT(GetTableIDFromTableName("t1"));
  tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(FULL, tablet_peers, table_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_default_replica_identity) = "DEFAULT";
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (a int primary key)"));
  table_id = ASSERT_RESULT(GetTableIDFromTableName("t2"));
  tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(DEFAULT, tablet_peers, table_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_default_replica_identity) = "NOTHING";
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (a int primary key)"));
  table_id = ASSERT_RESULT(GetTableIDFromTableName("t3"));
  tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_OK(IsReplicaIdentityPopulatedInTabletPeers(NOTHING, tablet_peers, table_id));

  // If an invalid value is provided to the flag ysql_yb_default_replica_identity, table creation
  // will fail.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_default_replica_identity) = "INVALID";
  ASSERT_NOK(conn.Execute("CREATE TABLE t4 (a int primary key)"));
}

TEST_F(PgMiniTest, TestNoStaleDataOnColocationIdReuse) {
  const auto kColocatedTableName = "colo_test";
  const auto kColocatedTableName2 = "colo_test2";
  const auto kDatabaseName = "testdb";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocated=true", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (v1 int) WITH (colocation_id=20001);",
      kColocatedTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (110), (111), (112);", kColocatedTableName));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0;", kColocatedTableName));

  // Create colocated table with different table name and reuse the same colocation_id.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (v1 int) WITH (colocation_id=20001);",
      kColocatedTableName2));
  // Verify read output is empty. This is to ensure that the tombstone is not being checked.
  auto scan_result =
      ASSERT_RESULT(conn.FetchAllAsString(Format("SELECT * FROM $0", kColocatedTableName2)));
  ASSERT_TRUE(scan_result.empty());
}

TEST_F_EX(PgMiniTest, VerifyTombstoneTimeCache, PgMiniTestSingleNode) {
  const std::string kDbName = "testdb";
  const std::string kTableName = "tombstone_test";
  const int kColocationId = 20001;
  const std::vector<int> kInitialData = {110, 111, 112};
  const std::vector<int> kNewData = {123};

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)",
      kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1), ($2), ($3)",
      kTableName, kInitialData[0], kInitialData[1], kInitialData[2]));

  auto result = ASSERT_RESULT(conn.FetchRows<int>(
      Format("SELECT v FROM $0 ORDER BY v", kTableName)));
  ASSERT_VECTORS_EQ(result, kInitialData);

  auto VerifyTombstoneTimeCache = [&](bool tombstone_time_should_exist) {
    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);

    for (const auto& peer : peers) {
      auto table_info = ASSERT_RESULT(peer->tablet_metadata()->GetTableInfo(kColocationId));
      ASSERT_TRUE(table_info && table_info->doc_read_context);
      auto tombstone_time = table_info->doc_read_context->table_tombstone_time();
      ASSERT_TRUE(tombstone_time.has_value());
      ASSERT_EQ(tombstone_time->is_valid(), tombstone_time_should_exist);
    }
  };

  // Verify no cached tombstone time before table drop.
  VerifyTombstoneTimeCache(/*tombstone_time_should_exist = */false);

  // Drop colocated table should write tombstone mark.
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", kTableName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)",
      kTableName, kColocationId));

  result = ASSERT_RESULT(conn.FetchRows<int>(Format("SELECT v FROM $0 ORDER BY v", kTableName)));
  ASSERT_TRUE(result.empty());
  // Verify tombstone mark hybrid time is cached
  VerifyTombstoneTimeCache(/*tombstone_time_should_exist = */true);

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, kNewData[0]));
  result = ASSERT_RESULT(conn.FetchRows<int>(Format("SELECT v FROM $0 ORDER BY v", kTableName)));
  ASSERT_VECTORS_EQ(result, kNewData);

  // Confirm tombstone cache is reloaded after restart.
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  result = ASSERT_RESULT(conn.FetchRows<int>(Format("SELECT v FROM $0 ORDER BY v", kTableName)));
  ASSERT_VECTORS_EQ(result, kNewData);
  VerifyTombstoneTimeCache(/*tombstone_time_should_exist = */true);

  // Verify that no tombstone time has been cached for pg_class.
  ASSERT_OK(conn.FetchRows<int64_t>("SELECT count(*) from pg_class"));
  auto pg_class_table_id = ASSERT_RESULT(GetTableIDFromTableName("pg_class"));
  const auto& sys_catalog = cluster_->mini_master(0)->master()->sys_catalog();
  auto table_info = ASSERT_RESULT(
      sys_catalog.tablet_peer()->tablet_metadata()->GetTableInfo(pg_class_table_id));
  ASSERT_TRUE(table_info && table_info->doc_read_context);
  auto tombstone_time = table_info->doc_read_context->table_tombstone_time();
  ASSERT_FALSE(tombstone_time.has_value());
}

namespace {

// Returns true if the DocDB dump contains a table-level tombstone (a DEL at the bare
// colocation doc key) for the given colocation id.
bool DumpHasTableTombstone(const std::string& dump, int colocation_id) {
  const auto prefix = Format("DocKey(ColocationId=$0, [], [])", colocation_id);
  size_t pos = 0;
  while ((pos = dump.find(prefix, pos)) != std::string::npos) {
    auto eol = dump.find('\n', pos);
    if (eol == std::string::npos) {
      eol = dump.size();
    }
    if (dump.substr(pos, eol - pos).find("-> DEL") != std::string::npos) {
      return true;
    }
    pos = eol;
  }
  return false;
}

} // namespace

// Regression test for the colocated-table tombstone-time cache never being invalidated on
// TRUNCATE (flag enable_colocated_table_tombstone_cache, default on). An "unsafe" TRUNCATE
// (yb_enable_alter_table_rewrite = false) deletes a colocated table's data by writing one
// table-level tombstone over the table's colocation prefix, and reads filter rows against
// that tombstone time. The per-DocReadContext cache of that lookup is populated on the first
// read and has no invalidation edge, and the tombstone-based TRUNCATE does not replace the
// DocReadContext, so a node that cached the pre-truncate value keeps serving every truncated
// row as live; an INSERT after the TRUNCATE mixes the new row with resurrected ones. With
// RF > 1 a replica with a cold cache correctly sees the table as empty while the stale-cache
// node resurrects the rows - divergent results for the same query - but RF1 is enough to
// demonstrate the mechanism.
// With the #32724 fix, the final assertions below must pass (they failed before
// TRUNCATE invalidated / refreshed the cached tombstone time).
TEST_F_EX(PgMiniTest, TruncateVisibleThroughTombstoneCache, PgMiniTestSingleNode) {
  const std::string kDbName = "testdb";
  const std::string kTableName = "truncate_cache_test";
  const int kColocationId = 20001;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (110), (111), (112)", kTableName));

  // Populate the cache: the first read stores the (absent) tombstone time in the
  // DocReadContext.
  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 3);

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_EQ(peers.size(), 1);
  auto peer = peers.front();
  auto read_cached_tombstone_time = [&]() {
    auto table_info = CHECK_RESULT(peer->tablet_metadata()->GetTableInfo(kColocationId));
    CHECK(table_info && table_info->doc_read_context);
    return table_info->doc_read_context->table_tombstone_time();
  };
  auto doc_read_context_ptr = [&]() -> const void* {
    auto table_info = CHECK_RESULT(peer->tablet_metadata()->GetTableInfo(kColocationId));
    return table_info->doc_read_context.get();
  };

  // Trigger guard 1: the read above populated the cache; for a never-truncated table the
  // cached value is kInvalid ("no tombstone").
  auto cache_before = read_cached_tombstone_time();
  ASSERT_TRUE(cache_before.has_value());
  ASSERT_FALSE(cache_before->is_valid());
  const auto* context_before = doc_read_context_ptr();

  // The unsafe (non-rewrite) TRUNCATE: writes a table-level tombstone at the colocation
  // prefix instead of switching to a new relfilenode.
  ASSERT_OK(conn.Execute("SET yb_enable_alter_table_rewrite = false"));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));

  // Trigger guard 2: prove the TRUNCATE took the tombstone path. The bytes on disk are
  // correct - this is a read-path bug - so the dump (not a SELECT) is the right probe for
  // this guard; do not "simplify" the wrongness assertions below to use the dump.
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  auto dump = tablet->TEST_DocDBDumpStr();
  ASSERT_TRUE(DumpHasTableTombstone(dump, kColocationId))
      << "TRUNCATE did not write a table-level tombstone (rewrite path taken?). Dump:\n" << dump;

  // The stale read, served through the cached pre-truncate tombstone time.
  auto count_after_truncate = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM $0", kTableName)));
  auto cache_after = read_cached_tombstone_time();
  const auto* context_after = doc_read_context_ptr();

  // Compounding evidence: an INSERT after the TRUNCATE mixes the new row with resurrected
  // ones.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1000)", kTableName));
  auto count_mixed = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM $0", kTableName)));

  // Causation control (passes today and after the fix): with the RUNTIME flag off the read
  // bypasses the cache, sees the tombstone, and returns only the post-truncate row.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_colocated_table_tombstone_cache) = false;
  auto count_uncached = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM $0", kTableName)));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_colocated_table_tombstone_cache) = true;
  ASSERT_EQ(count_uncached, 1)
      << "uncached read after TRUNCATE + one INSERT must see exactly the new row";

  // Correctness assertions for the warm-cache + legacy-TRUNCATE path.
  ASSERT_EQ(count_after_truncate, 0)
      << "TOMBSTONE-CACHE RESURRECTION: post-TRUNCATE read served " << count_after_truncate
      << " deleted rows from the stale cached tombstone time."
      << " Cached tombstone time before TRUNCATE: "
      << (cache_before ? cache_before->ToString() : "<not cached>")
      << ", after: " << (cache_after ? cache_after->ToString() : "<not cached>")
      << " (unchanged => no invalidation edge)."
      << " DocReadContext instance replaced: " << (context_before != context_after ? "yes" : "no")
      << ". Count after post-TRUNCATE INSERT: " << count_mixed
      << " (new row mixed with resurrected rows)."
      << " Uncached (flag-off) count: " << count_uncached << ".";
  ASSERT_EQ(count_mixed, 1)
      << "post-TRUNCATE INSERT followed by count must see exactly the new row";
}

// Control for TruncateVisibleThroughTombstoneCache (passes today): with the default
// yb_enable_alter_table_rewrite = true, TRUNCATE takes the rewrite path (new relfilenode,
// fresh metadata), so reads are correct even with the tombstone-time cache populated. This
// pins the wrongness above to the unsafe-truncate (table-tombstone) path.
TEST_F_EX(PgMiniTest, TruncateRewritePathControl, PgMiniTestSingleNode) {
  const std::string kDbName = "testdb";
  const std::string kTableName = "truncate_cache_test";
  const int kColocationId = 20001;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (110), (111), (112)", kTableName));

  // Populate the tombstone-time cache before the TRUNCATE, like the repro does.
  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 3);

  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));

  count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 0);

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1000)", kTableName));
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 1);
}

// Control for TruncateVisibleThroughTombstoneCache (passes today): a restart rebuilds every
// DocReadContext from the superblock, so the stale cached tombstone time does not survive
// it - the resurrection lasts only until the node restarts.
TEST_F_EX(PgMiniTest, TruncateTombstoneCacheHealedByRestart, PgMiniTestSingleNode) {
  const std::string kDbName = "testdb";
  const std::string kTableName = "truncate_cache_test";
  const int kColocationId = 20001;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (110), (111), (112)", kTableName));

  // Populate the tombstone-time cache, then truncate through the tombstone path.
  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 3);
  ASSERT_OK(conn.Execute("SET yb_enable_alter_table_rewrite = false"));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));

  {
    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(peers.size(), 1);
    auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
    ASSERT_TRUE(DumpHasTableTombstone(tablet->TEST_DocDBDumpStr(), kColocationId))
        << "TRUNCATE did not write a table-level tombstone (rewrite path taken?)";
  }

  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 0);
}

// Regression for #32724: legacy colocated TRUNCATE (yb_enable_alter_table_rewrite=false) must
// invalidate the colocated tombstone-time cache so subsequent reads see an empty table.
TEST_F_EX(PgMiniTest, TruncateColocatedInvalidatesTombstoneCache, PgMiniTestSingleNode) {
  const std::string kDbName = "truncate_tombstone_db";
  const std::string kTableName = "t";
  const int kColocationId = 20002;
  const std::vector<int> kInitialData = {1, 2, 3};

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES ($1), ($2), ($3)",
      kTableName, kInitialData[0], kInitialData[1], kInitialData[2]));

  // Warm the tombstone-time cache (caches "no tombstone").
  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, kInitialData.size());

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_FALSE(peers.empty());
  auto peer = peers.front();
  auto doc_read_context = [&]() -> Result<std::shared_ptr<docdb::DocReadContext>> {
    // Always re-fetch: a TableInfo (and with it the DocReadContext) can be rebuilt underneath us,
    // and asserting on a detached copy would not describe what reads actually see.
    auto table_info = VERIFY_RESULT(peer->tablet_metadata()->GetTableInfo(kColocationId));
    SCHECK(table_info && table_info->doc_read_context, IllegalState, "No DocReadContext");
    return table_info->doc_read_context;
  };
  {
    auto ctx = ASSERT_RESULT(doc_read_context());
    auto cached = ctx->table_tombstone_time();
    ASSERT_TRUE(cached.has_value());
    ASSERT_FALSE(cached->is_valid());
    // Serve-ready / AddTable should have armed the watermark (not left at kMax).
    ASSERT_NE(ctx->tombstone_cache_watermark(), HybridTime::kMax);
  }

  ASSERT_OK(conn.Execute("SET yb_enable_alter_table_rewrite = false"));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));

  count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 0);

  // Cache should now hold a real tombstone time (re-populated by the post-truncate read).
  {
    auto ctx = ASSERT_RESULT(doc_read_context());
    auto cached_after = ctx->table_tombstone_time();
    ASSERT_TRUE(cached_after.has_value());
    ASSERT_TRUE(cached_after->is_valid());
  }

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (4)", kTableName));
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 1);
}

// #32724 (a): a pinned read at R < truncate-HT must not re-poison the cache so that a later
// current-time read resurrects truncated rows.
TEST_F_EX(
    PgMiniTest, TruncateColocatedPinnedReadDoesNotRePoisonTombstoneCache, PgMiniTestSingleNode) {
  const std::string kDbName = "truncate_tombstone_pinned_db";
  const std::string kTableName = "t";
  const int kColocationId = 20003;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1), (2), (3)", kTableName));

  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 3);

  auto pinned_us = ASSERT_RESULT(conn.FetchRow<PGUint64>(
      "SELECT ((EXTRACT (EPOCH FROM CURRENT_TIMESTAMP))*1000000)::bigint"));

  ASSERT_OK(conn.Execute("SET yb_enable_alter_table_rewrite = false"));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));

  // Pinned-old read: rows still visible as of pre-truncate time (cold path / below watermark).
  ASSERT_OK(conn.ExecuteFormat("SET yb_read_time TO $0", pinned_us));
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 3);

  // Current read must stay empty: pinned read must not have re-cached "absence" under the old
  // watermark into the post-truncate epoch.
  ASSERT_OK(conn.Execute("SET yb_read_time TO 0"));
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName)));
  ASSERT_EQ(count, 0);
}

// #32724 (b): successive legacy truncates keep the single cache slot + watermark coherent.
TEST_F_EX(PgMiniTest, TruncateColocatedMultipleTruncates, PgMiniTestSingleNode) {
  const std::string kDbName = "truncate_tombstone_multi_db";
  const std::string kTableName = "t";
  const int kColocationId = 20004;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.Execute("SET yb_enable_alter_table_rewrite = false"));

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1), (2)", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 2);
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 0);

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (3), (4), (5)", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 3);
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 0);

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_FALSE(peers.empty());
  auto table_info = ASSERT_RESULT(peers.front()->tablet_metadata()->GetTableInfo(kColocationId));
  auto cached = table_info->doc_read_context->table_tombstone_time();
  ASSERT_TRUE(cached.has_value());
  ASSERT_TRUE(cached->is_valid());
  // The invariant is only that the watermark covers the cached tombstone. Anything else that arms
  // the context (schema rebuild, ADD_TABLE replay) may push the watermark past the last truncate,
  // so requiring equality here would make this test fail on unrelated re-arms.
  ASSERT_GE(table_info->doc_read_context->tombstone_cache_watermark(), cached->hybrid_time());
}

// #32724 (c): after truncate + schema rebuild (ALTER arms a fresh context), pinned then current
// reads stay correct (no kMin-init / unarmed-after-rebuild resurrection).
TEST_F_EX(PgMiniTest, TruncateColocatedAfterAlterSchema, PgMiniTestSingleNode) {
  const std::string kDbName = "truncate_tombstone_alter_db";
  const std::string kTableName = "t";
  const int kColocationId = 20005;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1), (2), (3)", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 3);

  auto pinned_us = ASSERT_RESULT(conn.FetchRow<PGUint64>(
      "SELECT ((EXTRACT (EPOCH FROM CURRENT_TIMESTAMP))*1000000)::bigint"));

  ASSERT_OK(conn.Execute("SET yb_enable_alter_table_rewrite = false"));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 0);

  // Rebuild DocReadContext (copy resets watermark to kMax) then AlterSchema re-arms at SafeTime.
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN extra int", kTableName));

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_FALSE(peers.empty());
  auto table_info = ASSERT_RESULT(peers.front()->tablet_metadata()->GetTableInfo(kColocationId));
  ASSERT_NE(table_info->doc_read_context->tombstone_cache_watermark(), HybridTime::kMax);

  ASSERT_OK(conn.ExecuteFormat("SET yb_read_time TO $0", pinned_us));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 3);

  ASSERT_OK(conn.Execute("SET yb_read_time TO 0"));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 0);
}

// #32724 (d): an unarmed DocReadContext (watermark kMax) fails closed: cache ineligible.
TEST(DocReadContextTombstoneCacheTest, UnarmedWatermarkFailsClosed) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddKeyColumn("k", DataType::INT32));
  auto schema = builder.Build();
  schema.set_colocation_id(42);
  auto ctx = docdb::DocReadContext::TEST_Create(schema);
  ASSERT_EQ(ctx.tombstone_cache_watermark(), HybridTime::kMax);
  ASSERT_FALSE(ctx.table_tombstone_time().has_value());

  // kMax is an "unarmed" sentinel, not a numeric bound, so no read_ht may be eligible, including
  // read_ht == kMax, which a naive read_ht >= watermark test would let through.
  ASSERT_FALSE(ctx.IsTombstoneCacheEligible(HybridTime::kInvalid));
  ASSERT_FALSE(ctx.IsTombstoneCacheEligible(HybridTime::kMin));
  ASSERT_FALSE(ctx.IsTombstoneCacheEligible(HybridTime::FromMicros(1)));
  ASSERT_FALSE(ctx.IsTombstoneCacheEligible(HybridTime::kMax));

  // A populated entry must stay invisible while unarmed: the guard cannot rely on the slot being
  // empty, since anything that arms and then re-unarms would leave a stale value behind. Assert the
  // populate took effect first, otherwise the ineligibility below would hold trivially.
  ctx.set_table_tombstone_time(DocHybridTime::kInvalid, ctx.tombstone_cache_generation());
  ASSERT_TRUE(ctx.table_tombstone_time().has_value());
  ASSERT_FALSE(ctx.IsTombstoneCacheEligible(HybridTime::kMax));

  // Arming makes it eligible again, which pins the assertions above to the watermark and not to
  // some unrelated reason for rejecting every read.
  ctx.AdvanceTombstoneCacheWatermark(HybridTime::FromMicros(100));
  ASSERT_TRUE(ctx.IsTombstoneCacheEligible(HybridTime::FromMicros(100)));
  ASSERT_FALSE(ctx.IsTombstoneCacheEligible(HybridTime::FromMicros(99)));
}

// #32724 (e): set must stamp the caller's pre-fetch generation, not the post-truncate epoch.
TEST(DocReadContextTombstoneCacheTest, SetStampsCallerGeneration) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddKeyColumn("k", DataType::INT32));
  auto schema = builder.Build();
  schema.set_colocation_id(42);
  auto ctx = docdb::DocReadContext::TEST_Create(schema);

  ctx.AdvanceTombstoneCacheWatermark(HybridTime::FromMicros(100));
  const auto gen_before = ctx.tombstone_cache_generation();

  // Truncate advances generation after the fetch, before set.
  ctx.OnTableTombstoneWritten(HybridTime::FromMicros(200));
  const auto gen_after = ctx.tombstone_cache_generation();
  ASSERT_NE(gen_before, gen_after);

  // Stamping absence under the pre-truncate epoch must miss for current readers.
  ctx.set_table_tombstone_time(DocHybridTime::kInvalid, gen_before);
  ASSERT_FALSE(ctx.table_tombstone_time().has_value());

  // Stamping under the current epoch is visible (sanity check of the hit path).
  ctx.set_table_tombstone_time(DocHybridTime::kInvalid, gen_after);
  ASSERT_TRUE(ctx.table_tombstone_time().has_value());
  ASSERT_EQ(*ctx.table_tombstone_time(), DocHybridTime::kInvalid);
}

// #32724: a tombstone HT above the watermark must not land in the cache (empty-table polarity
// in the commit-to-apply window). Absence (kInvalid) has no HT to compare and remains storable.
TEST(DocReadContextTombstoneCacheTest, RejectTombstoneAboveWatermark) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddKeyColumn("k", DataType::INT32));
  auto schema = builder.Build();
  schema.set_colocation_id(42);
  auto ctx = docdb::DocReadContext::TEST_Create(schema);

  ctx.AdvanceTombstoneCacheWatermark(HybridTime::FromMicros(100));
  const auto gen = ctx.tombstone_cache_generation();

  // Tombstone T > watermark: reject (would hide rows for watermark <= read_ht < T).
  ctx.set_table_tombstone_time(
      DocHybridTime(HybridTime::FromMicros(200), /*write_id=*/1), gen);
  ASSERT_FALSE(ctx.table_tombstone_time().has_value());

  // Tombstone at the watermark: accept.
  ctx.set_table_tombstone_time(
      DocHybridTime(HybridTime::FromMicros(100), /*write_id=*/1), gen);
  ASSERT_TRUE(ctx.table_tombstone_time().has_value());
  ASSERT_EQ(ctx.table_tombstone_time()->hybrid_time(), HybridTime::FromMicros(100));

  ctx.clear_table_tombstone_time();
  // Absence remains storable regardless of watermark.
  ctx.set_table_tombstone_time(DocHybridTime::kInvalid, gen);
  ASSERT_TRUE(ctx.table_tombstone_time().has_value());
  ASSERT_EQ(*ctx.table_tombstone_time(), DocHybridTime::kInvalid);
}

// #32724 (f): RF3. A replica that held a warm cache while it was a follower must still see the
// legacy TRUNCATE. Followers never run ApplyTruncateColocated (leader-side batch assembly), so
// only the apply-path notify invalidates them; without it, moving leadership onto such a replica
// (or a follower read) resurrects the truncated rows. Every single-node test above passes even
// when the invalidation is leader-only, so this is the one that pins the fix to the apply path.
TEST_F(PgMiniTest, TruncateColocatedInvalidatesTombstoneCacheOnFollower) {
  const std::string kDbName = "truncate_tombstone_rf3_db";
  const std::string kTableName = "t";
  const int kColocationId = 20006;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
  conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (v int PRIMARY KEY) WITH (colocation_id=$1)", kTableName, kColocationId));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1), (2), (3)", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 3);

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
  auto peers = ListTableActiveTabletPeers(cluster_.get(), table_id);
  ASSERT_EQ(peers.size(), NumTabletServers());
  const auto tablet_id = peers.front()->tablet_id();

  auto doc_read_context = [&](const tablet::TabletPeerPtr& peer) {
    auto table_info = CHECK_RESULT(peer->tablet_metadata()->GetTableInfo(kColocationId));
    CHECK(table_info && table_info->doc_read_context);
    return table_info->doc_read_context;
  };

  // Warm every replica with the pre-truncate "no tombstone" answer. The SELECT above only warmed
  // the leader, and the replicas this test is about are the ones that are followers at apply time;
  // stamping directly is the deterministic way to put them in that state.
  for (const auto& peer : peers) {
    auto ctx = doc_read_context(peer);
    ASSERT_NE(ctx->tombstone_cache_watermark(), HybridTime::kMax)
        << "replica " << peer->permanent_uuid() << " was never armed, so it cannot go stale and "
        << "this test would not exercise anything";
    ctx->set_table_tombstone_time(DocHybridTime::kInvalid, ctx->tombstone_cache_generation());
    ASSERT_TRUE(ctx->table_tombstone_time().has_value());
  }

  const auto leader_uuid =
      ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id))->permanent_uuid();

  ASSERT_OK(conn.Execute("SET yb_enable_alter_table_rewrite = false"));
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE $0", kTableName));

  // The tombstone is transactional: commit returns before APPLY finishes on every participant, so
  // a replica may still hold the DocHybridTime::kInvalid stamp we planted until apply runs. Wait
  // until every replica has dropped that "no tombstone" entry (cleared or replaced with a real
  // tombstone HT). Leadership transfer below does not need a similar wait: UpdateTxnOperation uses
  // MVCC, so safe time on the new leader cannot reach `now` until apply completes.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        for (const auto& peer : peers) {
          auto cached = doc_read_context(peer)->table_tombstone_time();
          if (cached.has_value() && !cached->is_valid()) {
            return false;
          }
        }
        return true;
      },
      30s * kTimeMultiplier,
      "Every replica dropped the planted \"no tombstone\" cache entry after TRUNCATE"));

  for (const auto& peer : peers) {
    auto cached = doc_read_context(peer)->table_tombstone_time();
    ASSERT_TRUE(!cached.has_value() || cached->is_valid())
        << "replica " << peer->permanent_uuid()
        << (peer->permanent_uuid() == leader_uuid ? " (leader)" : " (follower)")
        << " kept a cached \"no tombstone\" entry across the TRUNCATE";
  }

  // Now serve reads from a replica that was a follower when the tombstone applied.
  tablet::TabletPeerPtr new_leader;
  for (const auto& peer : peers) {
    if (peer->permanent_uuid() != leader_uuid) {
      new_leader = peer;
      break;
    }
  }
  ASSERT_TRUE(new_leader != nullptr);
  ASSERT_OK(TransferLeadership(cluster_.get(), tablet_id, new_leader->permanent_uuid()));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto leader = VERIFY_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
        return leader->permanent_uuid() == new_leader->permanent_uuid();
      },
      30s * kTimeMultiplier, "Leadership moved to the former follower"));

  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 0);

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (4)", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", kTableName))), 1);
}

TEST_F(PgMiniTest, SkipTableTombstoneCheckMetadata) {
  // Setup test data.
  const auto kNonColocatedTableName = "test";
  const auto kColocatedTableName = "colo_test";
  const auto kDatabaseName = "testdb";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocated=true", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (a int PRIMARY KEY, b int) WITH (colocation = false) SPLIT INTO 3 TABLETS",
      kNonColocatedTableName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a int PRIMARY KEY, b int)", kColocatedTableName));

  // Verify that skip_table_tombstone_check=true for non-colocated user tables.
  auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kNonColocatedTableName));
  auto tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  for (const auto& peer : tablet_peers) {
    ASSERT_TRUE(peer->tablet_metadata()->primary_table_info()->skip_table_tombstone_check);
  }

  // Verify that skip_table_tombstone_check=true for colocated user tables.
  table_id = ASSERT_RESULT(GetTableIDFromTableName(kColocatedTableName));
  tablet_peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  tablet::TabletPeerPtr colocated_tablet_peer = nullptr;
  for (const auto& peer : tablet_peers) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      continue;
    }
    if (tablet->regular_db() && peer->tablet_metadata()->colocated()) {
      colocated_tablet_peer = peer;
      break;
    }
  }
  ASSERT_NE(colocated_tablet_peer, nullptr);
  ASSERT_TRUE(ASSERT_RESULT(
      colocated_tablet_peer->tablet_metadata()->GetTableInfo(table_id))
      ->skip_table_tombstone_check);

  // Verify that skip_table_tombstone_check=false for pg system tables.
  table_id = ASSERT_RESULT(GetTableIDFromTableName("pg_class"));
  const auto& sys_catalog = cluster_->mini_master(0)->master()->sys_catalog();
  ASSERT_FALSE(ASSERT_RESULT(
      sys_catalog.tablet_peer()->tablet_metadata()->GetTableInfo(table_id))
      ->skip_table_tombstone_check);
}

PgSchemaName PgMiniTest::GetPgSchema(const string& tbl_name) {
  const auto tbl_id = EXPECT_RESULT(GetTableIDFromTableName(tbl_name));
  master::TableInfoPtr table = EXPECT_RESULT(catalog_manager())->GetTableInfo(tbl_id);
  const auto schema_name = table->pgschema_name();
  LOG(INFO) << "Table name = " << tbl_name << ", id =" << tbl_id << ", schema = " << schema_name;
  return schema_name;
}

TEST_F(PgMiniTest, AlterTableSetSchema) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SCHEMA S1"));
  ASSERT_OK(conn.Execute("CREATE SCHEMA S2"));
  ASSERT_OK(conn.Execute("CREATE SCHEMA S3"));
  ASSERT_OK(conn.Execute("CREATE TABLE S1.TBL (a1 INT PRIMARY KEY, a2 INT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX IDX ON S1.TBL(a2)"));

  ASSERT_OK(conn.Execute("ALTER TABLE S1.TBL SET SCHEMA S2"));
  // Check PG schema name in the CatalogManager.
  ASSERT_EQ("s2", GetPgSchema("tbl"));
  ASSERT_EQ("s2", GetPgSchema("idx"));

  ASSERT_OK(conn.Execute("ALTER TABLE IF EXISTS S2.TBL SET SCHEMA S3"));
  // Check PG schema name in the CatalogManager.
  ASSERT_EQ("s3", GetPgSchema("tbl"));
  ASSERT_EQ("s3", GetPgSchema("idx"));

  ASSERT_OK(conn.Execute("DROP TABLE S3.TBL"));
  ASSERT_OK(conn.Execute("DROP SCHEMA S3"));
  ASSERT_OK(conn.Execute("DROP SCHEMA S2"));
  ASSERT_OK(conn.Execute("DROP SCHEMA S1"));

  // The command is successful for the deleted table due to IF EXISTS.
  ASSERT_OK(conn.Execute("ALTER TABLE IF EXISTS S1.TBL SET SCHEMA S3"));
}

TEST_F(PgMiniTest, AlterPartitionedTableSetSchema) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SCHEMA S1"));
  ASSERT_OK(conn.Execute("CREATE SCHEMA S2"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE S1.P_TBL (k INT PRIMARY KEY, v TEXT)  PARTITION BY RANGE(k)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE S1.P_TBL_1 PARTITION OF S1.P_TBL FOR VALUES FROM (1) TO (3)"));
  ASSERT_OK(conn.Execute("CREATE TABLE S1.P_TBL_DEFAULT PARTITION OF S1.P_TBL DEFAULT"));
  ASSERT_OK(conn.Execute("CREATE INDEX P_TBL_IDX on S1.P_TBL(k)"));

  ASSERT_OK(conn.Execute("ALTER TABLE S1.P_TBL SET SCHEMA S2"));
  // Check PG schema name in the CatalogManager.
  ASSERT_EQ("s2", GetPgSchema("p_tbl"));
  ASSERT_EQ("s2", GetPgSchema("p_tbl_idx"));

  ASSERT_EQ("s1", GetPgSchema("p_tbl_1"));
  ASSERT_EQ("s1", GetPgSchema("p_tbl_default"));

  ASSERT_OK(conn.Execute("DROP TABLE S2.P_TBL"));
  ASSERT_OK(conn.Execute("DROP SCHEMA S2"));
  ASSERT_OK(conn.Execute("DROP SCHEMA S1"));
}

// ------------------------------------------------------------------------------------------------
// Tablet Splitting Tests
// ------------------------------------------------------------------------------------------------

namespace {

YB_DEFINE_ENUM(KeyColumnType, (kHash)(kAsc)(kDesc));

class PgMiniTestAutoScanNextPartitions : public PgMiniTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_index_read_multiple_partitions) = true;
    PgMiniTest::SetUp();
  }

  Status IndexScan(PGConn* conn, KeyColumnType table_key, KeyColumnType index_key) {
    RETURN_NOT_OK(conn->Execute("DROP TABLE IF EXISTS t"));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "CREATE TABLE t (k INT, v1 INT, v2 INT, PRIMARY KEY (k $0)) $1",
        ToPostgresKeyType(table_key), TableSplitOptions(table_key)));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "CREATE INDEX ON t(v1 $0, v2 $0)", ToPostgresKeyType(index_key)));

    constexpr int kNumRows = 100;
    RETURN_NOT_OK(conn->ExecuteFormat(
        "INSERT INTO t SELECT s, 1, s FROM generate_series(1, $0) AS s", kNumRows));

    // Secondary index read from the table
    // While performing secondary index read on ybctids, the pggate layer batches requests belonging
    // to the same tablet. However, if the tablet is split after batching, we need a mechanism to
    // execute the batched request across both the sub-tablets. We create a scenario to test this
    // phenomenon here.
    //
    // FLAGS_index_read_multiple_partitions is a test flag when set will create a scenario to check
    // if index scans of ybctids span across multiple tablets. Specifically in this example, we try
    // to scan the elements which contain value v1 = 1 and see if they match the expected number
    // of rows.
    constexpr auto kQuery = "SELECT k FROM t WHERE v1 = 1";
    RETURN_NOT_OK(conn->HasIndexScan(kQuery));
    return ResultToStatus(conn->FetchMatrix(kQuery, kNumRows, 1));
  }

  Status FKConstraint(PGConn* conn, KeyColumnType key_type) {
    RETURN_NOT_OK(conn->Execute("DROP TABLE IF EXISTS ref_t, t1, t2"));
    RETURN_NOT_OK(conn->ExecuteFormat("CREATE TABLE t1 (k INT, PRIMARY KEY(k $0)) $1",
                                      ToPostgresKeyType(key_type),
                                      TableSplitOptions(key_type)));
    RETURN_NOT_OK(conn->ExecuteFormat("CREATE TABLE t2 (k INT, PRIMARY KEY(k $0)) $1",
                                      ToPostgresKeyType(key_type),
                                      TableSplitOptions(key_type)));
    RETURN_NOT_OK(conn->Execute("CREATE TABLE ref_t (k INT,"
                                "                    fk_1 INT REFERENCES t1(k),"
                                "                    fk_2 INT REFERENCES t2(k))"));
    constexpr int kNumRows = 100;
    RETURN_NOT_OK(conn->ExecuteFormat(
        "INSERT INTO t1 SELECT s FROM generate_series(1, $0) AS s", kNumRows));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "INSERT INTO t2 SELECT s FROM generate_series(1, $0) AS s", kNumRows));
    return conn->ExecuteFormat(
        "INSERT INTO ref_t SELECT s, s, s FROM generate_series(1, $0) AS s", kNumRows);
  }

 private:
  static std::string TableSplitOptions(KeyColumnType key_type) {
    switch(key_type) {
      case KeyColumnType::kHash:
        return "SPLIT INTO 10 TABLETS";
      case KeyColumnType::kAsc:
        return "SPLIT AT VALUES ((12), (25), (37), (50), (62), (75), (87))";
      case KeyColumnType::kDesc:
        return "SPLIT AT VALUES ((87), (75), (62), (50), (37), (25), (12))";
    }
    FATAL_INVALID_ENUM_VALUE(KeyColumnType, key_type);
  }

  static std::string ToPostgresKeyType(KeyColumnType key_type) {
    switch(key_type) {
      case KeyColumnType::kHash: return "";
      case KeyColumnType::kAsc: return "ASC";
      case KeyColumnType::kDesc: return "DESC";
    }
    FATAL_INVALID_ENUM_VALUE(KeyColumnType, key_type);
  }

};

template <class T>
T* GetMetricOpt(const MetricEntity& metric_entity, const MetricPrototype& prototype) {
  const auto& map = metric_entity.TEST_UsageMetricsMap();
  auto it = map.find(&prototype);
  if (it == map.end()) {
    return nullptr;
  }
  return down_cast<T*>(it->second.get());
}

template <class T>
T* GetMetricOpt(const tserver::MiniTabletServer& server, const MetricPrototype& prototype) {
  return GetMetricOpt<T>(server.metric_entity(), prototype);
}

template <class T>
T* GetMetricOpt(const tablet::Tablet& tablet, const MetricPrototype& prototype) {
  return GetMetricOpt<T>(*tablet.GetTabletMetricsEntity(), prototype);
}

template <class T>
T& GetMetric(const tserver::MiniTabletServer& server, const MetricPrototype& prototype) {
  return *CHECK_NOTNULL(GetMetricOpt<T>(server, prototype));
}

} // namespace

// The test checks all rows are returned in case of index scan with dynamic table splitting for
// different table and index key column type combinations (hash, asc, desc)
TEST_F_EX(
    PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(AutoScanNextPartitionsIndexScan),
    PgMiniTestAutoScanNextPartitions) {
  auto conn = ASSERT_RESULT(Connect());
  for (auto table_key : kKeyColumnTypeArray) {
    for (auto index_key : kKeyColumnTypeArray) {
      ASSERT_OK_PREPEND(IndexScan(&conn, table_key, index_key),
                        Format("Bad status in test with table_key=$0, index_key=$1",
                               ToString(table_key),
                               ToString(index_key)));
    }
  }
}

// The test checks foreign key constraint is not violated in case of referenced table dynamic
// splitting for different key column types (hash, asc, desc).
TEST_F_EX(
    PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(AutoScanNextPartitionsFKConstraint),
    PgMiniTestAutoScanNextPartitions) {
  auto conn = ASSERT_RESULT(Connect());
  for (auto table_key : kKeyColumnTypeArray) {
    ASSERT_OK_PREPEND(FKConstraint(&conn, table_key),
                      Format("Bad status in test with table_key=$0", ToString(table_key)));
  }
}

class PgMiniTabletSplitTest : public PgMiniTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_num_shards_per_tserver) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 10_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) =
        FLAGS_tablet_force_split_threshold_bytes / 4;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 2_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_filter_block_size_bytes) = 2_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_index_block_size_bytes) = 2_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 1000;
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_TEST_inject_delay_between_prepare_ybctid_execute_batch_ybctid_ms) = 4000;
    PgMiniTest::SetUp();
  }

  Status SetupConnection(PGConn* conn) const override {
    return conn->Execute("SET yb_fetch_row_limit = 32");
  }

  void ExecuteReadWriteThreads(const std::string& table_name) {
    CountDownLatch latch{2};
    TestThreadHolder thread_holder;
    // Writer thread that does parallel writes into table
    thread_holder.AddThreadFunctor([this, &table_name, &latch] {
      LOG(INFO) << "Starting writes to " << table_name;
      auto conn = ASSERT_RESULT(Connect());
      latch.CountDown();
      latch.Wait();
      for (size_t i = 501; i < 2000; ++i) {
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 VALUES ($1, $2, $3, $4)", table_name, i, i, i, 1));
      }
      LOG(INFO) << "Completed writes to " << table_name;
    });

    // Index read from the table
    thread_holder.AddThread([this, &stop = thread_holder.stop_flag(), &table_name, &latch] {
      auto conn = ASSERT_RESULT(Connect());
      latch.CountDown();
      latch.Wait();
      do {
        auto result = ASSERT_RESULT(conn.FetchFormat(
            "SELECT * FROM  $0 WHERE i = 1 ORDER BY r", table_name));
        std::optional<int32_t> prev_value;
        for(int row = 0, row_count = PQntuples(result.get()); row < row_count; ++row) {
          const auto value = ASSERT_RESULT(GetValue<int32_t>(result.get(), row, 2));
          if (prev_value) {
            // Check all the rows are sorted in ascending order
            ASSERT_LE(*prev_value, value);
          }
          prev_value = value;
        }
      } while (!stop.load(std::memory_order_acquire));
    });
  }

  void CreateTableAndInitialize(const std::string& table_name, size_t num_tablets) {
    auto conn = ASSERT_RESULT(Connect());

    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (h1 int, h2 int, r int, i int, "
                                "PRIMARY KEY ((h1, h2) HASH, r ASC)) "
                                "SPLIT INTO $1 TABLETS", table_name, num_tablets));

    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0_idx "
                                "ON $1(i HASH, r ASC)", table_name, table_name));

    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT i, i, i, 1 FROM "
                                "(SELECT generate_series(1, 500) i) t", table_name));
  }

};

TEST_F_EX(
    PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(TabletSplitSecondaryIndexYSQL),
    PgMiniTabletSplitTest) {
  auto test_runner = [this](int num_tablets) {
    LOG(INFO) << "Run test with num_tablets = " << num_tablets;
    static const std::string table_name = "update_pk_complex_two_hash_one_range_keys";

    CreateTableAndInitialize(table_name, 1);

    const auto table_id = ASSERT_RESULT(GetTableIDFromTableName(table_name));
    const auto start_num_tablets = ListTableActiveTabletLeadersPeers(
        cluster_.get(), table_id).size();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

    /*
    * Writer thread writes into the table continuously, while the index read thread does a
    * secondary index lookup. During the index lookup, we inject artificial delays, specified by
    * the flag FLAGS_TEST_tablet_split_injected_delay_ms. Tablets will split in between those
    * delays into two different partitions.
    *
    * The purpose of this test is to verify that when the secondary index read request is being
    * executed, the results from both the tablets are being represented. Without the fix from
    * the pggate layer, only one half of the results will be obtained. Hence we verify that after
    * the split the number of elements is > 500, which is the number of elements inserted before
    * the split.
    */
    ExecuteReadWriteThreads(table_name);
    const auto end_num_tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size();
    ASSERT_GT(end_num_tablets, start_num_tablets);
    DestroyTable(table_name);
  };

  test_runner(/* num_tables= */ 1);

  // Rerun the same test where table is created with 3 tablets.
  // When a table is created with three tablets, the lower and upper bounds are as follows;
  // tablet 1 -- empty to A
  // tablet 2 -- A to B
  // tablet 3 -- B to empty
  // However, in situations where tables are created with just one tablet lower_bound and
  // upper_bound for the tablet is empty to empty. Hence, to test both situations we run this test
  // with one tablet and three tablets respectively.

  test_runner(/* num_tables= */ 3);
}

void PgMiniTest::ValidateAbortedTxnMetric() {
  auto tablet_peers = cluster_->GetTabletPeers(0);
  for(size_t i = 0; i < tablet_peers.size(); ++i) {
    auto tablet = ASSERT_RESULT(tablet_peers[i]->shared_tablet());
    auto* gauge = GetMetricOpt<const AtomicGauge<uint64>>(
        *tablet, METRIC_aborted_transactions_pending_cleanup);
    if (gauge) {
      EXPECT_EQ(0, gauge->value());
    }
  }
}

void PgMiniTest::RunManyConcurrentReadersTest() {
  constexpr int kNumConcurrentRead = 8;
  constexpr int kMinNumNonEmptyReads = 10;
  const std::string kTableName = "savepoints";
  TestThreadHolder thread_holder;

  std::atomic<int32_t> next_write_start{0};
  std::atomic<int32_t> num_non_empty_reads{0};
  CountDownLatch reader_latch(0);
  CountDownLatch writer_latch(1);
  std::atomic<bool> writer_thread_is_stopped{false};
  CountDownLatch reader_threads_are_stopped(kNumConcurrentRead);

  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (a int)", kTableName));
  }

  thread_holder.AddThreadFunctor([
      &stop = thread_holder.stop_flag(), &next_write_start, &reader_latch, &writer_latch,
      &writer_thread_is_stopped, kTableName, this] {
    auto conn = ASSERT_RESULT(Connect());
    while (!stop.load(std::memory_order_acquire)) {
      auto write_start = (next_write_start += 5);
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, write_start));
      ASSERT_OK(conn.Execute("SAVEPOINT one"));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, write_start + 1));
      ASSERT_OK(conn.Execute("SAVEPOINT two"));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, write_start + 2));
      ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT one"));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, write_start + 3));
      ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT one"));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, write_start + 4));

      // Start concurrent reader threads
      reader_latch.Reset(kNumConcurrentRead * 5);
      writer_latch.CountDown();

      // Commit while reader threads are running
      ASSERT_OK(conn.CommitTransaction());

      // Allow reader threads to complete and halt.
      ASSERT_TRUE(reader_latch.WaitFor(5s * kTimeMultiplier));
      writer_latch.Reset(1);
    }
    writer_thread_is_stopped = true;
  });

  for (int reader_idx = 0; reader_idx < kNumConcurrentRead; ++reader_idx) {
    thread_holder.AddThreadFunctor([
        &stop = thread_holder.stop_flag(), &next_write_start, &num_non_empty_reads,
        &reader_latch, &writer_latch, &reader_threads_are_stopped, kTableName, this] {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        ASSERT_TRUE(writer_latch.WaitFor(10s * kTimeMultiplier));

        auto read_start = next_write_start.load();
        auto read_end = read_start + 4;
        auto fetch_query = strings::Substitute(
            "SELECT * FROM $0 WHERE a BETWEEN $1 AND $2 ORDER BY a ASC",
            kTableName, read_start, read_end);

        const auto values = ASSERT_RESULT(conn.FetchRows<int32_t>(fetch_query));
        const auto fetched_values = values.size();
        if (fetched_values != 0) {
          num_non_empty_reads++;
          if (fetched_values != 2) {
            LOG(INFO)
                << "Expected to fetch (" << read_start << ") and (" << read_end << "). "
                << "Instead, got the following results:";
            for (size_t i = 0; i < fetched_values; ++i) {
              LOG(INFO) << "Result " << i << " - " << values[i];
            }
          }
          EXPECT_EQ(values, (decltype(values){read_start, read_start + 4}));
        }
        reader_latch.CountDown(1);
      }
      reader_threads_are_stopped.CountDown(1);
    });
    ValidateAbortedTxnMetric();
  }

  std::this_thread::sleep_for(60s);
  thread_holder.stop_flag().store(true, std::memory_order_release);
  while (!writer_thread_is_stopped.load(std::memory_order_acquire) ||
          reader_threads_are_stopped.count() != 0) {
    reader_latch.Reset(0);
    writer_latch.Reset(0);
    std::this_thread::sleep_for(10ms * kTimeMultiplier);
  }
  thread_holder.Stop();
  EXPECT_GE(num_non_empty_reads, kMinNumNonEmptyReads);
}

TEST_F(PgMiniTest, BigInsertWithAbortedIntentsAndRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_apply_intents_task_injected_delay_ms) = 200;

  constexpr int64_t kRowNumModToAbort = 7;
  constexpr int64_t kNumBatches = 10;
  constexpr int64_t kNumRows = RegularBuildVsSanitizers(10000, 1000);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) = kNumRows / kNumBatches;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (a int PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  for (int32_t row_num = 0; row_num < kNumRows; ++row_num) {
    auto should_abort = row_num % kRowNumModToAbort == 0;
    if (should_abort) {
      ASSERT_OK(conn.Execute("SAVEPOINT A"));
    }
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t VALUES ($0)", row_num));
    if (should_abort) {
      ASSERT_OK(conn.Execute("ROLLBACK TO A"));
    }
  }

  ASSERT_OK(conn.CommitTransaction());

  LOG(INFO) << "Restart cluster";
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());

  ASSERT_OK(WaitFor([this] {
    auto intents_count = CountIntents(cluster_.get());
    LOG(INFO) << "Intents count: " << intents_count;

    return intents_count == 0;
  }, 60s * kTimeMultiplier, "Intents cleanup", 200ms));

  for (int32_t row_num = 0; row_num < kNumRows; ++row_num) {
    auto should_abort = row_num % kRowNumModToAbort == 0;

    const auto values = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
        "SELECT * FROM t WHERE a = $0", row_num)));
    if (should_abort) {
      EXPECT_TRUE(values.empty()) << "Did not expect to find value for: " << row_num;
    } else {
      EXPECT_EQ(values.size(), 1);
      EXPECT_EQ(values[0], row_num);
    }
  }
  ValidateAbortedTxnMetric();
}

TEST_F(
    PgMiniTest,
    YB_DISABLE_TEST_IN_SANITIZERS(TestConcurrentReadersMaskAbortedIntentsWithApplyDelay)) {
  ASSERT_OK(cluster_->WaitForAllTabletServers());
  std::this_thread::sleep_for(10s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_apply_intents_task_injected_delay_ms) = 10000;
  RunManyConcurrentReadersTest();
}

TEST_F(
    PgMiniTest,
    YB_DISABLE_TEST_IN_SANITIZERS(TestConcurrentReadersMaskAbortedIntentsWithResponseDelay)) {
  ASSERT_OK(cluster_->WaitForAllTabletServers());
  std::this_thread::sleep_for(10s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_random_delay_on_txn_status_response_ms) = 30;
  RunManyConcurrentReadersTest();
}

TEST_F(
    PgMiniTest,
    YB_DISABLE_TEST_IN_SANITIZERS(TestConcurrentReadersMaskAbortedIntentsWithUpdateDelay)) {
  ASSERT_OK(cluster_->WaitForAllTabletServers());
  std::this_thread::sleep_for(10s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_participant_inject_latency_on_apply_update_txn_ms) = 30;
  RunManyConcurrentReadersTest();
}

// TODO(savepoint): This test would start failing until issue #9587 is fixed. It worked earlier but
// is expected to fail, as pointed out in https://phabricator.dev.yugabyte.com/D17177
// Change macro to YB_DISABLE_TEST_IN_TSAN if re-enabling.
TEST_F(PgMiniTest, YB_DISABLE_TEST(TestSerializableStrongReadLockNotAborted)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (a int PRIMARY KEY, b int) SPLIT INTO 1 TABLETS"));
  for (int i = 0; i < 100; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t VALUES ($0, $0)", i));
  }

  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn1.Execute("SAVEPOINT A"));
  auto res1 = ASSERT_RESULT(conn1.FetchFormat("SELECT b FROM t WHERE a = $0", 90));
  ASSERT_OK(conn1.Execute("ROLLBACK TO A"));

  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  auto update_status = conn2.ExecuteFormat("UPDATE t SET b = $0 WHERE a = $1", 1000, 90);

  auto commit_status = conn1.CommitTransaction();

  EXPECT_TRUE(commit_status.ok() ^ update_status.ok())
      << "Expected exactly one of commit of first transaction or update of second transaction to "
      << "fail.\n"
      << "Commit status: " << commit_status << ".\n"
      << "Update status: " << update_status << ".\n";
  ValidateAbortedTxnMetric();
}

void PgMiniTest::VerifyFileSizeAfterCompaction(PGConn* conn, size_t num_tables) {
  ASSERT_OK(cluster_->FlushTablets());
  uint64_t files_size = 0;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      continue;
    }
    files_size += tablet->GetCurrentVersionSstFilesUncompressedSize();
  }

  ASSERT_OK(conn->ExecuteFormat("ALTER TABLE test$0 DROP COLUMN string;", num_tables - 1));
  ASSERT_OK(conn->ExecuteFormat("ALTER TABLE test$0 DROP COLUMN string;", 0));

  ASSERT_OK(cluster_->CompactTablets());

  uint64_t new_files_size = 0;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      continue;
    }
    new_files_size += tablet->GetCurrentVersionSstFilesUncompressedSize();
  }

  LOG(INFO) << "Old files size: " << files_size << ", new files size: " << new_files_size;
  ASSERT_LE(new_files_size * 2, files_size);
  ASSERT_GE(new_files_size * 3, files_size);
}

TEST_F(PgMiniTest, ColocatedCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;

  const std::string kDatabaseName = "testdb";
  const auto kNumTables = 3;
  constexpr int kKeys = 100;

  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocated=true", kDatabaseName));

  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  for (int i = 0; i < kNumTables; ++i) {
    ASSERT_OK(conn.ExecuteFormat(R"#(
        CREATE TABLE test$0 (
          key INTEGER NOT NULL PRIMARY KEY,
          value INTEGER,
          string VARCHAR
        )
      )#", i));
    for (int j = 0; j < kKeys; ++j) {
      ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO test$0(key, value, string) VALUES($1, -$1, '$2')", i, j,
          RandomHumanReadableString(128_KB)));
    }
  }
  VerifyFileSizeAfterCompaction(&conn, kNumTables);
}

void PgMiniTest::CreateDBWithTablegroupAndTables(
    const std::string& database_name, const std::string& tablegroup_name, size_t num_tables,
    size_t keys, PGConn* conn) {
  ASSERT_OK(conn->ExecuteFormat("CREATE DATABASE $0", database_name));
  *conn = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn->ExecuteFormat("CREATE TABLEGROUP $0", tablegroup_name));
  for (size_t i = 0; i < num_tables; ++i) {
    ASSERT_OK(conn->ExecuteFormat(R"#(
        CREATE TABLE test$0 (
          key INTEGER NOT NULL PRIMARY KEY,
          value INTEGER,
          string VARCHAR
        ) tablegroup $1
      )#", i, tablegroup_name));
    for (size_t j = 0; j < keys; ++j) {
      ASSERT_OK(conn->ExecuteFormat(
          "INSERT INTO test$0(key, value, string) VALUES($1, -$1, '$2')", i, j,
          RandomHumanReadableString(128_KB)));
    }
  }
}

TEST_F(PgMiniTest, TablegroupCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;

  PGConn conn = ASSERT_RESULT(Connect());
  CreateDBWithTablegroupAndTables(
      "testdb" /* database_name */,
      "testtg" /* tablegroup_name */,
      3 /* num_tables */,
      100 /* keys */,
      &conn);
  VerifyFileSizeAfterCompaction(&conn, 3 /* num_tables */);
}

// Ensure that after restart, there is no data loss in compaction.
TEST_F(PgMiniTest, TablegroupCompactionWithRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;
  constexpr size_t kNumTables = 3;
  constexpr size_t kKeys = 100;

  PGConn conn = ASSERT_RESULT(Connect());
  CreateDBWithTablegroupAndTables(
      "testdb" /* database_name */,
      "testtg" /* tablegroup_name */,
      kNumTables,
      kKeys,
      &conn);
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->RestartSync());
  ASSERT_OK(cluster_->CompactTablets());
  conn = ASSERT_RESULT(ConnectToDB("testdb" /* database_name */));
  for (size_t i = 0; i < kNumTables; ++i) {
    auto res =
        ASSERT_RESULT(conn.template FetchRow<PGUint64>(Format("SELECT COUNT(*) FROM test$0", i)));
    ASSERT_EQ(res, kKeys);
  }
}

TEST_F(PgMiniTest, CompactionAfterDBDrop) {
  const std::string kDatabaseName = "testdb";
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto sys_catalog_tablet =
      ASSERT_RESULT(catalog_manager.sys_catalog()->tablet_peer()->shared_tablet());

  ASSERT_OK(sys_catalog_tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
  ASSERT_OK(sys_catalog_tablet->ForceManualRocksDBCompact());
  uint64_t base_file_size = sys_catalog_tablet->GetCurrentVersionSstFilesUncompressedSize();;

  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  ASSERT_OK(sys_catalog_tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  // Make sure compaction works without error for the hybrid_time > history_cutoff case.
  ASSERT_OK(sys_catalog_tablet->ForceManualRocksDBCompact());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_syscatalog_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;

  ASSERT_OK(sys_catalog_tablet->ForceManualRocksDBCompact());

  uint64_t new_file_size = sys_catalog_tablet->GetCurrentVersionSstFilesUncompressedSize();;
  LOG(INFO) << "Base file size: " << base_file_size << ", new file size: " << new_file_size;
  ASSERT_LE(new_file_size, base_file_size + 100_KB);
}

// The test checks that YSQL doesn't wait for sent RPC response in case of process termination.
TEST_F(PgMiniTest, NoWaitForRPCOnTermination) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr auto kLongTimeQuery = "SELECT pg_sleep(30)";
  std::atomic<MonoTime> termination_start;
  MonoTime termination_end;
  {
    CountDownLatch latch(2);
    TestThreadHolder thread_holder;
    thread_holder.AddThreadFunctor([this, &latch, &termination_start, kLongTimeQuery] {
      auto thread_conn = ASSERT_RESULT(Connect());
      latch.CountDown();
      latch.Wait();
      const auto deadline = MonoTime::Now() + MonoDelta::FromSeconds(30);
      while (MonoTime::Now() < deadline) {
        const auto local_termination_start = MonoTime::Now();
        const auto lines = ASSERT_RESULT(thread_conn.FetchRows<bool>(
            Format(
                "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE query like '$0'",
                kLongTimeQuery)));
        if (!lines.empty()) {
          ASSERT_TRUE(lines.size() == 1 && lines.front());
          termination_start.store(local_termination_start, std::memory_order_release);
          break;
        }
      }
    });
    latch.CountDown();
    latch.Wait();
    const auto res = conn.Fetch(kLongTimeQuery);
    ASSERT_NOK(res);
    ASSERT_TRUE(res.status().IsNetworkError());
    ASSERT_STR_CONTAINS(res.status().ToString(), "server closed the connection unexpectedly");
    termination_end = MonoTime::Now();
  }
  const auto termination_duration =
      (termination_end - termination_start.load(std::memory_order_acquire)).ToMilliseconds();
  ASSERT_GT(termination_duration, 0);
  ASSERT_LT(termination_duration, RegularBuildVsDebugVsSanitizers(3000, 5000, 5000));
}

TEST_F(PgMiniTest, ReadHugeRow) {
  constexpr size_t kNumColumns = 2;
  constexpr size_t kColumnSize = 254000000 / RegularBuildVsSanitizers(1, 16);
  if (IsSanitizer()) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_max_message_size) = kColumnSize + 1_MB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) = kColumnSize - 1_KB - 1;
  }

  std::string create_query = "CREATE TABLE test(pk INT PRIMARY KEY";
  for (size_t i = 0; i < kNumColumns; ++i) {
    create_query += Format(", text$0 TEXT", i);
  }
  create_query += ")";

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(create_query));
  ASSERT_OK(conn.Execute("INSERT INTO test(pk) VALUES(0)"));

  for (size_t i = 0; i < kNumColumns; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "UPDATE test SET text$0 = repeat('0', $1) WHERE pk = 0",
        i, kColumnSize));
  }

  const auto res = conn.Fetch("SELECT * FROM test LIMIT 1");
  ASSERT_NOK(res);
  ASSERT_STR_CONTAINS(res.status().ToString(), "Sending too long RPC message");
}

// Check that fetch of data amount exceeding the message size automatically paginates and succeeds.
//
// The IndexScan path here also exercises the batched-ybctid mid-batch pagination contract:
// the index emits 1000 ybctids in `i` order, the main-table fetch is via batched ybctid with
// per-arg order tags (keep_order=true), and the wide rows force response_size_limit to trigger
// mid-batch - driving response.batch_arg_count < batch_arguments.size() and the client's
// pop_front-based pagination loop. The content checks below catch any skip/duplicate or
// ordering regression introduced by the wire-order vs processed-order contract (e.g., a
// reintroduced server-side sort, or sorting batch_arguments while keep_order is set, which
// would break the k-way merge in MergingPgDocOpFetchStream).
TEST_F(PgMiniTest, ReadHugeRows) {
  // kNumRows should be less than default yb_fetch_row_limit, but not too low, so system can work
  constexpr size_t kNumRows = 1000;
  constexpr size_t kColumnSize = 100000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_max_message_size) = kColumnSize * kNumRows;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) =
      kColumnSize * kNumRows - 1_KB - 1;

  auto conn = ASSERT_RESULT(Connect());
  // One tablet to make sure that the node has enough data to exceed the message size
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test(pk INT PRIMARY KEY, i INT, t TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("CREATE INDEX on test(i ASC)"));
  for (size_t i = 0; i < kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test VALUES($0, $0 * 2, repeat('0', $1))", i, kColumnSize));
  }

  // SeqScan, direct fetch from the main table. Verify every pk is present exactly once.
  {
    auto pks = ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT pk FROM test ORDER BY pk"));
    ASSERT_EQ(pks.size(), kNumRows);
    for (size_t i = 0; i < kNumRows; ++i) {
      ASSERT_EQ(pks[i], i);
    }
    pks = ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT pk FROM test ORDER BY pk DESC"));
    ASSERT_EQ(pks.size(), kNumRows);
    for (size_t i = 0; i < kNumRows; ++i) {
      ASSERT_EQ(pks[i], kNumRows - 1 - i);
    }
    pks = ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT pk FROM test ORDER BY pk % 100, pk"));
    ASSERT_EQ(pks.size(), kNumRows);
    int32_t pk = 0;
    for (size_t i = 0; i < kNumRows; ++i) {
      ASSERT_EQ(pks[i], pk);
      pk += 100;
      if (std::cmp_greater_equal(pk, kNumRows)) {
        pk = pk % 100 + 1;
      }
    }
  }

  // IndexScan, fetch from the main table by ybctids. Each row carries i = pk * 2; verifying
  // that all (pk, i) pairs arrive in i order proves:
  //   - no row was dropped (mid-batch pagination didn't skip)
  //   - no row was duplicated (wire-order vs processed-order contract holds)
  //   - the MergingPgDocOpFetchStream's k-way merge produced globally-ordered results
  {
    auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(
      ("SELECT pk, i FROM test ORDER BY i"))));
    ASSERT_EQ(rows.size(), static_cast<size_t>(kNumRows));
    for (size_t i = 0; i < kNumRows; ++i) {
      const auto& [pk, idx] = rows[i];
      ASSERT_EQ(pk, i);
      ASSERT_EQ(idx, i * 2);
    }
    rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(
      ("SELECT pk, i FROM test ORDER BY i DESC"))));
    ASSERT_EQ(rows.size(), static_cast<size_t>(kNumRows));
    for (size_t i = 0; i < kNumRows; ++i) {
      const auto& [pk, idx] = rows[i];
      const auto expected_pk = kNumRows - 1 - i;
      ASSERT_EQ(pk, expected_pk);
      ASSERT_EQ(idx, expected_pk * 2);
    }
    rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(
      ("SELECT pk, i FROM test ORDER BY i % 100, i"))));
    ASSERT_EQ(rows.size(), static_cast<size_t>(kNumRows));
    int32_t expected_pk = 0;
    for (size_t i = 0; i < kNumRows; ++i) {
      const auto& [pk, idx] = rows[i];
      ASSERT_EQ(pk, expected_pk);
      ASSERT_EQ(idx, expected_pk * 2);
      expected_pk += 50;
      if (std::cmp_greater_equal(expected_pk, kNumRows)) {
        expected_pk = expected_pk % 50 + 1;
      }
    }
  }
}

// Test that ANALYZE on tables with different row width does not exceed the RPC size limit
// The FLAGS_rpc_max_message_size is set to be lower than total amount data to fetch by ANALYZE,
// so multiple messages are needed, and at least one has size as close to the limit as possible.
void PgMiniTest::TestAnalyze(int row_width) {
  // kNumRows is equal to the sample size, so ANALYZE fetches entire table.
  constexpr uint64_t kNumRows = 30000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_max_message_size) =
      std::min(FLAGS_rpc_max_message_size, row_width * kNumRows);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) =
      FLAGS_rpc_max_message_size - 1_KB - 1;

  auto conn = ASSERT_RESULT(Connect());
  // One tablet to make sure that the node has enough data to exceed the message size
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test(pk INT PRIMARY KEY, i INT, t TEXT) SPLIT INTO 1 TABLETS"));
  LOG(INFO) << "Test table is created";
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test SELECT i, i * 2, repeat('0', $0) FROM generate_series(1, $1) i",
      row_width, kNumRows));
  LOG(INFO) << "Test table is populated";
  ASSERT_OK(conn.Execute("ANALYZE test"));
}

TEST_F(PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(AnalyzeLargeRows)) {
  PgMiniTest::TestAnalyze(/* row_width = */ 10000);
}

TEST_F(PgMiniTest, AnalyzeMediumRows) {
  PgMiniTest::TestAnalyze(/* row_width = */ 500);
}

TEST_F(PgMiniTest, AnalyzeSmallRows) {
  // The row_width=25 makes the minimal rpc_max_message_size allowing to send
  // fetch request with 30,000 ybctids in it
  PgMiniTest::TestAnalyze(/* row_width = */ 25);
}

TEST_F_EX(
    PgMiniTest, CacheRefreshWithDroppedEntries, PgMiniTestSingleNode) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
  constexpr size_t kNumViews = 30;
  for (size_t i = 0; i < kNumViews; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE VIEW v_$0 AS SELECT * FROM t", i));
  }
  // Trigger catalog version increment
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v INT"));
  // New connection will load all the entries (tables and views) into catalog cache
  auto aux_conn = ASSERT_RESULT(Connect());
  for (size_t i = 0; i < kNumViews; ++i) {
    ASSERT_OK(conn.ExecuteFormat("DROP VIEW v_$0", i));
  }
  // Wait for update of catalog version in shared memory to trigger catalog refresh on next query
  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_heartbeat_interval_ms));
  // Check that connection can handle query (i.e. the catalog cache was updated without an issue)
  ASSERT_OK(aux_conn.Fetch("SELECT 1"));
}

int64_t PgMiniTest::GetBloomFilterCheckedMetric() {
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  auto bloom_filter_checked = 0;
  for (auto &peer : peers) {
    const auto tablet = peer->shared_tablet_maybe_null();
    if (tablet) {
      bloom_filter_checked += tablet->regulardb_statistics()
        ->getTickerCount(rocksdb::BLOOM_FILTER_CHECKED);
    }
  }
  return bloom_filter_checked;
}

TEST_F(PgMiniTest, BloomFilterBackwardScanTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (h int, r int, primary key(h, r))"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i / 10, i % 10 FROM generate_series(1, 500) i"));

  FlushAndCompactTablets();

  auto before_blooms_checked = GetBloomFilterCheckedMetric();

  ASSERT_OK(
      conn.Fetch("SELECT * FROM t WHERE h = 2 AND r > 2 ORDER BY r DESC;"));

  auto after_blooms_checked = GetBloomFilterCheckedMetric();
  ASSERT_EQ(after_blooms_checked, before_blooms_checked + 1);
}

class PgMiniStreamCompressionTest : public PgMiniTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_stream_compression_algo) = 1; // gzip
    // old default compression level
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_gzip_stream_compression_level) = 6;
    PgMiniTest::SetUp();
  }
};

TEST_F_EX(PgMiniTest, DISABLED_ReadsDuringRBS, PgMiniStreamCompressionTest) {
  constexpr auto kNumRows = RegularBuildVsSanitizers(10000, 100);
  constexpr auto kValueSize = RegularBuildVsSanitizers(10000, 100);
  constexpr auto kNumReaders = 200;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value BYTEA) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.CopyFromStdin(
      "t",
      [](PGConn::RowMaker<int32_t, std::string_view>& row) {
        for (auto key : Range(kNumRows)) {
          row(key, RandomString(kValueSize));
        }
      }));
  FlushAndCompactTablets();

  LOG(INFO) << "Rows: " << ASSERT_RESULT(conn.FetchAllAsString("SELECT key FROM t"));

  TestThreadHolder thread_holder;
  std::atomic<int> num_reads{0};
  for (int i = 0 ; i != kNumReaders; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag(), &num_reads]() {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop.load()) {
        ASSERT_RESULT(conn.FetchRow<int32_t>(
            Format("SELECT key FROM t WHERE key = $0", RandomUniformInt(0, kNumRows - 1))));
        ++num_reads;
      }
    });
  }

  // Do reads for 20 seconds. After 5 seconds, add new tserver to trigger remote bootstrap.
  for (int i = 0; i != 20; ++i) {
    if (i == 5) {
      ASSERT_OK(cluster_->AddTabletServer());
      ASSERT_OK(cluster_->AddTServerToBlacklist(0));
    }

    for (const auto& server : cluster_->mini_tablet_servers()) {
      GetMetric<Histogram>(*server, METRIC_handler_latency_outbound_transfer).Reset();
    }
    auto before_reads = num_reads.load();
    std::this_thread::sleep_for(1s);
    auto last_reads = num_reads.load() - before_reads;

    std::string suffix;
    for (const auto& server : cluster_->mini_tablet_servers()) {
      auto latency = MonoDelta::FromNanoseconds(
          GetMetric<Histogram>(*server, METRIC_handler_latency_outbound_transfer).MeanValue());
      auto busy_reactors =
          GetMetric<AtomicGauge<int64_t>>(*server, METRIC_rpc_busy_reactors).value();
      if (suffix.empty()) {
        suffix += ", latency (busy reactors): ";
      } else {
        suffix += ", ";
      }
      suffix += Format("$0 ($1)", latency, busy_reactors);
    }
    LOG(INFO) << "Num reads/s: " << last_reads << suffix;
  }

  thread_holder.Stop();
}

TEST_F_EX(PgMiniTest, RegexPushdown, PgMiniTestSingleNode) {
  // Create (a, aa, aaa, b, bb, bbb, ..., z, zz, zzz) rows.
  const int kMaxRepeats = 3;
  std::stringstream str;
  auto first = true;
  for (char c = 'a'; c <= 'z'; ++c) {
    for (size_t repeats = 1; repeats <= kMaxRepeats; ++repeats) {
      if (!first) {
        str << ", ";
      } else {
        first = false;
      }

      str << "('";
      for (size_t i = 0; i < repeats; ++i)
        str << c;
      str << "')";
    }
  }
  const auto values = str.str();

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_texticregex (t TEXT, PRIMARY KEY(t ASC)) SPLIT AT VALUES($0)", values));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test_texticregex VALUES $0", values));

  for (size_t i = 0; i < 10; ++i) {
    const auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>(
        "SELECT COUNT(*) FROM test_texticregex WHERE texticregexeq(t, t)"));
    ASSERT_EQ(count, ('z' - 'a' + 1) * kMaxRepeats);
  }
}

TEST_F_EX(PgMiniTest, RegexRecursionLimit, PgMiniTestSingleNode) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_regexp_count (c0 text)"));
  ASSERT_OK(conn.ExecuteFormat(
    "INSERT INTO test_regexp_count VALUES (repeat('a', 4)), (repeat('a', 10))"));

  auto query = "select count(*) from test_regexp_count where regexp_count(c0, repeat('a', $0)) > 0";
  auto too_complex_error = "regular expression is too complex";

  ASSERT_NOK_STR_CONTAINS(conn.ExecuteFormat(query, 10000), too_complex_error);
  ASSERT_NOK_STR_CONTAINS(conn.ExecuteFormat(query, 1800), too_complex_error);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<PGUint64>(Format(query, 500))), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<PGUint64>(Format(query, 10))), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<PGUint64>(Format(query, 3))), 2);
}

TEST_F(PgMiniTestSingleNode, TestBootstrapOnAppliedTransactionWithIntents) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_delete_intents_sst_files) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_bootstrap_intent_ht_filter) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_schedule_remove_intents) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = true;

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  LOG(INFO) << "Creating table";
  ASSERT_OK(conn1.Execute("CREATE TABLE test(a int) SPLIT INTO 1 TABLETS"));

  const auto& peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  tablet::TabletPeerPtr tablet_peer = nullptr;
  tablet::TabletPtr tablet = nullptr;
  for (auto peer : peers) {
    tablet = ASSERT_RESULT(peer->shared_tablet());
    if (tablet->regular_db()) {
      tablet_peer = peer;
      break;
    }
  }
  ASSERT_NE(tablet_peer, nullptr);

  LOG(INFO) << "T1 - BEGIN/INSERT";
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1.Execute("INSERT INTO test(a) VALUES (0)"));

  LOG(INFO) << "Flush";
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  LOG(INFO) << "T2 - BEGIN/INSERT";
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.Execute("INSERT INTO test(a) VALUES (1)"));

  LOG(INFO) << "Flush";
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  LOG(INFO) << "T1 - Commit";
  ASSERT_OK(conn1.CommitTransaction());

  ASSERT_OK(tablet_peer->FlushBootstrapState());

  LOG(INFO) << "Restarting cluster";
  ASSERT_OK(RestartCluster());

  conn1 = ASSERT_RESULT(Connect());
  auto res = ASSERT_RESULT(conn1.FetchRow<PGUint64>("SELECT COUNT(*) FROM test"));
  ASSERT_EQ(res, 1);
}

TEST_F(PgMiniTestSingleNode, TestBootstrapFilterOldTransactionNewWrite) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_delete_intents_sst_files) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_bootstrap_intent_ht_filter) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_schedule_remove_intents) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = true;

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  LOG(INFO) << "Creating tables";
  ASSERT_OK(conn1.Execute("CREATE TABLE test1(a int) SPLIT INTO 1 TABLETS"));

  const auto& peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  tablet::TabletPeerPtr tablet_peer = nullptr;
  tablet::TabletPtr tablet = nullptr;
  for (auto peer : peers) {
    tablet = ASSERT_RESULT(peer->shared_tablet());
    if (tablet->regular_db()) {
      tablet_peer = peer;
      break;
    }
  }
  ASSERT_NE(tablet_peer, nullptr);

  ASSERT_OK(conn1.Execute("CREATE TABLE test2(a int) SPLIT INTO 1 TABLETS"));

  // This tests the case where a very old transaction writes to a tablet for the first time,
  // after bootstrap state has been flushed, to ensure it is not filtered out. More context: #29642.
  LOG(INFO) << "T1 - BEGIN";
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  LOG(INFO) << "T1 - INSERT (test2)";
  ASSERT_OK(conn1.Execute("INSERT INTO test2(a) VALUES (0)"));

  LOG(INFO) << "T2 - BEGIN";
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  LOG(INFO) << "T2 - INSERT (test1)";
  ASSERT_OK(conn2.Execute("INSERT INTO test1(a) VALUES (10)"));
  ASSERT_OK(conn2.Execute("INSERT INTO test1(a) VALUES (11)"));

  LOG(INFO) << "T2 - Commit";
  ASSERT_OK(conn2.CommitTransaction());

  LOG(INFO) << "Flush bootstrap state";
  ASSERT_OK(tablet_peer->FlushBootstrapState());

  LOG(INFO) << "T1 - INSERT (test1)";
  ASSERT_OK(conn1.Execute("INSERT INTO test1(a) VALUES(20)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test1(a) VALUES(21)"));

  LOG(INFO) << "Flush";
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  LOG(INFO) << "T1 - Commit";
  ASSERT_OK(conn1.CommitTransaction());

  LOG(INFO) << "Restarting cluster";
  ASSERT_OK(RestartCluster());

  conn1 = ASSERT_RESULT(Connect());
  auto res = ASSERT_RESULT(conn1.FetchRow<PGUint64>("SELECT COUNT(*) FROM test1"));
  ASSERT_EQ(res, 4);
  res = ASSERT_RESULT(conn1.FetchRow<PGUint64>("SELECT COUNT(*) FROM test2"));
  ASSERT_EQ(res, 1);
}

TEST_F(PgMiniTestSingleNode, TestAppliedTransactionsStateReadOnly) {
  constexpr size_t kIters = 100;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_delete_intents_sst_files) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_bootstrap_intent_ht_filter) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_schedule_remove_intents) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = true;

  auto conn = ASSERT_RESULT(Connect());

  LOG(INFO) << "Creating tables";
  ASSERT_OK(conn.Execute("CREATE TABLE test1(a int primary key) SPLIT INTO 1 TABLETS"));

  tablet::TabletPeerPtr test1_peer = nullptr;
  tablet::TabletPtr tablet = nullptr;
  {
    const auto& peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    for (auto peer : peers) {
      tablet = ASSERT_RESULT(peer->shared_tablet());
      if (tablet && tablet->regular_db()) {
        test1_peer = peer;
        break;
      }
    }
    ASSERT_NE(test1_peer, nullptr);
  }
  std::string test1_tablet_id = tablet->tablet_id();

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO test1(a) VALUES (0)"));
  ASSERT_OK(conn.CommitTransaction());

  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
  ASSERT_OK(test1_peer->FlushBootstrapState());

  ASSERT_OK(conn.Execute("CREATE TABLE test2(a int references test1(a)) SPLIT INTO 1 TABLETS"));
  for (size_t i = 0; i < kIters; ++i) {
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.Execute("INSERT INTO test2(a) VALUES (0)"));
    ASSERT_OK(conn.CommitTransaction());
  }

  ASSERT_EQ(
      GetMetricOpt<AtomicGauge<uint64_t>>(
          *test1_peer->shared_tablet_maybe_null(), METRIC_wal_replayable_applied_transactions)
          ->value(),
      1);

  LOG(INFO) << "Restarting cluster";
  ASSERT_OK(RestartCluster());

  for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    if (!tablet || !tablet->regular_db()) {
      continue;
    }
    auto metric_value =
        GetMetricOpt<AtomicGauge<uint64_t>>(
            *peer->shared_tablet_maybe_null(), METRIC_wal_replayable_applied_transactions)
            ->value();
    if (tablet->tablet_id() == test1_tablet_id) {
      ASSERT_EQ(metric_value, 1);
    } else {
      ASSERT_EQ(metric_value, kIters);
    }
  }

  conn = ASSERT_RESULT(Connect());
  auto res = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM test1"));
  ASSERT_EQ(res, 1);
  res = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM test2"));
  ASSERT_EQ(res, kIters);
}

TEST_F(PgMiniTest, TestAppliedTransactionsStateInFlight) {
  const auto kApplyWait = 5s * kTimeMultiplier;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_delete_intents_sst_files) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_bootstrap_intent_ht_filter) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_schedule_remove_intents) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = true;

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  auto conn3 = ASSERT_RESULT(Connect());

  LOG(INFO) << "Creating table";
  ASSERT_OK(conn1.Execute("CREATE TABLE test(a int) SPLIT INTO 1 TABLETS"));

  const auto& pg_ts_uuid = cluster_->mini_tablet_server(kPgTsIndex)->server()->permanent_uuid();
  tablet::TabletPeerPtr tablet_peer = nullptr;
  tablet::TabletPtr tablet = nullptr;
  for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kNonLeaders)) {
    tablet = ASSERT_RESULT(peer->shared_tablet());
    if (tablet->regular_db() && peer->permanent_uuid() != pg_ts_uuid) {
      tablet_peer = peer;
      break;
    }
  }
  ASSERT_NE(tablet_peer, nullptr);

  tserver::MiniTabletServer* tablet_server = nullptr;
  for (size_t i = 0; i < NumTabletServers(); ++i) {
    auto* ts = cluster_->mini_tablet_server(i);
    if (ts->server()->permanent_uuid() == tablet_peer->permanent_uuid()) {
      tablet_server = ts;
      break;
    }
  }
  ASSERT_NE(tablet_server, nullptr);

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1.Execute("INSERT INTO test(a) VALUES (0)"));
  ASSERT_OK(conn1.CommitTransaction());

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1.Execute("INSERT INTO test(a) VALUES (1)"));

  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.Execute("INSERT INTO test(a) VALUES (2)"));
  ASSERT_OK(conn2.FetchRow<PGUint32>("SELECT a FROM test WHERE a = 0 FOR KEY SHARE"));

  ASSERT_OK(conn3.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn3.FetchRow<PGUint32>("SELECT a FROM test WHERE a = 0 FOR KEY SHARE"));

  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  ASSERT_OK(tablet_server->Restart());
  ASSERT_OK(tablet_server->WaitStarted());

  // The recently applied transactions map only retains a staircase of (first_write_ht,
  // apply_op_id) pairs, so an apply landing out of first write order subsumes the entry of an
  // earlier transaction. Applies are asynchronous with respect to commit, so let each transaction
  // apply, i.e. leave the participant, before committing the next one.
  const auto& tablet_id = tablet_peer->tablet_id();
  auto commit_and_wait_apply = [this, tablet_id, kApplyWait](
      PGConn* conn, size_t expected_running) -> Status {
    RETURN_NOT_OK(conn->CommitTransaction());
    return WaitFor([this, &tablet_id, expected_running]() -> Result<bool> {
      size_t running = 0;
      for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
        if (peer->tablet_id() != tablet_id) {
          continue;
        }
        auto peer_tablet = peer->shared_tablet_maybe_null();
        if (!peer_tablet) {
          return false;
        }
        running += peer_tablet->transaction_participant()->GetNumRunningTransactions();
      }
      return running <= expected_running;
    }, kApplyWait, Format("$0 running transactions left", expected_running));
  };

  ASSERT_OK(commit_and_wait_apply(&conn1, 6));
  ASSERT_OK(commit_and_wait_apply(&conn2, 3));
  ASSERT_OK(commit_and_wait_apply(&conn3, 0));

  std::unordered_map<std::string, uint64_t> metric_values;
  for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    if (ASSERT_RESULT(peer->shared_tablet())->regular_db()) {
      metric_values[peer->permanent_uuid()] =
          GetMetricOpt<AtomicGauge<uint64_t>>(
              *peer->shared_tablet_maybe_null(), METRIC_wal_replayable_applied_transactions)
              ->value();
    }
  }

  // Expecting metric value to be 3 on all peers: the intial insert + conn1/conn2 transactions.
  // conn3 transaction is readonly and not added to map.
  LOG(INFO) << "Metric values: " << CollectionToString(metric_values);
  for (const auto& [_, value] : metric_values) {
    ASSERT_EQ(value, 3);
  }
}

Status MockAbortFailure(
    const yb::tserver::PgFinishTransactionRequestPB* req,
    yb::tserver::PgFinishTransactionResponsePB* resp, tserver::PgClientMockCallContext* context) {
  // ASH collector takes session id 1.
  // If --ysql_enable_relcache_init_optimization=false, then the subsequent connections
  // take 2 and 3.
  // If --ysql_enable_relcache_init_optimization=true, we will have an additional
  // internal relcache init connection as 2, so the subsequent connections take 3 and 4.
  uint64_t intended_session_id = FLAGS_ysql_enable_relcache_init_optimization ? 4 : 3;
  LOG(INFO) << "FinishTransaction called for session: " << req->session_id()
            << ", intended_session_id: " << intended_session_id;
  if (req->session_id() < intended_session_id) {
    context->CloseConnection();
    // The return status should not matter here.
    return Status::OK();
  }
  if (req->session_id() == intended_session_id) {
    return STATUS(NetworkError, "Mocking network failure on FinishTransaction");
  }

  LOG(FATAL) << "Unexpected session id: " << req->session_id();
}

Status MockRollbackToSubtransactionFailure(
    const yb::tserver::PgRollbackToSubTransactionRequestPB* req,
    yb::tserver::PgRollbackToSubTransactionResponsePB* resp,
    tserver::PgClientMockCallContext* context) {

  LOG(INFO) << Format("Requested rollback to subtransaction: $0", req->sub_transaction_id());
  return STATUS(NetworkError, "Mocking network failure on RollbackToSubtransaction");
}

class PgRecursiveAbortTest : public PgMiniTestSingleNode,
                             public ::testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_pg_client_mock) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_relcache_init_optimization) = GetParam();
    PgMiniTest::SetUp();
  }

  bool IsTransactionalDdlEnabled() const {
    return ANNOTATE_UNPROTECTED_READ(FLAGS_ysql_yb_ddl_transaction_block_enabled);
  }

  template <class F>
  tserver::PgClientServiceMockImpl::Handle MockFinishTransaction(const F& mock) {
    auto* client = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientServiceMock();
    return client->MockFinishTransaction(mock);
  }

  template <class F>
  tserver::PgClientServiceMockImpl::Handle MockRollbackToSubtransaction(const F& mock) {
    auto* client = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientServiceMock();
    return client->MockRollbackToSubTransaction(mock);
  }
};

INSTANTIATE_TEST_CASE_P(, PgRecursiveAbortTest,
                        ::testing::Values(false, true));

TEST_P(PgRecursiveAbortTest, AbortOnTserverFailure) {
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (k INT)"));

  // Validate that "connection refused" from tserver during a transaction does not produce a PANIC.
  ASSERT_OK(conn.StartTransaction(SNAPSHOT_ISOLATION));
  // Run a command to ensure that the transaction is created in the backend.
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1)"));
  {
    auto handle = MockFinishTransaction(MockAbortFailure);
    auto status = conn.Execute("CREATE TABLE t2 (k INT)");
    // With transactional DDL enabled, "CREATE TABLE t2" won't auto-commit. So we need to explicitly
    // commit to trigger our expected failure.
    // This also means that the connection `conn` remains as `CONNECTION_OK` as the transaction gets
    // aborted due to the failure during COMMIT.
    if (IsTransactionalDdlEnabled()) {
      status = conn.Execute("COMMIT");
      ASSERT_EQ(conn.ConnStatus(), CONNECTION_OK);
    } else {
      ASSERT_EQ(conn.ConnStatus(), CONNECTION_BAD);
    }
    ASSERT_TRUE(status.IsNetworkError());
  }

  // Insert will fail since the table 't2' doesn't exist.
  conn = ASSERT_RESULT(Connect());
  ASSERT_NOK(conn.Execute("INSERT INTO t2 VALUES (1)"));
}

TEST_P(PgRecursiveAbortTest, MockAbortFailure) {
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (k INT)"));
  ASSERT_OK(conn.StartTransaction(SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1)"));
  // Validate that aborting a transaction does not produce a PANIC.
  auto handle = MockFinishTransaction(MockAbortFailure);
  auto status = conn.Execute("ABORT");
  ASSERT_TRUE(status.IsNetworkError());
  ASSERT_EQ(conn.ConnStatus(), CONNECTION_BAD);
}

TEST_P(PgRecursiveAbortTest, MockRollbackToSubtransactionFailure) {
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (k INT)"));
  ASSERT_OK(conn.StartTransaction(READ_COMMITTED));
  ASSERT_OK(conn.Execute("SAVEPOINT s1"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1)"));
  auto _ = MockRollbackToSubtransaction(MockRollbackToSubtransactionFailure);
  auto status = conn.Execute("ROLLBACK TO s1");
  ASSERT_TRUE(status.IsNetworkError());
  ASSERT_EQ(conn.ConnStatus(), CONNECTION_BAD);
}

class PgHeartbeatFailureTest : public PgMiniTestSingleNode {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_pg_client_mock) = true;
    PgMiniTest::SetUp();
  }

  template <class F>
  tserver::PgClientServiceMockImpl::Handle MockHeartbeat(const F& mock) {
    auto* client = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientServiceMock();
    return client->MockHeartbeat(mock);
  }

  void UnsetHeartbeatMock() {
    auto* client = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientServiceMock();
    client->UnsetMock("Heartbeat");
  }
};

static MonoDelta kHeartbeatFailureDuration = MonoDelta::FromSeconds(5);
static CoarseTimePoint kFailureStart = CoarseTimePoint();

Status MockHeartbeatFailure(
    const yb::tserver::PgHeartbeatRequestPB* req,
    yb::tserver::PgHeartbeatResponsePB* resp, tserver::PgClientMockCallContext* context) {
  LOG(INFO) << "Heartbeat called for session: " << req->session_id();
  if (kFailureStart == CoarseTimePoint()) {
    kFailureStart = CoarseMonoClock::Now();
  }

  if (CoarseMonoClock::Now() - kFailureStart < kHeartbeatFailureDuration) {
    return STATUS(NetworkError, "Mocking network failure on Heartbeat");
  }

  // Do not set the session ID. The client should create a new session.
  return Status::OK();
}

TEST_F(PgHeartbeatFailureTest, MockTransientHeartbeatFailure) {
  PGConn conn = ASSERT_RESULT(Connect());
  auto _ = MockHeartbeat(MockHeartbeatFailure);

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this] {
    SleepFor(5s);
    UnsetHeartbeatMock();
  });

  uint nfailures = 0;
  ASSERT_OK(WaitFor([this, &nfailures]() {
    auto result = ConnectToDB(std::string() /* dbname */, 1 /* timeout */ );
    if (!result.ok()) {
      nfailures++;
      return false;
    }

    // Validate that once a heartbeat is successful, the connection can service
    // queries.
    PGConn conn2 = std::move(*result);
    auto status = conn2.Execute("CREATE TABLE t1 (k INT)");
    if (!status.ok()) {
      nfailures++;
      return false;
    }

    return true;
  }, 10s, "Transient failure timeout", 1s));

  thread_holder.JoinAll();
  // Validate that at least one new connection experienced heartbeat failure.
  ASSERT_GT(nfailures, 0);

  // The old connection should still work as it received a valid session ID from
  // the tserver before the mock was installed. Validate that it can service
  // queries.
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1)"));
  auto nrows = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM t1"));
  ASSERT_EQ(nrows, 1);
}

class PggateTimeoutTest : public PgMiniTestSingleNode {
 public:
  static inline MonoDelta sleep_time;
  static inline std::atomic<uint> mocked_rpcs;
  static inline bool ignore_client_timeout;

  static void ClearMockState() {
    sleep_time = MonoDelta();
    ignore_client_timeout = false;
    mocked_rpcs.store(0, std::memory_order_release);
  }

  void SetUp() override {
    ClearMockState();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_pg_client_mock) = true;
    // Set the "extra" timeout to a small value in order to make the test run faster.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_extra_timeout_ms) = kPgClientExtraTimeout;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_ddl_rpc_timeout_sec) = kAlterTableTimeout;
    PgMiniTest::SetUp();
  }

  template <class F>
  tserver::PgClientServiceMockImpl::Handle MockPerformBefore(const F& before) {
    auto* client = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientServiceMock();
    return client->MockPerformBefore(before);
  }

  template <class F>
  tserver::PgClientServiceMockImpl::Handle MockPerformAfter(const F& after) {
    auto* client = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientServiceMock();
    return client->MockPerformAfter(after);
  }

  template <class F>
  tserver::PgClientServiceMockImpl::Handle MockAlterTableBefore(const F& before) {
    auto* client = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientServiceMock();
    return client->MockAlterTableBefore(before);
  }

  Result<PGConn> SetupCommonTestConnection() {
    PGConn conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE TABLE t1 (k INT, v1 INT UNIQUE, v2 INT)"));

    // Run all queries once to warm up the cache
    RETURN_NOT_OK(conn.Execute("INSERT INTO t1 (SELECT i, i, i FROM generate_series(1, 10) AS i)"));
    VERIFY_RESULT(conn.Fetch("SELECT * FROM t1 WHERE v1 = 1"));
    VERIFY_RESULT(conn.Fetch("SELECT * FROM t1 WHERE k <= 10"));

    // Set up GUCs for easy debugging
    RETURN_NOT_OK(conn.Execute("SET log_min_messages TO 'DEBUG2'"));
    RETURN_NOT_OK(conn.Execute("SET log_min_duration_statement TO 0"));
    RETURN_NOT_OK(conn.Execute("SET yb_debug_log_docdb_requests TO true"));

    return conn;
  }

  // Runs `query` and returns whether it hit a statement-timeout cancel. On timeout, wall-clock
  // latency must fall within:
  //   strict:     (0.9 * timeout_ms, 1.3 * timeout_ms)
  //   non-strict: (0.9 * timeout_ms, 1.3 * (timeout_ms + kPgClientExtraTimeout))
  // Non-strict covers paths where the client waits until the Perform RPC deadline (network)
  // or returns close to (or slightly after) the statement timer (shared memory)).
  Result<bool> ExpectStatementTimeout(
      PGConn& conn, string query, uint timeout_ms, bool use_fetch = true,
      bool strict = true) {
    mocked_rpcs.store(0, std::memory_order_release);

    auto start_time = CoarseMonoClock::Now();
    Status status = Status::OK();
    if (use_fetch) {
      auto res = conn.Fetch(query);
      if (!res.ok()) {
        status = res.status();
      }
    } else {
      status = conn.Execute(query);
    }
    int64_t duration_ms = ToMilliseconds(CoarseMonoClock::Now() - start_time);

    if (!status.ok()) {
      auto msg = status.message().ToBuffer();
      if (msg.find("canceling statement due to statement timeout") == std::string::npos) {
        return status;
      }

      LOG(INFO) << "Statement timed out after " << duration_ms << " ms";

      const uint upper_ref_ms = strict ? timeout_ms : timeout_ms + kPgClientExtraTimeout;
      int64_t lowerbound_ms = timeout_ms * 0.90;
      int64_t upperbound_ms = upper_ref_ms * 1.30;
      if (!(duration_ms > lowerbound_ms && duration_ms < upperbound_ms)) {
        return STATUS(NetworkError,
                      Format("Expected $0 ms < query latency < $1 ms. Received $2 ms",
                             lowerbound_ms, upperbound_ms, duration_ms));
      }

      return true;
    }

    return false;
  }

  Result<bool> ValidateStatementTimeout(
      PGConn& conn, string query, uint timeout_ms, bool use_fetch = true, bool strict = true) {
    RETURN_NOT_OK(conn.ExecuteFormat("SET statement_timeout TO '$0ms'", timeout_ms));
    auto result = ExpectStatementTimeout(conn, std::move(query), timeout_ms, use_fetch, strict);
    RETURN_NOT_OK(conn.Execute("RESET statement_timeout"));
    return result;
  }

  static const int32 kAlterTableTimeout = 3; /* in seconds */
  // Large enough that the Perform wait outlasts Postgres' statement-timeout SIGALRM.
  static const int32 kPgClientExtraTimeout = 1000; /* in ms */
  static constexpr auto kPerformMockWorkEstimate = 200ms;
};

// The server_work_estimate is how long the RPC is expected to take after this mock returns. It is
// subtracted from the remaining client deadline so the operation can finish without timing out.
void DoServerSideSleep(
    const tserver::PgClientMockCallContext* context,
    MonoDelta server_work_estimate = MonoDelta::kZero) {

  // Increment before sleeping so the client can time out while the mock is still running.
  const uint rpc_index = PggateTimeoutTest::mocked_rpcs.fetch_add(1, std::memory_order_acq_rel) + 1;

  MonoDelta sleep_for = PggateTimeoutTest::sleep_time.Initialized() ?
      PggateTimeoutTest::sleep_time :
      MonoDelta::kZero;

  if (!PggateTimeoutTest::ignore_client_timeout) {
    const auto remaining =
        context->GetClientDeadline() - CoarseMonoClock::Now() - server_work_estimate;
    sleep_for = std::min(sleep_for, std::max(remaining, MonoDelta::kZero));
  }

  LOG(INFO) << "Mock invoked for RPC " << rpc_index << ". Sleeping for " << sleep_for;
  SleepFor(sleep_for);
}

Result<bool> IsPerformOpOnCatalogTable(const yb::tserver::PgPerformRequestMsg* req) {
  if (req->ops().empty()) {
    return STATUS(IllegalState, "No operations in Perform request");
  }

  const auto& first_op = req->ops().front();
  string table_id = first_op.has_read() ? first_op.read().table_id().ToBuffer()
                                        : first_op.write().table_id().ToBuffer();

  const auto table_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id));
  if (table_oid < kPgFirstNormalObjectId) {
    return true;
  }

  return false;
}

Status MockPerformBeforeFunc(
    const yb::tserver::PgPerformRequestMsg* req,
    yb::tserver::PgPerformResponseMsg* resp, tserver::PgClientMockCallContext* context) {

  // Skip sleeping before catalog ops.
  if (VERIFY_RESULT(IsPerformOpOnCatalogTable(req))) {
    return Status::OK();
  }

  DoServerSideSleep(context, PggateTimeoutTest::kPerformMockWorkEstimate * kTimeMultiplier);
  return Status::OK();
}

Status MockAlterTableBeforeFunc(
    const yb::tserver::PgAlterTableRequestPB* req,
    yb::tserver::PgAlterTableResponsePB* resp, tserver::PgClientMockCallContext* context) {

  DoServerSideSleep(context, 150ms * kTimeMultiplier);
  return Status::OK();
}

// In some cases, it is beneficial for the Perform mock to run after the Perform response is
// constructed. The "after" mock is invoked after FlushAsync / session unlock, and hence does not
// HOL-block follow-up Perform RPCs in the same session. A frequent source of such RPCs are
// catalog lookups that happen after a statement timeout.
Status MockPerformAfterFunc(
    const yb::tserver::PgPerformRequestMsg* req,
    yb::tserver::PgPerformResponseMsg* resp, tserver::PgClientMockCallContext* context) {

  // Skip sleeping before catalog ops.
  if (VERIFY_RESULT(IsPerformOpOnCatalogTable(req))) {
    return Status::OK();
  }

  DoServerSideSleep(context);
  return Status::OK();
}

TEST_F(PggateTimeoutTest, YB_DISABLE_TEST_IN_SANITIZERS(MockLongRPC)) {
  PGConn conn = ASSERT_RESULT(SetupCommonTestConnection());
  sleep_time = 550ms;
  auto _ = MockPerformBefore(MockPerformBeforeFunc);
  bool timed_out = false;
  uint stmt_timeout_ms = 500;
  std::string query;

  // A single RPC takes longer than the statement timeout.
  query = "INSERT INTO t1 VALUES (11, 11, 11)";
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 1);

  query = "SELECT * FROM t1 WHERE v1 = 11";
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 1);

  // Multiple RPCs that cumulatively take longer than the statement timeout.
  // RPC 1: starts = 0ms,           ends = 110ms + delta
  // RPC 2: starts = 110ms + delta, ends = 220ms + delta
  // RPC 3: starts = 220ms + delta, ends = 330ms + delta
  // RPC 4: starts = 330ms + delta, ends = 440ms + delta
  // RPC 5: starts = 440ms + delta, ends = 500ms + delta
  sleep_time = 110ms;
  ASSERT_OK(conn.Execute("SET yb_fetch_row_limit TO 1"));
  query = "SELECT * FROM t1 WHERE k <= 10";
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 5);

  // Validate that an RPC is not scheduled while in the "extra timeout" period.
  // RPC 1: starts = 0ms,           ends = 125ms + delta
  // RPC 2: starts = 125ms + delta, ends = 250ms + delta
  // RPC 3: starts = 250ms + delta, ends = 375ms + delta
  // RPC 4: starts = 375ms + delta, ends = 500ms + delta
  // RPC 5: not started as 500 < start time < 500 + extra timeout
  sleep_time = 125ms;
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 4);
}

TEST_F(PggateTimeoutTest, YB_DISABLE_TEST_IN_SANITIZERS(RPCTimeoutShorterThanClientTimeout)) {
  PGConn conn = ASSERT_RESULT(SetupCommonTestConnection());

  // Validate that an RPC's explicit timeout is respected.
  // The AlterTable RPC is modified to have an explicit timeout of 3s.
  // A statement timeout of 3.5s is also set.
  // The expected behavior is that the AlterTable RPC completes within 3s and the statement timer
  // does not fire.
  auto _1 = MockAlterTableBefore(MockAlterTableBeforeFunc);
  bool timed_out = false;
  uint stmt_timeout_ms = 3500;
  sleep_time = 3500ms;
  std::string query = "ALTER TABLE t1 RENAME TO t1_modified";
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(
      conn, query, stmt_timeout_ms, false /* use_fetch */));
  ASSERT_FALSE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 1);

  // Repeating the above test with the statement timeout lower than the RPC timeout should cause
  // the statement timer to fire.
  stmt_timeout_ms = 2500;
  ignore_client_timeout = true;
  query = "ALTER TABLE t1_modified RENAME TO t1";
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(
    conn, query, stmt_timeout_ms, false /* use_fetch */));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 1);
}

TEST_F(PggateTimeoutTest, YB_DISABLE_TEST_IN_SANITIZERS(StatementTimeoutInTxn)) {
  PGConn conn = ASSERT_RESULT(SetupCommonTestConnection());

  auto _1 = MockPerformBefore(MockPerformBeforeFunc);
  bool timed_out = false;
  uint stmt_timeout_ms = 500;
  sleep_time = 550ms;
  std::string query = "SELECT * FROM t1 WHERE k <= 10";

  // Validate that a statement timeout set within a txn is reset once the txn completes.
  ASSERT_OK(conn.StartTransaction(READ_COMMITTED));
  ASSERT_OK(conn.ExecuteFormat("SET statement_timeout TO '$0ms'", stmt_timeout_ms));
  timed_out = ASSERT_RESULT(ExpectStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 1);
  ASSERT_OK(conn.RollbackTransaction());
  mocked_rpcs.store(0, std::memory_order_release);
  ASSERT_OK(conn.Fetch(query));
  ASSERT_EQ(mocked_rpcs.load(), 1);

  // Similarly, validate that rolling back to a subtransaction or resetting the statement timeout
  // also has the intended effect.
  ASSERT_OK(conn.StartTransaction(READ_COMMITTED));
  // No statement timeout
  ASSERT_OK(conn.Fetch(query));
  ASSERT_OK(conn.Execute("SAVEPOINT S1")); // S1 has no statement timeout
  ASSERT_OK(conn.ExecuteFormat("SET statement_timeout TO '$0ms'", stmt_timeout_ms));
  timed_out = ASSERT_RESULT(ExpectStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);

  ASSERT_OK(conn.Execute("ROLLBACK TO S1"));
  ASSERT_OK(conn.ExecuteFormat("SET statement_timeout TO '$0ms'", stmt_timeout_ms));
  ASSERT_OK(conn.Execute("SAVEPOINT S2")); // S2 has statement timeout

  // The statement timeout should still work
  timed_out = ASSERT_RESULT(ExpectStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);

  ASSERT_OK(conn.Execute("ROLLBACK TO S2"));
  ASSERT_OK(conn.Execute("RESET statement_timeout"));
  ASSERT_OK(conn.Fetch(query));
  ASSERT_OK(conn.ExecuteFormat("SET statement_timeout TO '$0ms'", stmt_timeout_ms));
  ASSERT_OK(conn.Execute("ROLLBACK TO S1"));
  ASSERT_OK(conn.Fetch(query));
}

TEST_F(PggateTimeoutTest, YB_DISABLE_TEST_IN_SANITIZERS(PostgresFeatures)) {
  PGConn conn = ASSERT_RESULT(SetupCommonTestConnection());

  auto _ = MockPerformBefore(MockPerformBeforeFunc);
  bool timed_out = false;
  uint stmt_timeout_ms = 500;
  std::string query = "SELECT pg_sleep(1) /* 1s */";

  // Validate that statement timeout is respected even when RPCs are not involved.
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 0);

  // Validate that statement timeout is applied correctly to cursors.
  ASSERT_OK(conn.StartTransaction(READ_COMMITTED));
  ASSERT_OK(conn.ExecuteFormat("SET statement_timeout TO '$0ms'", stmt_timeout_ms));
  // Declare two cursors and fetch one row at a time.
  ASSERT_OK(conn.Execute("SET yb_fetch_row_limit TO 1"));
  ASSERT_OK(conn.Execute("DECLARE c1 CURSOR FOR SELECT * FROM t1 WHERE k <= 10"));
  ASSERT_OK(conn.Execute("DECLARE c2 CURSOR FOR SELECT * FROM t1 WHERE k <= 10"));

  uint num_rows = 5;
  query = "FETCH FORWARD 1 FROM c1";
  sleep_time = 0ms;
  for (uint i = 0; i < num_rows; ++i) {
    ASSERT_OK(conn.Fetch(query));
  }

  // Time between queries on an active cursor should not be counted towards statement timeout.
  LOG(INFO) << "Sleeping before executing queries on cursor c1";
  SleepFor(1s);
  ASSERT_OK(conn.Fetch(query));

  // Similarly, time between queries across cursors should not have an effect on statement timeout.
  LOG(INFO) << "Sleeping between executing queries on cursor c1 and c2";
  SleepFor(1s);
  query = "FETCH FORWARD 1 FROM c2";
  ASSERT_OK(conn.Fetch(query));

  ASSERT_OK(conn.CommitTransaction());

  // Statement timeouts should still apply on the same connection after cursor activity.
  query = "SELECT * FROM t1 WHERE k <= 10";
  sleep_time = 550ms;
  timed_out = ASSERT_RESULT(ValidateStatementTimeout(conn, query, stmt_timeout_ms));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 1);
}

// Test to validate that a client is usable after a statement timeout even if the tserver is still
// working on the previous RPC.
TEST_F(PggateTimeoutTest, YB_DISABLE_TEST_IN_SANITIZERS(ClientAbortWhileServerWorking)) {
  PGConn conn = ASSERT_RESULT(SetupCommonTestConnection());

  // After-mock sleep runs after flush (session lock already released) so the client can abort at
  // the RPC deadline while server-side work is still in flight, without HOL-blocking follow-up
  // catalog Performs the way a Before sleep would.
  sleep_time = 2000ms;
  ignore_client_timeout = true;

  const uint stmt_timeout_ms = 500;
  // Network may wait until the Perform RPC deadline (stmt + extra); SHM can surface the
  // statement timeout closer to the timer itself. Accept either (strict=false).
  const std::string query = "SELECT * FROM t1 WHERE k <= 10";

  auto after_mock = std::optional(MockPerformAfter(MockPerformAfterFunc));
  bool timed_out = ASSERT_RESULT(ValidateStatementTimeout(
      conn, query, stmt_timeout_ms, true /* use_fetch */, false /* strict */));
  ASSERT_TRUE(timed_out);
  // Client returned well before the mock's sleep finishes; wait it out before
  // reusing the connection.
  SleepFor(sleep_time);
  after_mock.reset();

  ASSERT_OK(conn.Fetch(query));
  auto nrows = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM t1"));
  ASSERT_EQ(nrows, 10);
}

// Test to verify that interrupts are processed between RPCs even if control does not return back to
// Postgres.
TEST_F(PggateTimeoutTest, YB_DISABLE_TEST_IN_SANITIZERS(InterruptBetweenPggateRpcs)) {
  // Needle-in-a-haystack query where the needle (key value) is not found.
  // Multi tablets belonging to the table are scanned, and control does not
  // return back to Postgres between RPCs. The statement times out between
  // RPC 1 and 2.
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE mt (k INT PRIMARY KEY, v INT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO mt SELECT i, i FROM generate_series(1, 30) i"));
  ASSERT_OK(conn.Execute("SET log_min_messages TO 'DEBUG2'"));
  ASSERT_OK(conn.Execute("SET yb_debug_log_docdb_requests TO true"));

  auto mock_handle = std::optional(MockPerformBefore(MockPerformBeforeFunc));
  sleep_time = 1s;
  ignore_client_timeout = false;
  const uint stmt_timeout_ms = 300;
  const std::string query = "SELECT * FROM mt WHERE v = -1";

  bool timed_out = ASSERT_RESULT(ValidateStatementTimeout(
      conn, query, stmt_timeout_ms, true /* use_fetch */, false /* strict */));
  ASSERT_TRUE(timed_out);
  ASSERT_EQ(mocked_rpcs.load(), 1);
  mock_handle.reset();

  // Similar to the above, a statement cancellation arrives while the server is
  // working on RPC 2 and the query is cancelled between RPCs 2 and 3.
  ClearMockState();
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE p1 (k INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("CREATE TABLE p2 (k INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("CREATE TABLE p3 (k INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE child ("
      "  k INT PRIMARY KEY,"
      "  a INT REFERENCES p1(k),"
      "  b INT REFERENCES p2(k),"
      "  c INT REFERENCES p3(k))"));
  ASSERT_OK(conn.Execute("INSERT INTO p1 VALUES (1)"));
  ASSERT_OK(conn.Execute("INSERT INTO p2 VALUES (1)"));
  ASSERT_OK(conn.Execute("INSERT INTO p3 VALUES (1)"));
  // Warm pggate table cache so the INSERT under test does not issue OpenTable RPCs.
  ASSERT_OK(conn.Execute("SET ysql_session_max_batch_size = 1"));
  ASSERT_OK(conn.Execute("INSERT INTO child VALUES (0, 1, 1, 1)"));
  ASSERT_OK(conn.Execute("DELETE FROM child WHERE k = 0"));

  const auto pid = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  PGConn aux_conn = ASSERT_RESULT(Connect());

  mock_handle = MockPerformBefore(MockPerformBeforeFunc);
  // Uniform per-RPC sleep. Cancel mid-RPC2; one more RPC-length wait would allow a 3rd
  // Perform to start if the interrupt were not processed between RPCs.
  sleep_time = 200ms * kTimeMultiplier;
  ignore_client_timeout = true;

  Status insert_status;
  TestThreadHolder thread_holder;
  thread_holder.AddThread([&conn, &insert_status] {
    insert_status = conn.Execute("INSERT INTO child VALUES (1, 1, 1, 1)");
  });

  // Wait until we're sure that the server is working on RPC 2.
  SleepFor(sleep_time + sleep_time / 2);
  ASSERT_TRUE(ASSERT_RESULT(
      aux_conn.FetchRow<bool>(Format("SELECT pg_cancel_backend($0)", pid))));

  // Ensure that the client has had enough time to send further RPCs in case the
  // the interrupt was not processed. This will ensure that the test fails if
  // the interrupt was not processed between RPCs.
  SleepFor(sleep_time);
  ASSERT_EQ(mocked_rpcs.load(), 2);

  thread_holder.JoinAll();
  ASSERT_NOK(insert_status);
  const auto msg = insert_status.message().ToBuffer();
  ASSERT_NE(msg.find("canceling statement due to user request"), std::string::npos) << msg;
}


TEST_F(PgMiniTest, KillPGInTheMiddleOfBatcherOperation) {
  const std::string kTableName = "test_table";
  const auto kQuery = Format("SELECT * FROM $0", kTableName);

  auto& sync_point = *SyncPoint::GetInstance();
  sync_point.LoadDependency(
      {{"Batcher::ProcessRpcStatus1", "KillPGInTheMiddleOfBatcherOperation::BeforePgRestart"},
       {"KillPGInTheMiddleOfBatcherOperation::AfterPgRestart", "Batcher::ProcessRpcStatus2"}});

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int) SPLIT INTO 10 TABLETS", kTableName));
  ASSERT_OK(conn.FetchAllAsString(kQuery));  // Sanity check

  sync_point.EnableProcessing();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_batcher_rpc) = true;

  std::atomic<bool> select_complete = false;
  TestThreadHolder thread_holder;
  thread_holder.AddThread([&conn, &select_complete, kQuery] {
    ASSERT_NOK(conn.FetchAllAsString(kQuery));
    select_complete = true;
  });

  // Block the batcher operations.
  TEST_SYNC_POINT("KillPGInTheMiddleOfBatcherOperation::BeforePgRestart");

  // The select should still be running because it's stuck waiting for the sync point.
  ASSERT_FALSE(select_complete.load());

  LOG(INFO) << "Restarting Postgres";
  ASSERT_OK(RestartPostgres());
  // Wait for the Sessions to be killed.
  SleepFor(5s);

  // Unblock the batcher operation.
  TEST_SYNC_POINT("KillPGInTheMiddleOfBatcherOperation::AfterPgRestart");

  thread_holder.JoinAll();
  ASSERT_TRUE(select_complete.load());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_batcher_rpc) = false;
}

// The test checks absence of t-server crash in case some tables can't be opened during
// read/write operation
TEST_F_EX(PgMiniTest, OpenTableFailureDuringPerform, PgMiniTestSingleNode) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT PRIMARY KEY)"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_request_unknown_tables_during_perform) = true;
  auto has_object_not_found_errors = false;
  for ([[maybe_unused]] auto _  : std::views::iota(0, 10)) {
    auto res = conn.FetchRows<int32_t>("SELECT * FROM t");
    ASSERT_TRUE(res.ok() || res.ToString().contains("OBJECT_NOT_FOUND"));
    has_object_not_found_errors |= !res.ok();
  }
  ASSERT_TRUE(has_object_not_found_errors);
}

TEST_F(PgMiniTest, TabletMetadataCorrectnessWithHashPartitioning) {
  auto pg_conn = ASSERT_RESULT(Connect());

  // Create a hash-partitioned table with multiple tablets
  ASSERT_OK(pg_conn.Execute(
      "CREATE TABLE hash_test_table (id INT PRIMARY KEY, data TEXT) "
      "SPLIT INTO 3 TABLETS"));

  // Insert test data
  const int test_key = 12345;
  const std::string test_data = "test_data";
  ASSERT_OK(pg_conn.ExecuteFormat(
      "INSERT INTO hash_test_table (id, data) VALUES ($0, '$1')", test_key, test_data));

  // Get the hash code of the primary key using yb_hash_code()
  auto hash_code = ASSERT_RESULT(pg_conn.FetchRow<int32_t>(
      yb::Format("SELECT yb_hash_code($0)", test_key)));
  LOG(INFO) << "Hash code for key " << test_key << " is: " << hash_code;

  // Find which tablet this hash falls into using yb_tablet_metadata
  auto tablet_from_metadata = ASSERT_RESULT(pg_conn.FetchRow<std::string>(
      yb::Format("SELECT tablet_id FROM yb_tablet_metadata "
             "WHERE db_name = current_database() AND relname = 'hash_test_table' "
             "AND $0 >= start_hash_code AND $0 < end_hash_code", hash_code)));
  LOG(INFO) << "Tablet ID from yb_tablet_metadata: " << tablet_from_metadata;

  // Verify using internal methods - get the actual tablet ID
  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("hash_test_table"));
  auto tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);

  std::string actual_tablet_id;
  bool found_tablet = false;

  for (const auto& peer : tablet_peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto partition = tablet->metadata()->partition();

    // Check if this tablet contains our hash code
    auto hash_bounds = ASSERT_RESULT(partition->GetKeysAsHashBoundsInclusive());
    uint16_t start_hash = hash_bounds.first;
    uint16_t end_hash = hash_bounds.second;

    LOG(INFO) << "Tablet " << peer->tablet_id()
              << " hash range: [" << start_hash << ", " << end_hash << ")";

    // Check if our hash code falls in this tablet's range
    if (static_cast<uint16_t>(hash_code) >= start_hash &&
        static_cast<uint16_t>(hash_code) < end_hash) {
      actual_tablet_id = peer->tablet_id();
      found_tablet = true;
      LOG(INFO) << "Found tablet containing hash " << hash_code
                << ": " << actual_tablet_id;
      break;
    }
  }

  ASSERT_TRUE(found_tablet) << "Could not find tablet containing hash code " << hash_code;

  // Verify that both methods return the same tablet ID
  ASSERT_EQ(tablet_from_metadata, actual_tablet_id)
      << "Tablet ID mismatch: yb_tablet_metadata returned " << tablet_from_metadata
      << " but internal method found " << actual_tablet_id;

  LOG(INFO) << "Test verified: yb_tablet_metadata correctly identifies tablet "
            << actual_tablet_id << " for hash code " << hash_code;

  // Additional verification: query the data using the tablet information
  auto retrieved_data = ASSERT_RESULT(pg_conn.FetchRow<std::string>(
      yb::Format("SELECT data FROM hash_test_table WHERE id = $0", test_key)));
  ASSERT_EQ(retrieved_data, test_data);

  LOG(INFO) << "Successfully retrieved data '" << retrieved_data
            << "' from tablet " << actual_tablet_id;
}

TEST_F(PgMiniTest, TabletMetadataOidMatchesPgClass) {
  auto pg_conn = ASSERT_RESULT(Connect());
  // Create the table without a primary key, then ADD PRIMARY KEY to force a
  // table rewrite. The rewrite preserves the table's PG OID but assigns it a new
  // relfilenode (a new DocDB table whose UUID encodes that relfilenode), so
  // oid != relfilenode afterwards.
  ASSERT_OK(pg_conn.Execute("CREATE TABLE test_table (id INT, name TEXT)"));
  ASSERT_OK(pg_conn.Execute("ALTER TABLE test_table ADD PRIMARY KEY (id)"));

  const auto pg_class_oid = ASSERT_RESULT(pg_conn.FetchRow<pgwrapper::PGOid>(
      "SELECT oid FROM pg_class WHERE relname = 'test_table'"));
  const auto pg_class_relfilenode = ASSERT_RESULT(pg_conn.FetchRow<pgwrapper::PGOid>(
      "SELECT relfilenode FROM pg_class WHERE relname = 'test_table'"));

  // Guard the test's premise: the rewrite must actually have moved the storage,
  // otherwise relfilenode == oid and this test would not distinguish them.
  ASSERT_NE(pg_class_oid, pg_class_relfilenode)
      << "ALTER TABLE ADD PRIMARY KEY did not rewrite test_table; oid and "
      << "relfilenode are both " << pg_class_oid;

  // Look the table up in the view by (db_name, relname) and confirm it reports
  // pg_class's stable oid -- which survives the rewrite -- and not the now
  // diverged relfilenode.
  ASSERT_OK(WaitFor([&pg_conn, pg_class_oid]() -> Result<bool> {
    return pg_conn.FetchRow<bool>(Format(
        "SELECT EXISTS (SELECT 1 FROM yb_tablet_metadata "
        "WHERE db_name = current_database() AND relname = 'test_table' "
        "AND oid = $0)", pg_class_oid));
  }, 30s, "yb_tablet_metadata exposes test_table's stable oid"));
}

TEST_F(PgMiniTest, TabletMetadataMaskingSurvivesRewrite) {
  // A role holding SELECT on a table sees that table's yb_tablet_metadata rows
  // unmasked. A table rewrite assigns a new relfilenode but preserves the
  // pg_class OID.
  const std::string kTable = "rewrite_range_grant";
  const std::string kRole = "tmeta_unpriv";

  auto conn = ASSERT_RESULT(Connect());

  // Range-sharded so start_range/end_range carry real bounds.
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT NOT NULL, v INT, PRIMARY KEY (k ASC)) "
      "SPLIT AT VALUES ((5), (10))", kTable));
  ASSERT_OK(conn.ExecuteFormat("CREATE ROLE $0", kRole));
  ASSERT_OK(conn.ExecuteFormat("GRANT SELECT ON $0 TO $1", kTable, kRole));

  const auto oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT oid FROM pg_class WHERE relname = '$0'", kTable)));
  const auto relfilenode_before = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT relfilenode FROM pg_class WHERE relname = '$0'", kTable)));
  // A freshly created table's relfilenode equals its OID.
  ASSERT_EQ(oid, relfilenode_before);

  // Trigger a table rewrite via a volatile column default: new relfilenode, same OID.
  ASSERT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN w float DEFAULT random()", kTable));

  const auto oid_after = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT oid FROM pg_class WHERE relname = '$0'", kTable)));
  const auto relfilenode_after = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT relfilenode FROM pg_class WHERE relname = '$0'", kTable)));
  ASSERT_EQ(oid_after, oid) << "rewrite must not change the pg_class OID";
  ASSERT_NE(relfilenode_after, relfilenode_before)
      << "ALTER TABLE ... ADD COLUMN DEFAULT random() did not rewrite the table";

  // After the rewrite the granted role must still see the table unmasked,
  // because the ACL check uses the stable OID, not the changed relfilenode.
  ASSERT_OK(conn.ExecuteFormat("SET ROLE $0", kRole));

  // Wait for all three tablets of the rewritten range table to surface.
  ASSERT_OK(WaitFor([&conn, oid]() -> Result<bool> {
    return VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
        "SELECT count(*) FROM yb_tablet_metadata "
        "WHERE oid = $0 AND db_name = current_database()",
        oid))) == 3;
  }, 30s, "all 3 tablets of the rewritten range table are visible"));

  // relname is the real name on every row, not the masked placeholder.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(Format(
      "SELECT DISTINCT relname FROM yb_tablet_metadata "
      "WHERE oid = $0 AND db_name = current_database()",
      oid))), kTable);

  // No relname/start_range/end_range cell is masked.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
      "SELECT count(*) FROM yb_tablet_metadata "
      "WHERE oid = $0 AND db_name = current_database() "
      "AND '<insufficient privilege>' IN (relname, start_range, end_range)",
      oid))), 0) << "a cell was masked for a role that holds SELECT";

  // The middle [5,10) tablet exposes real (non-NULL) range bounds.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
      "SELECT count(*) FROM yb_tablet_metadata "
      "WHERE oid = $0 AND db_name = current_database() "
      "AND start_range IS NOT NULL AND end_range IS NOT NULL",
      oid))), 1) << "expected the [5,10) tablet to expose real range bounds";

  ASSERT_OK(conn.Execute("RESET ROLE"));
}

TEST_F(PgMiniTest, TabletMetadataStateColumn) {
  auto pg_conn = ASSERT_RESULT(Connect());

  // ======== RUNNING ========
  // Create a table and verify all its tablets report RUNNING.
  ASSERT_OK(pg_conn.Execute(
      "CREATE TABLE state_test (id INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  auto running_count = ASSERT_RESULT(pg_conn.FetchRow<int64_t>(
      "SELECT count(*) FROM yb_get_tablet_metadata() "
      "WHERE object_name = 'state_test' AND tablet_state = 'RUNNING'"));
  ASSERT_EQ(running_count, 1);
  LOG(INFO) << "RUNNING state verified";

  // ======== DELETED via DROP TABLE ========
  // Create a second table, record its tablet ID, then drop it.
  ASSERT_OK(pg_conn.Execute(
      "CREATE TABLE delete_test (id INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  auto delete_table_id = ASSERT_RESULT(GetTableIDFromTableName("delete_test"));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), delete_table_id);
  ASSERT_EQ(peers.size(), 1);
  auto deleted_tablet_id = peers[0]->tablet_id();

  // CleanUpDeletedTables marks the table DELETED on one cycle and erases it from tablet_map_ on
  // the next, so DELETED is observable for a single cycle only. Stretch the cycle so that window
  // outlives DROP, whose PG-side commit work is slow under sanitizers.
  const auto bg_task_wait_ms = FLAGS_catalog_manager_bg_task_wait_ms;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) = 5000 * kTimeMultiplier;

  ASSERT_OK(pg_conn.Execute("DROP TABLE delete_test"));

  ASSERT_OK(LoggedWaitFor(
      [&pg_conn, &deleted_tablet_id]() -> Result<bool> {
        auto count = VERIFY_RESULT(pg_conn.FetchRow<int64_t>(Format(
            "SELECT count(*) FROM yb_get_tablet_metadata() "
            "WHERE tablet_id = '$0' AND tablet_state = 'DELETED'", deleted_tablet_id)));
        return count > 0;
      },
      30s * kTimeMultiplier, "Wait for DELETED tablet state after DROP TABLE"));
  LOG(INFO) << "DELETED state verified for tablet " << deleted_tablet_id;
  // The REPLACED phase below needs the background task back at its normal cadence.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) = bg_task_wait_ms;

  // ======== REPLACED via creation timeout ========
  // Set a very low creation timeout, shut down 2 of 3 tservers so new tablets can't
  // get a quorum, and create a CQL table (non-blocking). The master will mark the
  // timed-out tablets as REPLACED.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_creation_timeout_ms) = 1000;

  cluster_->mini_tablet_server(1)->Shutdown();
  cluster_->mini_tablet_server(2)->Shutdown();

  ASSERT_OK(client_->CreateNamespaceIfNotExists("test_ks", YQLDatabase::YQL_DATABASE_CQL));

  client::YBSchemaBuilder builder;
  builder.AddColumn("id")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
  client::YBSchema schema;
  ASSERT_OK(builder.Build(&schema));

  auto table_name = client::YBTableName(YQL_DATABASE_CQL, "test_ks", "replaced_test");
  ASSERT_OK(client_->NewTableCreator()
      ->table_name(table_name)
      .schema(&schema)
      .num_tablets(1)
      .wait(false)
      .Create());

  // Wait for creation timeout to trigger REPLACED.
  ASSERT_OK(LoggedWaitFor(
      [&pg_conn]() -> Result<bool> {
        auto count = VERIFY_RESULT(pg_conn.FetchRow<int64_t>(
            "SELECT count(*) FROM yb_get_tablet_metadata() "
            "WHERE object_name = 'replaced_test' AND tablet_state = 'REPLACED'"));
        return count > 0;
      },
      30s * kTimeMultiplier, "Wait for REPLACED tablet state"));
  LOG(INFO) << "REPLACED state verified";

  // Restart stopped tservers and shut down cluster to prevent consistency check
  // from failing on the partially-created CQL table.
  ASSERT_OK(cluster_->mini_tablet_server(1)->RestartStoppedServer());
  ASSERT_OK(cluster_->mini_tablet_server(2)->RestartStoppedServer());
  cluster_->Shutdown();
}

TEST_F(PgMiniTest, TestYbGetLocalTserverUuid) {
  auto pg_conn = ASSERT_RESULT(Connect());
  auto local_tserver_uuid = ASSERT_RESULT(pg_conn.FetchRow<Uuid>(
      "SELECT yb_get_local_tserver_uuid()"));
  auto expected_uuid = ASSERT_RESULT(
      Uuid::FromHexStringBigEndian(cluster_->mini_tablet_server(0)->server()->permanent_uuid()));
  ASSERT_EQ(local_tserver_uuid, expected_uuid)
      << "Local tserver UUID mismatch";
}

// Despite the call to the stored procedure failing and the client issuing a commit, assert that
// the transaction abort is initiated inline with the commit request in addition to returning the
// failure status.
//
// Without the explicit abort, we expect the strong references of YBTransaction to drop to 0 and
// the txn heartbeat thread to stop, causing the status tablet to clean up the transaction due
// to missed heartbeats. But we observed a case of a stale transaction being left behind in the
// system, and weren't able to track down the leaked strong reference. Hence, we now initiate the
// abort inline and ensure that the transaction eventually gets cleaned up.
TEST_F_EX(PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(TestAbortTxnErroredWithReadRestartOnCommit),
          PgMiniLargeClockSkewTest) {
  google::SetVLOGLevel("transaction*", 2);
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE t1 (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("CREATE TABLE t2 (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO t1 select generate_series(1, 10), 0"));
  ASSERT_OK(setup_conn.Execute(R"#(
CREATE OR REPLACE PROCEDURE test_proc()
 LANGUAGE plpgsql
AS $procedure$
DECLARE
    i int;
BEGIN
    BEGIN
        DELETE FROM t1 WHERE k <= 10;
    END;
    BEGIN
        FOREACH i IN ARRAY ARRAY[1, 2, 3, 4, 5, 6, 7, 8, 9, 10] LOOP
            BEGIN
                PERFORM SUM(v) FROM t2 WHERE k < 500 OR k > 1200;
                INSERT INTO t1 VALUES(i, 0);
            EXCEPTION WHEN OTHERS THEN
                RAISE NOTICE 'INSERT FAILED WITH %', SQLERRM;
                RETURN;
            END;
        END LOOP;
    END;
END;
$procedure$
  )#"));

  constexpr std::chrono::milliseconds kClockSkew = -100ms;
  constexpr CoarseDuration kWaitTime = 30s;
  auto delta_changers = SkewClocks(cluster_.get(), kClockSkew);
  TestThreadHolder thread_holder;
  std::atomic<int> num_read_restarts(0);
  thread_holder.AddThreadFunctor([this, &num_read_restarts, &stop = thread_holder.stop_flag()] {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("SET statement_timeout='60s'"));
    for (int iter = 0; !stop.load(std::memory_order_acquire); iter++) {
      ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 VALUES($0, $0)", 100 + iter));
      auto txn_id = ASSERT_RESULT(conn.FetchRow<Uuid>("SELECT yb_get_current_transaction()"));
      ASSERT_OK(conn.Execute("CALL test_proc()"));
      RegexWaiterLogSink log_waiter(Format(R"#(.*$0.*Abort)#", txn_id.ToString()));
      auto status = conn.Execute("COMMIT");
      if (!status.ok()) {
        ASSERT_NOK_STR_CONTAINS(
            status, "Commit of transaction that requires restart is not allowed");
        num_read_restarts++;
        ASSERT_OK(log_waiter.WaitFor(1s * kTimeMultiplier));
      }
    }
  });

  for (int i = 0; i < 2; ++i) {
    thread_holder.AddThreadFunctor([this, i, &stop = thread_holder.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.Execute("SET statement_timeout='60s'"));
      const auto start_key = i * 1000 + 1;
      const auto end_key = (i + 1) * 1000;
      while (!stop.load(std::memory_order_acquire)) {
        ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO t2 select generate_series($0, $1), 0", start_key, end_key));
        ASSERT_OK(conn.Execute("COMMIT"));
        ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
        ASSERT_OK(conn.ExecuteFormat(
            "DELETE FROM t2 WHERE k >= $0 AND k <= $1", start_key, end_key));
        ASSERT_OK(conn.Execute("COMMIT"));
      }
    });
  }

  thread_holder.WaitAndStop(kWaitTime);
  ASSERT_GT(num_read_restarts, 0);
}

class PgMiniKeyRangesTest : public PgMiniTest {
 protected:
  static constexpr auto kDecodingFailedMessage = "Failed to get encoded size of key";

  size_t NumTabletServers() override { return 1; }

  void SetUp() override {
    // Use small data blocks so vector index reverse mapping entries span multiple data blocks and
    // SST index entries point inside the regular DB metadata section.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 4_KB;
    PgMiniTest::SetUp();
  }

  // Runs GetTabletKeyRanges with empty bounds in both directions on every tablet of the test
  // table and verifies returned boundaries are valid user doc keys and the iterator never visited
  // regular DB metadata records.
  void VerifyKeyRangesSkipRegularDbMetadataSection(size_t expected_num_tablets) {
    auto peers = ASSERT_RESULT(
        ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
    ASSERT_EQ(peers.size(), expected_num_tablets);

    StringWaiterLogSink log_sink(kDecodingFailedMessage);

    for (const auto& peer : peers) {
      auto tablet = ASSERT_RESULT(peer->shared_tablet());
      for (auto direction : {tablet::Direction::kForward, tablet::Direction::kBackward}) {
        LOG(INFO) << "tablet: " << peer->tablet_id() << " direction: " << AsString(direction);
        std::vector<std::string> boundaries;
        ASSERT_OK(tablet->TEST_GetTabletKeyRanges(
            /* lower_bound_key = */ Slice(), /* upper_bound_key = */ Slice(),
            /* max_num_ranges = */ std::numeric_limits<uint64_t>::max(),
            /* range_size_bytes = */ 4_KB, direction, /* max_key_length = */ 1024,
            [&boundaries](Slice key) { boundaries.push_back(key.ToBuffer()); }));
        ASSERT_GT(boundaries.size(), 1);
        for (const auto& key : boundaries) {
          if (key.empty()) {
            continue;
          }
          ASSERT_FALSE(dockv::IsRegularDBMetaKeyType(dockv::DecodeKeyEntryType(key[0])))
              << "Range boundary inside regular DB metadata section: "
              << Slice(key).ToDebugHexString();
          ASSERT_OK(dockv::DocKey::EncodedSize(key, dockv::DocKeyPart::kWholeDocKey));
        }
      }
    }

    ASSERT_EQ(log_sink.GetEventCount(), 0)
        << "GetTabletKeyRanges iterated over regular DB metadata records";
  }

  // End-to-end variant: drives GetTabletKeyRanges through actual parallel scans (PG parallel
  // workers -> pggate GetTableKeyRanges -> PgClientService -> Read RPC) in both directions and
  // verifies query results are correct and the scans never visited regular DB metadata records.
  void VerifyParallelScanSkipsRegularDbMetadataSection(int64_t expected_num_rows) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("ANALYZE test"));
    ASSERT_OK(conn.Execute("SET yb_enable_parallel_scan_range_sharded = true"));
    ASSERT_OK(conn.Execute("SET yb_enable_cbo = on"));
    // Produce more parallel ranges from the small test table.
    ASSERT_OK(conn.Execute("SET yb_parallel_range_rows = 1"));
    ASSERT_OK(conn.Execute("SET yb_parallel_range_size = 1024"));

    StringWaiterLogSink log_sink(kDecodingFailedMessage);

    // Forward parallel scan. The plan check makes sure the query actually runs in parallel,
    // otherwise the test is vacuously green.
    ASSERT_STR_CONTAINS(
        ASSERT_RESULT(conn.FetchAllAsString(
            "/*+ Parallel(test 2 hard) */ EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM test")),
        "Parallel");
    const auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>(
        "/*+ Parallel(test 2 hard) */ SELECT COUNT(*) FROM test"));
    ASSERT_EQ(count, expected_num_rows);

    // Backward parallel scan. Also verifies ranges don't overlap or miss rows: a duplicate or
    // lost range boundary would show up as a duplicate or missing id.
    ASSERT_STR_CONTAINS(
        ASSERT_RESULT(conn.FetchAllAsString(
            "/*+ Parallel(test 2 hard) */ EXPLAIN (COSTS OFF)"
            " SELECT id FROM test ORDER BY id DESC")),
        "Parallel Index Scan Backward");
    const auto ids = ASSERT_RESULT(conn.FetchRows<int64_t>(
        "/*+ Parallel(test 2 hard) */ SELECT id FROM test ORDER BY id DESC"));
    ASSERT_EQ(ids.size(), static_cast<size_t>(expected_num_rows));
    for (int64_t i = 0; i < expected_num_rows; ++i) {
      ASSERT_EQ(ids[i], expected_num_rows - i);
    }

    ASSERT_EQ(log_sink.GetEventCount(), 0)
        << "Parallel scan iterated over regular DB metadata records";
  }

  // Creates a range-sharded table with a vector index, fills it with num_rows rows and flushes
  // reverse mapping entries to SSTs. The index is created before inserting rows, so reverse
  // mapping entries are written by the DML path and no backfill is involved.
  Status CreateIndexedTableAndFill(const std::string& create_table_suffix, int64_t num_rows) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE test (id bigint, embedding vector(3), PRIMARY KEY (id ASC))$0",
        create_table_suffix));
    RETURN_NOT_OK(conn.Execute("CREATE INDEX ON test USING ybhnsw (embedding vector_l2_ops)"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO test SELECT i, ARRAY[i, i + 1, i + 2]::vector"
        " FROM generate_series(1, $0) i",
        num_rows));
    // Wait for all intents are applied and flush tablets.
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster_.get()));
    return cluster_->FlushTablets();
  }
};

TEST_F_EX(PgMiniTest, GetTabletKeyRangesSkipsRegularDbMetadataSection, PgMiniKeyRangesTest) {
  // Single-tablet table: partition key start is empty, so the forward scan starts from the very
  // beginning of the regular DB.
  constexpr auto kNumRows = 2000;
  ASSERT_OK(CreateIndexedTableAndFill(/* create_table_suffix = */ "", kNumRows));
  ASSERT_NO_FATALS(VerifyParallelScanSkipsRegularDbMetadataSection(kNumRows));
  ASSERT_NO_FATALS(VerifyKeyRangesSkipRegularDbMetadataSection(/* expected_num_tablets = */ 1));
}

TEST_F_EX(
    PgMiniTest, GetTabletKeyRangesSkipsRegularDbMetadataSectionPreSplit, PgMiniKeyRangesTest) {
  // Pre-split table: middle/last tablets have a non-empty partition key start, but each tablet's
  // regular DB still has its own metadata section below the partition start. The backward scan
  // shouldn't go below the partition start.
  constexpr auto kNumRows = 2000;
  ASSERT_OK(CreateIndexedTableAndFill(" SPLIT AT VALUES ((700), (1400))", kNumRows));
  ASSERT_NO_FATALS(VerifyParallelScanSkipsRegularDbMetadataSection(kNumRows));
  ASSERT_NO_FATALS(VerifyKeyRangesSkipRegularDbMetadataSection(/* expected_num_tablets = */ 3));
}

}  // namespace yb::pgwrapper
