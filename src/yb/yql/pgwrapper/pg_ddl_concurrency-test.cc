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

#include <chrono>

#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using namespace std::chrono_literals;

namespace yb::pgwrapper {
namespace {

Status SuppressAllowedErrors(const Status& s) {
  if (HasTransactionError(s) || IsRetryable(s)) {
    return Status::OK();
  }
  // Usually PG backend will append to the error message with a line of text like
  // Catalog Version Mismatch: A DDL occurred while processing this query. Try again.
  // The "Try again" will be detected by IsRetryable(s) as true. But in uncommon
  // cases, PG backend will not append this line, for this test we still want to
  // suppress this error.
  if (s.message().Contains("waiting for postgres backends to catch up")) {
    return Status::OK();
  }
  return s;
}

Status RunIndexCreationQueries(PGConn* conn, const std::string& table_name) {
  constexpr const char* kQueries[] = {
      "DROP TABLE IF EXISTS $0",
      "CREATE TABLE IF NOT EXISTS $0(k int PRIMARY KEY, v int)",
      "CREATE INDEX IF NOT EXISTS $0_v ON $0(v)",
  };
  while (true) {
    RETURN_NOT_OK(SuppressAllowedErrors(conn->ExecuteFormat(kQueries[0], table_name)));

    // CREATE TABLE may fail due to catalog version mismatch.
    // If it fails, skip creating the index on it.
    auto create_status = conn->ExecuteFormat(kQueries[1], table_name);
    RETURN_NOT_OK(SuppressAllowedErrors(create_status));
    if (!create_status.ok()) {
      continue;
    }

    auto index_status = conn->ExecuteFormat(kQueries[2], table_name);
    RETURN_NOT_OK(SuppressAllowedErrors(index_status));
    if (index_status.ok()) {
      return Status::OK();
    }
  }
}

} // namespace

class PgDDLConcurrencyTest : public LibPqTestBase {
 public:
  int GetNumMasters() const override { return 3; }

 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms=5000");
    options->extra_tserver_flags.push_back(
        "--ysql_yb_wait_for_backends_catalog_version_timeout=30000");
    LibPqTestBase::UpdateMiniClusterOptions(options);
  }
};

/*
 * Index creation commits DDL transaction at the middle.
 * Check that this behavior works properly and doesn't produces unexpected errors
 * in case transaction can't be committed (due to massive retry errors
 * caused by aggressive running of DDL in parallel).
 */
TEST_F(PgDDLConcurrencyTest, IndexCreation) {
  TestThreadHolder thread_holder;
  constexpr size_t kThreadsCount = 3;
  CountDownLatch start_latch(kThreadsCount + 1);
  for (size_t i = 0; i < kThreadsCount; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), idx = i, &start_latch] {
          const auto table_name = Format("t$0", idx);
          // TODO (#19975): Enable read committed isolation
          auto conn = ASSERT_RESULT(SetDefaultTransactionIsolation(
              Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
          start_latch.CountDown();
          start_latch.Wait();
          while (!stop.load(std::memory_order_acquire)) {
            ASSERT_OK(RunIndexCreationQueries(&conn, table_name));
          }
        });
  }

  const std::string table_name("t");
  // TODO (#19975): Enable read committed isolation
  auto conn = ASSERT_RESULT(SetDefaultTransactionIsolation(
      Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
  start_latch.CountDown();
  start_latch.Wait();
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_OK(RunIndexCreationQueries(&conn, table_name));
  }
}

/*
 * Test concurrent temp table creation across multiple threads.
 * Each thread creates its own connection and continuously creates temp tables
 * until the test completes.
 * Stress test to check read restart errors (see GH #29704).
 */
TEST_F(PgDDLConcurrencyTest, TempTableCreation) {
  TestThreadHolder thread_holder;
  constexpr size_t kThreadsCount = 10;
  CountDownLatch start_latch(kThreadsCount);
  for (size_t i = 0; i < kThreadsCount; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &start_latch] {
          start_latch.CountDown();
          start_latch.Wait();
          while (!stop.load(std::memory_order_acquire)) {
            auto conn = ASSERT_RESULT(SetDefaultTransactionIsolation(
                Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
            ASSERT_OK(conn.Execute("CREATE TEMP TABLE temp_table(k int, v int)"));
          }
        });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
}

class PgDDLConcurrencyWithObjectLockingTest : public PgDDLConcurrencyTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--enable_object_locking_for_table_locks=true");
    options->extra_tserver_flags.push_back(
        "--ysql_yb_ddl_transaction_block_enabled=true");
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=yb_fallback_to_legacy_catalog_read_time=false");
    PgDDLConcurrencyTest::UpdateMiniClusterOptions(options);
  }
};

// https://github.com/yugabyte/yugabyte-db/issues/28532
TEST_F(PgDDLConcurrencyWithObjectLockingTest, TableDrop) {
  TestThreadHolder thread_holder;
  auto conn1 = CHECK_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE sample (i INT)"));
  ASSERT_OK(conn1.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn1.Execute("DROP TABLE sample"));
  thread_holder.AddThreadFunctor(
      [this] {
    auto conn2 = CHECK_RESULT(Connect());
    ASSERT_OK(conn2.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
    // Verify that we get the same error message as native PG.
    // Prior to the fix, we get "ERROR:  cache lookup failed for relation 16384"
    ASSERT_NOK_STR_CONTAINS(conn2.Execute("DROP TABLE sample"), "table \"sample\" does not exist");
  });
  // With object locking, this sleep will block the DROP statement on conn2 for 5 seconds.
  SleepFor(5s);
  ASSERT_OK(conn1.Execute("COMMIT"));
  thread_holder.Stop();
}

// https://github.com/yugabyte/yugabyte-db/issues/28563
TEST_F(PgDDLConcurrencyWithObjectLockingTest, TableDropCascade) {
  TestThreadHolder thread_holder;
  auto conn1 = CHECK_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE dd (i INT)"));
  ASSERT_OK(conn1.Execute("CREATE MATERIALIZED VIEW mat AS SELECT * FROM dd"));
  ASSERT_OK(conn1.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn1.Execute("DROP TABLE dd CASCADE"));
  thread_holder.AddThreadFunctor(
      [this] {
    auto conn2 = CHECK_RESULT(Connect());
    ASSERT_OK(conn2.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
    // Verify that we get the same error message as native PG.
    // Prior to the fix, we get "ERROR:  could not open relation with OID 16387"
    ASSERT_NOK_STR_CONTAINS(conn2.Execute("REFRESH MATERIALIZED VIEW mat"),
                            "relation \"mat\" does not exist");
  });
  // With object locking, this sleep will block the REFRESH statement on conn2 for 5 seconds.
  SleepFor(5s);
  ASSERT_OK(conn1.Execute("COMMIT"));
  thread_holder.Stop();
}

} // namespace yb::pgwrapper
