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

#include <gtest/gtest.h>

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

using std::string;

using namespace std::literals;

DECLARE_bool(TEST_fail_in_apply_if_no_metadata);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(enable_wait_queues);

namespace yb {
namespace pgwrapper {

class PgTxnTest : public PgMiniTestBase {

 protected:
  void AssertEffectiveIsolationLevel(PGConn* conn, const string& expected) {
    auto value_from_deprecated_guc = ASSERT_RESULT(
        conn->FetchRow<std::string>("SHOW yb_effective_transaction_isolation_level"));
    auto value_from_proc = ASSERT_RESULT(
        conn->FetchRow<std::string>("SELECT yb_get_effective_transaction_isolation_level()"));
    ASSERT_EQ(value_from_deprecated_guc, value_from_proc);
    ASSERT_EQ(value_from_deprecated_guc, expected);
  }
};

TEST_F(PgTxnTest, YB_DISABLE_TEST_IN_SANITIZERS(EmptyUpdate)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_in_apply_if_no_metadata) = true;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (key TEXT, value TEXT, PRIMARY KEY((key) HASH))"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE test SET value = 'a' WHERE key = 'b'"));
  ASSERT_OK(conn.CommitTransaction());
}

TEST_F(PgTxnTest, YB_DISABLE_TEST_IN_SANITIZERS(ShowEffectiveYBIsolationLevel)) {

  auto conn = ASSERT_RESULT(Connect());
  AssertEffectiveIsolationLevel(&conn, "repeatable read");

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  AssertEffectiveIsolationLevel(&conn, "serializable");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL READ COMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  AssertEffectiveIsolationLevel(&conn, "serializable");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
  ASSERT_OK(RestartCluster());

  conn = ASSERT_RESULT(Connect());
  AssertEffectiveIsolationLevel(&conn, "read committed");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "read committed");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "read committed");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  AssertEffectiveIsolationLevel(&conn, "serializable");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // TODO(read committed): test cases with "BEGIN" followed by "SET TRANSACTION ISOLATION LEVEL".
  // This can be done after #12494 is fixed.
}

class PgTxnRF1Test : public PgTxnTest {
 public:
  size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F_EX(PgTxnTest, SelectRF1ReadOnlyDeferred, PgTxnRF1Test) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (key INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1)"));
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE, READ ONLY, DEFERRABLE"));
  auto res = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT * FROM test"));
  ASSERT_EQ(res, 1);
  ASSERT_OK(conn.Execute("COMMIT"));
}

class PgTxnTestFailOnConflict : public PgTxnTest {
 protected:
  void SetUp() override {
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = false;
    PgTxnTest::SetUp();
  }
};

TEST_F_EX(PgTxnTest, SerializableReadWriteConflicts, PgTxnTestFailOnConflict) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  constexpr double kPriorityBound = 0.5;

  ASSERT_OK(conn1.ExecuteFormat("SET yb_transaction_priority_lower_bound = $0", kPriorityBound));
  ASSERT_OK(conn1.Execute("CREATE TABLE test (key INT, value INT, PRIMARY KEY((key) HASH))"));
  ASSERT_OK(conn1.Execute("CREATE INDEX idx ON test (value)"));
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn1.Execute("INSERT INTO test VALUES (1, 1)"));
  ASSERT_OK(conn2.ExecuteFormat("SET yb_transaction_priority_upper_bound = $0", kPriorityBound));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_NOK(conn2.Fetch("SELECT key FROM test WHERE value = 1"));
  ASSERT_OK(conn1.Execute("COMMIT"));
}

