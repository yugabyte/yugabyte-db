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
#include "yb/common/ddl_mode-test-util.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/docdb/doc_read_context.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"
#include "yb/master/pg_catalog_versions_cache.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/ts_manager.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/statistics.h"

#include "yb/server/skewed_clock.h"

#include "yb/tserver/full_compaction_manager.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/tablet_server.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/gutil/casts.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/cgroups.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/format.h"
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
DECLARE_bool(enable_schema_packing_gc);
DECLARE_bool(enable_tracing);
DECLARE_bool(enable_wait_queues);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_bool(use_bootstrap_intent_ht_filter);
DECLARE_bool(ysql_allow_duplicating_repeatable_read_queries);
DECLARE_bool(ysql_disable_index_backfill);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_yb_enable_ash);
DECLARE_bool(ysql_yb_enable_replica_identity);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_int32(backfill_index_rate_rows_per_sec);
DECLARE_int32(index_backfill_rpc_max_retries);
DECLARE_uint64(online_schema_change_backfill_rows_per_chunk);
DECLARE_uint32(TEST_online_schema_change_backfill_replay_chunks_per_tablet);
DECLARE_bool(TEST_online_schema_change_backfill_pause_controller);

DECLARE_double(TEST_respond_write_failed_probability);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_double(auto_compact_percent_obsolete);

DECLARE_int32(TEST_inject_mvcc_delay_add_leader_pending_ms);
DECLARE_int32(TEST_txn_participant_inject_latency_on_apply_update_txn_ms);
DECLARE_int32(auto_compact_check_interval_sec);
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
DECLARE_uint32(auto_compact_min_obsolete_keys_found);
DECLARE_uint32(auto_compact_stat_window_seconds);
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

Result<int64_t> BeginOnlineSchemaChange(
    PGConn* conn, const std::string& source_table, const std::string& target_table,
    const std::string& transform_fn) {
  return conn->FetchRow<int64_t>(Format(
      "SELECT yb_begin_mirror_table('$0'::regclass, '$1'::regclass, '$2'::regproc)",
      source_table, target_table, transform_fn));
}

// The id of the latest job over the pair, which the other lifecycle functions take. Read from
// the view: the catalog is not readable by a table owner who is not a superuser.
Result<int64_t> OnlineSchemaChangeJobId(
    PGConn* conn, const std::string& source_table, const std::string& target_table) {
  return conn->FetchRow<int64_t>(Format(
      "SELECT job_id FROM pg_catalog.yb_online_schema_change "
      "WHERE source_table = '$0'::regclass AND target_table = '$1'::regclass "
      "ORDER BY job_id DESC LIMIT 1",
      source_table, target_table));
}

// Waits for the copy with yb_wait_mirror_table, which also adds the source's foreign keys to the
// target.
Status WaitForOnlineSchemaChange(
    PGConn* conn, const std::string& source_table, const std::string& target_table) {
  const auto job_id = VERIFY_RESULT(OnlineSchemaChangeJobId(conn, source_table, target_table));
  RETURN_NOT_OK(conn->Execute("SET statement_timeout = '120s'"));
  const auto status = conn->ExecuteFormat(
      "DO $$$$ BEGIN PERFORM yb_wait_mirror_table($0); END $$$$", job_id);
  RETURN_NOT_OK(conn->Execute("RESET statement_timeout"));
  return status;
}

Status FinishOnlineSchemaChange(
    PGConn* conn, const std::string& source_table, const std::string& target_table) {
  const auto job_id = VERIFY_RESULT(OnlineSchemaChangeJobId(conn, source_table, target_table));
  return conn->ExecuteFormat(
      "DO $$$$ BEGIN PERFORM yb_finish_mirror_table($0); END $$$$", job_id);
}

Status AbortOnlineSchemaChange(
    PGConn* conn, const std::string& source_table, const std::string& target_table) {
  const auto job_id = VERIFY_RESULT(OnlineSchemaChangeJobId(conn, source_table, target_table));
  return conn->ExecuteFormat(
      "DO $$$$ BEGIN PERFORM yb_abort_mirror_table($0); END $$$$", job_id);
}

Result<std::string> LatestOnlineSchemaChangeJobStatus(PGConn* conn) {
  return conn->FetchRow<std::string>(
      "SELECT status FROM pg_catalog.yb_online_schema_change ORDER BY job_id DESC LIMIT 1");
}

// Recreates t1, t2 and the t1_to_t2 transform, which returns ROW(<transform_exprs>)::t2, and
// returns the status of yb_begin_mirror_table over them. Dropping t1 with CASCADE also drops the
// transform, which takes t1's row type.
Status BeginOnlineSchemaChangeOverShapes(
    PGConn* conn, const std::string& src_ddl, const std::string& dst_ddl,
    const std::string& transform_exprs,
    const std::vector<std::string>& extra_src_ddl = {},
    const std::vector<std::string>& extra_dst_ddl = {}) {
  RETURN_NOT_OK(conn->Execute("DROP TABLE IF EXISTS t2 CASCADE"));
  RETURN_NOT_OK(conn->Execute("DROP TABLE IF EXISTS t1 CASCADE"));
  RETURN_NOT_OK(conn->Execute(src_ddl));
  for (const auto& stmt : extra_src_ddl) {
    RETURN_NOT_OK(conn->Execute(stmt));
  }
  RETURN_NOT_OK(conn->Execute(dst_ddl));
  for (const auto& stmt : extra_dst_ddl) {
    RETURN_NOT_OK(conn->Execute(stmt));
  }
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
      "SET search_path FROM CURRENT AS $$$$ BEGIN RETURN ROW($0)::t2; END $$$$",
      transform_exprs));
  return ResultToStatus(BeginOnlineSchemaChange(conn, "t1", "t2", "t1_to_t2"));
}

// Ids of the tables whose tablets retain delete markers. Read from the tablet metadata, which is
// what compaction consults, so this also shows that the pin reached the tablets.
std::set<TableId> TablesRetainingDeleteMarkers(MiniCluster* cluster) {
  std::set<TableId> result;
  for (const auto& peer : ListTabletPeers(cluster, ListPeersFilter::kAll)) {
    auto metadata = peer->tablet_metadata();
    if (!metadata) {
      continue;
    }
    metadata->IterateColocatedTables([&result](const tablet::TableInfo& info) {
      if (info.schema().table_properties().retain_delete_markers()) {
        result.insert(info.table_id);
      }
    });
  }
  return result;
}

// For tests whose copy fails the same way on every attempt: the chunk is otherwise retried 10
// times with backoff, about 7 s, before the job fails. Two retries still ride out a transient
// error.
auto FewerChunkRetries() {
  const auto old_retries = FLAGS_index_backfill_rpc_max_retries;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_index_backfill_rpc_max_retries) = 2;
  return ScopeExit([old_retries] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_index_backfill_rpc_max_retries) = old_retries;
  });
}

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
  size_t NumTabletServers() override {
    return 1;
  }
};

// Online schema change requires transactional DDL, which debug builds leave off by default.
class PgMiniOnlineSchemaChangeTest : public PgMiniTestSingleNode {
 protected:
  void SetUp() override {
    ToggleDDLMode(/* use_legacy = */ false);
    PgMiniTestSingleNode::SetUp();
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

// A freshly connected backend's session has recorded the backend's database, although the backend
// has not yet touched any of the database's relations.
TEST_F(PgMiniTest, SessionRecordsConnectedDatabase) {
  auto setup_conn = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(setup_conn.FetchRow<PGOid>(
      "SELECT oid FROM pg_database WHERE datname = current_database()"));

  // This is not the first connection to this database, which makes the test stronger: the first
  // connection to a database does extra work building the relcache init file via reading catalogs,
  // which later connections do not do; PG client service could infer the database from those reads
  // alone, so only a later connection shows it is recorded without them.
  auto conn = ASSERT_RESULT(Connect());
  auto* client_service = cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientService();
  ASSERT_EQ(client_service->TEST_SessionDatabaseOid(conn.BackendPID()), db_oid);
}

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
  // for the platform's ListenConnectionShutdown delay.
  // Poll until it drains instead of asserting immediately.
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

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeSimple) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));

  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 'c')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'b2' WHERE id = 2"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 1"));

  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto status = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT status FROM pg_catalog.yb_online_schema_change ORDER BY job_id DESC LIMIT 1"));
  ASSERT_EQ(status, "finished");

  // After the swap, t1 is the former t2.
  const auto new_count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(new_count, 2);  // rows 2 and 3 (row 1 was deleted)

  const auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v, v2 FROM t1 WHERE id = 2")));
  ASSERT_EQ(row, (decltype(row){"b2", "b2_x"}));

  const auto missing = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE id = 1"));
  ASSERT_EQ(missing, 0);

  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

// The copy names the source row "s" and the transform's result "t".  Source columns with those
// names must not change what the copy passes to the transform, and an overload of the transform
// that takes such a column's type must not be called instead of it.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeSourceColumnsNamedLikeAliases) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, s INT, t TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 10, 'a'), (2, 20, 'b')"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, s INT, t TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.s, src.t || '_x')::t2; END $$"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src INT) RETURNS t2 LANGUAGE sql IMMUTABLE "
      "AS $$ SELECT ROW(-src, -src, 'wrong')::t2 $$"));

  ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT yb_begin_mirror_table('t1'::regclass, 't2'::regclass, "
      "'t1_to_t2(t1)'::regprocedure::regproc)"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  const auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t, std::string>(
      "SELECT id, s, t FROM t2 ORDER BY id")));
  ASSERT_EQ(rows, (decltype(rows){{1, 10, "a_x"}, {2, 20, "b_x"}}));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeDistributedBackfill) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, 100) AS i"));

  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(WaitFor([&conn]() -> Result<bool> {
    auto status = VERIFY_RESULT(conn.FetchRow<std::string>(
        "SELECT status FROM pg_catalog.yb_online_schema_change "
        "ORDER BY job_id DESC LIMIT 1"));
    return status == "ready";
  }, 60s, "Wait for online schema change backfill to complete"));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  // After the swap, t1 is the former t2.
  const auto count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(count, 100);

  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeConcurrentDml) {
  constexpr int kNumRows = 200;
  constexpr int kNumThreads = 4;
  constexpr int kDmlDurationMs = 2000;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT) SPLIT INTO 4 TABLETS"));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows)));

  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT) "
                         "SPLIT INTO 4 TABLETS"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  std::atomic<int64_t> insert_count{0};
  std::atomic<int64_t> update_count{0};
  std::atomic<int64_t> delete_count{0};

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(),
         &insert_count, &update_count, &delete_count] {
      auto dml_conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        const auto key = RandomUniformInt(1, kNumRows);
        const auto value = RandomHumanReadableString(16);
        const auto op = RandomUniformInt(0, 2);

        if (op == 0) {
          auto status = dml_conn.Execute(Format(
              "INSERT INTO t1(id, v) VALUES ($0, '$1') "
              "ON CONFLICT (id) DO UPDATE SET v = EXCLUDED.v", key, value));
          if (status.ok()) {
            insert_count++;
          }
        } else if (op == 1) {
          auto status = dml_conn.Execute(Format(
              "UPDATE t1 SET v = '$1' WHERE id = $0", key, value));
          if (status.ok()) {
            update_count++;
          }
        } else {
          auto status = dml_conn.Execute(Format("DELETE FROM t1 WHERE id = $0", key));
          if (status.ok()) {
            delete_count++;
          }
        }
      }
    });
  }

  SleepFor(MonoDelta::FromMilliseconds(kDmlDurationMs / 2));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  SleepFor(MonoDelta::FromMilliseconds(kDmlDurationMs / 2));

  thread_holder.Stop();

  LOG(INFO) << "Operations completed - inserts: " << insert_count.load()
            << ", updates: " << update_count.load()
            << ", deletes: " << delete_count.load();

  auto job_id = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT job_id FROM pg_catalog.pg_yb_online_schema_change "
      "WHERE source_table_oid = 't1'::regclass ORDER BY job_id DESC LIMIT 1"));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  std::string old_table_name = Format("yb_osc_old_$0", job_id);

  // The renamed old source is the ground truth.
  std::map<int, std::string> expected_state;
  auto old_rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
      Format("SELECT id, v FROM $0", old_table_name))));
  for (const auto& [id, v] : old_rows) {
    expected_state[id] = v;
  }

  std::map<int, std::string> actual_state;
  auto new_rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>("SELECT id, v FROM t1")));
  for (const auto& [id, v] : new_rows) {
    actual_state[id] = v;
  }

  LOG(INFO) << "Old source row count: " << expected_state.size()
            << ", New target row count: " << actual_state.size();

  int missing_count = 0, extra_count = 0, mismatch_count = 0;

  for (const auto& [id, v] : expected_state) {
    auto it = actual_state.find(id);
    if (it == actual_state.end()) {
      if (missing_count < 10) {
        LOG(ERROR) << "Row id=" << id << " in old source (v='" << v
                   << "') but not in new target";
      }
      missing_count++;
    } else if (it->second != v) {
      if (mismatch_count < 5) {
        LOG(ERROR) << "Row id=" << id << " value mismatch: old='" << v
                   << "', new='" << it->second << "'";
      }
      mismatch_count++;
    }
  }

  for (const auto& [id, v] : actual_state) {
    if (expected_state.count(id) == 0) {
      if (extra_count < 5) {
        LOG(ERROR) << "Row id=" << id << " in new target but not in old source";
      }
      extra_count++;
    }
  }

  if (missing_count > 0 || extra_count > 0 || mismatch_count > 0) {
    LOG(ERROR) << "Verification failed - missing: " << missing_count
               << ", extra: " << extra_count << ", mismatched: " << mismatch_count;
  }

  ASSERT_EQ(missing_count, 0) << "Some rows from old source are missing in new target";
  ASSERT_EQ(extra_count, 0) << "Some rows in new target don't exist in old source";
  ASSERT_EQ(mismatch_count, 0)
      << "Some rows have different values between old source and new target";

  const auto transform_mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(transform_mismatched, 0) << "Transform function not applied correctly";
}

// Only the target has a unique index on v. A row inserted after begin duplicates the v of an
// existing row, and the trigger writes it to the target first. When the copy then writes the
// existing row at the earlier read time, the uniqueness check must see the later entry, as it does
// for online CREATE INDEX, and fail the backfill.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeUniqueIndexViolation) {
  auto retries = FewerChunkRetries();
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));

  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX ON t2 (v)"));

  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 'a')"));

  auto backfill_status = WaitForOnlineSchemaChange(&conn, "t1", "t2");
  ASSERT_NOK(backfill_status);
  ASSERT_STR_CONTAINS(backfill_status.ToString(), "failed");
}

// The target has non-unique, unique, multi-column, expression and partial indexes. DML runs between
// begin and the end of the backfill, so rows written by both the trigger and the copy must be
// reachable through each index.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeIndexes) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, name TEXT, age INT, city TEXT)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 VALUES"
      " (1, 'alice', 30, 'nyc'),"
      " (2, 'bob', 25, 'sfo'),"
      " (3, 'carol', 35, 'nyc'),"
      " (4, 'dave', 40, 'sea'),"
      " (5, 'eve', 28, 'sfo'),"
      " (6, 'frank', 33, 'nyc'),"
      " (7, 'grace', 45, 'sea'),"
      " (8, 'heidi', 22, 'sfo')"));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, name TEXT, age INT, city TEXT, tag TEXT)"));

  ASSERT_OK(conn.Execute("CREATE INDEX t2_city_idx ON t2 (city)"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t2_name_idx ON t2 (name)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_city_age_idx ON t2 (city, age)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_lower_name_idx ON t2 (lower(name))"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_senior_idx ON t2 (age) WHERE age >= 30"));

  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.name, src.age, src.city, src.city || "
      "'_' || src.age::text)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (9, 'ivan', 38, 'nyc')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET age = 31 WHERE id = 1"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 8"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET name = 'robert' WHERE id = 2"));

  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // The current t1 is the ground truth.
  auto expected_rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string, int32_t, std::string>(
      "SELECT id, name, age, city FROM t1 ORDER BY id")));

  {
    auto t2_count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2"));
    ASSERT_EQ(static_cast<size_t>(t2_count), expected_rows.size()) << "row count differs";
  }

  {
    auto t2_rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string, int32_t, std::string>(
        "SELECT id, name, age, city FROM t2 ORDER BY id")));
    ASSERT_EQ(expected_rows.size(), t2_rows.size());
    for (size_t i = 0; i < expected_rows.size(); ++i) {
      ASSERT_EQ(expected_rows[i], t2_rows[i]) << "row mismatch at position " << i;
    }
  }

  {
    auto bad = ASSERT_RESULT(conn.FetchRow<int64_t>(
        "SELECT count(*) FROM t2 WHERE tag IS DISTINCT FROM (city || '_' || age)"));
    ASSERT_EQ(bad, 0) << "transform function not applied correctly";
  }

  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));

  for (const auto& [id, name, age, city] : expected_rows) {
    {
      auto t2_id = ASSERT_RESULT(conn.FetchRow<int32_t>(Format(
          "SELECT id FROM t2 WHERE name = '$0'", name)));
      ASSERT_EQ(t2_id, id) << "unique index lookup for name='" << name << "' returned wrong id";
    }

    {
      auto found = ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(*) FROM t2 WHERE city = '$0' AND id = $1", city, id)));
      ASSERT_EQ(found, 1) << "city index: row id=" << id << " not found via city='" << city << "'";
    }

    {
      auto found = ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(*) FROM t2 WHERE city = '$0' AND age = $1 AND id = $2",
          city, age, id)));
      ASSERT_EQ(found, 1) << "multi-column index: row id=" << id << " not found";
    }

    {
      auto t2_id = ASSERT_RESULT(conn.FetchRow<int32_t>(Format(
          "SELECT id FROM t2 WHERE lower(name) = lower('$0')", name)));
      ASSERT_EQ(t2_id, id) << "expression index: lower('" << name << "') returned wrong id";
    }

    if (age >= 30) {
      auto found = ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(*) FROM t2 WHERE age = $0 AND id = $1", age, id)));
      ASSERT_EQ(found, 1) << "partial index: row id=" << id << " (age=" << age << ") not found";
    }
  }
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeBulkUpdateFlushRequests) {
  constexpr int kNumRows = 500;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'val_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  auto explain_json = ASSERT_RESULT(conn.FetchRow<JsonDocument>(Format(
      "EXPLAIN (ANALYZE, DIST, DEBUG, FORMAT JSON) "
      "UPDATE t1 SET v = 'new_' || v")));

  LOG(INFO) << "EXPLAIN output: " << ASSERT_RESULT(explain_json.Root().ToString());

  auto flush_requests = ASSERT_RESULT(
      explain_json.Root()[0]["Storage Flush Requests"].GetDouble());
  LOG(INFO) << "Storage Flush Requests for bulk UPDATE on a captured source (" << kNumRows
            << " rows): " << flush_requests;
  ASSERT_LE(flush_requests, 10)
      << "Statement-level capture trigger should batch writes into very few flushes";

  auto write_requests = ASSERT_RESULT(
      explain_json.Root()[0]["Storage Write Requests"].GetDouble());
  LOG(INFO) << "Storage Write Requests: " << write_requests;

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
}

// Each tablet's copy must scan only its own range of a range-sharded source.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeRangePartitioned) {
  constexpr int kNumRows = 400;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC)) "
      "SPLIT AT VALUES ((100), (200), (300))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT, v TEXT, v2 TEXT, PRIMARY KEY (id ASC)) "
      "SPLIT AT VALUES ((100), (200), (300))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(count, kNumRows);

  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);

  // Spot-check rows from each tablet's range.
  for (int id : {1, 100, 200, 300, 400}) {
    const auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(Format(
        "SELECT v, v2 FROM t1 WHERE id = $0", id))));
    ASSERT_EQ(row, (decltype(row){Format("v_$0", id), Format("v_$0_x", id)}));
  }
}

// The trigger and the copy write by primary key value, so the sharding schemes of the source and
// the target are independent.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeCrossSchemeRangeToHash) {
  constexpr int kNumRows = 200;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC)) "
      "SPLIT AT VALUES ((50), (100), (150))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT) SPLIT INTO 4 TABLETS"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(count, kNumRows);

  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeRangeConcurrentDml) {
  constexpr int kNumRows = 300;
  constexpr int kNumThreads = 4;
  constexpr int kDmlDurationMs = 2000;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC)) "
      "SPLIT AT VALUES ((75), (150), (225))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT, v TEXT, v2 TEXT, PRIMARY KEY (id ASC)) "
      "SPLIT AT VALUES ((75), (150), (225))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag()] {
      auto dml_conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        const auto key = RandomUniformInt(1, kNumRows);
        const auto value = RandomHumanReadableString(16);
        const auto op = RandomUniformInt(0, 2);
        if (op == 0) {
          (void) dml_conn.ExecuteFormat(
              "INSERT INTO t1(id, v) VALUES ($0, '$1') "
              "ON CONFLICT (id) DO UPDATE SET v = EXCLUDED.v", key, value);
        } else if (op == 1) {
          (void) dml_conn.ExecuteFormat(
              "UPDATE t1 SET v = '$1' WHERE id = $0", key, value);
        } else {
          (void) dml_conn.ExecuteFormat("DELETE FROM t1 WHERE id = $0", key);
        }
      }
    });
  }

  SleepFor(MonoDelta::FromMilliseconds(kDmlDurationMs / 2));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  SleepFor(MonoDelta::FromMilliseconds(kDmlDurationMs / 2));

  thread_holder.Stop();

  auto job_id = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT job_id FROM pg_catalog.pg_yb_online_schema_change "
      "WHERE source_table_oid = 't1'::regclass ORDER BY job_id DESC LIMIT 1"));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  // The renamed old source is the ground truth.
  std::string old_table_name = Format("yb_osc_old_$0", job_id);
  std::map<int, std::string> expected;
  for (const auto& [id, v] : ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
           Format("SELECT id, v FROM $0", old_table_name))))) {
    expected[id] = v;
  }
  std::map<int, std::string> actual;
  for (const auto& [id, v] : ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
           "SELECT id, v FROM t1")))) {
    actual[id] = v;
  }
  ASSERT_EQ(actual, expected);

  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

