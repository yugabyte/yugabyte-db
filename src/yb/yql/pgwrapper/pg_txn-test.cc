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

#include <gtest/gtest.h>

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using std::string;

using namespace std::literals;

DECLARE_bool(TEST_fail_in_apply_if_no_metadata);
DECLARE_bool(TEST_running_test);
DECLARE_bool(enable_wait_queues);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_int32(replication_factor);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(rocksdb_universal_compaction_min_merge_width);
DECLARE_int32(rocksdb_universal_compaction_size_ratio);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(txn_max_apply_batch_records);
DECLARE_int64(db_filter_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_string(time_source);
DECLARE_uint64(rocksdb_universal_compaction_always_include_size_threshold);
DECLARE_bool(pg_client_use_shared_memory);

namespace yb::pgwrapper {

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
  auto original_read_committed_setting = FLAGS_yb_enable_read_committed_isolation;

  // Ensure the original setting is restored at the end of this scope
  auto scope_exit = ScopeExit([original_read_committed_setting]() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) =
        original_read_committed_setting;
  });

  if (FLAGS_yb_enable_read_committed_isolation) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = false;
    ASSERT_OK(RestartCluster());
  }

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

struct Configuration {
  std::optional<size_t> num_tservers = std::nullopt;
  std::optional<bool> enable_read_committed = std::nullopt;
  std::optional<bool> client_use_shared_memory = std::nullopt;
};

template<class T>
std::optional<T> Merge(const std::optional<T>& o1, const std::optional<T>& o2) {
  return o2.has_value() ? o2 : o1;
}

Configuration Merge(const Configuration& c1, const Configuration& c2) {
  return {
      .num_tservers = Merge(c1.num_tservers, c2.num_tservers),
      .enable_read_committed = Merge(c1.enable_read_committed, c2.enable_read_committed),
      .client_use_shared_memory = Merge(c1.client_use_shared_memory, c2.client_use_shared_memory)};
}

template<class T>
void UpdateFlag(T& flag, const std::optional<T>& value) {
  if (value) {
    ANNOTATE_UNPROTECTED_WRITE(flag) = *value;
  }
}

template<const Configuration& Config>
class ConfigurableTest : public PgMiniTestBase {
 public:
  size_t NumTabletServers() override {
    return Config.num_tservers ? *Config.num_tservers : PgMiniTestBase::NumTabletServers();
  }

  void SetUp() override {
    UpdateFlag(FLAGS_yb_enable_read_committed_isolation, Config.enable_read_committed);
    UpdateFlag(FLAGS_pg_client_use_shared_memory, Config.client_use_shared_memory);
    PgMiniTestBase::SetUp();
  }
};

const Configuration kRF1Config = { .num_tservers = 1 };

using PgTxnRF1Test = ConfigurableTest<kRF1Config>;

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
    EnableFailOnConflict();
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
TEST_F_EX(PgTxnTest, SelectForUpdateExclusiveRead, PgTxnTestFailOnConflict) {
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

const auto kReadCommittedConfig = Merge(kRF1Config, { .enable_read_committed = true });

using PgReadCommittedTxnTest = ConfigurableTest<kReadCommittedConfig>;

// The test check that read committed transaction detects conflict while reading at multiple
// timestamps. The UPDATE statement and nested function slow_down uses different read timestamps.
// And the UPDATE statement are able to detect conflict when nested function returns control.
TEST_F_EX(PgTxnTest, ReadAtMultipleTimestampsWithConflict, PgReadCommittedTxnTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT, v INT, PRIMARY KEY(k ASC))"));
  ASSERT_OK(conn.Execute("CREATE TABLE aux_t (k INT, v INT, PRIMARY KEY(k ASC))"));
  ASSERT_OK(conn.Execute("INSERT INTO aux_t VALUES(1, 1)"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION slow_down(n INT) RETURNS INT AS '"
      "  DECLARE"
      "    vvv INT;"
      "  BEGIN "
      "    PERFORM pg_sleep(1);"
      "    SELECT v FROM aux_t WHERE k = 1 INTO vvv;"
      "    RETURN n + vvv / vvv - 1;"
      "  END;' language plpgsql"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1, 1), (2, 2), (3, 3), (4, 4), (5, 5)"));
  {
    CountDownLatch latch(1);
    auto aux_conn = ASSERT_RESULT(SetDefaultTransactionIsolation(
        Connect(), IsolationLevel::READ_COMMITTED));
    TestThreadHolder holder;
    holder.AddThreadFunctor([&aux_conn, &latch]{
        latch.CountDown();
        ASSERT_OK(aux_conn.Execute("UPDATE t SET v = v + 10 WHERE slow_down(k) = k"));
    });
    latch.Wait();
    SleepFor(NonTsanVsTsan(2s, 4s));
    ASSERT_OK(conn.Execute("UPDATE t SET v = v + 1000 WHERE k = 5"));
  }
  auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>("SELECT * FROM t")));
  ASSERT_EQ(rows, (decltype(rows){{1, 11}, {2, 12}, {3, 13}, {4, 14}, {5, 1015}}));
}