// Test concurrently insert increasing values, and in parallel perform read of several recent
// values.
// Checking that reads could be serialized.
TEST_F(PgTxnTest, ReadRecentSet) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr int kWriters = 16;
  constexpr int kReaders = 16;
  constexpr int kReadLength = 32;

  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (key INT, value INT, PRIMARY KEY((key) HASH)) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute(
      "CREATE INDEX idx ON test (value) SPLIT INTO 2 TABLETS"));
  TestThreadHolder thread_holder;
  std::atomic<int> value(0);
  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &value] {
      auto connection = ASSERT_RESULT(Connect());
      while (!stop.load()) {
        int cur = value.fetch_add(1);
        ASSERT_OK(connection.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
        auto status = connection.ExecuteFormat("INSERT INTO test VALUES ($0, $0)", cur);
        if (status.ok()) {
          status = connection.CommitTransaction();
        }
        if (!status.ok()) {
          ASSERT_OK(connection.RollbackTransaction());
        }
      }
    });
  }
  std::mutex reads_mutex;
  struct Read {
    int read_min;
    uint64_t mask;

    static std::string ValuesToString(int read_min, uint64_t mask) {
      auto v = read_min;
      auto m = mask;
      std::vector<int> values;
      while (m) {
        if (m & 1ULL) {
          values.push_back(v);
        }
        ++v;
        m >>= 1ULL;
      }

      return AsString(values);
    }

    std::string ToString() const {
      return Format("{ read range: $0-$1 values: $2 }",
                    read_min, read_min + kReadLength - 1, ValuesToString(read_min, mask));
    }
  };
  std::vector<Read> reads;
  for (int i = 0; i != kReaders; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &value, &reads, &reads_mutex] {
      auto connection = ASSERT_RESULT(Connect());
      char str_buffer[0x200];
      while (!stop.load()) {
        const auto read_min = std::max(value.load() - kReadLength, 0);
        char* p = str_buffer;
        for (auto v = read_min; v != read_min + kReadLength; ++v) {
          if (p != str_buffer) {
            *p++ = ',';
          }
          p = FastInt64ToBufferLeft(v, p);
        }
        ASSERT_OK(connection.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
        auto values_res = connection.FetchRows<int32_t>(
            Format("SELECT value FROM test WHERE value in ($0)", str_buffer));
        if (!values_res.ok()) {
          ASSERT_OK(connection.RollbackTransaction());
          continue;
        }
        auto status = connection.CommitTransaction();
        if (!status.ok()) {
          ASSERT_OK(connection.RollbackTransaction());
          continue;
        }
        uint64_t mask = 0;
        for (const auto& value : *values_res) {
          mask |= 1ULL << (value - read_min);
        }
        std::lock_guard lock(reads_mutex);
        Read new_read{read_min, mask};
        reads.erase(std::remove_if(reads.begin(), reads.end(),
            [&new_read, &stop](const auto& old_read) {
          int read_min_delta = new_read.read_min - old_read.read_min;
          // Existing read is too old, remove it.
          if (read_min_delta >= 64) {
            return true;
          }
          // New read is too old, cannot check it.
          if (read_min_delta <= -64) {
            return false;
          }
          constexpr auto kFullMask = (1ULL << kReadLength) - 1ULL;
          // Extract only numbers that belong to both reads.
          uint64_t lmask, rmask;
          if (read_min_delta >= 0) {
            lmask = new_read.mask & (kFullMask >> read_min_delta);
            rmask = (old_read.mask >> read_min_delta) & kFullMask;
          } else {
            lmask = (new_read.mask >> -read_min_delta) & kFullMask;
            rmask = old_read.mask & (kFullMask >> -read_min_delta);
          }
          // Check that one set is subset of another subset.
          // I.e. only one set is allowed to have elements that is not contained in another set.
          if ((lmask | rmask) != std::max(lmask, rmask)) {
            const auto read = std::max(old_read.read_min, new_read.read_min);
            ADD_FAILURE() << "R1: " << old_read.ToString() << "\nR2: " << new_read.ToString()
                          << "\nR1-R2: " << Read::ValuesToString(read, rmask ^ (lmask & rmask))
                          << ", R2-R1: " << Read::ValuesToString(read, lmask ^ (lmask & rmask));
            stop.store(true);
          }
          return false;
        }), reads.end());
        reads.push_back(new_read);
      }
    });
  }

  thread_holder.WaitAndStop(30s);
}