// Tablets that hold more than one prefetch page (yb_fetch_row_limit, default 1024) of rows, so the
// copy must resume across pages via the backfill spec's next_row_key.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeMultiPageTabletRange) {
  constexpr int kNumRows = 3000;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT, v TEXT, v2 TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(count, kNumRows);
  const auto out_of_range = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM t1 WHERE id < 1 OR id > $0", kNumRows)));
  ASSERT_EQ(out_of_range, 0);
  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeMultiPageTabletHash) {
  // Several tablets: a single hash tablet spans the whole hash space, so any resume key falls in
  // its bounds, which would hide a scan that ignores the tablet's partition key.
  constexpr int kNumRows = 6000;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(count, kNumRows);
  const auto out_of_range = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM t1 WHERE id < 1 OR id > $0", kNumRows)));
  ASSERT_EQ(out_of_range, 0);
  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

// backfill_index_rate_rows_per_sec paces the online schema change backfill as it paces an index
// backfill. Only a lower bound on the elapsed time is asserted; the unthrottled copy takes a
// fraction of a second.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeRateLimited) {
  // More than one page, in one tablet, so a single copy does all the pacing.
  constexpr int kNumRows = 2000;
  constexpr int kRowsPerSec = 500;
  // The final chunk is not paced, so the floor comes from the chunks before it.
  constexpr auto kMinDuration = 1500ms;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = kRowsPerSec;
  auto se = ScopeExit(
      [] { ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0; });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT, v TEXT, v2 TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  const auto start = MonoTime::Now();
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  const auto elapsed = MonoTime::Now() - start;
  LOG(INFO) << "Throttled backfill of " << kNumRows << " rows at " << kRowsPerSec
            << " rows/s took " << elapsed;
  ASSERT_GE(elapsed, MonoDelta(kMinDuration));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(count, kNumRows);
  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

// online_schema_change_backfill_rows_per_chunk splits the copy of one tablet into several chunk
// RPCs, and the master persists the resume key, a raw DocDB key, between them. A key that does not
// round-trip exactly shows up as missing or duplicated rows, and as a rows_done that differs from
// the row count.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeMultiChunkTablet) {
  constexpr int kNumRows = 6000;
  // Not page-aligned, so a chunk boundary falls mid-page.
  constexpr int kRowsPerChunk = 700;

  const auto old_rows_per_chunk = FLAGS_online_schema_change_backfill_rows_per_chunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) = kRowsPerChunk;
  auto se = ScopeExit([old_rows_per_chunk] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) =
        old_rows_per_chunk;
  });

  auto conn = ASSERT_RESULT(Connect());
  // Range-sharded and single-tablet, so every chunk of the run resumes the same scan.
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT, v TEXT, v2 TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // Accumulated per chunk, so it catches a chunk counted twice or skipped.
  const auto rows_done = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT rows_done FROM pg_catalog.yb_online_schema_change ORDER BY job_id DESC LIMIT 1"));
  ASSERT_EQ(rows_done, kNumRows);

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto count = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1"));
  ASSERT_EQ(count, kNumRows);
  const auto out_of_range = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT count(*) FROM t1 WHERE id < 1 OR id > $0", kNumRows)));
  ASSERT_EQ(out_of_range, 0);
  const auto mismatched = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')"));
  ASSERT_EQ(mismatched, 0);
}

// A chunk whose response never reached the master is copied again over the same range, and that
// must not fail: the base-table write is blind, and a unique index entry rewritten with identical
// bytes passes the duplicate check. The bytes are identical because indexed transform columns
// must be immutable.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeChunkReplayIsIdempotent) {
  constexpr int kNumRows = 3000;
  // Not page-aligned, so the re-copied range is not either.
  constexpr int kRowsPerChunk = 700;
  constexpr uint32_t kReplaysPerTablet = 2;

  const auto old_rows_per_chunk = FLAGS_online_schema_change_backfill_rows_per_chunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) = kRowsPerChunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_replay_chunks_per_tablet) =
      kReplaysPerTablet;
  auto se = ScopeExit([old_rows_per_chunk] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) =
        old_rows_per_chunk;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_replay_chunks_per_tablet) =
        0;
  });

  auto conn = ASSERT_RESULT(Connect());
  // Range-sharded and single-tablet, so every chunk of the run resumes the same scan.
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));

  // A unique index, whose entries a replayed chunk rewrites.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT, v TEXT, u TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t2_u_idx ON t2 (u)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_v_idx ON t2 (v)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, 'u_' || src.id::text)::t2; END $$"));

  StringWaiterLogSink dropped_chunks("TEST: dropping the response of a chunk");

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // Make sure that a chunk was actually replayed.
  ASSERT_GE(dropped_chunks.GetEventCount(), kReplaysPerTablet);

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), kNumRows);
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(
          "SELECT count(*) FROM t2 WHERE v IS DISTINCT FROM ('v_' || id) "
          "OR u IS DISTINCT FROM ('u_' || id)")),
      0);

  // Through the unique index, every row is reached exactly once.
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2 WHERE u >= 'u_'")), kNumRows);
  for (int id : {1, kRowsPerChunk, kNumRows / 2, kNumRows}) {
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int32_t>(Format("SELECT id FROM t2 WHERE u = 'u_$0'", id))),
        id);
  }
  ASSERT_OK(conn.Execute("SET enable_seqscan = on"));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), kNumRows);
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(
          "SELECT count(*) FROM t1 WHERE u IS DISTINCT FROM ('u_' || id)")),
      0);
}

// An abort takes effect between chunks, because a running chunk RPC cannot be interrupted. With
// small chunks and a throttled copy, the copy must stop short of the full row count.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeAbortObservedMidCopy) {
  constexpr int kNumRows = 10000;
  constexpr int kRowsPerChunk = 1000;

  const auto old_rows_per_chunk = FLAGS_online_schema_change_backfill_rows_per_chunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) = kRowsPerChunk;
  // Throttle the copy so that it cannot finish before the abort lands.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 500;
  auto se = ScopeExit([old_rows_per_chunk] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) =
        old_rows_per_chunk;
  });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT, v TEXT, v2 TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  // Abort only once the copy has made progress.
  ASSERT_OK(WaitFor([&conn]() -> Result<bool> {
    return VERIFY_RESULT(conn.FetchRow<int64_t>(
        "SELECT rows_done FROM pg_catalog.yb_online_schema_change "
        "ORDER BY job_id DESC LIMIT 1")) > 0;
  }, 60s, "Wait for the copy to report progress"));

  ASSERT_OK(AbortOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "aborted");

  // Once the master has released the target, no chunk is left to write to it.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).empty(); }, 30s,
      "Wait for the aborted job to release its delete-marker retention"));
  const auto copied = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2"));
  ASSERT_GT(copied, 0);
  ASSERT_LT(copied, kNumRows);

  ASSERT_OK(conn.Execute("DROP TABLE t2 CASCADE"));
}

// The backfill pins delete-marker retention on the target and on each of its indexes while it runs,
// and releases the pins when it finishes. The trigger's blind deletes write tombstones into the
// indexes too, and a unique index insert reads at the backfill time, which fails with
// SnapshotTooOld without the pin.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeRetentionPinnedDuringBackfill) {
  constexpr int kNumRows = 2000;
  // Slow the copy down enough to observe the pin while it is held.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 500;
  auto se = ScopeExit(
      [] { ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0; });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT, v TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT, v TEXT, v2 TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t2_v_uniq ON t2 (v)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_v2_idx ON t2 (v2)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  // CREATE INDEX releases its own retention pins asynchronously; wait for that first.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).empty(); }, 30s,
      "Wait for CREATE INDEX to release delete-marker retention"));

  // The throttled copy spans several controller passes. A pass must not build a chunk task for
  // the tablet whose chunk is in flight and then destroy it unrun (a DFATAL in debug builds).
  StringWaiterLogSink dropped_task("task in a wrong state");

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  // The target and its two indexes.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).size() == 3; }, 30s,
      "Wait for the online schema change backfill to pin the target and its indexes"));

  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(dropped_task.GetEventCount(), 0);

  // Released once the copy has succeeded, not deferred to cutover.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).empty(); }, 30s,
      "Wait for the online schema change backfill to release its delete-marker retention pins"));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), kNumRows);
}

// The target's indexes serve reads as soon as the cutover commits. These two indexes are built
// without a backfill, so they have no birth time of their own, and a pinned index with no birth
// time has its reads retried until the pin is released.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeIndexesReadableRightAfterCutover) {
  constexpr int kNumRows = 100;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT UNIQUE, v2 TEXT)"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_v2_idx ON t2 (v2)"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  ASSERT_OK(conn.Execute("SET statement_timeout = '20s'"));
  for (const auto& [column, value, index] : {
           std::tuple{"v", "v_7", "t2_v_key"},
           std::tuple{"v2", "v_7_x", "t2_v2_idx"}}) {
    const auto query = Format("SELECT id FROM t1 WHERE $0 = '$1'", column, value);
    const auto plan = ASSERT_RESULT(conn.FetchAllAsString("EXPLAIN " + query));
    ASSERT_STR_CONTAINS(plan, index);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(query)), 7);
  }
}

// After an abort from "ready", the trigger is gone, the source is untouched, and the pair can be
// changed again.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeAbortAfterReady) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_OK(AbortOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "aborted");

  ASSERT_NOK_STR_CONTAINS(AbortOnlineSchemaChange(&conn, "t1", "t2"), "is aborted");

  const auto t2_before = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 'c')"));
  const auto t2_after = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2"));
  ASSERT_EQ(t2_after, t2_before);

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 3);

  ASSERT_OK(conn.Execute("TRUNCATE t2"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
}

// A rewrite gives a table a new relfilenode, and the DocDB table id comes from it, so the master
// must not be sent the OIDs. The source has had its column type changed, and the target has been
// emptied the way begin's hint for a non-empty target suggests.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeRewrittenTables) {
  constexpr int kNumRows = 100;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v VARCHAR(20))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) AS i", kNumRows));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ALTER COLUMN v TYPE TEXT USING v || ''"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 VALUES (0, 'stale', 'stale')"));
  ASSERT_OK(conn.Execute("TRUNCATE t2"));
  const auto num_rewritten = [&conn] {
    return conn.FetchRow<int64_t>(
        "SELECT count(*) FROM pg_class WHERE relname IN ('t1', 't2') AND relfilenode <> oid");
  };
  ASSERT_EQ(ASSERT_RESULT(num_rewritten()), 2);

  // Abort names the source to the master too.
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(AbortOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "aborted");

  ASSERT_OK(conn.Execute("TRUNCATE t2"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (0, 'v_0')"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "ready");
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 = v || '_x'")), kNumRows + 1);
}

// A copy that completes after the abort must not bring the job back.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeAbortDuringBackfill) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, 2000) AS i"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(AbortOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "aborted");

  // Releasing the target's retention is the last of the master's teardown.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).empty(); }, 30s,
      "Wait for the aborted job to release its delete-marker retention"));
  ASSERT_NOK_STR_CONTAINS(WaitForOnlineSchemaChange(&conn, "t1", "t2"), "is aborted");
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "aborted");
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeAbortNoJob) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));

  ASSERT_NOK_STR_CONTAINS(conn.Fetch("SELECT yb_abort_mirror_table(12345)"),
                          "online schema change job 12345 does not exist");
}

namespace {

// Creates t1 with two rows, t2, and the t1_to_t2 transform.
Status CreateOnlineSchemaChangePair(PGConn* conn) {
  RETURN_NOT_OK(conn->Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  RETURN_NOT_OK(conn->Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));
  RETURN_NOT_OK(conn->Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  return conn->Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$");
}

}  // namespace

class PgMiniOnlineSchemaChangeLegacyDdlTest : public PgMiniTestSingleNode {
 protected:
  void SetUp() override {
    ToggleDDLMode(/* use_legacy = */ true);
    PgMiniTestSingleNode::SetUp();
  }
};

// Without transactional DDL, every lifecycle step is refused.
TEST_F(PgMiniOnlineSchemaChangeLegacyDdlTest, OnlineSchemaChangeRequiresTransactionalDdl) {
  constexpr auto kError = "online schema change requires transactional DDL";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));

  ASSERT_NOK_STR_CONTAINS(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"), kError);
  ASSERT_NOK_STR_CONTAINS(conn.Fetch("SELECT yb_finish_mirror_table(1)"), kError);
  ASSERT_NOK_STR_CONTAINS(conn.Fetch("SELECT yb_abort_mirror_table(1)"), kError);
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("ALTER TABLE t1 CONCURRENTLY ADD COLUMN w INT DEFAULT 1"), kError);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change")), 0);
}

// yb_begin_mirror_table returns the job id, which the other lifecycle functions take.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeLifecycleByReturnedJobId) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));

  const auto job_id = ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT job_id FROM pg_catalog.pg_yb_online_schema_change ORDER BY job_id DESC LIMIT 1")),
      job_id) << "yb_begin_mirror_table did not return the job id";
  const auto how = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(Format(
      "SELECT command, cutover_mode FROM pg_catalog.yb_online_schema_change WHERE job_id = $0",
      job_id))));
  ASSERT_EQ(how, (decltype(how){"yb_begin_mirror_table", "rename"}));
  const auto recorded = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(Format(
      "SELECT origin, query FROM pg_catalog.pg_yb_online_schema_change WHERE job_id = $0",
      job_id))));
  ASSERT_EQ(recorded, (decltype(recorded){
      "manual",
      "SELECT yb_begin_mirror_table('t1'::regclass, 't2'::regclass, 't1_to_t2'::regproc)"}));
  ASSERT_RESULT(conn.FetchFormat("SELECT yb_wait_mirror_table($0)", job_id));
  ASSERT_RESULT(conn.FetchFormat("SELECT yb_finish_mirror_table($0)", job_id));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(Format(
      "SELECT status FROM pg_catalog.pg_yb_online_schema_change WHERE job_id = $0", job_id))),
      "finished");
  ASSERT_NOK_STR_CONTAINS(conn.FetchFormat("SELECT yb_abort_mirror_table($0)", job_id),
                          "is finished");
}

// The job records only the begin statement of a multi-statement query string.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeRecordsBeginStatement) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));

  const std::string begin_stmt = "SELECT yb_begin_mirror_table('t1', 't2', 't1_to_t2')";
  ASSERT_OK(conn.Execute(
      "SET client_min_messages = notice; " + begin_stmt + "; RESET client_min_messages"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT query FROM pg_catalog.pg_yb_online_schema_change "
      "WHERE source_table_oid = 't1'::regclass")),
      begin_stmt);
  const auto job_id = ASSERT_RESULT(OnlineSchemaChangeJobId(&conn, "t1", "t2"));
  ASSERT_RESULT(conn.FetchFormat("SELECT yb_wait_mirror_table($0)", job_id));
  ASSERT_RESULT(conn.FetchFormat("SELECT yb_finish_mirror_table($0)", job_id));
}

// A copy that has finished survives a master restart: the new leader still reports it as done,
// and the cutover goes through.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeCutoverAfterMasterRestart) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_OK(cluster_->mini_master(0)->Restart());
  ASSERT_OK(cluster_->mini_master(0)->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(FinishOnlineSchemaChange(&conn2, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn2)), "finished");
}

// A cutover whose commit fails leaves the job "ready", and can be retried.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeCutoverRetriedAfterCommitFails) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_OK(conn.Execute("SET yb_test_fail_next_ddl = 1"));
  ASSERT_NOK_STR_CONTAINS(
      FinishOnlineSchemaChange(&conn, "t1", "t2"), "Failed DDL operation as requested");
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "ready");

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("SELECT v2 FROM t1 WHERE id = 1")), "a_x");
}

// The table swapped in gets the source's foreign key, by name and validated, and the old source
// keeps none.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeForeignKeyCarriedOver) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE parent (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO parent VALUES (1), (2), (3)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, pid INT REFERENCES parent(id), v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 1, 'a'), (2, 2, 'b')"));

  const auto conname = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT conname FROM pg_constraint WHERE conrelid = 't1'::regclass AND contype = 'f'"));

  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, pid INT, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.pid, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  // During the copy, only the source has the foreign key.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_constraint "
      "WHERE conrelid = 't2'::regclass AND contype = 'f'")), 0);
  ASSERT_NOK(conn.Execute("INSERT INTO t1 VALUES (9, 99, 'bad')"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 3, 'c')"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // The wait has added the foreign key to the target, under its name, and validated it.
  const auto promoted = ASSERT_RESULT((conn.FetchRow<std::string, bool>(Format(
      "SELECT conname, convalidated FROM pg_constraint "
      "WHERE conrelid = 't2'::regclass AND contype = 'f' AND conname = '$0'", conname))));
  ASSERT_EQ(promoted, (decltype(promoted){conname, true}));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 3);

  // t1 is now the former t2, and its foreign key is live.
  ASSERT_NOK(conn.Execute("INSERT INTO t1 VALUES (10, 99, 'bad', 'bad_x')"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (10, 1, 'ok', 'ok_x')"));

  // The old source has no foreign keys left.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_constraint c JOIN pg_class r ON r.oid = c.conrelid "
      "WHERE r.relname LIKE 'yb_osc_old_%' AND c.contype = 'f'")), 0);
}

// A cascading delete on the source during the copy reaches the target through the trigger, so the
// copy cannot bring back a row that the cascade deleted.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeForeignKeyCascadeDuringBackfill) {
  constexpr int kNumRows = 200;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE parent (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO parent SELECT i FROM generate_series(1, $0) AS i", kNumRows));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, "
      "pid INT REFERENCES parent(id) ON DELETE CASCADE, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, i, 'val_' || i FROM generate_series(1, $0) AS i", kNumRows));

  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, pid INT, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.pid, src.v)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  // Cascade away a third of the rows while the copy is in flight.
  ASSERT_OK(conn.Execute("DELETE FROM parent WHERE id % 3 = 0"));

  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // No difference in either direction.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM ("
      "(SELECT id, pid, v FROM t1 EXCEPT SELECT id, pid, v FROM t2) UNION ALL "
      "(SELECT id, pid, v FROM t2 EXCEPT SELECT id, pid, v FROM t1)) d")), 0);
  ASSERT_GT(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 0);

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeCutoverRequiresPromotedForeignKeys) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE parent (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO parent VALUES (1)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, pid INT REFERENCES parent(id))"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, pid INT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.pid)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // Finish refuses a target that lost one of the source's foreign keys.
  ASSERT_OK(conn.Execute("ALTER TABLE t2 DROP CONSTRAINT t1_pid_fkey"));
  auto status = FinishOnlineSchemaChange(&conn, "t1", "t2");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "is missing foreign key");

  // The wait, which can be repeated, adds it back.
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeForeignKeyValidation) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE parent (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, pid INT REFERENCES parent(id))"));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, pid INT REFERENCES parent(id))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.pid)::t2; END $$"));
  auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must not have foreign keys");

  ASSERT_OK(conn.Execute("DROP FUNCTION t1_to_t2(t1)"));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, other TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, 'x'::text)::t2; END $$"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "missing foreign-key column");

  ASSERT_OK(conn.Execute("DROP FUNCTION t1_to_t2(t1)"));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, pid INT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.pid)::t2; END $$"));
  // The foreign key would be added to the target under the name a CHECK already has.
  ASSERT_OK(conn.Execute("ALTER TABLE t2 ADD CONSTRAINT t1_pid_fkey CHECK (pid > 0)"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "already has a constraint named \"t1_pid_fkey\"");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_catalog.pg_yb_online_schema_change")), 0);
  ASSERT_OK(conn.Execute("ALTER TABLE t2 DROP CONSTRAINT t1_pid_fkey"));

  ASSERT_OK(conn.Execute("CREATE TABLE child (id INT PRIMARY KEY, t1_id INT REFERENCES t1(id))"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must not be referenced by foreign keys");

  ASSERT_OK(conn.Execute("DROP TABLE child"));
  ASSERT_OK(conn.Execute(
      "ALTER TABLE t1 ADD FOREIGN KEY (pid) REFERENCES t1(id) "
      "ON UPDATE CASCADE ON DELETE CASCADE"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must not be referenced by foreign keys");
}

// The target's primary key adds a derived column to the source's. Distinct ids already keep the
// rows apart, so the pair is accepted.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeKeyWitnessTargetPkSuperset) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, 50) AS i"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT, part INT, v TEXT, PRIMARY KEY (id, part))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.id % 4, src.v)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (51, 'v_51')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'updated' WHERE id = 2"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 3"));

  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 50);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1 WHERE id = 3")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("SELECT v FROM t1 WHERE id = 2")),
            "updated");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE part IS DISTINCT FROM id % 4")), 0);
}

// The source's key is (a, b) and the target's is (a), which OnlineSchemaChangeKeyWitnessRejected
// refuses, but a unique index on the source's a is a witness.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeKeyWitnessSecondaryUniqueIndex) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (a INT NOT NULL, b INT NOT NULL, v TEXT, PRIMARY KEY (a, b))"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t1_a_uniq ON t1 (a)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, i * 10, 'v_' || i FROM generate_series(1, 30) AS i"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.a, src.v)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (31, 310, 'v_31')"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE a = 4"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 30);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1 WHERE a = 4")), 0);
}

// A secondary-index witness, unlike a YB primary key, can be dropped while the job runs, so the
// cutover checks the witness again and refuses the swap without it.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeKeyWitnessRecheckedAtCutover) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (a INT NOT NULL, b INT NOT NULL, v TEXT, PRIMARY KEY (a, b))"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t1_a_uniq ON t1 (a)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, i * 10, 'v_' || i FROM generate_series(1, 10) AS i"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.a, src.v)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_OK(conn.Execute("DROP INDEX t1_a_uniq"));
  ASSERT_NOK_STR_CONTAINS(
      FinishOnlineSchemaChange(&conn, "t1", "t2"), "no unique index on source table \"t1\"");
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "ready");

  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t1_a_uniq ON t1 (a)"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 10);
}

// The witness need not list its columns in the order of the target's primary key.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeKeyWitnessColumnOrder) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (a INT NOT NULL, b INT NOT NULL, v TEXT, PRIMARY KEY (a, b))"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, i * 10, 'v_' || i FROM generate_series(1, 20) AS i"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (a INT NOT NULL, b INT NOT NULL, v TEXT, PRIMARY KEY (b, a))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.a, src.b, src.v)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'updated' WHERE a = 1"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 20);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("SELECT v FROM t1 WHERE a = 1")),
            "updated");
}

// A dropped target column leaves the primary key at an attnum that differs from its position among
// the live columns.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeKeyWitnessDroppedTargetColumn) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, 20) AS i"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (junk INT, id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 DROP COLUMN junk"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (21, 'v_21')"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 21);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM v || '_x'")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeGeneratedColumnsRejected) {
  auto conn = ASSERT_RESULT(Connect());

  auto status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)",
      "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, "
      "v2 TEXT GENERATED ALWAYS AS (v || '_x') STORED)",
      "src.id, src.v, src.v || '_x'");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must not have generated columns");
  ASSERT_STR_CONTAINS(status.ToString(), "\"v2\"");

  // The copy and the trigger never advance a sequence the target owns, so after cutover its next
  // value would repeat a copied key.
  for (const auto* identity : {"ALWAYS", "BY DEFAULT"}) {
    status = BeginOnlineSchemaChangeOverShapes(
        &conn,
        "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)",
        Format("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, n INT GENERATED $0 AS IDENTITY)",
               identity),
        "src.id, src.v, 1");
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.ToString(), "must not have identity columns");
    ASSERT_STR_CONTAINS(status.ToString(), "\"n\"");
  }

  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)",
      "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, n SERIAL)",
      "src.id, src.v, 1");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must not own a sequence");
  ASSERT_STR_CONTAINS(status.ToString(), "\"t2_n_seq\"");

  // A sequence the target does not own is accepted.
  ASSERT_OK(conn.Execute("CREATE SEQUENCE shared_seq"));
  ASSERT_OK(BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)",
      "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, n INT DEFAULT nextval('shared_seq'))",
      "src.id, src.v, 1"));
  // A copy still running at teardown holds up tserver shutdown for two minutes.
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
}

