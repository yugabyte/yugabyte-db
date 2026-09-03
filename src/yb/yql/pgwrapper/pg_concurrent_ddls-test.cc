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

#include "yb/client/client-test-util.h"
#include "yb/common/ddl_mode-test-util.h"
#include "yb/integration-tests/external_mini_cluster_fs_inspector.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/ysql_binary_runner.h"

DECLARE_bool(ysql_enable_auto_analyze);

using namespace std::literals;

namespace yb::pgwrapper {

class PgConcurrentDDLsTest : public LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    ToggleDDLMode(opts->extra_tserver_flags, /* use_legacy = */ false);
    ToggleDDLMode(opts->extra_master_flags, /* use_legacy = */ false);
    opts->extra_master_flags.emplace_back(
        "--master_ysql_operation_lease_ttl_ms=10000");
  }

  int GetNumTabletServers() const override {
    return 3;
  }
};

// With object locking enabled, analyze cannot always run in parallel to index creation because of
// the following deadlock issue
// CREATE INDEX                                               ANALYZE
// --------------                                             -------
// Phase 1:
// - create index table
// - acquire ShareUpdateExclusiveLock session lock
//   on parent table
//                                                            - ANALYZE gets blocked on
//                                                              ShareUpdateExclusiveLock <waiting>
// Phase 2:
// - wait for all backends to catch up to the latest
// - catalog version <waiting>
//
// This results in a deadlock, but the deadlock itself isn't captured in YB's deadlock graph.
//
// TODO(#27119): TBD if this issue goes away after addressing the GH.
//
// Running the test in release mode alone for now since it FATALs with a check failure in other
// build types.
#ifdef NDEBUG
TEST_F(PgConcurrentDDLsTest, CreateIndexAndConcurrentAnalyze) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(0, 10), 0"));
  yb::TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
    auto analyze_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(analyze_conn.Execute("SET statement_timeout=\'20s\'"));
    while (!stop.load()) {
      auto status = analyze_conn.Execute("ANALYZE test");
      LOG(INFO) << "Analyze returned status " << status;
      ASSERT_TRUE(
          status.ok() ||
          (status.IsNetworkError() && status.message().ToBuffer().find("Timed out")));
    }
  });

  for (int i = 0; i < 5; i++) {
    LOG(INFO) << "Creating index " << i;
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX idx_$0 ON test(k)", i));
  }

  thread_holder.Stop();
  thread_holder.JoinAll();
}
#endif

TEST_F(PgConcurrentDDLsTest, WholerowRaceCondition) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test_wholerow (k INT PRIMARY KEY, v INT, v2 INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_wholerow VALUES (1, 100, 200)"));

  TestThreadHolder thread_holder;

  thread_holder.AddThreadFunctor([this] {
    auto dml_conn = ASSERT_RESULT(Connect());

    // Set GUCs to induce the race
    ASSERT_OK(dml_conn.Execute("SET yb_test_sleep_before_executor_start_ms = 15000"));

    // Prepare statement so it plans without any secondary indexes.
    // We use a non-PK condition so it does a SeqScan, ensuring yb_fetch_target_tuple is true.
    ASSERT_OK(dml_conn.Execute("PREPARE my_dml AS DELETE FROM test_wholerow WHERE v = 100"));

    // Execute the statement. The test GUC will cause it to sleep inside standard_ExecutorStart
    // for 15 seconds (between planning and actual execution).
    // Meanwhile, the main thread creates an index and bumps the catalog version.
    // Upon waking up and proceeding, the backend will process background catalog invalidation
    // messages from the tablet server (due to object locking/heartbeats), causing it to
    // realize the schema changed.
    // It should throw schema version mismatch.
    auto status = dml_conn.Execute("EXECUTE my_dml");

    LOG(INFO) << "Execute returned status " << status;
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.ToString(), "schema version mismatch");
  });

  // Wait a little bit to ensure the DML thread starts executing and goes to sleep
  std::this_thread::sleep_for(1s);

  // Session 2: DDL
  auto ddl_conn = ASSERT_RESULT(Connect());

  // Disable wait so that CREATE INDEX CONCURRENTLY finishes instantly
  // instead of hanging while waiting for the sleeping DML transaction.
  ASSERT_OK(ddl_conn.Execute("SET yb_disable_wait_for_backends_catalog_version = true"));

  LOG(INFO) << "Creating concurrent index...";
  ASSERT_OK(ddl_conn.Execute("CREATE INDEX CONCURRENTLY idx_wholerow ON test_wholerow(v2)"));
  LOG(INFO) << "Created concurrent index successfully.";

  thread_holder.Stop();
  thread_holder.JoinAll();
}

