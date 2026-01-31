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
        yb::Format("--enable_object_locking_for_table_locks=$0", EnableTableLocks()));
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--ysql_yb_ddl_transaction_block_enabled=$0", EnableTransactionalDdl()));
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--ysql_pg_conf_csv=$0", "yb_fallback_to_legacy_catalog_read_time=false"));
  }

  int GetNumTabletServers() const override {
    return 3;
  }

  virtual bool EnableTableLocks() const {
    return true;
  }

  virtual bool EnableTransactionalDdl() const {
    return true;
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

class PgConcurrentCreateIndexWithSlowBackfillTest : public PgConcurrentDDLsTest {
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
    // slow down the backfill rate.
    options->extra_tserver_flags.push_back("--backfill_index_rate_rows_per_sec=1");
    options->extra_tserver_flags.push_back(
        "--ysql_yb_wait_for_backends_catalog_version_timeout=20000");
    options->extra_tserver_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms=10000");
    options->extra_tserver_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms=3000");

    PgConcurrentDDLsTest::UpdateMiniClusterOptions(options);
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

}  // namespace yb::pgwrapper