// Only the tserver's copy connection may call the chunk function, which runs a caller-chosen
// transform as the source table's owner.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeChunkFunctionIsTserverOnly) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr auto kCall =
      "SELECT * FROM pg_catalog.yb_backfill_online_schema_change_chunk(0, 0, 0, 0, NULL, '\\x00')";

  auto status = conn.Execute(kCall);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "can only be called by the tserver");

  ASSERT_OK(conn.Execute("CREATE ROLE admin"));
  ASSERT_OK(conn.Execute("GRANT yb_db_admin TO admin"));
  ASSERT_OK(conn.Execute("SET ROLE admin"));
  status = conn.Execute(kCall);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "permission denied for function");
}

// The copy writes GIN and vector index entries as backfill writes, like the base-table rows; a
// transactional index write would share a write batch with them, which is not allowed.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeGinAndVectorIndexes) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT, e vector(3)) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, 'v_' || i, format('[%s,%s,%s]', i, i, i)::vector "
      "FROM generate_series(1, 100) AS i"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, tags TEXT[], e vector(3))"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_tags ON t2 USING ybgin (tags)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_e ON t2 USING ybhnsw (e vector_l2_ops)"));
  // The transform may call only built-in functions, so it passes the vector through.
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, ARRAY[src.v], src.e)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (101, 'v_101', '[101,101,101]')"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE tags = ARRAY[v] AND e IS NOT NULL")), 101);
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(
      "SELECT id FROM t1 ORDER BY e <-> '[42,42,42]' LIMIT 1")), 42);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(
      "SELECT id FROM t1 ORDER BY e <-> '[101,101,101]' LIMIT 1")), 101);
}

// The cutover rename would leave the archived table in the inheritance hierarchy.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeInheritanceRejected) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr auto kSrcDdl = "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)";
  constexpr auto kDstDdl = "CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)";
  constexpr auto kBody = "src.id, src.v, src.v || '_x'";

  auto status = BeginOnlineSchemaChangeOverShapes(
      &conn, kSrcDdl, kDstDdl, kBody,
      {"CREATE TABLE t1_child (c INT, PRIMARY KEY (id)) INHERITS (t1)"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "source table \"t1\" has inheritance children");

  status = BeginOnlineSchemaChangeOverShapes(
      &conn, kSrcDdl, kDstDdl, kBody, {}, {"CREATE TABLE t2_child () INHERITS (t2)"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "target table \"t2\" has inheritance children");

  ASSERT_OK(conn.Execute("CREATE TABLE base (v TEXT)"));
  status = BeginOnlineSchemaChangeOverShapes(
      &conn, "CREATE TABLE t1 (id INT PRIMARY KEY) INHERITS (base)", kDstDdl, kBody);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "source table \"t1\" inherits from another table");

  // Inherited columns come first, so t2 is (v, id, v2).
  status = BeginOnlineSchemaChangeOverShapes(
      &conn, kSrcDdl, "CREATE TABLE t2 (id INT PRIMARY KEY, v2 TEXT) INHERITS (base)",
      "src.v, src.id, src.v || '_x'");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "target table \"t2\" inherits from another table");

  ASSERT_OK(BeginOnlineSchemaChangeOverShapes(&conn, kSrcDdl, kDstDdl, kBody));
  // A copy still running at teardown holds up tserver shutdown for two minutes.
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
}

// The copy only writes, so a target row without a source counterpart would survive the cutover.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTargetMustBeEmpty) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT g, 'good' || g FROM generate_series(1, 10) g"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 VALUES (999, 'stale', 'stale')"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "target table \"t2\" is not empty");

  ASSERT_OK(conn.Execute("TRUNCATE t2"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 10);
}

// Pairs without a key witness are refused.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeKeyWitnessRejected) {
  auto conn = ASSERT_RESULT(Connect());

  // The target's key is a prefix of the source's, so (1, 10) and (1, 20) would share target key 1.
  auto status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a INT NOT NULL, b INT NOT NULL, v TEXT, PRIMARY KEY (a, b))",
      "CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)",
      "src.a, src.v");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "no unique index on source table \"t1\"");
  ASSERT_STR_CONTAINS(status.ToString(), "is not part of the target's primary key");
  ASSERT_STR_CONTAINS(status.ToString(), "CREATE UNIQUE INDEX ON public.t1 (a)");

  // A partial unique index says nothing about the rows it excludes.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a INT NOT NULL, b INT NOT NULL, del BOOL NOT NULL DEFAULT false, "
      "v TEXT, PRIMARY KEY (a, b))",
      "CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)",
      "src.a, src.v",
      {"CREATE UNIQUE INDEX t1_a_live ON t1 (a) WHERE NOT del"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "\"t1_a_live\" is partial");

  // An expression index has no column to pair with a target key column.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a INT NOT NULL, b INT NOT NULL, v TEXT, PRIMARY KEY (a, b))",
      "CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)",
      "src.a, src.v",
      {"CREATE UNIQUE INDEX t1_abs_a ON t1 (abs(a))"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "\"t1_abs_a\" is on an expression");

  // A unique index allows any number of NULLs. Such rows fail on the target's NOT NULL key, but
  // only once the copy or the trigger reaches them.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (id INT PRIMARY KEY, a INT, v TEXT)",
      "CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)",
      "src.a, src.v",
      {"CREATE UNIQUE INDEX t1_a_uniq ON t1 (a)"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "column \"a\" of unique index \"t1_a_uniq\" is nullable");

  // A narrower typmod can map distinct source values to one target value.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a VARCHAR(64) PRIMARY KEY, v TEXT)",
      "CREATE TABLE t2 (a VARCHAR(8) PRIMARY KEY, v TEXT)",
      "src.a::varchar(8), src.v");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "does not have the same type, length or collation");

  // So can a narrower type.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a BIGINT PRIMARY KEY, v TEXT)",
      "CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)",
      "src.a::int, src.v");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "does not have the same type, length or collation");

  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a INT NOT NULL, v TEXT)",
      "CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)",
      "src.a, src.v");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "table \"t1\" must have a primary key");

  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a INT PRIMARY KEY, v TEXT)",
      "CREATE TABLE t2 (a INT NOT NULL, v TEXT)",
      "src.a, src.v");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "table \"t2\" must have a primary key");

  // No target key column exists on the source, so there is no index to suggest.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn,
      "CREATE TABLE t1 (a INT PRIMARY KEY, v TEXT)",
      "CREATE TABLE t2 (k INT PRIMARY KEY, v TEXT)",
      "src.a, src.v");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "names no column of the source");
}

// An invalid unique index is not a witness. No supported path leaves one behind, so the test sets
// indisvalid directly.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeKeyWitnessInvalidIndex) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, a INT NOT NULL, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t1_a_uniq ON t1 (a)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.a, src.v)::t2; END $$"));

  ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = true"));
  ASSERT_OK(conn.Execute(
      "UPDATE pg_index SET indisvalid = false WHERE indexrelid = 't1_a_uniq'::regclass"));
  ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = false"));
  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>(
      "SELECT indisvalid FROM pg_index WHERE indexrelid = 't1_a_uniq'::regclass")));

  const auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "\"t1_a_uniq\" is not valid");
}

namespace {

// The source's key is (b, a), so the copy runs in b order, and the target's is (a). Nothing on the
// source shows that a is unique, so only a target index that includes b can be the key witness.
constexpr auto kTargetWitnessSrcDdl =
    "CREATE TABLE t1 (a INT NOT NULL, b INT NOT NULL, v TEXT, PRIMARY KEY (b ASC, a ASC))";
constexpr auto kTargetWitnessDstDdl = "CREATE TABLE t2 (a INT PRIMARY KEY, b INT, v TEXT)";
constexpr auto kTargetWitnessIndexDdl = "CREATE UNIQUE INDEX t2_a_b ON t2 (a) INCLUDE (b)";

}  // namespace

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTargetKeyWitness) {
  auto conn = ASSERT_RESULT(Connect());
  const std::vector<std::string> seed = {
      "INSERT INTO t1 SELECT i, i * 10, 'v_' || i FROM generate_series(1, 30) AS i"};

  auto status = BeginOnlineSchemaChangeOverShapes(
      &conn, kTargetWitnessSrcDdl, kTargetWitnessDstDdl, "src.a, src.b, src.v", seed);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "Or add one on the target with: "
                      "CREATE UNIQUE INDEX ON public.t2 (a) INCLUDE (b)");

  ASSERT_OK(BeginOnlineSchemaChangeOverShapes(
      &conn, kTargetWitnessSrcDdl, kTargetWitnessDstDdl, "src.a, src.b, src.v", seed,
      {kTargetWitnessIndexDdl}));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (31, 310, 'v_31')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'updated' WHERE a = 2"));
  // A new source key on the same target key: the trigger's tombstone comes first, so this is not
  // a collision with the copy's older entry for a = 3.
  ASSERT_OK(conn.Execute("UPDATE t1 SET b = 41 WHERE a = 3"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE a = 4"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  constexpr auto kRows = "SELECT a, b, v FROM t1 ORDER BY a";
  const auto expected = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t, std::string>(kRows)));
  ASSERT_EQ(expected.size(), 30);
  ASSERT_EQ(expected[1], std::make_tuple(2, 20, std::string("updated")));
  ASSERT_EQ(expected[2], std::make_tuple(3, 41, std::string("v_3")));
  ASSERT_EQ(std::get<0>(expected[3]), 5);
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT((conn.FetchRows<int32_t, int32_t, std::string>(kRows))), expected);
}

// Two source rows that reach one target key must fail the job, or the statement that creates the
// second one, instead of collapsing into one target row.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTargetKeyWitnessCollision) {
  // Enough rows for several 20-row chunks between b = 1 and b = 500, and few enough that the copy
  // throttled to 50 rows/s below takes about a second.
  constexpr int kSeedRows = 51;
  auto retries = FewerChunkRetries();
  const auto old_rows_per_chunk = FLAGS_online_schema_change_backfill_rows_per_chunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) = 20;
  auto se = ScopeExit([old_rows_per_chunk] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) =
        old_rows_per_chunk;
  });
  auto conn = ASSERT_RESULT(Connect());
  const auto begin = [&conn](const std::string& seed, const std::string& transform_exprs) {
    return BeginOnlineSchemaChangeOverShapes(
        &conn, kTargetWitnessSrcDdl, kTargetWitnessDstDdl, transform_exprs,
        {Format("INSERT INTO t1 SELECT i, i, 'v_' || i FROM generate_series(10, $0) AS i",
                10 + kSeedRows - 1),
         seed},
        {kTargetWitnessIndexDdl});
  };
  const auto expect_copy_fails = [&conn]() -> Status {
    auto status = WaitForOnlineSchemaChange(&conn, "t1", "t2");
    SCHECK(!status.ok(), IllegalState, "the copy kept one of two colliding rows");
    SCHECK_STR_CONTAINS(status.ToString(), "failed");
    SCHECK(!FinishOnlineSchemaChange(&conn, "t1", "t2").ok(), IllegalState,
           "finish accepted a failed job");
    return AbortOnlineSchemaChange(&conn, "t1", "t2");
  };

  // Both rows are copied, in one chunk and then many chunks apart.
  for (const auto* seed : {"INSERT INTO t1 VALUES (1, 1, 'x'), (1, 2, 'y')",
                           "INSERT INTO t1 VALUES (1, 1, 'x'), (1, 500, 'y')"}) {
    SCOPED_TRACE(seed);
    ASSERT_OK(begin(seed, "src.a, src.b, src.v"));
    ASSERT_OK(expect_copy_fails());
  }

  // The trigger writes the second row before the throttled copy reaches the first.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 50;
  ASSERT_OK(begin("INSERT INTO t1 VALUES (1, 500, 'x')", "src.a, src.b, src.v"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 600, 'y')"));
  ASSERT_OK(expect_copy_fails());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0;

  // After the copy, the trigger's insert fails on the target's primary key.
  ASSERT_OK(begin("INSERT INTO t1 VALUES (1, 1, 'x')", "src.a, src.b, src.v"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_NOK_STR_CONTAINS(conn.Execute("INSERT INTO t1 VALUES (1, 2, 'y')"), "duplicate key");
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), kSeedRows + 1);

  // The witness only separates the rows while the transform copies b unchanged.
  ASSERT_NOK_STR_CONTAINS(
      begin("INSERT INTO t1 VALUES (1, 1, 'x'), (1, 2, 'y')", "src.a, 0, src.v"),
      "must pass key column \"b\" through unchanged");
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTargetKeyWitnessRejected) {
  auto conn = ASSERT_RESULT(Connect());

  // A key without b says nothing about the rows that differ only in b.
  auto status = BeginOnlineSchemaChangeOverShapes(
      &conn, kTargetWitnessSrcDdl, kTargetWitnessDstDdl, "src.a, src.b, src.v", {},
      {"CREATE UNIQUE INDEX t2_a ON t2 (a)"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "unique index \"t2_a\" does not include source primary key column \"b\"");

  // Rows that share a target key can differ in b, and so in this index's key.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn, kTargetWitnessSrcDdl, kTargetWitnessDstDdl, "src.a, src.b, src.v", {},
      {"CREATE UNIQUE INDEX t2_a_b ON t2 (a, b)"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "column \"b\" of unique index \"t2_a_b\" is not part of the target's "
                      "primary key");

  status = BeginOnlineSchemaChangeOverShapes(
      &conn, kTargetWitnessSrcDdl, kTargetWitnessDstDdl, "src.a, src.b, src.v", {},
      {"CREATE UNIQUE INDEX t2_a_b ON t2 (a) INCLUDE (b) WHERE v IS NOT NULL"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "unique index \"t2_a_b\" is partial");

  // The target cannot hold the source's b unchanged, so no target index can be a witness.
  status = BeginOnlineSchemaChangeOverShapes(
      &conn, kTargetWitnessSrcDdl, "CREATE TABLE t2 (a INT PRIMARY KEY, b BIGINT, v TEXT)",
      "src.a, src.b, src.v", {}, {"CREATE UNIQUE INDEX t2_a_b ON t2 (a) INCLUDE (b)"});
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "source primary key column \"b\" does not have the same type, length or "
                      "collation on the target");

  status = BeginOnlineSchemaChangeOverShapes(
      &conn, kTargetWitnessSrcDdl, "CREATE TABLE t2 (a INT PRIMARY KEY, v TEXT)",
      "src.a, src.v");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "source primary key column \"b\" is not a column of the target");
  ASSERT_STR_NOT_CONTAINS(status.ToString(), "Or add one on the target");
}

// The transform must be an IMMUTABLE PL/pgSQL function that sets its own search_path.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTransformMustBeDeterministic) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT)"));

  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION sqlfn(src t1) RETURNS t2 LANGUAGE SQL IMMUTABLE AS $$ "
      "SELECT src.id, src.v $$"));
  auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "sqlfn"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must be a PL/pgSQL function");

  // STABLE is not enough: the copy and the trigger call the transform in different statements.
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION stbl(src t1) RETURNS t2 LANGUAGE plpgsql STABLE "
      "SET search_path FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v)::t2; END $$"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "stbl"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must be IMMUTABLE");

  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION nopath(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE AS $$ "
      "BEGIN RETURN ROW(src.id, src.v)::t2; END $$"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "nopath"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must set search_path");

  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION good(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
      "SET search_path = public AS $$ BEGIN RETURN ROW(src.id, src.v)::t2; END $$"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "good"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
}

// The transform is opaque, so a volatile one is refused even when only a column the online schema
// change never recomputes, such as one filled by gen_random_uuid(), uses the volatile function.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeVolatileTransform) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, u UUID)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql VOLATILE "
      "SET search_path FROM CURRENT AS $$ "
      "BEGIN RETURN ROW(src.id, src.v, gen_random_uuid())::t2; END $$"));
  const auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must be IMMUTABLE");
}

// Begin checks the transform's body once, so the body must have a shape it can check: a single
// RETURN of ROW(...)::<target>.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTransformBodyShape) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (id INT PRIMARY KEY, v TEXT)"));

  const std::vector<std::pair<std::string, std::string>> kRefused = {
      {"DECLARE x TEXT := 'a'; BEGIN RETURN ROW(src.id, x)::t2; END",
       "must be a single RETURN statement"},
      {"BEGIN RETURN ROW(src.id, src.v)::t2; EXCEPTION WHEN others THEN RETURN NULL; END",
       "must be a single RETURN statement"},
      {"BEGIN IF src.id > 0 THEN RETURN ROW(src.id, 'p')::t2; END IF; "
       "RETURN ROW(src.id, src.v)::t2; END",
       "must be a single RETURN statement"},
      {"BEGIN RETURN ROW(src.id, src.v); END", "must return ROW(...)::t2"},
      {"BEGIN RETURN ROW(src.id, x.v)::t2 FROM t3 x WHERE x.id = src.id; END",
       "with no FROM, WHERE or other SELECT clause"},
      {"BEGIN RETURN ROW(src.id, FOUND::text)::t2; END",
       "refers to a variable other than its argument"},
  };
  for (const auto& [body, error] : kRefused) {
    ASSERT_OK(conn.Execute("DROP FUNCTION IF EXISTS xf"));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE FUNCTION xf(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
        "SET search_path FROM CURRENT AS $$$$ $0 $$$$", body));
    ASSERT_NOK_STR_CONTAINS(ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "xf")),
                            error);
  }
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change")), 0);
}

// Each column's expression is checked when the job begins, and the error names the column.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTransformColumnRules) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'v' || i FROM generate_series(1, 10) AS i"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, w TEXT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX ON t2 (w)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE SEQUENCE s"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION myupper(t TEXT) RETURNS TEXT LANGUAGE sql IMMUTABLE AS "
      "$$ SELECT upper(t) $$"));

  const std::vector<std::pair<std::string, std::string>> kRefused = {
      // The witness proves the source keys distinct, not the transform's output.
      {"ROW(src.id + 1, src.v, src.v)", "must pass key column \"id\" through unchanged"},
      {"ROW(0, src.v, src.v)", "must pass key column \"id\" through unchanged"},
      {"ROW(src.id, (SELECT x.v FROM t3 x WHERE x.id = src.id), src.v)",
       "computes column \"v\" with a subquery"},
      {"ROW(src.id, myupper(src.v), src.v)", "computes column \"v\" with a user-defined function"},
      {"ROW(src.id, nextval('s')::text, src.v)", "computes column \"v\" with a sequence function"},
      {"ROW(src.id, src.v || now(), src.v)", "computes column \"v\" with a stable expression"},
      {"ROW(src.id, src.v, gen_random_uuid()::text)",
       "computes indexed column \"w\" with a volatile expression"},
  };
  for (const auto& [row, error] : kRefused) {
    ASSERT_OK(conn.Execute("DROP FUNCTION IF EXISTS xf"));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE FUNCTION xf(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
        "SET search_path FROM CURRENT AS $$$$ BEGIN RETURN $0::t2; END $$$$", row));
    ASSERT_NOK_STR_CONTAINS(ResultToStatus(BeginOnlineSchemaChange(&conn, "t1", "t2", "xf")),
                            error);
  }
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change")), 0);
  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("SELECT is_called FROM s")));

  // Built-in functions, CASE and a volatile expression for a column no index covers are fine.
  ASSERT_OK(conn.Execute("DROP INDEX t2_w_idx"));
  ASSERT_OK(conn.Execute("DROP FUNCTION xf"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION xf(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
      "SET search_path FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, "
      "CASE WHEN src.id % 2 = 0 THEN upper(src.v) ELSE src.v END, "
      "gen_random_uuid()::text)::t2; END $$"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "xf"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (12, 'v12')"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v = CASE WHEN id % 2 = 0 THEN upper('v' || id) "
      "ELSE 'v' || id END AND w IS NOT NULL")), 11);
}

// The job relies on the transform it checked at begin, so the transform cannot be replaced,
// altered, renamed, moved or dropped until the job ends.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTransformFrozenDuringJob) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE SCHEMA other"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
      "SET search_path FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v)::t2; END $$"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // The new body would land row 2 on target key 3.
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute(
          "CREATE OR REPLACE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
          "SET search_path FROM CURRENT AS $$ BEGIN RETURN ROW(src.id + 1, src.v)::t2; END $$"),
      "cannot replace function t1_to_t2(t1) while it is the transform of online schema change "
      "job");
  ASSERT_NOK_STR_CONTAINS(conn.Execute("ALTER FUNCTION t1_to_t2 VOLATILE"),
                          "cannot alter function t1_to_t2(t1)");
  ASSERT_NOK_STR_CONTAINS(conn.Execute("ALTER FUNCTION t1_to_t2 SET search_path = other"),
                          "cannot alter function t1_to_t2(t1)");
  ASSERT_NOK_STR_CONTAINS(conn.Execute("ALTER FUNCTION t1_to_t2 RENAME TO xf"),
                          "cannot rename function t1_to_t2(t1)");
  ASSERT_NOK_STR_CONTAINS(conn.Execute("ALTER FUNCTION t1_to_t2 SET SCHEMA other"),
                          "cannot set the schema of function t1_to_t2(t1)");
  ASSERT_NOK_STR_CONTAINS(conn.Execute("DROP FUNCTION t1_to_t2"),
                          "cannot drop function t1_to_t2(t1)");
  ASSERT_NOK_STR_CONTAINS(conn.Execute("DROP FUNCTION t1_to_t2 CASCADE"),
                          "cannot drop function t1_to_t2(t1)");

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (2, 'b')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2 WHERE id = 2")), 1);

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 VOLATILE"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 RENAME TO xf"));
  ASSERT_OK(conn.Execute("DROP FUNCTION xf"));
}

// The capture triggers are internal, so pg_dump leaves them out and DROP TRIGGER is refused, and
// a backend that cached the source's relcache entry before begin still captures. Abort finds them
// by function, so it also drops a renamed one.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeCaptureTriggersAreInternal) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE "
      "SET search_path FROM CURRENT AS $$ "
      "BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  auto other = ASSERT_RESULT(Connect());
  ASSERT_OK(other.Execute("INSERT INTO t1 VALUES (1, 'a')"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_trigger WHERE tgrelid = 't1'::regclass AND tgisinternal")), 3);
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));

  // Only the trigger can deliver a row written after the copy.
  ASSERT_OK(other.Execute("INSERT INTO t1 VALUES (2, 'b')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2 WHERE id = 2")), 1);

  const auto trigger = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT tgname::text FROM pg_trigger WHERE tgrelid = 't1'::regclass ORDER BY tgname "
      "LIMIT 1"));
  ASSERT_NOK_STR_CONTAINS(conn.ExecuteFormat("DROP TRIGGER $0 ON t1", trigger),
                          "because table t1 requires it");
  ASSERT_OK(conn.ExecuteFormat("ALTER TRIGGER $0 ON t1 RENAME TO renamed", trigger));

  ASSERT_OK(AbortOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_trigger WHERE tgrelid = 't1'::regclass")), 0);
}

// The catalog writes and trigger DDL that the lifecycle runs as superuser do not resolve names
// through the caller's search_path.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeSearchPathIsPinned) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_user"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("GRANT CREATE ON SCHEMA public TO osc_user"));
  ASSERT_OK(conn.Execute("CREATE SCHEMA evil AUTHORIZATION osc_user"));

  auto admin_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(admin_conn.Execute("SET ROLE osc_user"));
  ASSERT_OK(admin_conn.Execute("CREATE TABLE evil.calls (who TEXT)"));
  ASSERT_OK(admin_conn.Execute(
      "CREATE FUNCTION evil.int8eq(int8, int8) RETURNS bool LANGUAGE plpgsql AS $$ "
      "BEGIN INSERT INTO evil.calls VALUES (current_user); "
      "RETURN $1 OPERATOR(pg_catalog.=) $2; END $$"));
  ASSERT_OK(admin_conn.Execute(
      "CREATE OPERATOR evil.= (LEFTARG = int8, RIGHTARG = int8, FUNCTION = evil.int8eq)"));
  ASSERT_OK(admin_conn.Execute("SET search_path = evil, pg_catalog, public"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&admin_conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&admin_conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&admin_conn, "t1", "t2"));

  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM evil.calls")), 0);
}