// Test for #32080.
//
// When object locking is enabled, the catalog version mismatch check is not required
// since object locking and invalidation messages ensure that any concurrent DML/ DDL
// reads the latest data.
//
// Before the fix, a concurrent transaction will result in a  MISMATCHED_SCHEMA ("the
// catalog snapshot used for this transaction has been invalidated") error.
TEST_F(PgConcurrentDDLsTest, CatalogVersionCheckDisabled) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE ctas_src (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO ctas_src SELECT s, s FROM generate_series(1, 2000) AS s"));

  // An entirely unrelated table + index. Only the index will be renamed.
  ASSERT_OK(conn.Execute("CREATE TABLE unrelated (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX unrelated_idx ON unrelated(v)"));

  // Delay every transactional read so the paginated scan reliably spans the
  // concurrent index rename.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_transactional_read_delay_ms", "800"));

  TestThreadHolder thread_holder;
  Status victim_status;
  thread_holder.AddThreadFunctor([this, &victim_status] {
    auto victim_conn = ASSERT_RESULT(Connect());
    // Small fetch limit => the CTAS source scan issues many read RPCs over time, each
    // carrying the catalog version pinned when the scan began.
    ASSERT_OK(victim_conn.Execute("SET yb_fetch_row_limit = 100"));
    victim_status =
        victim_conn.Execute("CREATE TABLE ctas_dst AS SELECT * FROM ctas_src");
    LOG(INFO) << "CTAS returned status: " << victim_status;
  });

  // Let the scan get a few read RPCs in, then bump the catalog version with a breaking
  // ALTER INDEX ... RENAME on the unrelated index.
  SleepFor(2s);
  LOG(INFO) << "Renaming unrelated index concurrently with the scan";
  ASSERT_OK(conn.Execute("ALTER INDEX unrelated_idx RENAME TO unrelated_idx_renamed"));

  thread_holder.JoinAll();

  // Renaming an unrelated index must not abort the scan: with object locking the
  // catalog-version (last breaking version) check is disabled.
  ASSERT_OK(victim_status);
}

TEST_F(PgConcurrentDDLsTest, ConcurrentCreateIndex) {
  auto kNumTables = 2;
  // TODO(#30015): If multiple threads create indexes on the same table, the "only a single oid is
  // allowed in BACKFILL INDEX" error is thrown.
  auto kNumThreads = 2;
  auto kNumIndexesPerThread = 4;

  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i < kNumTables; i++) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test$0(k INT PRIMARY KEY, v INT)", i));
  }

  std::vector<yb::TestThreadHolder> thread_holders(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    thread_holders[i].AddThreadFunctor([this, i, kNumTables, kNumIndexesPerThread] {
      auto conn = ASSERT_RESULT(Connect());
      for (int j = 0; j < kNumIndexesPerThread; j++) {
        ASSERT_OK(conn.ExecuteFormat("CREATE INDEX idx_$1_$2 ON test$0(k)", i%kNumTables, i, j));
      }
    });
  }

  for (int i = 0; i < kNumThreads; i++) {
    thread_holders[i].JoinAll();
  }
}

class PgConcurrentCreateIndexWithSlowOtherDDLTest : public PgConcurrentDDLsTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Scale RPC/operation budgets by kTimeMultiplier so that under sanitizers, where a freshly
    // forked PG backend can take several seconds to reach ReadyForQuery, the master->tserver
    // probes do not time out and trigger an avalanche of retried local PG connections (and
    // therefore an avalanche of newly forked backends).
    options->extra_master_flags.push_back(Format(
        "--ysql_yb_wait_for_backends_catalog_version_timeout=$0", 20000 * kTimeMultiplier));
    options->extra_master_flags.push_back(Format(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms=$0",
        10000 * kTimeMultiplier));
    options->extra_master_flags.push_back(Format(
        "--wait_for_ysql_backends_catalog_version_master_tserver_rpc_timeout_ms=$0",
        5000 * kTimeMultiplier));
    options->extra_master_flags.push_back(Format(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms=$0",
        3000 * kTimeMultiplier));
    options->extra_tserver_flags.push_back(Format(
        "--ysql_yb_wait_for_backends_catalog_version_timeout=$0", 20000 * kTimeMultiplier));
    options->extra_tserver_flags.push_back(Format(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms=$0",
        10000 * kTimeMultiplier));
    options->extra_tserver_flags.push_back(Format(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms=$0",
        3000 * kTimeMultiplier));

    PgConcurrentDDLsTest::UpdateMiniClusterOptions(options);
  }
};

