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

#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/ysql_binary_runner.h"

DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(ysql_enable_auto_analyze);

using namespace std::literals;

namespace yb::pgwrapper {

class PgConcurrentDDLsTest : public LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back(
        "--enable_object_locking_for_table_locks=true");
    opts->extra_tserver_flags.emplace_back(
        "--ysql_yb_ddl_transaction_block_enabled=true");
    opts->extra_tserver_flags.emplace_back("--ysql_enable_concurrent_ddl=true");
    AppendFlagToAllowedPreviewFlagsCsv(
        opts->extra_tserver_flags, "ysql_enable_concurrent_ddl");
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

  // This query generates (2000 * 2000) rows internally.
  ASSERT_OK(conn.Execute(
      "CREATE MATERIALIZED VIEW slow_mv AS "
      "SELECT (s1 * 10000 + s2) AS unique_key, t1.v "
      "FROM base_table t1 "
      "CROSS JOIN generate_series(1, 2000) s1 "
      "CROSS JOIN generate_series(1, 2000) s2"));

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

// https://github.com/yugabyte/yugabyte-db/issues/30908
class PgDdlTransactionWithoutConcurrentDDLSupportTest : public LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back(
        "--enable_object_locking_for_table_locks=true");
    opts->extra_tserver_flags.emplace_back(
        "--ysql_yb_ddl_transaction_block_enabled=true");
    // 30908 only appears when concurrent DDL is disabled.
    opts->extra_tserver_flags.emplace_back("--ysql_enable_concurrent_ddl=false");
    AppendFlagToAllowedPreviewFlagsCsv(opts->extra_tserver_flags, "ysql_enable_concurrent_ddl");
  }
};

TEST_F(PgDdlTransactionWithoutConcurrentDDLSupportTest, ParallelDdlTransactionBlockCrash) {
  auto conn = ASSERT_RESULT(Connect());

  std::atomic<bool> bug_reproduced{false};
  std::string reproduced_msg;
  std::mutex msg_mutex;

  std::string table1 = "sample";
  std::string table2 = "sample1";

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, v INT)", table1));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, v INT)", table2));

  auto is_bug_reproduced = [&reproduced_msg, &msg_mutex](const yb::Status& status) {
    if (status.ok()) return false;
    const auto msg = status.ToString();
    if (msg.find("server closed the connection unexpectedly") != std::string::npos ||
        msg.find("unexpected state END") != std::string::npos) {
      std::lock_guard<std::mutex> lock(msg_mutex);
      reproduced_msg = msg;
      return true;
    }
    return false;
  };

  auto run_ddl = [&bug_reproduced, &is_bug_reproduced, this](
      const std::string& table_name) {
    auto thread_conn = ASSERT_RESULT(Connect());

    for (int iter = 0; iter < 40; ++iter) {
      if (bug_reproduced.load(std::memory_order_acquire)) {
        break;
      }

      ASSERT_OK(thread_conn.Execute("BEGIN"));

      auto status = thread_conn.ExecuteFormat(
          "ALTER TABLE $0 ADD COLUMN a_$1 INT", table_name, iter);

      if (status.ok()) {
        auto commit_status = thread_conn.Execute("COMMIT");
        if (is_bug_reproduced(commit_status)) {
          bug_reproduced.store(true, std::memory_order_release);
          return;
        }

        ASSERT_OK(thread_conn.Execute("BEGIN"));

        auto drop_status = thread_conn.ExecuteFormat(
            "ALTER TABLE $0 DROP COLUMN a_$1", table_name, iter);
        if (drop_status.ok()) {
          commit_status = thread_conn.Execute("COMMIT");
          if (is_bug_reproduced(commit_status)) {
            bug_reproduced.store(true, std::memory_order_release);
            return;
          }
        } else {
          auto rollback_status = thread_conn.Execute("ROLLBACK");
          if (is_bug_reproduced(rollback_status)) {
            bug_reproduced.store(true, std::memory_order_release);
            return;
          }
        }
      } else {
        auto rollback_status = thread_conn.Execute("ROLLBACK");
        if (is_bug_reproduced(rollback_status)) {
          bug_reproduced.store(true, std::memory_order_release);
          return;
        }
      }
    }
  };

  std::thread t1([&, table1] { run_ddl(table1); });
  std::thread t2([&, table2] { run_ddl(table2); });

  t1.join();
  t2.join();

  if (bug_reproduced.load(std::memory_order_acquire)) {
    FAIL() << "Bug 30908 was reproduced successfully! Status message: " << reproduced_msg;
  }
}

// Test that serialization error is properly translated to 40001 to the client
// instead of internal error YB003.
// See https://github.com/yugabyte/yugabyte-db/issues/31736

TEST_F(PgDdlTransactionWithoutConcurrentDDLSupportTest, TestReportProperSerializationErrorInRepeatableRead) {
  auto conn0 = ASSERT_RESULT(Connect());
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  // Create initial schema
  ASSERT_OK(conn0.Execute("CREATE TABLE tab1 (id SERIAL PRIMARY KEY)"));
  ASSERT_OK(conn0.Execute("CREATE TABLE tab2 (id SERIAL PRIMARY KEY)"));

  // Execute concurrent (interleaved) DDL
  ASSERT_OK(conn1.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn2.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn1.Execute("ALTER TABLE tab1 ADD COLUMN new_col INT"));
  ASSERT_OK(conn2.Execute("ALTER TABLE tab2 ADD COLUMN new_col INT"));
  ASSERT_OK(conn1.Execute("COMMIT"));
  ASSERT_NOK_STR_CONTAINS(conn2.Execute("COMMIT"), "pgsql error 40001");
}
}  // namespace yb::pgwrapper