namespace {

Status RunOnlineSchemaChangeLifecycleAs(PGConn* conn, PGConn* role_conn) {
  RETURN_NOT_OK(BeginOnlineSchemaChange(role_conn, "t1", "t2", "t1_to_t2"));
  RETURN_NOT_OK(WaitForOnlineSchemaChange(role_conn, "t1", "t2"));
  RETURN_NOT_OK(role_conn->Execute("INSERT INTO t1 VALUES (3, 'c')"));
  RETURN_NOT_OK(FinishOnlineSchemaChange(role_conn, "t1", "t2"));

  SCHECK_EQ(VERIFY_RESULT(LatestOnlineSchemaChangeJobStatus(conn)), "finished", IllegalState,
            "job status");
  SCHECK_EQ(VERIFY_RESULT(conn->FetchRow<int64_t>("SELECT count(*) FROM t1")), 3, IllegalState,
            "row count");
  SCHECK_EQ(VERIFY_RESULT(conn->FetchRow<int64_t>(
      "SELECT count(*) FROM t1 WHERE v2 IS DISTINCT FROM (v || '_x')")), 0, IllegalState,
      "rows not transformed");
  return Status::OK();
}

}  // namespace

// A role that owns both tables and the transform, and may rename tables in their schema, runs the
// whole lifecycle with no other privilege.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTableOwnerLifecycle) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_user"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("GRANT CREATE ON SCHEMA public TO osc_user"));

  auto user_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(user_conn.Execute("SET ROLE osc_user"));
  ASSERT_OK(RunOnlineSchemaChangeLifecycleAs(&conn, &user_conn));
}

// A yb_db_admin member may ALTER any table, so it can change tables it does not own. The copy
// runs as the transform's owner, the caller here, not as the tables' superuser owner.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeYbDbAdminLifecycle) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_admin"));
  ASSERT_OK(conn.Execute("GRANT yb_db_admin TO osc_admin"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO osc_admin"));

  auto admin_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(admin_conn.Execute("SET ROLE osc_admin"));
  ASSERT_OK(RunOnlineSchemaChangeLifecycleAs(&conn, &admin_conn));
}

// session_replication_role = replica skips triggers that fire on origin, and a yb_db_admin member
// may set it. The capture triggers fire always, so writes made under it still reach the target.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeCapturesUnderReplicaRole) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));
  ASSERT_OK(conn.Execute("CREATE ROLE writer"));
  ASSERT_OK(conn.Execute("GRANT yb_db_admin TO writer"));
  ASSERT_OK(conn.Execute("GRANT ALL ON t1 TO writer"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_trigger WHERE tgrelid = 't1'::regclass AND "
      "tgfoid = 'yb_online_schema_change_trigger'::regproc AND tgenabled <> 'A'")), 0);

  auto writer_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(writer_conn.Execute("SET ROLE writer"));
  ASSERT_OK(writer_conn.Execute("SET session_replication_role = replica"));
  ASSERT_OK(writer_conn.Execute("INSERT INTO t1 VALUES (3, 'c')"));
  ASSERT_OK(writer_conn.Execute("UPDATE t1 SET v = 'b2' WHERE id = 2"));
  ASSERT_OK(writer_conn.Execute("DELETE FROM t1 WHERE id = 1"));

  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "t1", "t2"));

  const auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string, std::string>(
      "SELECT id, v, v2 FROM t1 ORDER BY id")));
  ASSERT_EQ(rows, (decltype(rows){{2, "b2", "b2_x"}, {3, "c", "c_x"}}));
}

// The lifecycle functions require the privilege ALTER TABLE requires on both tables. Begin also
// requires the privileges of the transform's owner, which the copy runs as, so that a yb_db_admin
// member cannot run a transform as a superuser.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeRequiresAlterPrivilege) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_user"));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_admin"));
  ASSERT_OK(conn.Execute("GRANT yb_db_admin TO osc_admin"));

  auto user_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(user_conn.Execute("SET ROLE osc_user"));
  constexpr auto kNotT1Owner = "must be owner of table t1";
  ASSERT_NOK_STR_CONTAINS(BeginOnlineSchemaChange(&user_conn, "t1", "t2", "t1_to_t2"),
                          kNotT1Owner);
  ASSERT_NOK_STR_CONTAINS(BeginOnlineSchemaChange(&user_conn, "t2", "t1", "t1_to_t2"),
                          kNotT1Owner);

  // The cutover renames the tables, which ALTER TABLE allows only with CREATE on the schema.
  ASSERT_OK(conn.Execute("ALTER TABLE t1 OWNER TO osc_user"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO osc_user"));
  ASSERT_NOK_STR_CONTAINS(BeginOnlineSchemaChange(&user_conn, "t1", "t2", "t1_to_t2"),
                          "permission denied for schema public");
  ASSERT_OK(conn.Execute("ALTER TABLE t1 OWNER TO CURRENT_USER"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO CURRENT_USER"));

  auto admin_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(admin_conn.Execute("SET ROLE osc_admin"));
  ASSERT_NOK_STR_CONTAINS(BeginOnlineSchemaChange(&admin_conn, "t1", "t2", "t1_to_t2"),
                          "must be owner of transform function t1_to_t2");

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_catalog.pg_yb_online_schema_change")), 0);

  // Abort makes the same check on the job's tables.
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK_STR_CONTAINS(AbortOnlineSchemaChange(&user_conn, "t1", "t2"), kNotT1Owner);
  ASSERT_OK(AbortOnlineSchemaChange(&conn, "t1", "t2"));
}

// The copy runs the transform as its owner. The transform cannot name the role, since
// current_user is stable, so it calls pg_read_file, which only superusers may call: the copy fails
// even though a superuser owns the tables and began the job. The owner cannot change while the
// job runs.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeBackfillRunsAsTransformOwner) {
  auto retries = FewerChunkRetries();
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE ROLE osc_owner"));
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, 100) AS i"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, version TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, pg_read_file('PG_VERSION'))::t2; "
      "END $$"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO osc_owner"));
  ASSERT_OK(conn.Execute("GRANT SELECT ON t1 TO osc_owner"));
  ASSERT_OK(conn.Execute("GRANT INSERT ON t2 TO osc_owner"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_NOK_STR_CONTAINS(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO CURRENT_USER"),
                          "cannot change the owner of function");
  ASSERT_NOK_STR_CONTAINS(conn.Execute("ALTER ROUTINE t1_to_t2 OWNER TO CURRENT_USER"),
                          "cannot change the owner of function");
  ASSERT_NOK_STR_CONTAINS(WaitForOnlineSchemaChange(&conn, "t1", "t2"),
                          "the copy of online schema change job");
  ASSERT_STR_CONTAINS(
      ASSERT_RESULT(conn.FetchRow<std::string>(
          "SELECT error_message FROM pg_catalog.yb_online_schema_change")),
      "permission denied for function pg_read_file");
  // The failure can quote source data, so a role that may not ALTER the source does not see it.
  ASSERT_OK(conn.Execute("CREATE ROLE osc_reader"));
  ASSERT_OK(conn.Execute("SET ROLE osc_reader"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT status || ':' || coalesce(error_message, '<hidden>') || ':' || command "
      "FROM pg_catalog.yb_online_schema_change")), "failed:<hidden>:yb_begin_mirror_table");
  // The catalog keeps the text of the statement that started the job, which can quote literals.
  ASSERT_NOK_STR_CONTAINS(
      conn.Fetch("SELECT query FROM pg_catalog.pg_yb_online_schema_change"),
      "permission denied for table pg_yb_online_schema_change");
  ASSERT_OK(conn.Execute("RESET ROLE"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeBeginRequiresTransformOwnerPrivileges) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateOnlineSchemaChangePair(&conn));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_owner"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION t1_to_t2 OWNER TO osc_owner"));

  ASSERT_NOK_STR_CONTAINS(
      BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"),
      "owner of transform function t1_to_t2 cannot read table \"t1\"");
  ASSERT_OK(conn.Execute("GRANT SELECT ON t1 TO osc_owner"));
  ASSERT_NOK_STR_CONTAINS(
      BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"),
      "owner of transform function t1_to_t2 cannot insert into target table \"t2\"");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_catalog.pg_yb_online_schema_change")), 0);

  ASSERT_OK(conn.Execute("GRANT INSERT ON t2 TO osc_owner"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "t1", "t2"));
}

TEST_F(PgMiniTestSingleNode, BulkUpdateFlushRequestsNoOnlineSchemaChange) {
  constexpr int kNumRows = 500;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'val_' || i FROM generate_series(1, $0) AS i", kNumRows));

  auto explain_json = ASSERT_RESULT(conn.FetchRow<JsonDocument>(
      "EXPLAIN (ANALYZE, DIST, DEBUG, FORMAT JSON) "
      "UPDATE t1 SET v = 'new_' || v"));

  LOG(INFO) << "EXPLAIN output (no capture trigger): "
            << ASSERT_RESULT(explain_json.Root().ToString());

  auto flush_requests = ASSERT_RESULT(
      explain_json.Root()[0]["Storage Flush Requests"].GetDouble());
  LOG(INFO) << "Storage Flush Requests for bulk UPDATE without capture trigger ("
            << kNumRows << " rows): " << flush_requests;

  auto write_requests = ASSERT_RESULT(
      explain_json.Root()[0]["Storage Write Requests"].GetDouble());
  LOG(INFO) << "Storage Write Requests (no capture trigger): " << write_requests;
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

  // Keeps the colocation tablet alive when kTableName is dropped.
  ASSERT_OK(conn.Execute("CREATE TABLE keep_tablet (v int PRIMARY KEY)"));

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
  // The tombstone is a DDL txn intent applied to regular DB asynchronously after commit.
  auto tablet = ASSERT_RESULT(peer->shared_tablet());
  std::string dump;
  ASSERT_TRUE(WaitFor([&] {
    dump = tablet->TEST_DocDBDumpStr();
    return DumpHasTableTombstone(dump, kColocationId);
  }, 30s * kTimeMultiplier, "Table tombstone applied").ok())
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
    std::string dump;
    ASSERT_TRUE(WaitFor([&] {
      dump = tablet->TEST_DocDBDumpStr();
      return DumpHasTableTombstone(dump, kColocationId);
    }, 30s * kTimeMultiplier, "Table tombstone applied").ok())
        << "TRUNCATE did not write a table-level tombstone (rewrite path taken?). Dump:\n" << dump;
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

// The TableInfo rebuilds that keep the table's data (schema GC, backfill done) carry the
// tombstone-cache state over, including what a tombstone notify did to the old context before the
// rebuild. The plain copy, used for same-version rebuilds, still resets it.
TEST(DocReadContextTombstoneCacheTest, RebuildCarriesCacheState) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddKeyColumn("k", DataType::INT32));
  auto schema = builder.Build();
  schema.set_colocation_id(42);
  auto ctx = docdb::DocReadContext::TEST_Create(schema);
  ASSERT_EQ(
      docdb::DocReadContext(ctx, SchemaVersion{0}).tombstone_cache_watermark(), HybridTime::kMax);

  const auto kWatermark = HybridTime::FromMicros(100);
  const auto kReadHt = HybridTime::FromMicros(150);
  ctx.AdvanceTombstoneCacheWatermark(kWatermark);
  ctx.set_table_tombstone_time(DocHybridTime::kInvalid, ctx.tombstone_cache_generation());
  ASSERT_TRUE(ctx.GetCachedTableTombstoneTime(kReadHt).has_value());

  {
    const docdb::DocReadContext filtered(ctx, SchemaVersion{0});
    const docdb::DocReadContext replaced(ctx, ctx.schema());
    for (const auto* rebuilt : {&filtered, &replaced}) {
      SCOPED_TRACE(rebuilt == &filtered ? "schema GC rebuild" : "backfill-done rebuild");
      ASSERT_EQ(rebuilt->tombstone_cache_watermark(), kWatermark);
      ASSERT_EQ(rebuilt->tombstone_cache_generation(), ctx.tombstone_cache_generation());
      auto cached = rebuilt->GetCachedTableTombstoneTime(kReadHt);
      ASSERT_TRUE(cached.has_value());
      ASSERT_EQ(*cached, DocHybridTime::kInvalid);
    }
    ASSERT_EQ(docdb::DocReadContext(ctx).tombstone_cache_watermark(), HybridTime::kMax);
  }

  // A tombstone applied before the rebuild: the raised watermark and the dropped entry carry over,
  // and a populate stamped before the notify still misses in the rebuilt context.
  const auto generation_before_notify = ctx.tombstone_cache_generation();
  const auto kTombstoneHt = HybridTime::FromMicros(200);
  ctx.OnTableTombstoneWritten(kTombstoneHt);
  const docdb::DocReadContext filtered(ctx, SchemaVersion{0});
  const docdb::DocReadContext replaced(ctx, ctx.schema());
  for (const auto* rebuilt : {&filtered, &replaced}) {
    SCOPED_TRACE(rebuilt == &filtered ? "schema GC rebuild" : "backfill-done rebuild");
    ASSERT_EQ(rebuilt->tombstone_cache_watermark(), kTombstoneHt);
    ASSERT_FALSE(rebuilt->IsTombstoneCacheEligible(kReadHt));
    ASSERT_FALSE(rebuilt->table_tombstone_time().has_value());
    rebuilt->set_table_tombstone_time(DocHybridTime::kInvalid, generation_before_notify);
    ASSERT_FALSE(rebuilt->table_tombstone_time().has_value());
  }
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
  // CREATE TABLE returns once the leader has applied the colocated ADD_TABLE op; followers apply
  // it asynchronously and only report the table afterwards, so poll instead of sampling once.
  std::vector<tablet::TabletPeerPtr> peers;
  ASSERT_OK(WaitFor(
      [&] {
        peers = ListTableActiveTabletPeers(cluster_.get(), table_id);
        if (peers.size() != NumTabletServers()) {
          return false;
        }
        // Arming happens later in the same apply, so wait for it too.
        for (const auto& peer : peers) {
          auto table_info = peer->tablet_metadata()->GetTableInfo(kColocationId);
          if (!table_info.ok() || !(*table_info)->doc_read_context ||
              (*table_info)->doc_read_context->tombstone_cache_watermark() == HybridTime::kMax) {
            return false;
          }
        }
        return true;
      },
      30s * kTimeMultiplier, "Every replica of the colocated tablet armed the table"));
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

// A colocated table's DocReadContext starts unarmed (tombstone-cache watermark kMax: the cache is
// neither consumed nor populated), so every path that installs a new TableInfo has to arm it or
// carry over the old context's cache state. A path that does neither leaves the table's cache off
// until the next ALTER or restart, and every scan of the table then does a full table-tombstone
// lookup (an IntentAwareIterator plus a seek) that the cache exists to avoid. These tests cover
// old-schema-version GC, which runs after compactions, and the index backfill-done metadata update.
YB_STRONGLY_TYPED_BOOL(WithIndex);
YB_STRONGLY_TYPED_BOOL(IndexAfterRows);

class PgMiniTombstoneCacheRebuildTest : public PgMiniTest {
 protected:
  void SetUp() override {
    // Packed rows default to off in debug builds, and schema GC only sees schema versions recorded
    // for packed rows.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
    PgMiniTest::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }

  // PgMiniTestBase turns online index backfill off; production has it on, and the backfill-done
  // metadata update is one of the rebuild paths under test.
  void BeforePgProcessStart() override {
    PgMiniTest::BeforePgProcessStart();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_disable_index_backfill) = false;
  }

  static constexpr int kTableColocationId = 30001;
  static constexpr int kIndexColocationId = 30002;
  static constexpr int kNumRows = 100;
  static constexpr int kNumUpdates = 200;

  // Creates colocated table t(k PK, v, w), optionally with an index on v (which bumps t's schema
  // version) created before or after the rows, and warms t's tombstone-time cache with a
  // base-table scan.
  Status SetUpTable(
      WithIndex with_index, IndexAfterRows index_after_rows = IndexAfterRows::kFalse) {
    const std::string kDbName = "tombstone_cache_rebuild_db";
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated=true", kDbName));
    conn_.emplace(VERIFY_RESULT(ConnectToDB(kDbName)));
    RETURN_NOT_OK(conn_->ExecuteFormat(
        "CREATE TABLE t (k int PRIMARY KEY, v int, w int) WITH (colocation_id=$0)",
        kTableColocationId));
    auto create_index_stmt = Format(
        "CREATE INDEX t_v_idx ON t (v) WITH (colocation_id=$0)", kIndexColocationId);
    if (with_index && !index_after_rows) {
      RETURN_NOT_OK(conn_->Execute(create_index_stmt));
    }
    RETURN_NOT_OK(conn_->ExecuteFormat(
        "INSERT INTO t SELECT i, i, 0 FROM generate_series(1, $0) i", kNumRows));
    if (with_index && index_after_rows) {
      RETURN_NOT_OK(conn_->Execute(create_index_stmt));
    }
    // w is not in the index, so this scans the base table.
    RETURN_NOT_OK(conn_->FetchRow<int64_t>("SELECT sum(w) FROM t"));

    auto table_id = VERIFY_RESULT(GetTableIDFromTableName("t"));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected one colocated tablet leader");
    peer_ = peers.front();
    return Status::OK();
  }

  Result<tablet::TableInfoPtr> ColocatedTableInfo(ColocationId colocation_id) {
    auto table_info = VERIFY_RESULT(peer_->tablet_metadata()->GetTableInfo(colocation_id));
    SCHECK(table_info && table_info->doc_read_context, IllegalState, "No DocReadContext");
    return table_info;
  }

  // Regular-DB seeks issued by kNumUpdates single-row primary-key UPDATEs of the non-indexed
  // column. Each UPDATE reads its row through one DocRowwiseIterator; with the cache off that
  // iterator also does the table-tombstone lookup, i.e. one more regular-DB seek per UPDATE.
  Result<uint64_t> RegularDbSeeksForUpdates() {
    auto tablet = VERIFY_RESULT(peer_->shared_tablet());
    const auto& stats = tablet->regulardb_statistics();
    const auto before = stats->getTickerCount(rocksdb::NUMBER_DB_SEEK);
    for (int i = 0; i < kNumUpdates; ++i) {
      RETURN_NOT_OK(conn_->ExecuteFormat("UPDATE t SET w = w + 1 WHERE k = $0", i % kNumRows + 1));
    }
    return stats->getTickerCount(rocksdb::NUMBER_DB_SEEK) - before;
  }

  // Compacts and checks that t's TableInfo was not replaced and its warm cache was left alone: same
  // watermark and generation, entry still cached before any further read could refill it.
  void CompactAndCheckCacheUntouched(const tablet::TableInfoPtr& before) {
    const auto& ctx = *before->doc_read_context;
    const auto watermark_before = ctx.tombstone_cache_watermark();
    const auto generation_before = ctx.tombstone_cache_generation();
    ASSERT_NE(watermark_before, HybridTime::kMax);
    ASSERT_TRUE(ctx.table_tombstone_time().has_value());
    const auto seeks_before = ASSERT_RESULT(RegularDbSeeksForUpdates());

    FlushAndCompactTablets();

    ASSERT_EQ(ASSERT_RESULT(ColocatedTableInfo(kTableColocationId)), before);
    ASSERT_EQ(ctx.tombstone_cache_watermark(), watermark_before);
    ASSERT_EQ(ctx.tombstone_cache_generation(), generation_before);
    ASSERT_TRUE(ctx.table_tombstone_time().has_value());
    const auto seeks_after = ASSERT_RESULT(RegularDbSeeksForUpdates());
    LOG(INFO) << "Regular-DB seeks for " << kNumUpdates << " PK UPDATEs: " << seeks_before
              << " before compaction, " << seeks_after << " after";
  }

  std::optional<PGConn> conn_;
  tablet::TabletPeerPtr peer_;
};

// CREATE INDEX bumps the indexed table's schema version, so the first compaction afterwards finds
// t's packing storage holding a version below the minimum in use and rebuilds t's TableInfo. The
// rebuilt DocReadContext must keep the old one's tombstone-cache state, warm entry included.
TEST_F_EX(PgMiniTest, TombstoneCacheArmedAfterSchemaGC, PgMiniTombstoneCacheRebuildTest) {
  ASSERT_OK(SetUpTable(WithIndex::kTrue));

  auto before_gc = ASSERT_RESULT(ColocatedTableInfo(kTableColocationId));
  // Trigger guard: CREATE INDEX bumped t's schema version, and the old packing is still there for
  // the GC to drop.
  ASSERT_GT(before_gc->schema_version, 0U);
  ASSERT_TRUE(before_gc->doc_read_context->schema_packing_storage.HasVersionBelow(
      before_gc->schema_version))
      << before_gc->doc_read_context->schema_packing_storage.VersionsToString();
  // The AlterSchema that applied the bump armed the cache, and the scan in SetUpTable filled it.
  const auto watermark_before_gc = before_gc->doc_read_context->tombstone_cache_watermark();
  const auto generation_before_gc = before_gc->doc_read_context->tombstone_cache_generation();
  ASSERT_NE(watermark_before_gc, HybridTime::kMax);
  ASSERT_TRUE(before_gc->doc_read_context->table_tombstone_time().has_value());
  const auto seeks_before_gc = ASSERT_RESULT(RegularDbSeeksForUpdates());

  FlushAndCompactTablets();

  // Trigger guard: the compaction's old-schema-version GC replaced t's TableInfo and dropped the
  // old packing.
  tablet::TableInfoPtr after_gc;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        after_gc = VERIFY_RESULT(ColocatedTableInfo(kTableColocationId));
        return after_gc != before_gc;
      },
      30s * kTimeMultiplier, "Old-schema-version GC rebuilt the TableInfo"));
  const auto& ctx = *after_gc->doc_read_context;
  ASSERT_FALSE(ctx.schema_packing_storage.HasVersionBelow(after_gc->schema_version))
      << ctx.schema_packing_storage.VersionsToString();
  // A late AlterSchema would also replace the TableInfo, but with a new schema version.
  ASSERT_EQ(after_gc->schema_version, before_gc->schema_version);

  // Checked before any further read, which could refill a cache the rebuild dropped.
  const auto watermark_after_gc = ctx.tombstone_cache_watermark();
  const auto generation_after_gc = ctx.tombstone_cache_generation();
  const auto cached_after_gc = ctx.table_tombstone_time().has_value();

  const auto seeks_after_gc = ASSERT_RESULT(RegularDbSeeksForUpdates());
  LOG(INFO) << "Regular-DB seeks for " << kNumUpdates << " PK UPDATEs: " << seeks_before_gc
            << " before schema GC, " << seeks_after_gc << " after";

  const auto diag = Format(
      "TOMBSTONE CACHE STATE LOST: old-schema-version GC rebuilt the table's DocReadContext "
      "(packings now $0). Watermark $1 -> $2, generation $3 -> $4, cached entry kept: $5. "
      "Regular-DB seeks for $6 single-row PK UPDATEs: $7 before the GC, $8 after.",
      ctx.schema_packing_storage.VersionsToString(), watermark_before_gc, watermark_after_gc,
      generation_before_gc, generation_after_gc, cached_after_gc, kNumUpdates, seeks_before_gc,
      seeks_after_gc);
  ASSERT_EQ(watermark_after_gc, watermark_before_gc) << diag;
  ASSERT_EQ(generation_after_gc, generation_before_gc) << diag;
  ASSERT_TRUE(cached_after_gc) << diag;
}