// https://github.com/yugabyte/yugabyte-db/issues/30114
class PgConcurrentCreateIndexWithSlowBackfillTest :
    public PgConcurrentCreateIndexWithSlowOtherDDLTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // slow down the backfill rate.
    options->extra_tserver_flags.push_back("--backfill_index_rate_rows_per_sec=1");
    PgConcurrentCreateIndexWithSlowOtherDDLTest::UpdateMiniClusterOptions(options);
  }
};

TEST_F(PgConcurrentCreateIndexWithSlowBackfillTest, SlowBackfillTest) {
  auto kNumTables = 2;

  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i < kNumTables; i++) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test$0(k INT PRIMARY KEY, v INT)", i));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test$0 SELECT s, s FROM generate_series(1, 100) AS s", i));
  }
  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumTables; i++) {
    thread_holder.AddThreadFunctor([this, i] {
      // Stagger the concurrent create index DDLs starting time one after another to
      // trigger lagging catalog version backends: if a backend is doing index backfill
      // it will not be able to refresh its catalog version.
      SleepFor(i * 10s);
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.ExecuteFormat("CREATE INDEX CONCURRENTLY ON test$0(v)", i));
    });
  }
  thread_holder.JoinAll();
  for (int i = 0; i < kNumTables; i++) {
    ASSERT_OK((conn.FetchFormat("SELECT yb_index_check('test$0_v_idx'::regclass)", i)));
  }
}

// This is the SlowBackfillTest for partitioned table, where we can allow concurrent
// execution of CREATE INDEX on child partitions to speed up indexing process.
TEST_F(PgConcurrentCreateIndexWithSlowBackfillTest, PartitionedSlowBackfillTest) {
  // TSAN amplifies catalog-cache and cluster startup overhead. 50 rows still keep
  // the 1 row/sec backfill active across the 10s stagger with three tablets.
  constexpr int kRowsPerPartition = NonTsanVsTsan(100, 50);
  auto setup_script =
    Format(R"#(
CREATE TABLE parent_partition(c1 int, c2 int) PARTITION BY RANGE (c1);
CREATE TABLE child_part_1 PARTITION OF parent_partition FOR VALUES FROM (0) to ($0);
CREATE TABLE child_part_2 PARTITION OF parent_partition FOR VALUES FROM ($1) to ($2);
CREATE INDEX parent_index ON ONLY parent_partition (c1, c2);
-- Insert rows of data into both partitions.
INSERT INTO parent_partition (c1, c2)
SELECT
    CASE
        WHEN i <= $0 THEN i - 1
        ELSE i
    END,
    (random() * 1000)::int
FROM generate_series(1, $3) AS i;
        )#",
           kRowsPerPartition,
           kRowsPerPartition + 1,
           2 * kRowsPerPartition + 1,
           2 * kRowsPerPartition);
  // Verify each partition has the expected number of rows.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(setup_script));
  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM child_part_1"));
  ASSERT_EQ(count, kRowsPerPartition);
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM child_part_2"));
  ASSERT_EQ(count, kRowsPerPartition);
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM parent_partition"));
  ASSERT_EQ(count, 2 * kRowsPerPartition);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.Execute("CREATE INDEX child_part_1_index ON child_part_1 (c1, c2)"));
    });
  thread_holder.AddThreadFunctor([this] {
      auto conn = ASSERT_RESULT(Connect());
      // Stagger the concurrent create index DDLs starting time one after another to
      // trigger lagging catalog version backends: if a backend is doing index backfill
      // it will not be able to refresh its catalog version.
      SleepFor(10s);
      ASSERT_OK(conn.Execute("CREATE INDEX child_part_2_index ON child_part_2 (c1, c2)"));
    });
  thread_holder.JoinAll();
  ASSERT_OK(conn.Execute("ALTER INDEX parent_index ATTACH PARTITION child_part_1_index"));
  ASSERT_OK(conn.Execute("ALTER INDEX parent_index ATTACH PARTITION child_part_2_index"));
  ASSERT_OK((conn.Fetch("SELECT yb_index_check('parent_index'::regclass)")));
  ASSERT_OK((conn.Fetch("SELECT yb_index_check('child_part_1_index'::regclass)")));
  ASSERT_OK((conn.Fetch("SELECT yb_index_check('child_part_2_index'::regclass)")));
}

