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
#include <semaphore>
#include <string>
#include <vector>

#include "yb/util/countdown_latch.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_int32(ysql_select_parallelism);

DECLARE_uint64(pg_client_heartbeat_interval_ms);
DECLARE_uint64(pg_client_session_expiration_ms);

DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_concurrent_ddl);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_uint64(max_clock_skew_usec);

namespace yb::pgwrapper {
namespace {

using ErrorDetector = std::counting_semaphore<0xFFFF>;

auto MakeErrorReporter(ErrorDetector& error_detector) {
  return CancelableScopeExit([&error_detector]() { error_detector.release(); });
}

} // namespace

class PgDebugReadRestartsTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 3;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_select_parallelism) = 1;
    PgMiniTestBase::SetUp();
  }
};

class PgDebugReadRestartsTestShortSessionExpiration : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_session_expiration_ms) = 5000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_heartbeat_interval_ms) = 2000;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F(PgDebugReadRestartsTest, RecommendReadCommitted) {
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("DROP TABLE IF EXISTS tokens"));
  ASSERT_OK(setup_conn.Execute("CREATE TABLE tokens(token INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO tokens SELECT i FROM GENERATE_SERIES(1, 100) i"));

  auto read_conn = ASSERT_RESULT(Connect());
  auto insert_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(read_conn.StartTransaction(SNAPSHOT_ISOLATION));
  auto rows = ASSERT_RESULT(read_conn.FetchRows<int32_t>("SELECT token FROM tokens LIMIT 1"));
  ASSERT_OK(insert_conn.Execute("INSERT INTO tokens SELECT i FROM GENERATE_SERIES(200, 300) i"));
  auto result = read_conn.FetchRows<int32_t>("SELECT token FROM tokens ORDER BY token");
  ASSERT_NOK(result);
  auto error_string = result.status().ToString();
  // Recommend read committed isolation level
  ASSERT_STR_CONTAINS(error_string, "Consider using READ COMMITTED");
  ASSERT_OK(read_conn.RollbackTransaction());
}

// Test checks absence of non retryable errors in case of massive read restarts during read with
// parallel workers
TEST_F_EX(
    PgDebugReadRestartsTest, ParallelWorkersReadRestarts,
    PgDebugReadRestartsTestShortSessionExpiration) {
  constexpr auto kRowsCount = 10000;
  constexpr auto kNumWriters = 10;
  constexpr auto kNumReaders = 50;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT) SPLIT INTO 10 TABLETS"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t SELECT s, s FROM generate_series(1, $0) AS s", kRowsCount));

  std::atomic<uint64_t> read_count{0}, write_count{0};
  ErrorDetector error_detector{0};
  {
    std::vector<PGConn> conns;
    CountDownLatch latch(kNumWriters + kNumReaders);
    TestThreadHolder thread_holder;
    conns.reserve(kNumWriters + kNumReaders);
    for (size_t i = 0; i < kNumWriters; ++i) {
      conns.emplace_back(ASSERT_RESULT(Connect()));
      constexpr auto kWSize = kRowsCount / kNumWriters;
      thread_holder.AddThreadFunctor([
          &conn = conns.back(), &stop = thread_holder.stop_flag(), &latch, &write_count, i,
          &error_detector] {
        auto error_reporter = MakeErrorReporter(error_detector);
        unsigned int seed = SeedRandom();
        latch.CountDown();
        ASSERT_OK(StoppableWait(latch, stop));
        while(!stop) {
          ASSERT_OK(conn.ExecuteFormat(
              "UPDATE t SET v = v + 1 WHERE k = $0", i * kWSize + rand_r(&seed) % kWSize + 1));
          ++write_count;
        }
        error_reporter.Cancel();
      });
    }

    for (size_t i = 0; i < kNumReaders; ++i) {
      conns.emplace_back(ASSERT_RESULT(Connect()));
      thread_holder.AddThreadFunctor([
          &conn = conns.back(), &stop = thread_holder.stop_flag(), &latch, &kRowsCount, &read_count,
          &error_detector] {
        auto error_reporter = MakeErrorReporter(error_detector);
        ASSERT_OK(conn.Execute(
            "SET yb_enable_base_scans_cost_model TO true;" \
            "SET yb_parallel_range_rows TO 1;" \
            "SET parallel_setup_cost TO 0;" \
            "SET parallel_tuple_cost TO 0;"));
        // Make sure query will use parallel workers
        const std::string query("SELECT * FROM t");
        const auto top_node_type = ASSERT_RESULT(ASSERT_RESULT(conn.FetchRow<JsonDocument>(
            Format("EXPLAIN (FORMAT JSON, COSTS OFF) $0", query)))
                .Root()[0]["Plan"]["Node Type"].GetString());
        ASSERT_EQ(top_node_type, "Gather"s);
        latch.CountDown();
        ASSERT_OK(StoppableWait(latch, stop));
        while(!stop) {
          const auto rows = conn.FetchRows<int32_t, int32_t>(query);
          if (!rows.ok()) {
            const auto& status = rows.status();
            ASSERT_TRUE(IsRetryable(status)) << ToString(status);
          } else {
            ASSERT_EQ(rows->size(), kRowsCount);
          }
          ++read_count;
        }
        error_reporter.Cancel();
      });
    }

    ASSERT_FALSE(error_detector.try_acquire_for(3 * FLAGS_pg_client_session_expiration_ms * 1ms));
  }
  ASSERT_GT(read_count, 0);
  ASSERT_GT(write_count, 0);
}