// Control for TombstoneCacheArmedAfterSchemaGC: the same compaction with schema GC disabled does
// not rebuild t's TableInfo, and the cache stays armed and filled.
TEST_F_EX(
    PgMiniTest, TombstoneCacheArmedAfterCompactionWithoutSchemaGC,
    PgMiniTombstoneCacheRebuildTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_schema_packing_gc) = false;
  ASSERT_OK(SetUpTable(WithIndex::kTrue));

  auto before = ASSERT_RESULT(ColocatedTableInfo(kTableColocationId));
  ASSERT_TRUE(before->doc_read_context->schema_packing_storage.HasVersionBelow(
      before->schema_version));
  ASSERT_NO_FATALS(CompactAndCheckCacheUntouched(before));
}

// Control for TombstoneCacheArmedAfterSchemaGC: a table with a single schema version has nothing
// for the GC to drop, so compaction leaves its TableInfo and cache alone.
TEST_F_EX(
    PgMiniTest, TombstoneCacheArmedAfterCompactionSingleSchemaVersion,
    PgMiniTombstoneCacheRebuildTest) {
  ASSERT_OK(SetUpTable(WithIndex::kFalse));

  auto before = ASSERT_RESULT(ColocatedTableInfo(kTableColocationId));
  ASSERT_FALSE(before->doc_read_context->schema_packing_storage.HasVersionBelow(
      before->schema_version));
  ASSERT_NO_FATALS(CompactAndCheckCacheUntouched(before));
}

// The index backfill-done metadata update (Tablet::MarkBackfillDone) rebuilds the index's
// TableInfo to clear retain_delete_markers. The index's DocReadContext must still be able to use
// the tombstone-time cache afterwards. In a YSQL CREATE INDEX the index-permission AlterSchema that
// follows also arms every colocated table in the tablet, so this does not isolate the carry-over
// in the backfill-done rebuild; DocReadContextTombstoneCacheTest.RebuildCarriesCacheState does.
TEST_F_EX(PgMiniTest, TombstoneCacheArmedAfterIndexBackfill, PgMiniTombstoneCacheRebuildTest) {
  StringWaiterLogSink backfill_done("Setting backfill as done");
  ASSERT_OK(SetUpTable(WithIndex::kTrue, IndexAfterRows::kTrue));

  // Trigger guard: the backfill-done op was applied and cleared retain_delete_markers.
  ASSERT_OK(backfill_done.WaitFor(30s * kTimeMultiplier));
  tablet::TableInfoPtr index_info;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        index_info = VERIFY_RESULT(ColocatedTableInfo(kIndexColocationId));
        return !index_info->schema().table_properties().retain_delete_markers();
      },
      30s * kTimeMultiplier, "Backfill done cleared retain_delete_markers on the index"));

  // Read through the index, which would fill an armed index cache.
  ASSERT_OK(conn_->Execute("SET enable_seqscan = off"));
  ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int32_t>("SELECT v FROM t WHERE v = 5")), 5);
  index_info = ASSERT_RESULT(ColocatedTableInfo(kIndexColocationId));
  const auto& ctx = *index_info->doc_read_context;
  const auto diag = Format(
      "TOMBSTONE CACHE LEFT OFF: the backfill-done update rebuilt the index's DocReadContext and "
      "nothing re-armed it (watermark $0)", ctx.tombstone_cache_watermark());
  ASSERT_NE(ctx.tombstone_cache_watermark(), HybridTime::kMax) << diag;
  ASSERT_TRUE(ctx.table_tombstone_time().has_value()) << diag;
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
        auto fetch_query = Format(
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

class PgSysCatalogFullCompactionTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    // Disable the background task of the full compaction manager. The test collects the
    // statistics and schedules the compactions itself.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_check_interval_sec) = 0;
    PgMiniTestBase::SetUp();
  }
};

// Dropped tables leave dead pg_attribute rows in the sys catalog tablet. Scans of pg_attribute
// read these rows as obsolete keys, which must trigger a full compaction of the sys catalog tablet.
TEST_F(PgSysCatalogFullCompactionTest, CompactBasedOnStats) {
  constexpr int kNumColumns = 1000;
  constexpr int kNumTables = 5;
  constexpr int kNumDeadRows = kNumColumns * kNumTables;
  constexpr int kHistoryRetentionSec = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_stat_window_seconds) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_percent_obsolete) = 50;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_min_obsolete_keys_found) = kNumDeadRows;

  auto& sys_catalog =
      *ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager().sys_catalog();
  auto tablet = ASSERT_RESULT(sys_catalog.tablet_peer()->shared_tablet());
  auto* compact_manager = sys_catalog.full_compaction_manager();
  // One check interval per stats window: each window holds the stats between two consecutive
  // ScheduleFullCompactions() calls.
  compact_manager->TEST_SetCheckIntervalSec(1);

  auto conn = ASSERT_RESULT(Connect());
  std::string columns;
  for (int i = 0; i < kNumColumns; ++i) {
    columns += Format("$0c$1 INT", i == 0 ? "" : ", ", i);
  }
  for (int i = 0; i < kNumTables; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t$0 ($1)", i, columns));
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE t$0", i));
  }
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  const auto collect_stats_after_scan = [&]() -> Result<tserver::KeyStatistics> {
    compact_manager->ScheduleFullCompactions();
    RETURN_NOT_OK(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM pg_attribute"));
    compact_manager->ScheduleFullCompactions();
    const auto stats = compact_manager->TEST_CurrentWindowStats(master::kSysCatalogTabletId);
    LOG(INFO) << "Sys catalog key stats: total: " << stats.total
              << ", obsolete past cutoff: " << stats.obsolete_cutoff;
    return stats;
  };

  // The sys catalog tablet still retains the deleted rows for
  // timestamp_syscatalog_history_retention_interval_sec, so they are not counted as obsolete
  // past the history cutoff, and no compaction is scheduled.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      kHistoryRetentionSec;
  SleepFor(2s * kHistoryRetentionSec);
  auto stats = ASSERT_RESULT(collect_stats_after_scan());
  ASSERT_GE(stats.total, kNumDeadRows);
  ASSERT_EQ(stats.obsolete_cutoff, 0);
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 0);

  // Move the history cutoff of the sys catalog tablet past the deletes of the dropped tables.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_syscatalog_history_retention_interval_sec) =
      kHistoryRetentionSec;
  SleepFor(2s * kHistoryRetentionSec);
  const auto before_compaction = tablet->clock()->Now();
  stats = ASSERT_RESULT(collect_stats_after_scan());
  ASSERT_GE(stats.obsolete_cutoff, kNumDeadRows);
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 1);
  ASSERT_OK(LoggedWaitFor(
      [&tablet, before_compaction] {
        const auto last_compact_time = HybridTime(tablet->metadata()->last_full_compaction_time());
        return !last_compact_time.is_special() && last_compact_time >= before_compaction &&
               !tablet->HasActiveFullCompaction();
      },
      30s * kTimeMultiplier, "Waiting for full compaction of the sys catalog tablet"));

  // The compaction removed the dead rows, so the next scan does not trigger another compaction.
  stats = ASSERT_RESULT(collect_stats_after_scan());
  ASSERT_GT(stats.total, 0);
  ASSERT_LT(stats.obsolete_cutoff, kNumColumns);
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 0);
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

  // The transaction status tablets are created before every tablet server has registered, so the
  // load balancer is still adding their third replica. Restarting one of the two voters they have
  // until then costs those tablets their quorum and stalls applies for seconds.
  ASSERT_OK(WaitAllReplicasReady(cluster_.get(), 120s * kTimeMultiplier, UserTabletsOnly::kFalse));
  ASSERT_OK(cluster_->WaitForLoadBalancerToStabilize(120s * kTimeMultiplier));

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

  // Every peer must report exactly the expected number of transactions: a peer that has not
  // replicated them yet, or has not loaded them back after the restart, reports none of its own and
  // would satisfy a check on the total vacuously.
  const auto& tablet_id = tablet_peer->tablet_id();
  auto wait_running = [this, &tablet_id, kApplyWait](size_t expected_running) -> Status {
    return WaitFor([this, &tablet_id, expected_running]() -> Result<bool> {
      size_t peers_done = 0;
      for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
        if (peer->tablet_id() != tablet_id) {
          continue;
        }
        auto peer_tablet = peer->shared_tablet_maybe_null();
        if (!peer_tablet) {
          return false;
        }
        if (peer_tablet->transaction_participant()->GetNumRunningTransactions() !=
            expected_running) {
          return false;
        }
        ++peers_done;
      }
      return peers_done == NumTabletServers();
    }, kApplyWait, Format("$0 running transactions left on every peer", expected_running));
  };

  // Catching up with the tablet leader and loading the transactions back from the flushed intents
  // are both asynchronous with the restart.
  ASSERT_OK(wait_running(3));

  // The recently applied transactions map only retains a staircase of (first_write_ht,
  // apply_op_id) pairs, so an apply landing out of first write order subsumes the entry of an
  // earlier transaction. Applies are asynchronous with respect to commit, so let each transaction
  // apply, i.e. leave the participant, before committing the next one.
  auto commit_and_wait_apply = [&wait_running](PGConn* conn, size_t expected_running) -> Status {
    RETURN_NOT_OK(conn->CommitTransaction());
    return wait_running(expected_running);
  };

  ASSERT_OK(commit_and_wait_apply(&conn1, 2));
  ASSERT_OK(commit_and_wait_apply(&conn2, 1));
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
    ASSERT_OK(conn.Execute("SET enable_sort = off"));
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

// The master's catalog versions cache refresh runs on a pool of its own, so it keeps refreshing
// while every worker of the catalog manager's shared background pool is busy. Before, its body
// was submitted to that shared pool and queued behind whatever held the workers.
TEST_F(PgMiniTest, CatalogVersionsCacheRefreshesWhileBackgroundPoolIsBusy) {
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  auto cached_read_time = [&catalog_manager] {
    master::DbOidToCatalogVersionMap versions;
    uint64_t fingerprint = 0;
    HybridTime read_ht;
    catalog_manager.pg_catalog_versions_cache().GetAll(&versions, &fingerprint, &read_ht);
    return read_ht;
  };
  ASSERT_OK(WaitFor(
      [&cached_read_time] { return cached_read_time().is_valid(); }, 30s * kTimeMultiplier,
      "First catalog versions cache refresh"));

  // The shared pool is built with the default worker count, NumEffectiveCPUs(). Hold every worker,
  // and wait until each holder runs, so nothing queued behind them can be running.
  //
  // Each holder shares the latches rather than reaching them on this stack: the test can return
  // while holders are still running or, after the ASSERT below fails, still queued in the pool,
  // and the master outlives the test body.
  struct Holders {
    explicit Holders(int num_workers) : started(num_workers) {}
    CountDownLatch started;
    CountDownLatch release{1};
  };
  const auto num_workers = NumEffectiveCPUs();
  auto holders = std::make_shared<Holders>(num_workers);
  auto release_workers = ScopeExit([holders] { holders->release.CountDown(); });
  for (int i = 0; i != num_workers; ++i) {
    ASSERT_OK(catalog_manager.SubmitBackgroundTask([holders] {
      holders->started.CountDown();
      holders->release.Wait();
    }));
  }
  ASSERT_TRUE(holders->started.WaitFor(30s * kTimeMultiplier))
      << holders->started.count() << " of " << num_workers
      << " background workers never started";

  // The refresh runs every heartbeat_interval_ms / 2. Allow many periods, so only a refresh that
  // cannot run at all fails this.
  const auto before = cached_read_time();
  ASSERT_OK(WaitFor(
      [&cached_read_time, before] { return cached_read_time() > before; },
      MonoDelta::FromMilliseconds(20 * FLAGS_heartbeat_interval_ms) * kTimeMultiplier,
      "Catalog versions cache refresh while the background pool is busy"));
}

// ------------------------------------------------------------------------------------------------
// Online schema change change-capture trigger tests
// ------------------------------------------------------------------------------------------------
// The triggers are attached directly, as YbOscCreateTrigger does, to an empty target. A source row
// inserted before the triggers are attached stands for a row the backfill has not copied yet.

namespace {

Status CreateOnlineSchemaChangeTriggers(
    PGConn* conn, const std::string& src, const std::string& target,
    const std::string& transform, const std::string& src_root = "") {
  const auto target_oid = VERIFY_RESULT(conn->FetchRow<std::string>(
      Format("SELECT '$0'::regclass::oid::text", target)));
  const auto transform_oid = VERIFY_RESULT(conn->FetchRow<std::string>(
      Format("SELECT '$0'::regproc::oid::text", transform)));
  const auto src_root_oid = VERIFY_RESULT(conn->FetchRow<std::string>(
      Format("SELECT '$0'::regclass::oid::text", src_root.empty() ? src : src_root)));

  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE TRIGGER yb_osc_ins AFTER INSERT ON $0 "
      "REFERENCING NEW TABLE AS yb_osc_new_rows "
      "FOR EACH STATEMENT EXECUTE FUNCTION "
      "pg_catalog.yb_online_schema_change_trigger('$1', '$2', '$3')",
      src, target_oid, transform_oid, src_root_oid));
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE TRIGGER yb_osc_upd AFTER UPDATE ON $0 "
      "REFERENCING OLD TABLE AS yb_osc_old_rows NEW TABLE AS yb_osc_new_rows "
      "FOR EACH STATEMENT EXECUTE FUNCTION "
      "pg_catalog.yb_online_schema_change_trigger('$1', '$2', '$3')",
      src, target_oid, transform_oid, src_root_oid));
  RETURN_NOT_OK(conn->ExecuteFormat(
      "CREATE TRIGGER yb_osc_del AFTER DELETE ON $0 "
      "REFERENCING OLD TABLE AS yb_osc_old_rows "
      "FOR EACH STATEMENT EXECUTE FUNCTION "
      "pg_catalog.yb_online_schema_change_trigger('$1', '$2', '$3')",
      src, target_oid, transform_oid, src_root_oid));
  return Status::OK();
}

// Attach the capture triggers to every relation of a partitioned source
// hierarchy, as YbOscCreateTrigger does.
Status AttachOnlineSchemaChangeTriggersOnTree(
    PGConn* conn, const std::string& src_root, const std::string& target,
    const std::string& transform) {
  const auto nodes = VERIFY_RESULT(conn->FetchRows<std::string>(
      Format("SELECT relid::regclass::text FROM pg_partition_tree('$0') ORDER BY level",
             src_root)));
  for (const auto& node : nodes) {
    RETURN_NOT_OK(CreateOnlineSchemaChangeTriggers(conn, node, target, transform, src_root));
  }
  return Status::OK();
}

using PartRows = std::vector<std::tuple<int32_t, std::string, int32_t, std::string>>;
Result<PartRows> FetchPartRows(PGConn* conn, const std::string& table) {
  return conn->FetchRows<int32_t, std::string, int32_t, std::string>(
      Format("SELECT id, region, yr, v FROM $0 ORDER BY id, region, yr", table));
}

Result<double> ExplainStorageStat(PGConn* conn, const std::string& dml, const char* key) {
  auto doc = VERIFY_RESULT(conn->FetchRow<JsonDocument>(
      Format("EXPLAIN (ANALYZE, DIST, DEBUG, FORMAT JSON) $0", dml)));
  return doc.Root()[0][key].GetDouble();
}

Result<double> ExplainFlushes(PGConn* conn, const std::string& dml) {
  return ExplainStorageStat(conn, dml, "Storage Flush Requests");
}

// The rows t2 should hold for the current rows of t1, and the rows it holds.
Result<std::string> ExpectedTargetRows(PGConn* conn) {
  return conn->FetchAllAsString("SELECT t1_to_t2(t1)::text AS r FROM t1 ORDER BY r");
}

Result<std::string> TargetRows(PGConn* conn) {
  return conn->FetchAllAsString("SELECT t2::text AS r FROM t2 ORDER BY r");
}

constexpr auto kOnlineSchemaChangeReplayOrderError =
    "this way while an online schema change is copying it";

}  // namespace

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerInsertReplay) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b'), (3, NULL)"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 3);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t2 WHERE id = 3 AND v IS NULL AND v2 IS NULL")), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t2 WHERE v2 IS DISTINCT FROM (v || '_x')")), 0);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerDeletePresent) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 2);

  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 1"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2 WHERE id = 1")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 1);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerDeleteAbsentTarget) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 0);

  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 1"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2 WHERE id = 1")), 0);
}

// The tombstone for a row missing from the target is a blind delete; with a vector column on the
// target, DocDB must not try to read the old vector id of a row it did not find.
TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerDeleteAbsentTargetWithVector) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, e vector(3), v TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, '[1,2,3]'::vector, src.v)::t2; END $$"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 1"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'b2' WHERE id = 2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchAllAsString("SELECT id, e::text, v FROM t2 ORDER BY id")),
            "2, [1,2,3], b2");
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerUpdatePresent) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'a2' WHERE id = 1"));

  const auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v, v2 FROM t2 WHERE id = 1")));
  ASSERT_EQ(row, (decltype(row){"a2", "a2_x"}));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 1);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerUpdateAbsentTarget) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'a2' WHERE id = 1"));

  const auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v, v2 FROM t2 WHERE id = 1")));
  ASSERT_EQ(row, (decltype(row){"a2", "a2_x"}));
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerUpdateChangesPrimaryKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET id = 2 WHERE id = 1"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2 WHERE id = 1")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2 WHERE id = 2")), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 1);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerEmptyStatement) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'v' FROM generate_series(1, 0) i"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'x' WHERE id = 999"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 999"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 0);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerDroppedTargetColumn) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, dead INT, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 DROP COLUMN dead"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 1);
  const auto row = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT v, v2 FROM t2 WHERE id = 1")));
  ASSERT_EQ(row, (decltype(row){"a", "a_x"}));
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerSecondaryIndexes) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, name TEXT, age INT, city TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, name TEXT, age INT, city TEXT, tag TEXT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_city_idx ON t2 (city)"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX t2_name_idx ON t2 (name)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_city_age_idx ON t2 (city, age)"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_lower_name_idx ON t2 (lower(name))"));
  ASSERT_OK(conn.Execute("CREATE INDEX t2_senior_idx ON t2 (age) WHERE age >= 30"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.name, src.age, src.city, src.city || "
      "'_' || src.age::text)::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 VALUES (1,'alice',30,'nyc'), (2,'bob',25,'sfo'), "
      "(3,'carol',35,'nyc'), (4,'dave',40,'sea')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET name = 'robert', age = 26 WHERE id = 2"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 3"));

  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t2 WHERE name = 'carol'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t2 WHERE lower(name) = 'carol'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t2 WHERE city = 'nyc' AND age = 35")), 0);

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(
      "SELECT id FROM t2 WHERE name = 'robert'")), 2);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t2 WHERE name = 'bob'")), 0);

  auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string, int32_t, std::string>(
      "SELECT id, name, age, city FROM t1 ORDER BY id")));
  for (const auto& [id, name, age, city] : rows) {
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(
        Format("SELECT id FROM t2 WHERE name = '$0'", name))), id);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT count(*) FROM t2 WHERE city = '$0' AND id = $1", city, id))), 1);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT count(*) FROM t2 WHERE city = '$0' AND age = $1 AND id = $2",
               city, age, id))), 1);
    if (age >= 30) {
      ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
          Format("SELECT count(*) FROM t2 WHERE age = $0 AND id = $1", age, id))), 1);
    }
  }
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerDataTypes) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (id INT PRIMARY KEY, n NUMERIC, b BOOL, ts TIMESTAMPTZ, raw BYTEA)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (id INT PRIMARY KEY, n NUMERIC, b BOOL, ts TIMESTAMPTZ, raw BYTEA)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.n, src.b, src.ts, src.raw)::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 VALUES "
      "(1, 123.456, true, '2024-01-02 03:04:05+00', '\\xdeadbeef'), "
      "(2, -0.001, false, '1999-12-31 23:59:59+00', '\\x00')"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 2"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t1 JOIN t2 USING (id, n, b, ts, raw)")), 1);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerWorkloadEquivalence) {
  constexpr int kKeys = 50;
  constexpr int kOps = 400;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  for (int i = 0; i < kOps; ++i) {
    const auto key = RandomUniformInt(1, kKeys);
    const auto op = RandomUniformInt(0, 2);
    if (op == 0) {
      ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO t1 VALUES ($0, '$1') ON CONFLICT (id) DO UPDATE SET v = EXCLUDED.v",
          key, RandomHumanReadableString(8)));
    } else if (op == 1) {
      ASSERT_OK(conn.ExecuteFormat(
          "UPDATE t1 SET v = '$1' WHERE id = $0", key, RandomHumanReadableString(8)));
    } else {
      ASSERT_OK(conn.ExecuteFormat("DELETE FROM t1 WHERE id = $0", key));
    }
  }

  auto src = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
      "SELECT id, v FROM t1 ORDER BY id")));
  auto dst = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
      "SELECT id, v FROM t2 ORDER BY id")));
  ASSERT_EQ(src, dst);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t2 WHERE v2 IS DISTINCT FROM (v || '_x')")), 0);
}