// Parameterized test class for https://github.com/yugabyte/yugabyte-db/issues/30219
class PgConcurrentCreateIndexWithSlowRefreshMatViewTest :
    public PgConcurrentCreateIndexWithSlowOtherDDLTest,
    public ::testing::WithParamInterface<bool> {
 public:
  int GetNumMasters() const override { return 1; }
  int GetNumTabletServers() const override { return 1; }
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->replication_factor = 1;
    PgConcurrentCreateIndexWithSlowOtherDDLTest::UpdateMiniClusterOptions(options);
    // TODO(#32565): remove this override once backfill pins history at its read time.  The test
    // harness runs with timestamp_history_retention_interval_sec=0 to surface stale-read-point
    // bugs.  Here that canary trips on a known, deferred limitation instead: the minutes-long
    // CREATE UNIQUE INDEX backfill of slow_mv reads at a fixed read time, and a full compaction of
    // slow_mv between two backfill reads advances the history cutoff past that read time, failing
    // the backfill with "Snapshot too old".  Raise retention to at least the test's entire
    // lifetime, even under a raised YB_TEST_TIMEOUT, so the cutoff can never reach a read time
    // chosen during the test.
    options->extra_tserver_flags.push_back("--timestamp_history_retention_interval_sec=3600");
  }
};

TEST_P(PgConcurrentCreateIndexWithSlowRefreshMatViewTest,
       YB_DISABLE_TEST_IN_SANITIZERS(SlowRefreshMatViewTest)) {
  bool is_concurrent_refresh = GetParam();
  auto conn = ASSERT_RESULT(Connect());

  // 1. Setup: Create a view that is naturally slow to refresh.
  // We use a cross join on generate_series to create a high CPU/executor load.
  ASSERT_OK(conn.Execute("CREATE TABLE base_table(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO base_table VALUES (1, 1)"));

  // The refresh only needs to run long enough to still be an in-flight "lagging" backend while the
  // concurrent CREATE INDEX runs (which takes tens of seconds in the slowest build). In release the
  // executor is fast enough that 2000*2000 = 4M rows fits within the test timeout, but in debug and
  // fastdebug builds materializing 4M rows for both the initial CREATE and the later REFRESH cannot
  // finish in time, so use a much smaller inner dimension there. Keeping the inner bound below
  // 10000 preserves uniqueness of unique_key = s1 * 10000 + s2 (required by the concurrent index
  // variant).
#ifdef NDEBUG
  constexpr int kInnerSeriesMax = 2000;
#else
  constexpr int kInnerSeriesMax = 250;
#endif
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE MATERIALIZED VIEW slow_mv AS "
      "SELECT (s1 * 10000 + s2) AS unique_key, t1.v "
      "FROM base_table t1 "
      "CROSS JOIN generate_series(1, 2000) s1 "
      "CROSS JOIN generate_series(1, $0) s2", kInnerSeriesMax));

  LOG(INFO) << "Created slow_mv";
  if (is_concurrent_refresh) {
    ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX slow_mv_idx ON slow_mv(unique_key)"));
    LOG(INFO) << "Created slow_mv_idx";
  }

  // 2. Setup table for the actual Index DDL
  ASSERT_OK(conn.Execute("CREATE TABLE test_table(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table SELECT s, s FROM generate_series(1, 100) AS s"));

  // 3. Setup the same scenario as from a real use case script that executes
  // REFRESH MATERIALIZED VIEW from a procedure.
  auto refresh_mv_cmd = is_concurrent_refresh ?
      "REFRESH MATERIALIZED VIEW CONCURRENTLY slow_mv"s :
      "REFRESH MATERIALIZED VIEW NONCONCURRENTLY slow_mv"s;
  auto create_proc_cmd = Format(
    R"#(
CREATE OR REPLACE PROCEDURE test_proc()
LANGUAGE plpgsql
AS $$$$
BEGIN
  $0;
END;
$$$$)#", refresh_mv_cmd);
  ASSERT_OK(conn.Execute(create_proc_cmd));

  TestThreadHolder thread_holder;

  // Thread 1: The "Lagging" Backend (Materialized View Refresh)
  thread_holder.AddThreadFunctor([this, is_concurrent_refresh] {
    auto conn = ASSERT_RESULT(Connect());

    // Test the same scenario as from a real use case script.
    auto cmd =
    R"#(
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SET client_min_messages TO log;
CALL test_proc();
COMMIT;
        )#";
    auto mode = is_concurrent_refresh ? "CONCURRENTLY" : "NONCONCURRENTLY";

    ASSERT_OK(conn.Execute("SET yb_read_after_commit_visibility = 'relaxed'"));
    LOG(INFO) << "Starting REFRESH MATERIALIZED VIEW " << mode;
    ASSERT_OK(conn.Execute(cmd));
    LOG(INFO) << "Completed REFRESH MATERIALIZED VIEW " << mode;
  });

  // Thread 2: The "Blocked" Backend (Concurrent Index)
  thread_holder.AddThreadFunctor([this] {
    // Give the refresh thread a head start to ensure it is the "lagging" backend
    SleepFor(5s);
    auto conn = ASSERT_RESULT(Connect());

    // This used to get blocked waiting for Thread 1 to finish.
    LOG(INFO) << "Starting CREATE INDEX CONCURRENTLY";
    ASSERT_OK(conn.Execute("CREATE INDEX CONCURRENTLY ON test_table(v)"));
    LOG(INFO) << "Completed CREATE INDEX CONCURRENTLY";
  });

  thread_holder.JoinAll();

  // 4. Validation
  ASSERT_OK(conn.Fetch("SELECT yb_index_check('test_table_v_idx'::regclass)"));
}

