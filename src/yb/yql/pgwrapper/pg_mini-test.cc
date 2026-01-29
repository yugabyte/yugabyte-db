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
#include <optional>
#include <thread>
#include <string_view>

#include <boost/preprocessor/seq/for_each.hpp>

#include <gtest/gtest.h>

#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_flags.h"
#include "yb/common/pgsql_error.h"

#include "yb/dockv/value_type.h"

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
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/random_util.h"
#include "yb/util/range.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/rpc/rpc_context.h"

#include "yb/yql/pggate/pggate_flags.h"

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
DECLARE_bool(delete_intents_sst_files);
DECLARE_bool(enable_automatic_tablet_splitting);
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

DECLARE_double(TEST_respond_write_failed_probability);
DECLARE_double(TEST_transaction_ignore_applying_probability);

DECLARE_int32(TEST_inject_mvcc_delay_add_leader_pending_ms);
DECLARE_int32(TEST_txn_participant_inject_latency_on_apply_update_txn_ms);
DECLARE_int32(gzip_stream_compression_level);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(stream_compression_algo);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_int32(tracing_level);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(txn_max_apply_batch_records);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_int32(ysql_yb_ash_sample_size);
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

Result<int64_t> GetCatalogVersion(PGConn* conn) {
  if (FLAGS_ysql_enable_db_catalog_version_mode) {
    const auto db_oid = VERIFY_RESULT(conn->FetchRow<PGOid>(Format(
        "SELECT oid FROM pg_database WHERE datname = '$0'", PQdb(conn->get()))));
    return conn->FetchRow<PGUint64>(
        Format("SELECT current_version FROM pg_yb_catalog_version where db_oid = $0", db_oid));
  }
  return conn->FetchRow<PGUint64>("SELECT current_version FROM pg_yb_catalog_version");
}

Result<bool> IsCatalogVersionChangedDuringDdl(PGConn* conn, const std::string& ddl_query) {
  const auto initial_version = VERIFY_RESULT(GetCatalogVersion(conn));
  RETURN_NOT_OK(conn->Execute(ddl_query));
  return initial_version != VERIFY_RESULT(GetCatalogVersion(conn));
}

Status IsReplicaIdentityPopulatedInTabletPeers(
    PgReplicaIdentity expected_replica_identity, std::vector<tablet::TabletPeerPtr> tablet_peers,
    std::string table_id) {
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
  ASSERT_EQ(connections.size() + kAshConnection, client_service->TEST_SessionsCount());

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
      if (strcmp(base_filename, "trace.cc") == 0) {
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

  SetAtomicFlag(0.25, &FLAGS_TEST_respond_write_failed_probability);

  LOG(INFO) << "Insert " << kKeys << " keys";
  for (int key = 0; key != kKeys; ++key) {
    auto status = conn.ExecuteFormat("INSERT INTO t (key) VALUES ($0)", key);
    ASSERT_TRUE(status.ok() || PgsqlError(status) == YBPgErrorCode::YB_PG_UNIQUE_VIOLATION ||
                status.ToString().find("Duplicate request") != std::string::npos) << status;
  }

  SetAtomicFlag(0, &FLAGS_TEST_respond_write_failed_probability);

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
    SetAtomicFlag(250000ULL, &FLAGS_max_clock_skew_usec);
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
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

  // Truncate the table, and flush. Tabletombstone should be in a seperate sst file.
  ASSERT_OK(conn.Execute("TRUNCATE t1"));
  SleepFor(1s);
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

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
      ASSERT_OK(connection.CopyBegin(Format("COPY $0 FROM STDIN WITH BINARY", kTableName)));
      for (int j = 0; j != kBatchSize; ++j) {
        connection.CopyStartRow(2);
        connection.CopyPutInt32(++key);
        connection.CopyPutString(RandomHumanReadableString(kValueSize));
      }

      ASSERT_OK(connection.CopyEnd());
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
  SetAtomicFlag(1.0, &FLAGS_TEST_transaction_ignore_applying_probability);
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
        ASSERT_TRUE(msg.find("server closed the connection unexpectedly") != std::string::npos)
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

  ASSERT_OK(sys_catalog_tablet->Flush(tablet::FlushMode::kSync));
  ASSERT_OK(sys_catalog_tablet->ForceManualRocksDBCompact());
  uint64_t base_file_size = sys_catalog_tablet->GetCurrentVersionSstFilesUncompressedSize();;

  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  ASSERT_OK(sys_catalog_tablet->Flush(tablet::FlushMode::kSync));

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

// Check that fetch of data amount exceeding the message size automatically paginates and succeeds
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

  // SeqScan, direct fetch from the main table
  ASSERT_OK(conn.Fetch("SELECT * FROM test"));
  // IndexScan, fetch from the main table by ybctids
  ASSERT_OK(conn.Fetch("SELECT * FROM test ORDER BY i"));
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
    FLAGS_stream_compression_algo = 1; // gzip
    FLAGS_gzip_stream_compression_level = 6; // old default compression level
    PgMiniTest::SetUp();
  }
};

TEST_F_EX(PgMiniTest, DISABLED_ReadsDuringRBS, PgMiniStreamCompressionTest) {
  constexpr auto kNumRows = RegularBuildVsSanitizers(10000, 100);
  constexpr auto kValueSize = RegularBuildVsSanitizers(10000, 100);
  constexpr auto kNumReaders = 200;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value BYTEA) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.CopyBegin("COPY t FROM STDIN WITH BINARY"));
  for (auto key : Range(kNumRows)) {
    conn.CopyStartRow(2);
    conn.CopyPutInt32(key);
    conn.CopyPutString(RandomString(kValueSize));
  }
  ASSERT_OK(conn.CopyEnd());

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
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

  LOG(INFO) << "T2 - BEGIN/INSERT";
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.Execute("INSERT INTO test(a) VALUES (1)"));

  LOG(INFO) << "Flush";
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

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
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

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

  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
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

  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

  ASSERT_OK(tablet_server->Restart());
  ASSERT_OK(tablet_server->WaitStarted());

  ASSERT_OK(conn1.CommitTransaction());
  ASSERT_OK(conn2.CommitTransaction());
  ASSERT_OK(conn3.CommitTransaction());

  // Wait for apply.
  SleepFor(kApplyWait);

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
    yb::tserver::PgFinishTransactionResponsePB* resp, yb::rpc::RpcContext* context) {
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
    rpc::RpcContext* context) {

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
    yb::tserver::PgHeartbeatResponsePB* resp, yb::rpc::RpcContext* context) {
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
             "WHERE relname = 'hash_test_table' "
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
  ASSERT_OK(pg_conn.Execute(
      "CREATE TABLE test_table (id INT PRIMARY KEY, name TEXT)"));
  auto pg_class_oid = ASSERT_RESULT(pg_conn.FetchRow<pgwrapper::PGOid>(
      "SELECT oid FROM pg_class WHERE relname = 'test_table'"));
  auto tablet_metadata_oid = ASSERT_RESULT(pg_conn.FetchRow<pgwrapper::PGOid>(
      "SELECT oid FROM yb_tablet_metadata WHERE relname = 'test_table' LIMIT 1;"));
  ASSERT_EQ(pg_class_oid, tablet_metadata_oid)
      << "OID mismatch: pg_class returned " << pg_class_oid
      << " but yb_tablet_metadata returned " << tablet_metadata_oid;
}

}  // namespace yb::pgwrapper