// MERGE is the only statement that could fire all three capture triggers, and YB rejects it in the
// grammar. If MERGE becomes supported this test fails; then check that every tombstone is still
// written before every insert, that each trigger replays only its own rows, and that an action
// matching no rows does not hit the "missing old/new transition table" errors.
TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerMergeUnsupported) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1,'keep'), (2,'upd'), (3,'del')"));
  ASSERT_OK(conn.Execute("CREATE TABLE s (id INT PRIMARY KEY, v TEXT, act TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (2,'upd_new','u'), (3,NULL,'d'), (4,'ins','i')"));

  const auto status = conn.Execute(
      "MERGE INTO t1 USING s ON t1.id = s.id "
      "WHEN MATCHED AND s.act = 'd' THEN DELETE "
      "WHEN MATCHED AND s.act = 'u' THEN UPDATE SET v = s.v "
      "WHEN NOT MATCHED THEN INSERT (id, v) VALUES (s.id, s.v)");
  ASSERT_NOK(status) << "MERGE now works on YB relations -- verify the capture trigger "
                        "against it (see the comment above this test)";
  ASSERT_STR_CONTAINS(status.ToString(), "not supported yet");

  auto src = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
      "SELECT id, v FROM t1 ORDER BY id")));
  auto dst = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
      "SELECT id, v FROM t2 ORDER BY id")));
  ASSERT_EQ(src, dst);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerRequiresSuperuser) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE victim (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE ROLE attacker"));
  ASSERT_OK(conn.Execute("GRANT CREATE ON SCHEMA public TO attacker"));

  ASSERT_OK(conn.Execute("SET ROLE attacker"));
  ASSERT_OK(conn.Execute("CREATE TABLE mine (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION mine_to_victim(src mine) RETURNS victim LANGUAGE plpgsql IMMUTABLE SET "
      "search_path FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v)::victim; END $$"));
  ASSERT_NOK_STR_CONTAINS(
      CreateOnlineSchemaChangeTriggers(&conn, "mine", "victim", "mine_to_victim"),
      "permission denied for function pg_catalog.yb_online_schema_change_trigger");
  ASSERT_OK(conn.Execute("INSERT INTO mine VALUES (1, 'x')"));
  ASSERT_OK(conn.Execute("RESET ROLE"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM victim")), 0);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerRunsAsInvoker) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, who TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, current_user::text)::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("CREATE ROLE app"));
  ASSERT_OK(conn.Execute("GRANT SELECT, INSERT, UPDATE ON t1 TO app"));

  ASSERT_OK(conn.Execute("SET ROLE app"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'c' WHERE id = 2"));
  ASSERT_OK(conn.Execute("RESET ROLE"));

  const auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string, std::string>(
      "SELECT id, v, who FROM t2 ORDER BY id")));
  ASSERT_EQ(rows, (decltype(rows){{1, "a", "app"}, {2, "c", "app"}}));
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerRejectsSystemTarget) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (objoid OID PRIMARY KEY, classoid OID, objsubid INT, description TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_desc(src t1) RETURNS pg_description LANGUAGE plpgsql IMMUTABLE "
      "SET search_path FROM CURRENT AS $$ BEGIN RETURN ROW(src.objoid, src.classoid, "
      "src.objsubid, src.description)::pg_description; END $$"));
  ASSERT_OK(
      CreateOnlineSchemaChangeTriggers(&conn, "t1", "pg_catalog.pg_description", "t1_to_desc"));

  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("INSERT INTO t1 VALUES (4000000000, 1259, 0, 'x')"),
      "permission denied: \"pg_description\" is a system catalog");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_description WHERE objoid = 4000000000")), 0);
}

// The seed rows are inserted before the triggers are attached, so the target does not have them.
TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerRejectsUnreplayableOrder) {
  struct Case {
    std::vector<std::string> setup;
    std::string seed;
    std::string statement;
  };
  const std::vector<Case> cases = {
      // INSERT fires before the DELETE of the WITH query, which LIMIT leaves unfinished.
      {{},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, NULL, 'b')",
       "WITH d AS (DELETE FROM t1 WHERE id = 1 RETURNING id, v) "
       "INSERT INTO t1 SELECT id, NULL, v || '2' FROM d LIMIT 1"},
      {{},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, NULL, 'b')",
       "WITH u AS (UPDATE t1 SET id = 102 WHERE id = 2 RETURNING v) "
       "INSERT INTO t1 SELECT 2, NULL, v || '2' FROM u LIMIT 1"},
      // The cascade's DELETE is queued again after the INSERT.
      {{"ALTER TABLE t1 ADD FOREIGN KEY (parent) REFERENCES t1 (id) ON DELETE CASCADE"},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, 1, 'b')",
       "WITH d AS (DELETE FROM t1 WHERE id = 1 RETURNING id, v) "
       "INSERT INTO t1 SELECT id, NULL, v || '2' FROM d"},
      {{"CREATE FUNCTION touch() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
        "IF pg_trigger_depth() = 1 THEN "
        "UPDATE t1 SET v = NEW.v || '_touched' WHERE id = NEW.id; END IF; "
        "RETURN NULL; END $$",
        "CREATE TRIGGER touch AFTER UPDATE ON t1 FOR EACH ROW EXECUTE FUNCTION touch()"},
       "INSERT INTO t1 VALUES (1, NULL, 'a')",
       "UPDATE t1 SET v = 'b' WHERE id = 1"},
      {{"CREATE FUNCTION prune() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
        "DELETE FROM t1 WHERE id <= NEW.id - 2; RETURN NULL; END $$",
        "CREATE TRIGGER prune AFTER INSERT ON t1 FOR EACH ROW EXECUTE FUNCTION prune()"},
       "",
       "INSERT INTO t1 VALUES (11, NULL, 'x'), (12, NULL, 'y'), (13, NULL, 'z')"},
  };

  auto conn = ASSERT_RESULT(Connect());
  for (size_t i = 0; i < cases.size(); ++i) {
    const auto& c = cases[i];
    SCOPED_TRACE(c.statement);
    ASSERT_OK(conn.ExecuteFormat("CREATE SCHEMA c$0", i));
    ASSERT_OK(conn.ExecuteFormat("SET search_path TO c$0", i));
    ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, parent INT, v TEXT)"));
    ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, parent INT, v TEXT, v2 TEXT)"));
    ASSERT_OK(conn.Execute(
        "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
        "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.parent, src.v, src.v || '_x')::t2; END "
        "$$"));
    for (const auto& stmt : c.setup) {
      ASSERT_OK(conn.Execute(stmt));
    }
    if (!c.seed.empty()) {
      ASSERT_OK(conn.Execute(c.seed));
    }
    ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
    const auto source_before = ASSERT_RESULT(conn.FetchAllAsString(
        "SELECT t1::text AS r FROM t1 ORDER BY r"));

    ASSERT_NOK_STR_CONTAINS(conn.Execute(c.statement), kOnlineSchemaChangeReplayOrderError);

    ASSERT_EQ(ASSERT_RESULT(conn.FetchAllAsString("SELECT t1::text AS r FROM t1 ORDER BY r")),
              source_before);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), 0);
  }
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerReplayableOrders) {
  struct Case {
    std::vector<std::string> setup;
    std::string seed;
    std::string statement;
  };
  const std::vector<Case> cases = {
      {{},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, NULL, 'b')",
       "WITH d AS (DELETE FROM t1 WHERE id = 1 RETURNING id, v) "
       "INSERT INTO t1 SELECT id, NULL, v || '2' FROM d"},
      {{},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, NULL, 'b')",
       "WITH u AS (UPDATE t1 SET id = 102 WHERE id = 2 RETURNING v) "
       "INSERT INTO t1 SELECT 2, NULL, v || '2' FROM u"},
      {{},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, NULL, 'b')",
       "INSERT INTO t1 VALUES (1, NULL, 'a2'), (3, NULL, 'c') "
       "ON CONFLICT (id) DO UPDATE SET v = EXCLUDED.v"},
      {{"ALTER TABLE t1 ADD FOREIGN KEY (parent) REFERENCES t1 (id) ON DELETE SET NULL"},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, 1, 'b')",
       "DELETE FROM t1 WHERE id = 1"},
      // A cascade's rows join the transition table of the statement that caused it.
      {{"ALTER TABLE t1 ADD FOREIGN KEY (parent) REFERENCES t1 (id) "
        "ON UPDATE CASCADE ON DELETE CASCADE"},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, 1, 'b'), (3, 2, 'c'), (4, NULL, 'd')",
       "DELETE FROM t1 WHERE id = 1"},
      {{"ALTER TABLE t1 ADD FOREIGN KEY (parent) REFERENCES t1 (id) "
        "ON UPDATE CASCADE ON DELETE CASCADE"},
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, 1, 'b'), (3, 2, 'c'), (4, NULL, 'd')",
       "UPDATE t1 SET id = 10 WHERE id = 1"},
      // A statement trigger that sorts after the capture triggers fires after them.
      {{"CREATE FUNCTION touch() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN "
        "UPDATE t1 SET v = v || '_touched' WHERE id = 1; RETURN NULL; END $$",
        "CREATE TRIGGER zz_touch AFTER INSERT ON t1 FOR EACH STATEMENT "
        "EXECUTE FUNCTION touch()"},
       "",
       "INSERT INTO t1 VALUES (1, NULL, 'a'), (2, NULL, 'b')"},
  };

  auto conn = ASSERT_RESULT(Connect());
  for (size_t i = 0; i < cases.size(); ++i) {
    const auto& c = cases[i];
    SCOPED_TRACE(c.statement);
    ASSERT_OK(conn.ExecuteFormat("CREATE SCHEMA c$0", i));
    ASSERT_OK(conn.ExecuteFormat("SET search_path TO c$0", i));
    ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, parent INT, v TEXT)"));
    ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, parent INT, v TEXT, v2 TEXT)"));
    ASSERT_OK(conn.Execute(
        "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
        "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.parent, src.v, src.v || '_x')::t2; END "
        "$$"));
    for (const auto& stmt : c.setup) {
      ASSERT_OK(conn.Execute(stmt));
    }
    ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
    if (!c.seed.empty()) {
      ASSERT_OK(conn.Execute(c.seed));
    }

    ASSERT_OK(conn.Execute(c.statement));

    ASSERT_EQ(ASSERT_RESULT(TargetRows(&conn)), ASSERT_RESULT(ExpectedTargetRows(&conn)));
  }
}

// Command IDs restart in every transaction, including after a COMMIT in a DO block, so the first
// statements of a transaction must not be taken for statements nested in the previous one's. And
// consecutive statements, whose EStates can share an address, must not be taken for one statement.
TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerReplayOrderAcrossStatements) {
  constexpr int kIters = 30;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (int i = 1; i <= kIters; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, 'a')", i));
    ASSERT_OK(conn.ExecuteFormat("UPDATE t1 SET v = 'b' WHERE id = $0", i));
    ASSERT_OK(conn.ExecuteFormat("DELETE FROM t1 WHERE id = $0", i - 1));
  }
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM t1 WHERE id = $0", kIters));

  ASSERT_OK(conn.ExecuteFormat(
      "DO $$$$ BEGIN "
      "FOR i IN 1..$0 LOOP "
      "INSERT INTO t1 VALUES (i, 'c'); UPDATE t1 SET v = 'd' WHERE id = i; "
      "END LOOP; "
      "COMMIT; "
      "DELETE FROM t1 WHERE id = 1; "
      "END $$$$",
      kIters));

  ASSERT_EQ(ASSERT_RESULT(TargetRows(&conn)), ASSERT_RESULT(ExpectedTargetRows(&conn)));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), kIters - 1);
}

// YB retries a statement that hits a transaction conflict, and when nothing ran a
// CommandCounterIncrement since the failed attempt, the retry runs with its command ID. With
// yb_speculatively_execute_pl_statements the capture trigger does not flush, so the conflict on the
// shared row 0 can surface after the INSERT firing was replayed. The UPDATE firing of the retry
// must not be taken for a later firing of the failed attempt.
TEST_F_EX(PgMiniTest, OnlineSchemaChangeTriggerRetriedStatement, PgMiniTestFailOnConflict) {
  constexpr int kThreads = 4;
  constexpr int kIters = 100;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (0, 'seed')"));

  std::atomic<int> committed{0};
  std::atomic<int> failed{0};
  std::atomic<int> misordered{0};
  TestThreadHolder thread_holder;
  for (int t = 1; t <= kThreads; ++t) {
    thread_holder.AddThreadFunctor([this, t, &committed, &failed, &misordered] {
      auto thread_conn = ASSERT_RESULT(Connect());
      ASSERT_OK(thread_conn.Execute("SET yb_speculatively_execute_pl_statements = on"));
      for (int i = 0; i < kIters; ++i) {
        // Updates the shared row and inserts a new one, so each attempt fires both triggers.
        auto status = thread_conn.ExecuteFormat(
            "INSERT INTO t1 VALUES (0, 'v$0_$1'), ($2, 'n') "
            "ON CONFLICT (id) DO UPDATE SET v = EXCLUDED.v",
            t, i, 1000 * t + i);
        if (status.ok()) {
          ++committed;
        } else if (
            status.ToString().find(kOnlineSchemaChangeReplayOrderError) != std::string::npos) {
          LOG(WARNING) << "Retried upsert rejected: " << status;
          ++misordered;
        } else {
          ++failed;
        }
      }
    });
  }
  thread_holder.JoinAll();

  LOG(INFO) << "Upserts: " << committed << " committed, " << failed << " failed, " << misordered
            << " rejected as misordered";
  ASSERT_EQ(misordered, 0);
  ASSERT_GT(committed, 0);
  ASSERT_EQ(ASSERT_RESULT(TargetRows(&conn)), ASSERT_RESULT(ExpectedTargetRows(&conn)));
}

// The flush tests compare a statement's storage flushes with and without the capture triggers. The
// replay must add a constant number of flushes, not one per row. Row counts stay below
// ysql_session_max_batch_size, past which the buffer flushes on its own.
TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerFlushInsert) {
  constexpr int kNumRows = 500;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE base (id INT PRIMARY KEY, v TEXT)"));
  const auto base_single = ASSERT_RESULT(ExplainFlushes(
      &conn, "INSERT INTO base VALUES (-1, 'x')"));
  const auto base_bulk = ASSERT_RESULT(ExplainFlushes(&conn, Format(
      "INSERT INTO base SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows)));

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  const auto osc_single = ASSERT_RESULT(ExplainFlushes(
      &conn, "INSERT INTO t1 VALUES (-1, 'x')"));
  const auto osc_bulk = ASSERT_RESULT(ExplainFlushes(&conn, Format(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows)));

  LOG(INFO) << "[FLUSH] INSERT single: baseline=" << base_single << " osc=" << osc_single
            << " extra=" << (osc_single - base_single);
  LOG(INFO) << "[FLUSH] INSERT bulk(" << kNumRows << "): baseline=" << base_bulk
            << " osc=" << osc_bulk << " extra=" << (osc_bulk - base_bulk);

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), kNumRows + 1);
  ASSERT_LE(osc_bulk, base_bulk + 10) << "INSERT trigger should add O(1) flushes";
  ASSERT_LE(osc_bulk, 20);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerFlushUpdate) {
  constexpr int kNumRows = 500;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE base (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO base SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows));
  const auto base_bulk = ASSERT_RESULT(ExplainFlushes(
      &conn, "UPDATE base SET v = 'new_' || v"));

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  const auto osc_bulk = ASSERT_RESULT(ExplainFlushes(
      &conn, "UPDATE t1 SET v = 'new_' || v"));

  LOG(INFO) << "[FLUSH] UPDATE bulk(" << kNumRows << "): baseline=" << base_bulk
            << " osc=" << osc_bulk << " extra=" << (osc_bulk - base_bulk);

  ASSERT_LE(osc_bulk, base_bulk + 15) << "UPDATE trigger should add O(1) flushes";
  ASSERT_LE(osc_bulk, 30);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerFlushDelete) {
  constexpr int kNumRows = 500;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE base (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO base SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows));
  const auto base_bulk = ASSERT_RESULT(ExplainFlushes(&conn, "DELETE FROM base"));

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  const auto osc_bulk = ASSERT_RESULT(ExplainFlushes(&conn, "DELETE FROM t1"));

  LOG(INFO) << "[FLUSH] DELETE bulk(" << kNumRows << "): baseline=" << base_bulk
            << " osc=" << osc_bulk << " extra=" << (osc_bulk - base_bulk);

  ASSERT_LE(osc_bulk, base_bulk + 10) << "DELETE trigger should add O(1) flushes";
  ASSERT_LE(osc_bulk, 20);
}

TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerFlushSecondaryIndexes) {
  constexpr int kNumRows = 500;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE s0 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE d0 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION s0_to_d0(src s0) RETURNS d0 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::d0; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "s0", "d0", "s0_to_d0"));
  const auto no_idx = ASSERT_RESULT(ExplainFlushes(&conn, Format(
      "INSERT INTO s0 SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows)));

  ASSERT_OK(conn.Execute("CREATE TABLE s3 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE d3 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX ON d3 (v)"));
  ASSERT_OK(conn.Execute("CREATE INDEX ON d3 (v2)"));
  ASSERT_OK(conn.Execute("CREATE INDEX ON d3 (v2, id)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION s3_to_d3(src s3) RETURNS d3 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::d3; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "s3", "d3", "s3_to_d3"));
  const auto three_idx = ASSERT_RESULT(ExplainFlushes(&conn, Format(
      "INSERT INTO s3 SELECT i, 'v_' || i FROM generate_series(1, $0) i", kNumRows)));

  LOG(INFO) << "[FLUSH] INSERT bulk(" << kNumRows << ") target indexes: 0=" << no_idx
            << " 3=" << three_idx << " extra_per_idx=" << ((three_idx - no_idx) / 3.0);

  ASSERT_LE(three_idx, 30) << "secondary indexes add writes, not O(rows) flushes";
}

// With yb_speculatively_execute_pl_statements on, a PL/pgSQL loop of INSERTs batches into one
// flush, and the trigger must not turn that back into a flush per iteration.
TEST_F(PgMiniTestSingleNode, OnlineSchemaChangeTriggerFlushPlpgsqlLoop) {
  constexpr int kIters = 50;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE base (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION ins_base(n INT) RETURNS void LANGUAGE plpgsql AS $$ BEGIN "
      "FOR i IN 1..n LOOP INSERT INTO base VALUES (i, 'v'); END LOOP; END $$"));
  const auto base = ASSERT_RESULT(ExplainFlushes(&conn, Format("SELECT ins_base($0)", kIters)));

  ASSERT_OK(conn.Execute("CREATE TABLE t1 (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (id INT PRIMARY KEY, v TEXT, v2 TEXT)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION t1_to_t2(src t1) RETURNS t2 LANGUAGE plpgsql IMMUTABLE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.v, src.v || '_x')::t2; END $$"));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "t1", "t2", "t1_to_t2"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION ins_t1(n INT) RETURNS void LANGUAGE plpgsql AS $$ BEGIN "
      "FOR i IN 1..n LOOP INSERT INTO t1 VALUES (i, 'v'); END LOOP; END $$"));
  const auto osc = ASSERT_RESULT(ExplainFlushes(&conn, Format("SELECT ins_t1($0)", kIters)));

  LOG(INFO) << "[FLUSH] default: plpgsql loop of " << kIters << " single-row INSERTs: baseline="
            << base << " osc=" << osc << " extra=" << (osc - base);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), kIters);
  // Each firing ends with a flush while the setting is off. The plain loop flushes once per
  // iteration in release builds but once in total in debug builds, so bound the firings' share.
  ASSERT_LE(osc, base + kIters + 5) << "trigger should add at most one flush per firing";

  ASSERT_OK(conn.Execute("SET yb_speculatively_execute_pl_statements = on"));
  ASSERT_OK(conn.Execute("TRUNCATE base"));
  ASSERT_OK(conn.Execute("TRUNCATE t1"));
  ASSERT_OK(conn.Execute("TRUNCATE t2"));
  const auto base_spec =
      ASSERT_RESULT(ExplainFlushes(&conn, Format("SELECT ins_base($0)", kIters)));
  const auto osc_spec =
      ASSERT_RESULT(ExplainFlushes(&conn, Format("SELECT ins_t1($0)", kIters)));
  LOG(INFO) << "[FLUSH] speculative: plpgsql loop of " << kIters
            << " single-row INSERTs: baseline=" << base_spec << " osc=" << osc_spec
            << " extra=" << (osc_spec - base_spec);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")), kIters);
  ASSERT_LE(osc_spec, base_spec + 5)
      << "trigger flush defeats yb_speculatively_execute_pl_statements";
}

// ---- Part C: partitioned source / target ----

namespace {

constexpr auto kPartSrcDdl =
    "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, PRIMARY KEY (id, region, yr)) "
    "PARTITION BY LIST (region); "
    "CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu') PARTITION BY RANGE (yr); "
    "CREATE TABLE s_eu_2025 PARTITION OF s_eu FOR VALUES FROM (2025) TO (2026); "
    "CREATE TABLE s_eu_2026 PARTITION OF s_eu FOR VALUES FROM (2026) TO (2027); "
    "CREATE TABLE s_us PARTITION OF s FOR VALUES IN ('us')";

constexpr auto kPartDstDdl =
    "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, "
    "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region); "
    "CREATE TABLE d_eu PARTITION OF d FOR VALUES IN ('eu') PARTITION BY RANGE (yr); "
    "CREATE TABLE d_eu_2025 PARTITION OF d_eu FOR VALUES FROM (2025) TO (2026); "
    "CREATE TABLE d_eu_2026 PARTITION OF d_eu FOR VALUES FROM (2026) TO (2027); "
    "CREATE TABLE d_us PARTITION OF d FOR VALUES IN ('us'); "
    "CREATE INDEX d_v_idx ON d (v)";

constexpr auto kPartTransformDdl =
    "CREATE FUNCTION s_to_d(src s) RETURNS d LANGUAGE plpgsql IMMUTABLE SET search_path FROM "
    "CURRENT AS $$ BEGIN RETURN ROW(src.id, src.region, src.yr, src.v, src.v || '_x')::d; "
    "END $$";

}  // namespace

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTriggerPartitionedHierarchyDml) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(AttachOnlineSchemaChangeTriggersOnTree(&conn, "s", "d", "s_to_d"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO s VALUES (1, 'eu', 2025, 'a'), (2, 'eu', 2026, 'b'), (3, 'us', 2025, 'c')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_eu VALUES (4, 'eu', 2025, 'd'), (5, 'eu', 2026, 'e')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_us VALUES (6, 'us', 2026, 'f')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_eu_2026 VALUES (7, 'eu', 2026, 'g')"));

  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2025")), 2);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2026")), 3);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")), 2);

  ASSERT_OK(conn.Execute("UPDATE s SET v = 'A' WHERE id = 1"));
  ASSERT_OK(conn.Execute("UPDATE s_eu SET v = 'D' WHERE id = 4"));
  ASSERT_OK(conn.Execute("UPDATE s_us SET v = 'F' WHERE id = 6"));
  ASSERT_OK(conn.Execute("DELETE FROM s WHERE id = 2"));
  ASSERT_OK(conn.Execute("DELETE FROM s_eu WHERE id = 5"));
  ASSERT_OK(conn.Execute("DELETE FROM s_eu_2026 WHERE id = 7"));

  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM d WHERE v2 IS DISTINCT FROM (v || '_x')")), 0);

  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  const auto survivors = ASSERT_RESULT(FetchPartRows(&conn, "s"));
  for (const auto& [id, region, yr, v] : survivors) {
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(
        Format("SELECT id FROM d WHERE v = '$0'", v))), id);
  }
  for (const auto* gone : {"a", "b", "d", "e", "f", "g"}) {
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT count(*) FROM d WHERE v = '$0'", gone))), 0) << gone;
  }
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTriggerPartitionedCrossPartitionUpdate) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(AttachOnlineSchemaChangeTriggersOnTree(&conn, "s", "d", "s_to_d"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO s VALUES (1, 'eu', 2025, 'a'), (2, 'eu', 2026, 'b'), (3, 'us', 2025, 'c')"));

  ASSERT_OK(conn.Execute("UPDATE s SET region = 'us' WHERE region = 'eu'"));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")), 3);

  ASSERT_OK(conn.Execute("UPDATE s SET region = 'eu' WHERE id IN (1, 2)"));
  ASSERT_OK(conn.Execute("UPDATE s_eu SET yr = 2026 WHERE yr = 2025"));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2025")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2026")), 2);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")), 1);

  // PG refuses a cross-partition update addressed to a leaf.
  ASSERT_NOK(conn.Execute("UPDATE s_us SET region = 'eu' WHERE id = 3"));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTriggerPartitionedSourcePlainTarget) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr))"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(AttachOnlineSchemaChangeTriggersOnTree(&conn, "s", "d", "s_to_d"));

  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (1, 'eu', 2025, 'a'), (2, 'us', 2025, 'b')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_eu VALUES (3, 'eu', 2026, 'c')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_us VALUES (4, 'us', 2026, 'd')"));
  ASSERT_OK(conn.Execute("UPDATE s SET region = 'us' WHERE id = 1"));
  ASSERT_OK(conn.Execute("UPDATE s_eu_2026 SET v = 'C' WHERE id = 3"));
  ASSERT_OK(conn.Execute("DELETE FROM s_us WHERE id = 2"));

  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM d WHERE v2 IS DISTINCT FROM (v || '_x')")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTriggerPlainSourcePartitionedTarget) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, PRIMARY KEY (id, region, yr))"));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(CreateOnlineSchemaChangeTriggers(&conn, "s", "d", "s_to_d"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO s VALUES (1, 'eu', 2025, 'a'), (2, 'eu', 2026, 'b'), (3, 'us', 2025, 'c')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2025")), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2026")), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")), 1);

  ASSERT_OK(conn.Execute("UPDATE s SET region = 'us' WHERE id = 1"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2025")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")), 2);
  ASSERT_OK(conn.Execute("DELETE FROM s WHERE id = 2"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu")), 0);

  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT id FROM d WHERE v = 'a'")), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d WHERE v = 'b'")), 0);

  ASSERT_NOK(conn.Execute("INSERT INTO s VALUES (9, 'apac', 2025, 'z')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s WHERE id = 9")), 0);
}