INSTANTIATE_TEST_CASE_P(, PgConcurrentCreateIndexWithSlowRefreshMatViewTest,
                        ::testing::Values(false, true)); // true = CONCURRENTLY, false = standard

TEST_F(PgConcurrentDDLsTest, ConcurrentMaterializedViewRefreshAndWrites) {
  auto setup_conn = ASSERT_RESULT(Connect());

  // Create base table
  ASSERT_OK(setup_conn.Execute(
      "CREATE TABLE mv_base_table (id int PRIMARY KEY, value int)"));

  // Insert some initial data
  ASSERT_OK(setup_conn.Execute(
      "INSERT INTO mv_base_table SELECT i, i * 10 FROM generate_series(1, 10000) i"));

  // Create materialized view
  ASSERT_OK(setup_conn.Execute(
      "CREATE MATERIALIZED VIEW mv_test AS SELECT * FROM mv_base_table"));

  TestThreadHolder thread_holder;
  constexpr size_t kThreadsCount = 2;
  CountDownLatch start_latch(kThreadsCount);
  std::atomic<size_t> refresh_count{0};
  std::atomic<size_t> write_count{0};

  // Thread 1: Continuously refresh materialized view
  thread_holder.AddThreadFunctor(
      [this, &stop = thread_holder.stop_flag(), &start_latch, &refresh_count] {
        auto conn = ASSERT_RESULT(Connect());
        start_latch.CountDown();
        start_latch.Wait();
        while (!stop.load(std::memory_order_acquire)) {
          ASSERT_OK(conn.Execute("REFRESH MATERIALIZED VIEW mv_test"));
          refresh_count.fetch_add(1, std::memory_order_relaxed);
        }
        LOG(INFO) << "Refresh thread completed " << refresh_count.load() << " refreshes";
      });

  // Thread 2: Constant writes to the base table
  thread_holder.AddThreadFunctor(
      [this, &stop = thread_holder.stop_flag(), &start_latch, &write_count] {
        auto conn = ASSERT_RESULT(Connect());
        start_latch.CountDown();
        start_latch.Wait();
        size_t counter = 10000;
        while (!stop.load(std::memory_order_acquire)) {
          ASSERT_OK(conn.ExecuteFormat(
              "INSERT INTO mv_base_table VALUES ($0, $1) "
              "ON CONFLICT (id) DO UPDATE SET value = EXCLUDED.value",
              (counter % 10000) + 1, counter));
          write_count.fetch_add(1, std::memory_order_relaxed);
          counter++;
        }
        LOG(INFO) << "Write thread completed " << write_count.load() << " writes";
      });

  thread_holder.WaitAndStop(30s * kTimeMultiplier);

  LOG(INFO) << "Test completed - Refreshes: " << refresh_count.load()
            << ", Writes: " << write_count.load();
}

