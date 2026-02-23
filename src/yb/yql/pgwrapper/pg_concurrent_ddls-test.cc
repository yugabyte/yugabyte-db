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
#include "yb/util/ysql_binary_runner.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

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
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--ysql_pg_conf_csv=$0", "yb_enable_concurrent_ddl=true"));
  }

  int GetNumTabletServers() const override {
    return 3;
  }
};

TEST_F(PgConcurrentDDLsTest, CreateIndexAndConcurrentAnalyze) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(0, 10), 0"));
  yb::TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
    auto analyze_conn = ASSERT_RESULT(Connect());
    while (!stop.load()) {
      ASSERT_OK(analyze_conn.Execute("ANALYZE test"));
    }
  });

  for (int i = 0; i < 10; i++) {
    LOG(INFO) << "Creating index " << i;
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX idx_$0 ON test(k)", i));
  }

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
    options->extra_master_flags.push_back(
        "--ysql_yb_wait_for_backends_catalog_version_timeout=20000");
    options->extra_master_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms=10000");
    options->extra_master_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_master_tserver_rpc_timeout_ms=5000");
    options->extra_master_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms=3000");
    options->extra_tserver_flags.push_back(
        "--ysql_yb_wait_for_backends_catalog_version_timeout=20000");
    options->extra_tserver_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms=10000");
    options->extra_tserver_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms=3000");

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
  auto setup_script =
    R"#(
CREATE TABLE parent_partition(c1 int, c2 int) PARTITION BY RANGE (c1);
CREATE TABLE child_part_1 PARTITION OF parent_partition FOR VALUES FROM (0) to (100);
CREATE TABLE child_part_2 PARTITION OF parent_partition FOR VALUES FROM (101) to (201);
CREATE INDEX parent_index ON ONLY parent_partition (c1, c2);
-- Insert 200 rows of data
-- 100 rows into child_part_1 (c1 from 0 to 99)
-- 100 rows into child_part_2 (c1 from 101 to 200)
INSERT INTO parent_partition (c1, c2)
SELECT
    CASE
        WHEN i <= 100 THEN i - 1      -- 0 to 99
        ELSE i                        -- 101 to 200
    END,
    (random() * 1000)::int            -- random data for c2
FROM generate_series(1, 200) AS i;
        )#";
  // Verify each partition has 100 rows.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(setup_script));
  auto count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM child_part_1"));
  ASSERT_EQ(count, 100);
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM child_part_2"));
  ASSERT_EQ(count, 100);
  count = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM parent_partition"));
  ASSERT_EQ(count, 200);
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

}  // namespace yb::pgwrapper