// A column dropped on the root before a leaf is created gives the leaf
// different attnums.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangeTriggerPartitionedDivergentAttnums) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, junk INT, region TEXT, yr INT, v TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute("CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu')"));
  ASSERT_OK(conn.Execute("ALTER TABLE s DROP COLUMN junk"));
  ASSERT_OK(conn.Execute("CREATE TABLE s_us PARTITION OF s FOR VALUES IN ('us')"));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, junk INT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute("CREATE TABLE d_eu PARTITION OF d FOR VALUES IN ('eu')"));
  ASSERT_OK(conn.Execute("ALTER TABLE d DROP COLUMN junk"));
  ASSERT_OK(conn.Execute("CREATE TABLE d_us PARTITION OF d FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute("CREATE INDEX d_v_idx ON d (v)"));

  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(AttachOnlineSchemaChangeTriggersOnTree(&conn, "s", "d", "s_to_d"));

  // s_eu still carries the dropped column, s_us never had it.
  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (1, 'eu', 2025, 'a'), (2, 'us', 2025, 'b')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_eu VALUES (3, 'eu', 2026, 'c')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_us VALUES (4, 'us', 2026, 'd')"));
  ASSERT_OK(conn.Execute("UPDATE s_eu SET v = 'C' WHERE id = 3"));
  ASSERT_OK(conn.Execute("UPDATE s_us SET v = 'D' WHERE id = 4"));
  ASSERT_OK(conn.Execute("UPDATE s SET region = 'us' WHERE id = 1"));
  ASSERT_OK(conn.Execute("DELETE FROM s_us WHERE id = 2"));

  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")),
            ASSERT_RESULT(FetchPartRows(&conn, "d")));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM d WHERE v2 IS DISTINCT FROM (v || '_x')")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu")), 1);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")), 2);

  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT id FROM d WHERE v = 'C'")), 3);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT id FROM d WHERE v = 'D'")), 4);
  for (const auto* gone : {"b", "c", "d"}) {
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT count(*) FROM d WHERE v = '$0'", gone))), 0) << gone;
  }
}

// ---- Partitioned sources and targets: the full lifecycle ----

namespace {

Result<int64_t> CountOnlineSchemaChangeTriggers(PGConn* conn) {
  return conn->FetchRow<int64_t>(
      "SELECT count(*) FROM pg_trigger WHERE tgname LIKE 'yb_osc_%'");
}

Result<int64_t> CountUncomputed(PGConn* conn, const std::string& table) {
  return conn->FetchRow<int64_t>(Format(
      "SELECT count(*) FROM $0 WHERE v2 IS DISTINCT FROM (v || '_x')", table));
}

Status SeedPartRows(PGConn* conn, const std::string& table, int n, int first = 1) {
  return conn->ExecuteFormat(
      "INSERT INTO $0 SELECT i, CASE WHEN i % 2 = 0 THEN 'eu' ELSE 'us' END, "
      "2025 + (i / 2) % 2, 'v_' || i FROM generate_series($1, $2) AS i",
      table, first, first + n - 1);
}

// Begin src -> dst, run `dml` while the copy is in flight, wait for the copy, cut over, and return
// the rows src held just before the swap. The copy is throttled and chunked small so that the DML
// races it.
Result<PartRows> RunPartitionedOnlineSchemaChange(
    PGConn* conn, const std::string& src, const std::string& dst, const std::string& xf,
    const std::vector<std::string>& dml) {
  const auto old_rows_per_chunk = FLAGS_online_schema_change_backfill_rows_per_chunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) = 20;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 100;
  auto se = ScopeExit([old_rows_per_chunk] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) =
        old_rows_per_chunk;
  });

  RETURN_NOT_OK(ResultToStatus(BeginOnlineSchemaChange(conn, src, dst, xf)));
  RETURN_NOT_OK(WaitFor([conn]() -> Result<bool> {
    return VERIFY_RESULT(conn->FetchRow<int64_t>(
        "SELECT rows_done FROM pg_catalog.yb_online_schema_change "
        "ORDER BY job_id DESC LIMIT 1")) > 0;
  }, 60s, "Wait for the copy to report progress"));
  for (const auto& stmt : dml) {
    RETURN_NOT_OK(conn->Execute(stmt));
  }
  RETURN_NOT_OK(WaitForOnlineSchemaChange(conn, src, dst));
  auto expected = VERIFY_RESULT(FetchPartRows(conn, src));
  RETURN_NOT_OK(FinishOnlineSchemaChange(conn, src, dst));
  return expected;
}

void ExpectTopologyFenceBlocks(PGConn* conn, const std::string& ddl) {
  auto status = conn->Execute(ddl);
  ASSERT_NOK(status) << "expected the topology fence to reject: " << ddl;
  ASSERT_STR_CONTAINS(status.ToString(), "while online schema change job");
}

const std::vector<std::string> kPartDml = {
    "INSERT INTO s VALUES (1001, 'eu', 2025, 'r1'), (1002, 'us', 2026, 'r2')",
    "INSERT INTO s_eu VALUES (1003, 'eu', 2026, 'i1')",
    "INSERT INTO s_eu_2026 VALUES (1004, 'eu', 2026, 'l1')",
    "UPDATE s SET v = 'root_upd' WHERE id = 2",
    "UPDATE s_eu SET v = 'mid_upd' WHERE id = 4",
    "UPDATE s_us SET v = 'leaf_upd' WHERE id = 5",
    "DELETE FROM s WHERE id = 6",
    "DELETE FROM s_eu_2025 WHERE id = 8",
    "DELETE FROM s_us WHERE id = 7",
};

}  // namespace

// One source leaf is in its own tablespace.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedSameShape) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLESPACE osc_ts LOCATION '/data'"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, PRIMARY KEY (id, region, yr)) "
      "PARTITION BY LIST (region); "
      "CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu') PARTITION BY RANGE (yr); "
      "CREATE TABLE s_eu_2025 PARTITION OF s_eu FOR VALUES FROM (2025) TO (2026); "
      "CREATE TABLE s_eu_2026 PARTITION OF s_eu FOR VALUES FROM (2026) TO (2027) "
      "  TABLESPACE osc_ts; "
      "CREATE TABLE s_us PARTITION OF s FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 300));

  const auto expected =
      ASSERT_RESULT(RunPartitionedOnlineSchemaChange(&conn, "s", "d", "s_to_d", kPartDml));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s WHERE v2 IS NULL")), 0);
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 0);

  // Partitions follow their roots through the rename.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRows<std::string>(
      "SELECT relid::regclass::text FROM pg_partition_tree('s') WHERE isleaf ORDER BY 1")),
      (std::vector<std::string>{"d_eu_2025", "d_eu_2026", "d_us"}));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu_2025")),
            ASSERT_RESULT(conn.FetchRow<int64_t>(
                "SELECT count(*) FROM s WHERE region = 'eu' AND yr = 2025")));

  const auto leaf_counts = ASSERT_RESULT((conn.FetchRow<int32_t, int32_t>(
      "SELECT source_leaf_count, target_leaf_count FROM yb_online_schema_change "
      "ORDER BY job_id DESC LIMIT 1")));
  ASSERT_EQ(leaf_counts, (decltype(leaf_counts){3, 3}));
}

TEST_F(PgMiniOnlineSchemaChangeTest,
       OnlineSchemaChangePartitionedCrossPartitionUpdateDuringBackfill) {
  constexpr int kNumRows = 2000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 500;
  auto se = ScopeExit(
      [] { ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0; });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", kNumRows));

  const auto expected = ASSERT_RESULT(RunPartitionedOnlineSchemaChange(
      &conn, "s", "d", "s_to_d", {"UPDATE s SET region = 'us' WHERE region = 'eu'"}));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s")), kNumRows);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")), kNumRows);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_eu")), 0);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedRepartitionByDifferentKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY RANGE (yr); "
      "CREATE TABLE d_2025 PARTITION OF d FOR VALUES FROM (2025) TO (2026); "
      "CREATE TABLE d_2026 PARTITION OF d FOR VALUES FROM (2026) TO (2027); "
      "CREATE INDEX d_v_idx ON d (v)"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 200));

  const auto expected =
      ASSERT_RESULT(RunPartitionedOnlineSchemaChange(&conn, "s", "d", "s_to_d", kPartDml));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_2025")),
            ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s WHERE yr = 2025")));
}

// The target is keyed and partitioned on (id, yr) and leaves out the source's partition key region.
// A unique index on the partitioned source must include region, so only a target index can be the
// key witness. Moving rows between regions changes their source key but not their target key.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedTargetKeyWitness) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, PRIMARY KEY (id, yr)) "
      "PARTITION BY RANGE (yr); "
      "CREATE TABLE d_2025 PARTITION OF d FOR VALUES FROM (2025) TO (2026); "
      "CREATE TABLE d_2026 PARTITION OF d FOR VALUES FROM (2026) TO (2027)"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 300));

  const auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      "Add a witness on the target with: "
                      "CREATE UNIQUE INDEX ON public.d (id, yr) INCLUDE (region)");

  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX d_witness ON d (id, yr) INCLUDE (region)"));
  auto dml = kPartDml;
  dml.push_back("UPDATE s SET region = 'us' WHERE region = 'eu' AND id <= 100");
  const auto expected =
      ASSERT_RESULT(RunPartitionedOnlineSchemaChange(&conn, "s", "d", "s_to_d", dml));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_2025")),
            ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s WHERE yr = 2025")));
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedPlainSourcePartitionedTarget) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, PRIMARY KEY (id, region, yr))"));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 200));

  const auto expected = ASSERT_RESULT(RunPartitionedOnlineSchemaChange(
      &conn, "s", "d", "s_to_d",
      {"INSERT INTO s VALUES (1001, 'eu', 2026, 'r1')",
       "UPDATE s SET region = 'us' WHERE id = 2",
       "DELETE FROM s WHERE id = 3"}));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_us")),
            ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s WHERE region = 'us'")));
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedSourcePlainTarget) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr)); "
      "CREATE INDEX d_v_idx ON d (v)"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 200));

  const auto expected =
      ASSERT_RESULT(RunPartitionedOnlineSchemaChange(&conn, "s", "d", "s_to_d", kPartDml));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT relkind::text FROM pg_class WHERE oid = 's'::regclass")), "r");
}

// s_us is created after the root dropped a column and s_apac is attached with another column
// order, so both have attnums that differ from the root's.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedDivergentAttnums) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, junk INT, region TEXT, yr INT, v TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute("CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu')"));
  ASSERT_OK(conn.Execute("ALTER TABLE s DROP COLUMN junk"));
  ASSERT_OK(conn.Execute("CREATE TABLE s_us PARTITION OF s FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s_apac (v TEXT, yr INT, region TEXT, id INT, PRIMARY KEY (id, region, yr))"));
  ASSERT_OK(conn.Execute("ALTER TABLE s ATTACH PARTITION s_apac FOR VALUES IN ('apac')"));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, junk INT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute("CREATE TABLE d_eu PARTITION OF d FOR VALUES IN ('eu')"));
  ASSERT_OK(conn.Execute("ALTER TABLE d DROP COLUMN junk"));
  ASSERT_OK(conn.Execute("CREATE TABLE d_us PARTITION OF d FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute("CREATE TABLE d_apac PARTITION OF d FOR VALUES IN ('apac')"));
  ASSERT_OK(conn.Execute("CREATE INDEX d_v_idx ON d (v)"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));

  ASSERT_OK(conn.Execute(
      "INSERT INTO s VALUES (1, 'eu', 2025, 'a'), (2, 'us', 2025, 'b'), (3, 'apac', 2025, 'c')"));
  ASSERT_OK(conn.Execute("INSERT INTO s_apac (id, region, yr, v) VALUES (4, 'apac', 2026, 'd')"));

  const auto expected = ASSERT_RESULT(RunPartitionedOnlineSchemaChange(
      &conn, "s", "d", "s_to_d",
      {"INSERT INTO s_eu VALUES (5, 'eu', 2026, 'e')",
       "INSERT INTO s_apac (id, region, yr, v) VALUES (6, 'apac', 2026, 'f')",
       "UPDATE s_apac SET v = 'C' WHERE id = 3",
       "UPDATE s SET region = 'apac' WHERE id = 1",
       "DELETE FROM s_us WHERE id = 2"}));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d_apac")), 4);

  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT id FROM s WHERE v = 'C'")), 3);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s WHERE v = 'b'")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedResumeAcrossChunks) {
  constexpr int kNumRows = 1200;
  const auto old_rows_per_chunk = FLAGS_online_schema_change_backfill_rows_per_chunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) = 50;
  auto se = ScopeExit([old_rows_per_chunk] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) =
        old_rows_per_chunk;
  });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, "
      "PRIMARY KEY (id ASC, region ASC, yr ASC)) PARTITION BY LIST (region); "
      "CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu') PARTITION BY RANGE (yr); "
      "CREATE TABLE s_eu_2025 PARTITION OF s_eu FOR VALUES FROM (2025) TO (2026); "
      "CREATE TABLE s_eu_2026 PARTITION OF s_eu FOR VALUES FROM (2026) TO (2027); "
      "CREATE TABLE s_us PARTITION OF s FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id ASC, region ASC, yr ASC)) PARTITION BY LIST (region); "
      "CREATE TABLE d_eu PARTITION OF d FOR VALUES IN ('eu'); "
      "CREATE TABLE d_us PARTITION OF d FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", kNumRows));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "s", "d"));
  // Accumulated per chunk, so a chunk counted twice or skipped shows up here.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT rows_done FROM pg_catalog.yb_online_schema_change ORDER BY job_id DESC LIMIT 1")),
      kNumRows);
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s")), kNumRows);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn, "s")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedUnroutableRow) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region); "
      "CREATE TABLE d_eu PARTITION OF d FOR VALUES IN ('eu')"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 20));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  auto status = WaitForOnlineSchemaChange(&conn, "s", "d");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "failed");
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "failed");
  const auto failure = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT error_message FROM pg_catalog.yb_online_schema_change "
      "ORDER BY job_id DESC LIMIT 1"));
  ASSERT_STR_CONTAINS(failure, "no partition of relation \"d\"");
  // The failed job's trigger keeps firing until abort.
  ASSERT_NOK(conn.Execute("INSERT INTO s VALUES (21, 'us', 2025, 'x')"));
  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (22, 'eu', 2025, 'y')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d WHERE id = 22")), 1);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedTopologyFence) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 20));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE loose (id INT, region TEXT, yr INT, v TEXT, PRIMARY KEY (id, region, yr))"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "s", "d"));
  // 5 source nodes, 3 triggers each.
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 15);

  auto conn2 = ASSERT_RESULT(Connect());
  ExpectTopologyFenceBlocks(&conn2, "CREATE TABLE s_apac PARTITION OF s FOR VALUES IN ('apac')");
  ExpectTopologyFenceBlocks(
      &conn2, "CREATE TABLE s_eu_2027 PARTITION OF s_eu FOR VALUES FROM (2027) TO (2028)");
  ExpectTopologyFenceBlocks(&conn2, "CREATE TABLE d_apac PARTITION OF d FOR VALUES IN ('apac')");
  ExpectTopologyFenceBlocks(&conn2, "ALTER TABLE s ATTACH PARTITION loose FOR VALUES IN ('apac')");
  ExpectTopologyFenceBlocks(&conn2, "ALTER TABLE s DETACH PARTITION s_us");
  ExpectTopologyFenceBlocks(&conn2, "ALTER TABLE s_eu DETACH PARTITION s_eu_2026 CONCURRENTLY");
  ExpectTopologyFenceBlocks(&conn2, "ALTER TABLE d_eu DETACH PARTITION d_eu_2025");

  ASSERT_OK(AbortOnlineSchemaChange(&conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 0);
  ASSERT_OK(conn2.Execute("CREATE TABLE s_apac PARTITION OF s FOR VALUES IN ('apac')"));
  ASSERT_OK(conn2.Execute("ALTER TABLE s DETACH PARTITION s_apac"));
  ASSERT_OK(conn2.Execute("DROP TABLE s_apac"));

  ASSERT_OK(conn.Execute("TRUNCATE d"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "s", "d"));
  ExpectTopologyFenceBlocks(&conn2, "CREATE TABLE s_apac PARTITION OF s FOR VALUES IN ('apac')");
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 0);
  ASSERT_OK(conn2.Execute("CREATE TABLE s_apac PARTITION OF s FOR VALUES IN ('apac')"));
}

// A volatile transform is refused whether its volatile part computes a partition key column, which
// would misroute tombstones, or a column the online schema change never recomputes.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedVolatilePartitionKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION s_to_d_bad(src s) RETURNS d LANGUAGE plpgsql VOLATILE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, CASE WHEN random() < 2 THEN src.region ELSE "
      "'us' END, src.yr, src.v, src.v || '_x')::d; END $$"));
  auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d_bad"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must be IMMUTABLE");

  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION s_to_d_v2(src s) RETURNS d LANGUAGE plpgsql VOLATILE SET search_path "
      "FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.region, src.yr, src.v, "
      "gen_random_uuid()::text)::d; END $$"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d_v2"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must be IMMUTABLE");
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedOverlappingJobs) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION s_eu_to_d_eu(src s_eu) RETURNS d_eu LANGUAGE plpgsql IMMUTABLE SET "
      "search_path FROM CURRENT AS $$ BEGIN RETURN ROW(src.id, src.region, src.yr, src.v, "
      "src.v || '_x')::d_eu; END $$"));

  auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s_eu", "d_eu", "s_eu_to_d_eu"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "source table \"s_eu\" is a partition");
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s", "d_eu", "s_to_d"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "target table \"d_eu\" is a partition");

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "active online schema change job already exists");
  ASSERT_OK(AbortOnlineSchemaChange(&conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedTargetMustBeEmpty) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", 10));
  ASSERT_OK(conn.Execute("INSERT INTO d_eu_2026 VALUES (999, 'eu', 2026, 'stale', 'stale')"));

  auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "target table \"d\" is not empty");
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 0);
}

// The whole row must reach the transform in the root's column order on both paths, including for
// s_us, whose column order differs.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedWholeRowTransform) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, PRIMARY KEY (id, region, yr)) "
      "PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute("CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s_us (v TEXT, yr INT, region TEXT, id INT, PRIMARY KEY (id, region, yr))"));
  ASSERT_OK(conn.Execute("ALTER TABLE s ATTACH PARTITION s_us FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION s_to_d(src s) RETURNS d LANGUAGE plpgsql IMMUTABLE SET search_path FROM "
      "CURRENT AS $$ BEGIN RETURN ROW(src.id, src.region, src.yr, src.v, "
      "hash_record(src)::text)::d; END $$"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO s VALUES (1, 'eu', 2025, 'a'), (2, 'us', 2025, 'b'), (3, 'us', 2026, 'c')"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_OK(conn.Execute("INSERT INTO s_us (id, region, yr, v) VALUES (4, 'us', 2026, 'd')"));
  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (5, 'us', 2025, 'e')"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM d WHERE v2 IS DISTINCT FROM "
      "hash_record(ROW(id, region, yr, v)::s)::text")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d")), 5);
  ASSERT_OK(FinishOnlineSchemaChange(&conn, "s", "d"));
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedFailover) {
  constexpr int kNumRows = 6000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 500;
  auto se = ScopeExit(
      [] { ANNOTATE_UNPROTECTED_WRITE(FLAGS_backfill_index_rate_rows_per_sec) = 0; });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", kNumRows));
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).empty(); }, 30s,
      "Wait for CREATE INDEX to release delete-marker retention"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));

  // Three target leaves and the three leaf indexes of d_v_idx.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).size() == 6; }, 30s,
      "Wait for the online schema change backfill to pin every target leaf and its indexes"));

  ASSERT_OK(cluster_->mini_master(0)->Restart());
  ASSERT_OK(cluster_->mini_master(0)->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_EQ(TablesRetainingDeleteMarkers(cluster_.get()).size(), 6);

  ASSERT_OK(conn2.Execute("INSERT INTO s_eu VALUES (10001, 'eu', 2026, 'late')"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn2, "s", "d"));
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).empty(); }, 30s,
      "Wait for the online schema change backfill to release its delete-marker retention pins"));

  ASSERT_OK(FinishOnlineSchemaChange(&conn2, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(conn2.FetchRow<int64_t>("SELECT count(*) FROM s")), kNumRows + 1);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&conn2, "s")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedForeignKeyPromotion) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE parent (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO parent SELECT i FROM generate_series(1, 50) i"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, pid INT REFERENCES parent(id), "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region); "
      "CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu'); "
      "CREATE TABLE s_us PARTITION OF s FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, pid INT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region); "
      "CREATE TABLE d_eu PARTITION OF d FOR VALUES IN ('eu'); "
      "CREATE TABLE d_us PARTITION OF d FOR VALUES IN ('us')"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION s_to_d(src s) RETURNS d LANGUAGE plpgsql IMMUTABLE SET search_path FROM "
      "CURRENT AS $$ BEGIN RETURN ROW(src.id, src.region, src.yr, src.v, src.pid, src.v || "
      "'_x')::d; END $$"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO s SELECT i, CASE WHEN i % 2 = 0 THEN 'eu' ELSE 'us' END, 2025, 'v_' || i, i "
      "FROM generate_series(1, 20) i"));
  const auto conname = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT conname FROM pg_constraint WHERE conrelid = 's'::regclass AND contype = 'f'"));

  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_NOK(conn.Execute("INSERT INTO s VALUES (99, 'us', 2025, 'bad', 999)"));
  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (21, 'us', 2025, 'ok', 21)"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "s", "d"));

  const auto promoted = ASSERT_RESULT((conn.FetchRow<std::string, bool>(Format(
      "SELECT conname, convalidated FROM pg_constraint "
      "WHERE conrelid = 'd'::regclass AND contype = 'f' AND conname = '$0'", conname))));
  ASSERT_EQ(promoted, (decltype(promoted){conname, true}));

  ASSERT_OK(FinishOnlineSchemaChange(&conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM s")), 21);
  ASSERT_NOK(conn.Execute("INSERT INTO s VALUES (100, 'eu', 2025, 'bad', 999, 'bad_x')"));
  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (100, 'eu', 2025, 'ok', 1, 'ok_x')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_constraint c JOIN pg_class r ON r.oid = c.conrelid "
      "WHERE r.relname LIKE 'yb_osc_old_%' AND c.contype = 'f'")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedIncomingForeignKeyOnLeaf) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE child (id INT PRIMARY KEY, sid INT, region TEXT, yr INT, "
      "FOREIGN KEY (sid, region, yr) REFERENCES s_us (id, region, yr))"));

  auto status = ResultToStatus(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "must not be referenced by foreign keys");
  ASSERT_EQ(ASSERT_RESULT(CountOnlineSchemaChangeTriggers(&conn)), 0);
}