// The test check that statement in read committed transaction may use different read timestamps.
// In current test statement initiate the read and when perform a long sleep. After some delay
// another transaction updates all records in the table. Statement continues reading from the table
// and returns data prior to modification.
// Note: There is no read restart after the data modification, because modification is performed
// after delay and DB can detect that it was performed after the read start.
TEST_F_EX(PgTxnTest, ReadAtMultipleTimestamps, PgReadCommittedTxnTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET yb_fetch_row_limit = 1"));
  ASSERT_OK(conn.Execute("CREATE TABLE t (pk INT, v INT, PRIMARY KEY(pk ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION fresh_v(k INT) RETURNS INT AS '"
      "  DECLARE"
      "    v INT;"
      "  BEGIN "
      "    SELECT t.v FROM t WHERE t.pk = k INTO v;"
      "    RETURN v;"
      "  END;' language plpgsql"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION same_value_with_sleep(k INT) RETURNS INT AS '"
      "  DECLARE"
      "    v INT;"
      "  BEGIN "
      "    IF k = 1 THEN "
      "      PERFORM pg_sleep(10); "
      "      v := fresh_v(k); "
      "      IF v <> 1010 THEN "
      "        RAISE EXCEPTION ''Bad value %'', v; "
      "      END IF;"
      "    END IF;"
      "    RETURN k;"
      "  END;' language plpgsql"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION same_value(k INT) RETURNS INT AS 'BEGIN RETURN k; END;' language plpgsql"));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i * 10 FROM generate_series(1, 10) AS i"));
  {
    CountDownLatch latch(2);
    auto aux_conn = ASSERT_RESULT(SetDefaultTransactionIsolation(
        Connect(), IsolationLevel::READ_COMMITTED));
    TestThreadHolder holder;
    holder.AddThreadFunctor([&aux_conn, &latch]{
        latch.CountDown();
        latch.Wait();
        SleepFor(NonTsanVsTsan(2s, 4s));
        ASSERT_OK(aux_conn.Execute("UPDATE t SET v = v + 1000"));
    });
    latch.CountDown();
    latch.Wait();
    auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t, int32_t, int32_t>(
        "SELECT t_1.pk, t_1.v, t_2.v, fresh_v(t_1.pk)"
        "  FROM t AS t_1 INNER JOIN t AS t_2 ON (t_1.pk = same_value(t_2.pk))"
        "  WHERE t_1.pk = same_value_with_sleep(t_1.pk)"
        "  ORDER BY t_1.pk")));
    // Check that only last column (i.e. fresh_v(t_1.pk)) reads data with modification
    ASSERT_EQ(
        rows,
        (decltype(rows){
            {1, 10, 10, 1010},
            {2, 20, 20, 1020},
            {3, 30, 30, 1030},
            {4, 40, 40, 1040},
            {5, 50, 50, 1050},
            {6, 60, 60, 1060},
            {7, 70, 70, 1070},
            {8, 80, 80, 1080},
            {9, 90, 90, 1090},
            {10, 100, 100, 1100}}));
  }
}

TEST_F(PgTxnTest, MultiInsertUpdate) {
  constexpr int kBig = 100000000;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test (id INT PRIMARY KEY, value INT)"));
  TestThreadHolder thread_holder;
  // Use the same type as key/value in the table.
  std::atomic<int> counter = 0;
  for (int i = 0; i != 8; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop_flag = thread_holder.stop_flag(), &counter] {
      auto conn = ASSERT_RESULT(Connect());
      std::vector<int> values(5);
      while (!stop_flag.load()) {
        std::generate(values.begin(), values.end(), [&counter]() {return ++counter; });
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        for (auto value : values) {
          ASSERT_OK(conn.ExecuteFormat(
              "INSERT INTO test VALUES ($0, $1)", value, value + kBig));
        }
        ASSERT_OK(conn.CommitTransaction());
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        for (auto value : values) {
          ASSERT_OK(conn.ExecuteFormat(
              "UPDATE test SET value = $0 WHERE id = $0", value));
        }
        ASSERT_OK(conn.CommitTransaction());
      }
    });
  }
  thread_holder.WaitAndStop(3s);

  auto res = ASSERT_RESULT((conn.FetchRows<int, int>("SELECT * FROM test ORDER BY id")));
  ASSERT_EQ(res.size(), counter.load());
  int i = 0;
  for (auto [key, value] : res) {
    ++i;
    ASSERT_EQ(i, key);
    ASSERT_EQ(i, value);
  }
}