#ifdef NDEBUG
TEST_F(PgConcurrentDDLsTest, ConcurrentCreateDropDatabase) {
  // Use 2 threads to maximize the chance of a race condition on catalog version increments.
  const int kNumThreads = 2;
  const int kNumIterations = 100;

  std::vector<yb::TestThreadHolder> thread_holders(kNumThreads);

  for (int i = 0; i < kNumThreads; i++) {
    thread_holders[i].AddThreadFunctor([this, i] {
      auto conn = ASSERT_RESULT(Connect());

      for (int j = 0; j < kNumIterations; j++) {
        // Use a unique name per thread and iteration to avoid "database already exists"
        // conflicts, focusing purely on the concurrency of the DDL engine itself.
        std::string db_name = Format("db_t$0_i$1", i, j);

        // 1. Create the database
        ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", db_name));

        // 2. Drop the database
        ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", db_name));
      }
    });
  }

  // Join all threads to ensure the iterations complete.
  for (int i = 0; i < kNumThreads; i++) {
    thread_holders[i].JoinAll();
  }
}
#endif

// Test that concurrent "CREATE OR REPLACE FUNCTION" and "DROP FUNCTION"
// on the same function does not crash the backend process.
// See issue: https://github.com/yugabyte/yugabyte-db/issues/31247

class PgConcurrentCreateOrReplaceCrashTest : public LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    // Object locking would serialize DDLs on the same object and close the
    // stale-syscache race window, so it must be disabled to reproduce.
    ToggleDDLMode(opts->extra_tserver_flags, /* use_legacy = */ true);
    ToggleDDLMode(opts->extra_master_flags, /* use_legacy = */ true);
  }

  static bool IsBackendCrash(const Status& status) {
    if (status.ok()) {
      return false;
    }
    const auto msg = status.ToString();
    return msg.find("server closed the connection unexpectedly") != std::string::npos ||
           msg.find("terminating connection due to") != std::string::npos;
  }
};

TEST_F(PgConcurrentCreateOrReplaceCrashTest, ConcurrentCreateOrReplaceWithDropCrash) {
  constexpr int kNumReplacers = 4;
  const int kNumIterations = NonTsanVsTsan(200, 80);
  const std::string kCreateOrReplace =
      "CREATE OR REPLACE FUNCTION add_fn(INTEGER, INTEGER) RETURNS INTEGER "
      "LANGUAGE SQL AS 'SELECT $1 + $2;'";
  const std::string kCreate =
      "CREATE FUNCTION add_fn(INTEGER, INTEGER) RETURNS INTEGER "
      "LANGUAGE SQL AS 'SELECT $1 + $2;'";
  const std::string kDrop =
      "DROP FUNCTION IF EXISTS add_fn(INTEGER, INTEGER)";
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute(kCreate));

  std::atomic<bool> crashed{false};
  std::string crash_msg;
  std::mutex crash_mutex;
  auto record_crash = [&](const Status& status) {
    if (IsBackendCrash(status)) {
      std::lock_guard<std::mutex> lock(crash_mutex);
      crash_msg = status.ToString();
      crashed.store(true, std::memory_order_release);
      return true;
    }
    return false;
  };

  TestThreadHolder thread_holder;

  // Replacer threads: hammer CREATE OR REPLACE on the same function so that they
  // contend on the same pg_proc row and frequently run with a stale syscache.
  for (int i = 0; i < kNumReplacers; ++i) {
    thread_holder.AddThreadFunctor([&] {
      auto conn = ASSERT_RESULT(Connect());
      for (int j = 0; j < kNumIterations && !crashed.load(std::memory_order_acquire); ++j) {
        // A stale-syscache replace can either crash (the bug) or fail benignly
        // because the function was concurrently dropped. Only a crash fails the
        // test; benign DDL errors are expected under this raciness.
        record_crash(conn.Execute(kCreateOrReplace));
      }
    });
  }

  // Churn thread: keep dropping and recreating the function so its OID (and thus
  // the pg_proc row's ybctid) changes underneath the replacers' stale caches.
  thread_holder.AddThreadFunctor([&] {
    auto conn = ASSERT_RESULT(Connect());
    for (int j = 0; j < kNumIterations && !crashed.load(std::memory_order_acquire); ++j) {
      if (record_crash(conn.Execute(kDrop))) {
        return;
      }
      record_crash(conn.Execute(kCreate));
    }
  });

  thread_holder.JoinAll();

  ASSERT_FALSE(crashed.load(std::memory_order_acquire))
      << "backend crashed during concurrent CREATE OR REPLACE. "
      << "Status: " << crash_msg;

  // Even when no backend crashes, we would like to further assert that
  // pg_proc's secondary index (pg_proc_proname_args_nsp_index)
  // is consistent with the base pg_proc table in the face of
  // concurrent updates to pg_proc and its secondary index.
  ASSERT_OK(
      setup_conn.Fetch("SELECT yb_index_check('pg_proc_proname_args_nsp_index'::regclass)"));
}