// GH #28190: under read restarts, a tablet response that asks for a restart can reach the
// session's read point after the read point lost its read time, which logs
// "Unexpected restart without a read time set" (DFATAL). The bank workload hit it with
// table-level locks and READ COMMITTED; the report says only parallel-query runs failed, a later
// report saw it without parallel query, so the test runs both.
class PgStaleRestartTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_concurrent_ddl) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    PgMiniTestBase::SetUp();
    // From the stale-restart clone (#28190), run with yb_enable_replay_retry on.
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("ALTER DATABASE yugabyte SET yb_enable_replay_retry = on"));
  }

  size_t NumTabletServers() override {
    return 1;
  }

  void TestRestartLandsOnReadPoint(bool parallel) {
    constexpr auto kRowsCount = 10000;
    constexpr auto kNumWriters = 10;
    constexpr auto kNumReaders = 20;
    constexpr auto kDuration = 60s;
    StringWaiterLogSink no_read_time_sink("Unexpected restart without a read time set");

    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT) SPLIT INTO 10 TABLETS"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t SELECT s, s FROM generate_series(1, $0) AS s", kRowsCount));

    std::atomic<uint64_t> read_count{0}, write_count{0}, read_errors{0};
    {
      std::vector<PGConn> conns;
      conns.reserve(kNumWriters + kNumReaders);
      TestThreadHolder thread_holder;
      for (size_t i = 0; i < kNumWriters; ++i) {
        conns.emplace_back(ASSERT_RESULT(Connect()));
        constexpr auto kWSize = kRowsCount / kNumWriters;
        thread_holder.AddThreadFunctor(
            [&conn = conns.back(), &stop = thread_holder.stop_flag(), &write_count, i] {
          unsigned int seed = SeedRandom();
          while (!stop) {
            auto status = conn.ExecuteFormat(
                "UPDATE t SET v = v + 1 WHERE k = $0", i * kWSize + rand_r(&seed) % kWSize + 1);
            if (status.ok()) {
              ++write_count;
            }
          }
        });
      }
      for (size_t i = 0; i < kNumReaders; ++i) {
        conns.emplace_back(ASSERT_RESULT(Connect()));
        auto& reader = conns.back();
        ASSERT_OK(reader.Execute("SET default_transaction_isolation = 'read committed'"));
        if (parallel) {
          ASSERT_OK(reader.Execute(
              "SET yb_enable_base_scans_cost_model TO true;"
              "SET yb_parallel_range_rows TO 1;"
              "SET max_parallel_workers_per_gather TO 2;"
              "SET parallel_setup_cost TO 0;"
              "SET parallel_tuple_cost TO 0;"));
        } else {
          ASSERT_OK(reader.Execute("SET max_parallel_workers_per_gather TO 0"));
        }
        const std::string query("SELECT count(*), sum(k) FROM t");
        const auto plan = ASSERT_RESULT(reader.FetchAllAsString(
            Format("EXPLAIN (COSTS OFF) $0", query)));
        ASSERT_EQ(plan.find("Gather") != std::string::npos, parallel) << plan;
        thread_holder.AddThreadFunctor(
            [&reader, &stop = thread_holder.stop_flag(), &read_count, &read_errors, query] {
          while (!stop) {
            if (reader.FetchRow<int64_t, int64_t>(query).ok()) {
              ++read_count;
            } else {
              ++read_errors;
            }
          }
        });
      }
      thread_holder.WaitAndStop(kDuration);
    }
    LOG(INFO) << "reads: " << read_count << ", read errors: " << read_errors
              << ", writes: " << write_count
              << ", restarts without read time: " << no_read_time_sink.GetEventCount();
    ASSERT_GT(read_count, 0);
    ASSERT_GT(write_count, 0);
    ASSERT_EQ(no_read_time_sink.GetEventCount(), 0);
  }
};

TEST_F(PgStaleRestartTest, ParallelQuery) {
  TestRestartLandsOnReadPoint(/* parallel= */ true);
}

TEST_F(PgStaleRestartTest, NoParallelQuery) {
  TestRestartLandsOnReadPoint(/* parallel= */ false);
}

} // namespace yb::pgwrapper