TEST_F(PgTxnTest, CleanupIntentsDuringShutdown) {
  constexpr int kNumRows = 256;
  constexpr int64_t kExpectedSum = (kNumRows + 1) * kNumRows / 2;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id INT PRIMARY KEY, value INT)"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test SELECT generate_series(1, $0), 0", kNumRows));

  auto tablets = ASSERT_RESULT(ListTabletsForTableName(cluster_.get(), "test"));
  for (const auto& tablet : tablets) {
    tablet->TEST_SleepBeforeApplyIntents(1s * kTimeMultiplier);
    tablet->TEST_SleepBeforeDeleteIntentsFile(1s * kTimeMultiplier);
  }

  ASSERT_OK(conn.CommitTransaction());
  auto sum = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT SUM(id) FROM test"));
  ASSERT_EQ(sum, kExpectedSum);
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  sum = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT SUM(id) FROM test"));
  ASSERT_EQ(sum, kExpectedSum);
}

TEST_F(PgTxnTest, FlushLargeTransaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) = 14;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_filter_block_size_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 128_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_universal_compaction_size_ratio) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_universal_compaction_always_include_size_threshold) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_universal_compaction_min_merge_width) = 2;

  constexpr auto kTxnRows = 15;
  constexpr auto kValueLen = 16_KB;
  constexpr auto kExtraRows = 1;
  constexpr auto kExtraValueLen = 16_KB;

  auto random_string_expr = Format(
      "array_to_string(ARRAY("
          "SELECT chr((97 + round(random() * 25))::INT) FROM generate_series(1, $0)), '')",
      kValueLen);

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (id INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test SELECT generate_series(1, $0), $1", kTxnRows, random_string_expr));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  // At this point we should have single SST file with primary_schema_version 0
  // (before fix for GHI #25106 primary_schema_version was empty).

  // This flush is not necessary to reproduce the bug, but there is some logic to check
  // that test itself is not broken and verifies desired behaviour.
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  auto tablets = ASSERT_RESULT(ListTabletsForTableName(
      cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(tablets.size(), 1);
  auto leader = tablets[0];
  std::set<uint64_t> name_ids;
  auto two_files_waiter = [leader, &name_ids]() -> Result<bool> {
    auto files = leader->doc_db().regular->GetLiveFilesMetaData();
    LOG(INFO) << leader->LogPrefix() << "Files: " << AsString(files);
    if (files.size() != 2) {
      return false;
    }
    name_ids.clear();
    for (const auto& file : files) {
      name_ids.insert(file.name_id);
    }
    return true;
  };
  ASSERT_OK(WaitFor(two_files_waiter, 10s, "Wait for second SST file to flush"));
  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN v2 TEXT"));

  for (int i = kTxnRows; i++ <= kTxnRows + kExtraRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test VALUES($0, '$1', '$1')", i, RandomHumanReadableString(kExtraValueLen)));
    ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
    ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  }

  auto old_name_ids = name_ids;
  // This logic ensures the tserver's behavior remains unchanged and validates that the test
  // still confirms the expected functionality.
  ASSERT_OK(WaitFor(two_files_waiter, 10s, "Wait for compaction"));
  // Check that the first file (before fix w/o primary_schema_version) still there.
  ASSERT_EQ(*old_name_ids.begin(), *name_ids.begin());
  // Check that other files were compacted.
  // Since we have 0 retention, this file will contain only rows with most recent schema version.
  ASSERT_NE(*(++old_name_ids.begin()), *(++name_ids.begin()));

  auto res = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT SUM(LENGTH(value)) FROM test"));
  ASSERT_EQ(res, kValueLen * kTxnRows + kExtraValueLen * kExtraRows);
}

const auto kReadCommittedClientWithoutSharedMemoryConfig =
    Merge(kReadCommittedConfig, { .client_use_shared_memory = false });

// The test checks absence of error in case of using user defined function as a filter.
TEST_F_EX(
    PgTxnTest, ReadCommitedFilterFunction,
    ConfigurableTest<kReadCommittedClientWithoutSharedMemoryConfig>) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET yb_fetch_row_limit = 1"));
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT, v INT, PRIMARY KEY(k ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION fetch_limited_v(key INT, l INT) RETURNS INT AS '"
      "  DECLARE"
      "    v INT;"
      "  BEGIN "
      "    SELECT t.v FROM t WHERE t.k = key AND t.v < l INTO v;"
      "    RETURN v;"
      "  END;' language plpgsql"));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 10) AS i"));
  for (size_t i = 0; i < 200; ++i) {
    const auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(
        "SELECT * FROM t WHERE k = fetch_limited_v(k, 6) ORDER BY k")));
    ASSERT_EQ(rows, (decltype(rows){{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}}));
  }
}

} // namespace yb::pgwrapper