// Every chunk runs as the owner of the transform, which must be able to read each leaf.
TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedBackfillRunsAsTransformOwner) {
  constexpr int kNumRows = 100;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(kPartSrcDdl));
  ASSERT_OK(conn.Execute(kPartDstDdl));
  ASSERT_OK(conn.Execute(kPartTransformDdl));
  ASSERT_OK(SeedPartRows(&conn, "s", kNumRows));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_owner"));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_leaf_owner"));
  for (const auto* node : {"s", "s_eu", "s_eu_2025", "s_eu_2026"}) {
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 OWNER TO osc_owner", node));
  }
  ASSERT_OK(conn.Execute("ALTER TABLE s_us OWNER TO osc_leaf_owner"));
  ASSERT_OK(conn.Execute("ALTER FUNCTION s_to_d OWNER TO osc_owner"));
  ASSERT_OK(conn.Execute("GRANT INSERT ON d TO osc_owner"));

  ASSERT_NOK_STR_CONTAINS(
      BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"),
      "owner of transform function s_to_d cannot read table \"s_us\"");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_catalog.pg_yb_online_schema_change")), 0);

  ASSERT_OK(conn.Execute("GRANT SELECT ON s_us TO osc_owner"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  ASSERT_OK(WaitForOnlineSchemaChange(&conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM d")), kNumRows);
}

TEST_F(PgMiniOnlineSchemaChangeTest, OnlineSchemaChangePartitionedClonePartitions) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLESPACE osc_ts LOCATION '/data'"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE s (id INT, region TEXT, yr INT, v TEXT, PRIMARY KEY (id, region, yr)) "
      "PARTITION BY LIST (region); "
      "CREATE TABLE s_eu PARTITION OF s FOR VALUES IN ('eu') PARTITION BY RANGE (yr); "
      "CREATE TABLE s_eu_2025 PARTITION OF s_eu FOR VALUES FROM (2025) TO (2026); "
      "CREATE TABLE s_eu_2026 PARTITION OF s_eu FOR VALUES FROM (2026) TO (2027) "
      "  TABLESPACE osc_ts; "
      "CREATE TABLE s_us PARTITION OF s FOR VALUES IN ('us') SPLIT INTO 3 TABLETS; "
      "CREATE TABLE s_other PARTITION OF s DEFAULT; "
      "CREATE INDEX s_us_v_idx ON s_us (v)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE d (id INT, region TEXT, yr INT, v TEXT, v2 TEXT, "
      "PRIMARY KEY (id, region, yr)) PARTITION BY LIST (region)"));
  ASSERT_OK(conn.Execute(kPartTransformDdl));

  ASSERT_OK(conn.Execute(
      "DO $$ BEGIN PERFORM yb_online_schema_change_clone_partitions('s', 'd', '_new', true); END "
      "$$"));

  const auto pairs = ASSERT_RESULT((conn.FetchRows<std::string, bool, bool, bool, bool>(
      "SELECT sc.relname, "
      "       pg_get_expr(sc.relpartbound, sc.oid) = pg_get_expr(dc.relpartbound, dc.oid), "
      "       pg_get_partkeydef(sc.oid) IS NOT DISTINCT FROM pg_get_partkeydef(dc.oid), "
      "       sc.reltablespace = dc.reltablespace, "
      "       (SELECT num_tablets FROM yb_table_properties(sc.oid)) = "
      "         (SELECT num_tablets FROM yb_table_properties(dc.oid)) "
      "FROM pg_partition_tree('s') st "
      "JOIN pg_class sc ON sc.oid = st.relid "
      "JOIN pg_class dc ON dc.relname = sc.relname || '_new' "
      "WHERE st.level > 0 ORDER BY sc.relname")));
  ASSERT_EQ(pairs.size(), 5);
  for (const auto& [name, bound, partkey, tablespace, tablets] : pairs) {
    EXPECT_TRUE(bound && partkey && tablespace && tablets) << name;
  }
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT parentrelid::regclass::text FROM pg_partition_tree('d') "
      "WHERE relid = 's_eu_2026_new'::regclass")), "s_eu_new");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT tablename FROM pg_indexes WHERE indexname = 's_us_v_idx_new'")), "s_us_new");

  auto status = conn.Execute(
      "DO $$ BEGIN PERFORM yb_online_schema_change_clone_partitions('s', 'd'); END $$");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "already has partitions");

  ASSERT_OK(SeedPartRows(&conn, "s", 100));
  ASSERT_OK(conn.Execute("INSERT INTO s VALUES (101, 'apac', 2025, 'dflt')"));
  // The master releases the cloned index's own retain_delete_markers asynchronously. Each such
  // schema bump, like the job's pins below, is one more version the trigger's first write can trip
  // over (PG refreshes one table per retry), so let both settle before the DML.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).empty(); }, 30s,
      "Wait for CREATE INDEX to release delete-marker retention"));
  ASSERT_RESULT(BeginOnlineSchemaChange(&conn, "s", "d", "s_to_d"));
  // Four tablet-bearing target leaves and the cloned leaf-local index.
  ASSERT_OK(WaitFor(
      [this] { return TablesRetainingDeleteMarkers(cluster_.get()).size() == 5; }, 30s,
      "Wait for the online schema change backfill to pin every target leaf and the local index"));
  // The backend that created the clones caches their pre-pin schema versions, and the trigger's
  // write is not retried on a mismatch, so the DML runs on a fresh backend.
  auto dml_conn = ASSERT_RESULT(Connect());
  for (const auto& stmt : kPartDml) {
    ASSERT_OK(dml_conn.Execute(stmt));
  }
  ASSERT_OK(WaitForOnlineSchemaChange(&dml_conn, "s", "d"));
  const auto expected = ASSERT_RESULT(FetchPartRows(&dml_conn, "s"));
  ASSERT_OK(FinishOnlineSchemaChange(&dml_conn, "s", "d"));
  ASSERT_EQ(ASSERT_RESULT(FetchPartRows(&dml_conn, "s")), expected);
  ASSERT_EQ(ASSERT_RESULT(CountUncomputed(&dml_conn, "s")), 0);
  ASSERT_EQ(ASSERT_RESULT(dml_conn.FetchRow<int64_t>("SELECT count(*) FROM s_other_new")), 1);
}

// A volatile default for a column no index covers. DML replayed before the copy reaches its rows
// and chunks copied twice each compute their own value, and whichever write is newest wins.
TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyVolatileDefault) {
  constexpr int kNumRows = 3000;
  constexpr int kRowsPerChunk = 700;
  constexpr uint32_t kReplaysPerTablet = 2;

  const auto old_rows_per_chunk = FLAGS_online_schema_change_backfill_rows_per_chunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) = kRowsPerChunk;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_replay_chunks_per_tablet) =
      kReplaysPerTablet;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_pause_controller) = true;
  auto se = ScopeExit([old_rows_per_chunk] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_rows_per_chunk) =
        old_rows_per_chunk;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_replay_chunks_per_tablet) =
        0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_pause_controller) = false;
  });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT, v TEXT, PRIMARY KEY (id ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t SELECT i, 'v' || i FROM generate_series(1, $0) AS i", kNumRows));
  ASSERT_OK(conn.Execute("CREATE INDEX t_v_idx ON t (v)"));

  StringWaiterLogSink dropped_chunks("TEST: dropping the response of a chunk");

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this] {
    auto alter_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(alter_conn.Execute(
        "ALTER TABLE t CONCURRENTLY ADD COLUMN u UUID NOT NULL DEFAULT gen_random_uuid()"));
  });

  ASSERT_OK(WaitFor([&conn]() -> Result<bool> {
    auto rows = VERIFY_RESULT(conn.FetchRows<std::string>(
        "SELECT status FROM pg_catalog.yb_online_schema_change"));
    return !rows.empty() && rows[0] == "backfilling";
  }, 30s, "Wait for the job to begin"));

  // The controller is paused, so no chunk has run: every row below is replayed before it is
  // copied.
  auto dml_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(dml_conn.Execute("UPDATE t SET v = v || '!' WHERE id <= 100"));
  ASSERT_OK(dml_conn.Execute("DELETE FROM t WHERE id BETWEEN 101 AND 200"));
  ASSERT_OK(dml_conn.ExecuteFormat(
      "INSERT INTO t SELECT i, 'v' || i FROM generate_series($0, $1) AS i",
      kNumRows + 1, kNumRows + 100));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_pause_controller) = false;
  thread_holder.JoinAll();
  ASSERT_GE(dropped_chunks.GetEventCount(), kReplaysPerTablet);
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t")), kNumRows);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(DISTINCT u) FROM t")), kNumRows);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t WHERE id BETWEEN 101 AND 200")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE v IS DISTINCT FROM "
      "'v' || id || CASE WHEN id <= 100 THEN '!' ELSE '' END) FROM t")), 0);
  ASSERT_RESULT(conn.Fetch("SELECT yb_index_check('t_v_idx'::regclass)"));

  ASSERT_OK(conn.Execute("INSERT INTO t (id, v) VALUES (0, 'new')"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t WHERE id = 0 AND u IS NOT NULL")), 1);
}

// Expressions the change-capture trigger cannot replay are refused when the job begins, before
// anything is copied, and nothing is left behind.
TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyUnreplayableExpressionRefused) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 'a')"));
  ASSERT_OK(conn.Execute("CREATE INDEX t_lower_v_idx ON t (lower(v))"));
  ASSERT_OK(conn.Execute("CREATE SEQUENCE t_seq"));

  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("ALTER TABLE t CONCURRENTLY ADD COLUMN u UUID DEFAULT gen_random_uuid() UNIQUE"),
      "does not support a volatile expression for indexed column \"u\"");
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute(
          "ALTER TABLE t CONCURRENTLY ALTER COLUMN v TYPE TEXT USING (v || random()::text)"),
      "does not support a volatile expression for indexed column \"v\"");
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("ALTER TABLE t CONCURRENTLY ADD COLUMN c BIGINT DEFAULT nextval('t_seq')"),
      "does not support a sequence function in the expression for column \"c\"");
  // A stable default would not rewrite the table, but a stable USING expression does.
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute(
          "ALTER TABLE t CONCURRENTLY ALTER COLUMN v TYPE TEXT USING (v || now()::text)"),
      "does not support a stable expression for column \"v\"");
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute(
          "ALTER TABLE t CONCURRENTLY ADD COLUMN w TEXT DEFAULT gen_random_uuid()::text || now()"),
      "does not support a stable expression for column \"w\"");

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT last_value FROM t_seq")), 1);
  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("SELECT is_called FROM t_seq")));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_proc WHERE proname LIKE 'yb_osc%'")), 0);
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN u UUID NOT NULL DEFAULT gen_random_uuid()"));
}

// The rewritten key would make distinct rows collide in the target. Begin refuses it from the
// transform's body, so the statement fails at the start even with no row to copy.
TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyKeyChangeRefused) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id BIGINT PRIMARY KEY, v TEXT)"));

  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("ALTER TABLE t CONCURRENTLY ALTER COLUMN id TYPE BIGINT USING id + 1"),
      "cannot change the values of key column \"id\"");

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_proc WHERE proname LIKE 'yb_osc%'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_trigger WHERE tgrelid = 't'::regclass")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlySwapCutover) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (id INT PRIMARY KEY, v TEXT, n INT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 'v' || i, i * 2 FROM generate_series(1, 500) AS i"));
  ASSERT_OK(conn.Execute("CREATE INDEX t_v_idx ON t (v)"));

  const auto oid_before = ASSERT_RESULT(conn.FetchRow<PGOid>("SELECT 't'::regclass::oid"));

  const std::string alter_stmt =
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)";
  ASSERT_OK(conn.Execute(alter_stmt));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<PGOid>("SELECT 't'::regclass::oid")), oid_before);
  const auto how = ASSERT_RESULT((conn.FetchRow<std::string, std::string>(
      "SELECT command, cutover_mode FROM pg_catalog.yb_online_schema_change "
      "ORDER BY job_id DESC LIMIT 1")));
  ASSERT_EQ(how, (decltype(how){"ALTER TABLE CONCURRENTLY", "swap"}));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT query FROM pg_catalog.pg_yb_online_schema_change ORDER BY job_id DESC LIMIT 1")),
      alter_stmt);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE n IS DISTINCT FROM id * 20) FROM t")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT id FROM t WHERE v = 'v42'")), 42);
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_proc WHERE proname LIKE 'yb_osc%'")), 0);
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN w INT"));

  // DDL-atomicity verification maps the swapped-in DocDB table back to pg_class through its
  // pg_table_id, which must name t, not the dropped shadow.
  ASSERT_OK(conn.Execute(
      "ALTER TABLE t ADD COLUMN u2 UUID NOT NULL DEFAULT gen_random_uuid()"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE u2 IS NULL) FROM t")), 0);
  // A failing ALTER is rolled back by the verifier.
  ASSERT_NOK(conn.Execute("ALTER TABLE t ADD COLUMN nn INT NOT NULL"));
  ASSERT_OK(conn.FetchRow<int64_t>("SELECT count(*) FROM t"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_attribute WHERE attrelid = 't'::regclass AND attname = 'nn'")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyConcurrentDml) {
  constexpr int kNumRows = 2000;
  constexpr int kNumThreads = 4;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (id INT PRIMARY KEY, v TEXT, n INT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO t SELECT i, 'v' || i, i * 2 FROM generate_series(1, $0) AS i", kNumRows)));

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
      auto dml_conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        const auto key = RandomUniformInt(1, kNumRows);
        const auto value = RandomHumanReadableString(12);
        const auto op = RandomUniformInt(0, 2);
        if (op == 0) {
          WARN_NOT_OK(dml_conn.Execute(Format(
              "INSERT INTO t(id, v, n) VALUES ($0, '$1', $0 * 2) "
              "ON CONFLICT (id) DO UPDATE SET v = EXCLUDED.v", key + kNumRows, value)), "insert");
        } else if (op == 1) {
          WARN_NOT_OK(dml_conn.Execute(Format(
              "UPDATE t SET v = '$1' WHERE id = $0", key, value)), "update");
        } else {
          WARN_NOT_OK(dml_conn.Execute(Format("DELETE FROM t WHERE id = $0", key)), "delete");
        }
      }
    });
  }

  SleepFor(MonoDelta::FromMilliseconds(500 * kTimeMultiplier));
  ASSERT_OK(conn.Execute(
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)"));
  thread_holder.Stop();

  // Every row written before the cutover, whether copied or replayed, went through the USING
  // expression exactly once. The threads keep inserting until they are stopped, so a new row
  // written after the cutover has n = id * 2.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
      "SELECT count(*) FILTER (WHERE n IS DISTINCT FROM id * 20 AND "
      "(id <= $0 OR n IS DISTINCT FROM id * 2)) FROM t", kNumRows))), 0);
  const auto rows = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(DISTINCT id) FROM t")), rows);
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyNoRewriteFallback) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 'a')"));
  // A constant default does not rewrite the table.
  ASSERT_OK(conn.Execute("ALTER TABLE t CONCURRENTLY ADD COLUMN c INT DEFAULT 7"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT c FROM t WHERE id = 1")), 7);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyAlterColumnType) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, n INT)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, i * 2 FROM generate_series(1, 300) AS i"));
  ASSERT_OK(conn.Execute(
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT format_type(atttypid, atttypmod) FROM pg_attribute "
      "WHERE attrelid = 't'::regclass AND attname = 'n'")), "bigint");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT n FROM t WHERE id = 5")), 100);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE n <> id * 20) FROM t")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyRejections) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 'a')"));

  ASSERT_OK(conn.Execute("BEGIN"));
  auto status = conn.Execute("ALTER TABLE t CONCURRENTLY ALTER COLUMN v TYPE VARCHAR(10)");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "cannot run inside a transaction block");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // Without a primary key the shadow would be keyed by ybrowid, which replay cannot recompute.
  status = conn.Execute("ALTER TABLE t CONCURRENTLY DROP CONSTRAINT t_pkey");
  ASSERT_NOK(status);

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN w INT"));
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyAlterIndexedColumnType) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, n INT) SPLIT INTO 2 TABLETS"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, i * 2 FROM generate_series(1, 300) AS i"));
  ASSERT_OK(conn.Execute("CREATE INDEX t_n_idx ON t (n)"));

  ASSERT_OK(conn.Execute(
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)"));

  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT format_type(atttypid, atttypmod) FROM pg_attribute "
      "WHERE attrelid = 't_n_idx'::regclass AND attnum = 1")), "bigint");
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT id FROM t WHERE n = 100")), 5);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM t WHERE n BETWEEN 0 AND 6000")), 300);
  ASSERT_OK(conn.Execute("RESET enable_seqscan"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE n <> id * 20) FROM t")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN w INT"));
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyFailedCutoverIsRetried) {
  constexpr auto kAlter =
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT, n INT)"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 'v' || i, i * 2 FROM generate_series(1, 100) AS i"));
  ASSERT_OK(conn.Execute("CREATE INDEX t_v_idx ON t (v)"));

  ASSERT_OK(conn.Execute("SET yb_test_fail_osc_cutover = true"));
  auto status = conn.Execute(kAlter);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "yb_test_fail_osc_cutover is set");
  ASSERT_STR_CONTAINS(
      status.ToString(), "re-issue the same ALTER TABLE ... CONCURRENTLY statement");
  ASSERT_OK(conn.Execute("RESET yb_test_fail_osc_cutover"));

  // The cutover rolled back with its transaction, status change included.
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "ready");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT format_type(atttypid, atttypmod) FROM pg_attribute "
      "WHERE attrelid = 't'::regclass AND attname = 'n'")), "integer");

  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn2.Execute("INSERT INTO t VALUES (101, 'v101', 202)"));

  // Only the same statement resumes the job, compared by its parse tree, not its text.
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 2)"),
      "another online schema change job is in progress on table \"t\"");
  ASSERT_OK(conn.Execute(
      "alter table t concurrently\n  alter column n type bigint using (n::bigint * 10)"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE n IS DISTINCT FROM id * 20) FROM t")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT id FROM t WHERE v = 'v101'")), 101);
}

// A cancel during the copy rolls the job back, as a failed copy does, and leaves nothing behind.
TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyCancelRollsBack) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_pause_controller) = true;
  auto se = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_pause_controller) = false;
  });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, n INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 100) AS i"));

  auto alter_conn = ASSERT_RESULT(Connect());
  const auto alter_pid = ASSERT_RESULT(alter_conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&alter_conn] {
    ASSERT_NOK_STR_CONTAINS(
        alter_conn.Execute(
            "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)"),
        "canceling statement due to user request");
  });

  ASSERT_OK(WaitFor([&conn]() -> Result<bool> {
    auto rows = VERIFY_RESULT(conn.FetchRows<std::string>(
        "SELECT status FROM pg_catalog.yb_online_schema_change"));
    return !rows.empty() && rows[0] == "backfilling";
  }, 30s, "Wait for the job to begin"));
  ASSERT_TRUE(ASSERT_RESULT(
      conn.FetchRow<bool>(Format("SELECT pg_cancel_backend($0)", alter_pid))));
  thread_holder.JoinAll();

  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "aborted");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_proc WHERE proname LIKE 'yb_osc%'")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_trigger WHERE tgrelid = 't'::regclass")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT format_type(atttypid, atttypmod) FROM pg_attribute "
      "WHERE attrelid = 't'::regclass AND attname = 'n'")), "integer");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_pause_controller) = false;
  ASSERT_OK(conn.Execute(
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE n <> id * 10) FROM t")), 0);
}

// A begin that fails leaves the shadow table without a job. The next statement on the table drops
// it and starts over.
TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyDropsOrphanShadow) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, n INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 100) AS i"));
  const auto oid = ASSERT_RESULT(conn.FetchRow<PGOid>("SELECT 't'::regclass::oid"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE yb_osc_shadow_$0 (id INT PRIMARY KEY)", oid));

  ASSERT_OK(conn.Execute(
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)"));
  ASSERT_EQ(ASSERT_RESULT(LatestOnlineSchemaChangeJobStatus(&conn)), "finished");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE n <> id * 10) FROM t")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);
}

TEST_F(PgMiniOnlineSchemaChangeTest, AlterConcurrentlyAsTableOwner) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE ROLE osc_owner"));
  ASSERT_OK(conn.Execute("CREATE ROLE osc_other"));
  ASSERT_OK(conn.Execute("GRANT CREATE ON SCHEMA public TO osc_owner"));

  ASSERT_OK(conn.Execute("SET ROLE osc_owner"));
  ASSERT_OK(conn.Execute("CREATE TABLE t (id INT PRIMARY KEY, n INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 200) AS i"));
  ASSERT_OK(conn.Execute(
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE BIGINT USING (n::bigint * 10)"));
  ASSERT_OK(conn.Execute(
      "ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE NUMERIC USING (n::numeric)"));
  ASSERT_OK(conn.Execute("RESET ROLE"));

  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change WHERE status = 'finished'")), 2);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT format_type(atttypid, atttypmod) FROM pg_attribute "
      "WHERE attrelid = 't'::regclass AND attname = 'n'")), "numeric");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FILTER (WHERE n <> id * 10) FROM t")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname LIKE 'yb_osc%'")), 0);

  ASSERT_OK(conn.Execute("SET ROLE osc_other"));
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE TEXT"),
      "must be owner of table t");
  ASSERT_OK(conn.Execute("RESET ROLE"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change")), 2);

  // yb_db_admin may ALTER any table, so it may also ALTER it concurrently.
  ASSERT_OK(conn.Execute("CREATE ROLE osc_admin"));
  ASSERT_OK(conn.Execute("GRANT yb_db_admin TO osc_admin"));
  ASSERT_OK(conn.Execute("SET ROLE osc_admin"));
  ASSERT_OK(conn.Execute("ALTER TABLE t CONCURRENTLY ALTER COLUMN n TYPE TEXT"));
  ASSERT_OK(conn.Execute("RESET ROLE"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_yb_online_schema_change WHERE status = 'finished'")), 3);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT pg_get_userbyid(relowner) FROM pg_class WHERE oid = 't'::regclass")), "osc_owner");
}

}  // namespace yb::pgwrapper