// Deterministic repro of the Jepsen si.append-table silent data loss (DB-22722 / DB-20699).
//
// Root cause: a plain CREATE INDEX run INSIDE a transaction block is transparently downgraded to
// nonconcurrent (utility.c: online multi-phase backfill needs multiple commits, impossible in a
// txn block). A nonconcurrent build sets skip_index_backfill=true, so the async DocDB backfill
// (BackfillTable / AllowCompactionsToGCDeleteMarkers) never runs and the index's docdb "birth_time"
// is never set (stays 0). The index is instead populated inline within the DDL transaction, so its
// entries are stamped at the DDL commit time.
//
// The read guard (read_query.cc) only rejects a stale index read when birth_time != 0, and only
// returns a retryable error when birth_time == 0 && retain_delete_markers == true. For this
// inline-built index birth_time == 0 AND retain_delete_markers == false, so NEITHER branch fires:
// a snapshot-isolation read whose read time precedes the CREATE INDEX commit reads the fresh index,
// finds none of the pre-existing rows (their index entries are stamped after the read time), and
// silently returns an incomplete result.
//
// This mirrors the Jepsen client, which runs its lazy DDL as `BEGIN; CREATE INDEX ...`.
TEST_F(PgConcurrentDDLsTest,
       YB_DISABLE_TEST_IN_TSAN(SnapshotReadFreshNonConcurrentIndex)) {
  // Enable the index birth-time guard diagnostic (read_query.cc) so the test log shows the
  // birth_time / retain_delete_markers / read_time the guard saw for the offending index read.
  ASSERT_OK(cluster_->SetFlagOnTServers("vmodule", "read_query=1"));

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
  // The committed row the snapshot read must never lose.
  ASSERT_OK(setup_conn.Execute("INSERT INTO t VALUES (0, 13)"));
  // Unrelated table used only to fix the reader's snapshot without locking t.
  ASSERT_OK(setup_conn.Execute("CREATE TABLE anchor (k INT PRIMARY KEY)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO anchor VALUES (1)"));

  // Reader fixes its REPEATABLE READ snapshot by reading `anchor`; it does NOT touch t yet.
  auto reader = ASSERT_RESULT(Connect());
  ASSERT_OK(reader.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_EQ(ASSERT_RESULT(reader.FetchAllAsString("SELECT k FROM anchor")), "1");

  // Create the covering index INSIDE a transaction block -> nonconcurrent inline build ->
  // skip_index_backfill -> birth_time is never set (stays 0). Its entries are stamped at COMMIT,
  // which is after the reader's snapshot.
  auto indexer = ASSERT_RESULT(Connect());
  ASSERT_OK(indexer.Execute("BEGIN"));
  ASSERT_OK(indexer.Execute("CREATE INDEX idx_t_k_v ON t (k, v)"));
  ASSERT_OK(indexer.Execute("COMMIT"));

  // Force the reader's first read of t onto the fresh (birth_time==0) index.
  ASSERT_OK(reader.Execute("SET enable_seqscan = off"));
  const auto plan = ASSERT_RESULT(reader.FetchAllAsString(
      "EXPLAIN (COSTS OFF) SELECT k, v FROM t ORDER BY k"));
  LOG(INFO) << "Index read plan:\n" << plan;
  // Ensure the read actually goes through the fresh index; otherwise the test wouldn't exercise the
  // bug.
  ASSERT_STR_CONTAINS(plan, "idx_t_k_v");

  // The committed row (0, 13) must be visible via the index. The bug returns an empty result
  // (silent loss): the inline index entry is stamped after the reader's snapshot and, with no
  // birth_time, the guard does not reject the stale read. Correct behavior: the row, or a loud
  // error (SnapshotTooOld once birth_time is set).
  auto index_read_result = reader.FetchAllAsString("SELECT k, v FROM t ORDER BY k");
  LOG(INFO) << "Read via index result: "
            << (index_read_result.ok() ? *index_read_result
                                       : index_read_result.status().ToString());
  if (index_read_result.ok()) {
    ASSERT_EQ(*index_read_result, "0, 13")
        << "Index read silently dropped a committed row: read a nonconcurrent index whose "
           "birth_time was never set.";
  }
}

// The master finalizes a nonconcurrently built index's birth_time on its tablets with an RPC
// (AsyncMarkNonConcurrentIndexDone), which can be lost. The tablet validator then finalizes it
// from the master's state when the tablet is opened.
TEST_F(PgConcurrentDDLsTest,
       YB_DISABLE_TEST_IN_TSAN(NonConcurrentIndexBirthTimeRecoveredByTabletValidator)) {
  constexpr auto kIndexName = "idx_t_v";
  // Drop the finalize on the tablets, as if the master's RPC were lost.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_metadata_non_concurrent_index_done", "true"));

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (0, 13)"));
  // Inside a transaction block, CREATE INDEX is built nonconcurrently.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON t (v)", kIndexName));
  ASSERT_OK(conn.Execute("COMMIT"));

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const auto index_id =
      ASSERT_RESULT(client::GetTableIdByTableName(client.get(), "yugabyte", kIndexName));
  std::vector<TabletId> tablet_ids;
  ASSERT_OK(client->GetTabletsFromTableId(index_id, /* max_tablets = */ 0, &tablet_ids));
  ASSERT_FALSE(tablet_ids.empty());

  // Returns the index's IndexInfoPB on every tablet replica, as persisted in the tablet
  // superblocks.
  itest::ExternalMiniClusterFsInspector inspector(cluster_.get());
  auto get_replica_index_infos = [&]() -> Result<std::vector<IndexInfoPB>> {
    std::vector<IndexInfoPB> index_infos;
    for (const auto& tablet_id : tablet_ids) {
      for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
        tablet::RaftGroupReplicaSuperBlockPB superblock;
        RETURN_NOT_OK(inspector.ReadTabletSuperBlockOnTS(i, tablet_id, &superblock));
        for (const auto& table : superblock.kv_store().tables()) {
          if (table.table_id() == index_id) {
            index_infos.push_back(table.index_info());
          }
        }
      }
    }
    return index_infos;
  };

  // Give the master's finalize RPC time to reach the tablets, and check it was dropped there.
  SleepFor(5s * kTimeMultiplier);
  auto index_infos = ASSERT_RESULT(get_replica_index_infos());
  ASSERT_EQ(index_infos.size(), tablet_ids.size() * cluster_->num_tablet_servers());
  for (const auto& index_info : index_infos) {
    ASSERT_EQ(index_info.birth_time_state(), IndexInfoPB::BIRTH_TIME_PENDING_DDL_COMMIT);
  }

  // Restart the tservers without dropping the finalize. The tablet validator runs when the tablets
  // are opened and finalizes the index from the master's state.
  cluster_->Shutdown(ExternalMiniCluster::TS_ONLY);
  cluster_->AddExtraFlagOnTServers(
      "tablet_validator_retain_delete_markers_validation_period_sec", "1");
  ASSERT_OK(cluster_->Restart());

  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    for (const auto& index_info : VERIFY_RESULT(get_replica_index_infos())) {
      if (index_info.birth_time_state() != IndexInfoPB::BIRTH_TIME_FINAL ||
          index_info.birth_time() == 0) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, "Wait for the tablet validator to finalize the index birth time"));

  // The index is readable again.
  conn = ASSERT_RESULT(Connect());
  const std::string query = "SELECT k FROM t WHERE v = 13";
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(query)), 0);
}
}  // namespace yb::pgwrapper