// This test ensures that concurrent SELECT...FOR UPDATE queries to the same row perform conflict
// resolution and intent writes in serial with each other. It does this by setting a sync point in
// the write path code immediately after in-memory locks are acquired, which depends on a sync point
// in the test which is only hit after the test thread has spawned multiple concurrent queries and
// slept. In the time the test thread is sleeping, we expect the following sequence of events:
// 1. One of the postgres sessions successfully acquires the locks, and is hanging on the sync point
// 2. All other sessions are now blocked on the lock acquisition
// 3. The test thread sleeps
// 4. While the test thread is sleeping, some number of the sessions time out, returning error
// 5. Test thread wakes up and hits the sync point, thereby releasing the first session that was
//    waiting there with the locks.
// 6. Among those which did not timeout, they should now perform conflict resolution in serial.
//    These sessions will either
//      (1) succeed and abort any txns which have written intents
//      -or-
//      (2) fail and return error status
// 7. At the end of this, exactly one session should have successfully committed.
//
// If multiple sessions are allowed to acquire the same in-memory locks, then we will likely see
// more than one of them succeed, as they will perform conflict resolution concurrently and not see
// each others intents. This test therefore ensures with high likelihood that the in-memory locking
// is working correctly for SELECT...FOR UPDATE queries.
//
// Important note -- sync point only works in debug mode. Non-debug test runs may not catch these
// issues as reliably.
TEST_F_EX( PgTxnTest, SelectForUpdateExclusiveRead, PgTxnTestFailOnConflict) {
  // Note -- we disable wait-on-conflict behavior here because this regression test is specifically
  // targeting a bug in fail-on-conflict behavior.
  constexpr int kNumThreads = 10;
  constexpr int kNumSleepSeconds = 1;
  TestThreadHolder thread_holder;

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE test (key INT NOT NULL PRIMARY KEY, value INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO test SELECT generate_series(1, 5), 0"));

  // Ensure that any request threads which are allowed to acquire a lock on the row read below wait
  // until the SleepFor duration specified at this test's sync point to create the opportunity for
  // a race.
#ifndef NDEBUG
  SyncPoint::GetInstance()->LoadDependency({{
    "PgTxnTest::SelectForUpdateExclusiveRead::SelectComplete",
    "WriteQuery::DoExecute::PreparedDocWriteOps"
  }});
  SyncPoint::GetInstance()->EnableProcessing();
#endif // NDEBUG

  bool read_succeeded[kNumThreads] {};
  std::vector<PGConn> conns;

  for (int thread_idx = 0; thread_idx < kNumThreads; ++thread_idx) {
    conns.emplace_back(ASSERT_RESULT(Connect()));
    thread_holder.AddThreadFunctor([thread_idx, &read_succeeded, &conns] {
      auto& conn = conns[thread_idx];
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      // Each thread will try to acquire an exclusive lock on the same row. Only one thread should
      // be able to acquire it at a given time. If the code being tested has the correct behavior,
      // then we should expect one RPC thread to hold the lock for 10s while the others wait for
      // the sync point in this test to be hit. Then, each RPC thread should proceed in serial after
      // that, acquiring the lock and resolving conflicts.
      auto res = conn.FetchRow<int32_t>("SELECT value FROM test WHERE key=1 FOR UPDATE");

      read_succeeded[thread_idx] = res.ok();
      LOG(INFO) << "Thread read " << thread_idx << (res.ok() ? " succeeded" : " failed");
    });
  }

  SleepFor(1s * kNumSleepSeconds * kTimeMultiplier);
  TEST_SYNC_POINT("PgTxnTest::SelectForUpdateExclusiveRead::SelectComplete");

  thread_holder.WaitAndStop(1s * kNumSleepSeconds * kTimeMultiplier);

#ifndef NDEBUG
  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearTrace();
#endif // NDEBUG

  bool found_success = false;
  for (int thread_idx = 0; thread_idx < kNumThreads; ++thread_idx) {
    // It's possible that two threads had a successful SELECT...FOR UPDATE if a later one was
    // assigned higher priority. However, in that case, the earlier thread should not commit. In
    // general, only one thread should have a successful read and a successful commit.
    if (read_succeeded[thread_idx] && conns[thread_idx].CommitTransaction().ok()) {
      LOG(INFO) << "Read succeeded on thread_idx " << thread_idx;
      EXPECT_FALSE(found_success)
          << "Found more than one thread with successful concurrent exclusive read.";
      found_success = true;
    }
  }
  // We expect one of the threads to have succeeded.
  EXPECT_TRUE(found_success);

  // Once the successful thread commits, we should be free to read the same row without conflict.
  EXPECT_OK(setup_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  EXPECT_OK(setup_conn.Fetch("SELECT * FROM test WHERE key=1 FOR UPDATE"));
  EXPECT_OK(setup_conn.CommitTransaction());
}

} // namespace pgwrapper
} // namespace yb
