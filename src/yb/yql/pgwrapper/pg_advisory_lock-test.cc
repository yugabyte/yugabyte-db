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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(enable_wait_queues);
DECLARE_bool(ysql_yb_enable_advisory_locks);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_uint32(num_advisory_locks_tablets);
DECLARE_uint64(pg_client_session_expiration_ms);
DECLARE_uint64(pg_client_heartbeat_interval_ms);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_uint64(refresh_waiter_timeout_ms);
DECLARE_int32(pg_client_extra_timeout_ms);

namespace yb::pgwrapper {

const std::string session_level_locks[] = {
  "select pg_advisory_lock(1)",
  "select pg_advisory_unlock(1)",
  "select pg_advisory_lock_shared(1)",
  "select pg_advisory_unlock_shared(1)",
  "select pg_advisory_lock(1, 1)",
  "select pg_advisory_unlock(1, 1)",
  "select pg_advisory_lock_shared(1, 1)",
  "select pg_advisory_unlock_shared(1, 1)",
  "select pg_try_advisory_lock(1)",
  "select pg_try_advisory_lock(1, 1)",
  "select pg_try_advisory_lock_shared(1)",
  "select pg_try_advisory_lock_shared(1, 1)",
  "select pg_advisory_unlock_all()",
};

const std::string xact_level_locks[] = {
  "select pg_advisory_xact_lock(1)",
  "select pg_advisory_xact_lock_shared(1)",
  "select pg_advisory_xact_lock(1, 1)",
  "select pg_advisory_xact_lock_shared(1, 1)",
  "select pg_try_advisory_xact_lock(1)",
  "select pg_try_advisory_xact_lock(1, 1)",
  "select pg_try_advisory_xact_lock_shared(1)",
  "select pg_try_advisory_xact_lock_shared(1, 1)",
};

class PgAdvisoryLockTestBase : public PgMiniTestBase {
 protected:
  void CheckStmtNotFullyImplemented(const std::string& stmt) {
    auto conn = ASSERT_RESULT(Connect());
    auto status = conn.Execute(stmt);
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.message().ToBuffer(),
                        "session-level advisory locks are not yet implemented");
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    PgMiniTestBase::SetUp();
  }

  static constexpr int kExpiredSessionCleanupMs = 3000;
};

class PgAdvisoryLockTest : public PgAdvisoryLockTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_advisory_locks_tablets) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_heartbeat_interval_ms) =
        kExpiredSessionCleanupMs / 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_session_expiration_ms) = kExpiredSessionCleanupMs;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = MaxQueryLayerRetriesConf(5);
    PgAdvisoryLockTestBase::SetUp();
  }
};


TEST_F(PgAdvisoryLockTest, TwoSessionsWithDependencies) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  // Verify session-level locks.
  ASSERT_OK(conn1.Execute("CREATE TABLE test_table (id INT PRIMARY KEY, value INT);"));
  ASSERT_OK(conn1.Execute("INSERT INTO test_table (id, value) VALUES (0, 0);"));
  ASSERT_OK(conn1.Fetch("SELECT pg_advisory_lock(10);"));
  ASSERT_OK(conn1.Execute("UPDATE test_table SET value = value + 1 WHERE id = 0;"));
  std::future<Status> conn2_session_lock_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn2.Fetch("SELECT pg_advisory_lock(10);"));
    return Status::OK();
  });
  // conn2 attempt to acquire session-level lock on the same key should block.
  ASSERT_EQ(conn2_session_lock_future.wait_for(
      std::chrono::seconds(1)), std::future_status::timeout);
  ASSERT_TRUE(ASSERT_RESULT(conn1.FetchRow<bool>("SELECT pg_advisory_unlock(10);")));
  // Unlocking the conn1 session-level lock should allow conn2 session-level lock to proceed.
  ASSERT_OK(conn2_session_lock_future.get());
  ASSERT_OK(conn2.Execute("UPDATE test_table SET value = value + 1 WHERE id = 0;"));
  auto result =
      ASSERT_RESULT(conn2.FetchRow<int32_t>("SELECT value FROM test_table WHERE id = 0;"));
  ASSERT_EQ(result, 2);
  ASSERT_TRUE(ASSERT_RESULT(conn2.FetchRow<bool>("SELECT pg_advisory_unlock(10);")));

  // Verify transaction-level locks.
  ASSERT_OK(conn1.Execute("BEGIN;"));
  ASSERT_OK(conn1.Fetch("SELECT pg_advisory_xact_lock(10);"));
  ASSERT_OK(conn1.Execute("UPDATE test_table SET value = value + 1 WHERE id = 0;"));
  std::future<Status> conn2_txn_lock_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn2.Execute("BEGIN;"));
    RETURN_NOT_OK(conn2.Fetch("SELECT pg_advisory_xact_lock(10);"));
    return Status::OK();
  });
  // conn2 attempt to acquire xact lock on the same key should block.
  ASSERT_EQ(conn2_txn_lock_future.wait_for(
      std::chrono::seconds(1)), std::future_status::timeout);
  ASSERT_OK(conn1.Execute("COMMIT;"));
  // Unlocking the conn1 xact lock should allow conn2 xact lock to proceed.
  ASSERT_OK(conn2_txn_lock_future.get());
  ASSERT_OK(conn2.Execute("UPDATE test_table SET value = value + 1 WHERE id = 0;"));
  ASSERT_OK(conn2.Execute("COMMIT;"));
  result =
      ASSERT_RESULT(conn2.FetchRow<int32_t>("SELECT value FROM test_table WHERE id = 0;"));
  ASSERT_EQ(result, 4);
}

TEST_F(PgAdvisoryLockTest, AcquireXactLocksInDifferentDBs) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE db1"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Fetch("select pg_advisory_xact_lock(1)"));
  auto conn2 = ASSERT_RESULT(ConnectToDB("db1"));
  ASSERT_OK(conn2.Fetch("select pg_advisory_xact_lock(1)"));
  ASSERT_OK(conn.CommitTransaction());
}

TEST_F(PgAdvisoryLockTest, SessionAdvisoryLockAndUnlock) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
  ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("select pg_advisory_unlock_shared(10)")));

  ASSERT_OK(conn.Fetch("select pg_advisory_lock_shared(10)"));
  ASSERT_OK(conn.Fetch("select pg_advisory_unlock_shared(10)"));

  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("select pg_advisory_unlock_shared(10)")));

  ASSERT_OK(conn.Fetch("select pg_advisory_unlock(10)"));
  ASSERT_OK(conn.Fetch("select pg_advisory_unlock(10)"));

  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("select pg_advisory_unlock(10)")));
}

TEST_F(PgAdvisoryLockTest, CleanupSessionAdvisoryLock) {
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
  }
  SleepFor(2 * kExpiredSessionCleanupMs * 1ms * kTimeMultiplier);
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
  ASSERT_OK(conn.Fetch("select pg_advisory_unlock(10)"));

  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("select pg_advisory_unlock(10)")));
}

// Verify that session-level and transaction-level advisory locks within the same session
// coexist without conflicts.
TEST_F(PgAdvisoryLockTest, VerifyNoConflictBetweenSessionAndTransactionLocks) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Fetch("select pg_advisory_xact_lock(10)"));
  ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
  ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>("select pg_advisory_unlock(10)")));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>("select pg_advisory_unlock(10)")));
  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("select pg_advisory_unlock(10)")));
}

TEST_F(PgAdvisoryLockTest, SessionAdvisoryLocksDeadlock) {
  std::atomic<bool> deadlock_detected{false};
  CountDownLatch num_selects(2);

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
    num_selects.CountDown();
    ASSERT_TRUE(num_selects.WaitFor(5s * kTimeMultiplier));
    if (!conn.Fetch("select pg_advisory_lock(11)").ok()) {
      LOG(INFO) << "Connection 1 deadlocked";
      deadlock_detected = true;
    }
  });

  thread_holder.AddThreadFunctor([&] {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Fetch("select pg_advisory_lock(11)"));
    num_selects.CountDown();
    ASSERT_TRUE(num_selects.WaitFor(5s * kTimeMultiplier));
    if (!conn.Fetch("select pg_advisory_lock(10)").ok()) {
      LOG(INFO) << "Connection 2 deadlocked";
      deadlock_detected = true;
    }
  });
  thread_holder.JoinAll();
  SleepFor(2 * kExpiredSessionCleanupMs * 1ms * kTimeMultiplier);
  ASSERT_TRUE(deadlock_detected);
}

TEST_F(PgAdvisoryLockTest, SessionAdvisoryLockReleaseUnlocksWaiters) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Fetch("select pg_advisory_lock(10)"));

  auto conn2 = ASSERT_RESULT(Connect());
  auto status_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn2.Fetch("select pg_advisory_lock(10)"));
    return Status::OK();
  });

  ASSERT_OK(conn1.Fetch("select pg_advisory_unlock(10)"));
  ASSERT_OK(status_future.get());
}

TEST_F(PgAdvisoryLockTest, SessionLockDeadlockWithRowLocks) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO foo SELECT generate_series(0, 11), 0"));

  ASSERT_OK(conn1.Fetch("select pg_advisory_lock(10)"));

  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn2.Execute("UPDATE foo SET v=v+1 WHERE k=1"));

  auto status_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn2.Fetch("select pg_advisory_lock(10)"));
    return Status::OK();
  });
  // Introduce a deadlock with session lock and row lock. Eventually conn1 will go through
  // because of retries. conn2 cannot be retried since it is not the first statement.
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn1.Execute("UPDATE foo SET v=v+1 where k=1"));
  ASSERT_TRUE(status_future.wait_for(2s * kTimeMultiplier) == std::future_status::ready);
  ASSERT_NOK(status_future.get());
  ASSERT_OK(conn2.RollbackTransaction());
  ASSERT_OK(conn1.CommitTransaction());
}

TEST_F(PgAdvisoryLockTest, YB_DISABLE_TEST_IN_TSAN(PgLocksSanityTest)) {
  constexpr int kMinTxnAgeMs = 0;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO foo SELECT generate_series(0, 11), 0"));

  ASSERT_OK(conn.Fetch("select pg_advisory_lock(10)"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn.Execute("UPDATE foo SET v=v+1 where k=1"));

  // Ensure that the active locks are visible in pg_locks.
  ASSERT_OK(conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));
  SleepFor(MonoDelta::FromSeconds(1 * kTimeMultiplier));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64>(
      "SELECT COUNT(DISTINCT(ybdetails->>'transactionid')) FROM pg_locks")), 2);
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(conn.Fetch("select pg_advisory_unlock(10)"));
}

TEST_F(PgAdvisoryLockTest, YB_DISABLE_TEST_IN_TSAN(PgLocksWithWaiters)) {
  constexpr int kMinTxnAgeMs = 0;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));

  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Fetch("select pg_advisory_lock(10)"));

  auto conn2 = ASSERT_RESULT(Connect());
  SyncPoint::GetInstance()->LoadDependency({
      {"WaitQueue::Impl::SetupWaiterUnlocked:1", "PgLocksWithWaiters"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();
  auto status_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn2.Fetch("select pg_advisory_lock(10)"));
    return Status::OK();
  });

  TEST_SYNC_POINT("PgLocksWithWaiters");
  SleepFor(MonoDelta::FromSeconds(1 * kTimeMultiplier));
  // Session advisory lock waiting on session lock.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64>(
      "SELECT COUNT(DISTINCT(ybdetails->>'transactionid')) FROM pg_locks WHERE NOT GRANTED")), 1);

  ASSERT_OK(conn1.Fetch("select pg_advisory_unlock(10)"));
  ASSERT_OK(status_future.get());

  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();
  status_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_xact_lock(10)"));
    return Status::OK();
  });
  TEST_SYNC_POINT("PgLocksWithWaiters");
  SleepFor(MonoDelta::FromSeconds(1 * kTimeMultiplier));
  // Transaction advisory lock waiting on session lock.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64>(
      "SELECT COUNT(DISTINCT(ybdetails->>'transactionid')) FROM pg_locks WHERE NOT GRANTED")), 1);

  ASSERT_OK(conn2.Fetch("select pg_advisory_unlock(10)"));
  ASSERT_OK(status_future.get());

  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();
  status_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn2.Fetch("select pg_advisory_xact_lock(10)"));
    return Status::OK();
  });
  TEST_SYNC_POINT("PgLocksWithWaiters");
  SleepFor(MonoDelta::FromSeconds(1 * kTimeMultiplier));
  // Transaction advisory lock waiting on transaction advisory lock.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64>(
      "SELECT COUNT(DISTINCT(ybdetails->>'transactionid')) FROM pg_locks WHERE NOT GRANTED")), 1);
  ASSERT_OK(conn1.CommitTransaction());
  ASSERT_OK(status_future.get());
  ASSERT_OK(conn2.CommitTransaction());
}

TEST_F(PgAdvisoryLockTest, VerifyLockTimeout) {
  constexpr int kLockTimeoutDurationMs = 3000;
  constexpr int kExtraTimeoutMs = 1000;

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Fetch("SELECT pg_advisory_lock(10);"));

  ASSERT_OK(conn2.ExecuteFormat("SET lock_timeout='$0ms';", kLockTimeoutDurationMs));
  std::future<Status> conn2_session_lock_future = std::async(std::launch::async, [&]() -> Status {
    RETURN_NOT_OK(conn2.Fetch("SELECT pg_advisory_lock(10);"));
    return Status::OK();
  });

  // Wait briefly to confirm that conn2 is actually blocking on the lock.
  ASSERT_EQ(conn2_session_lock_future.wait_for(
      std::chrono::seconds(1)), std::future_status::timeout);

  auto expected_timeout_ms =
      kLockTimeoutDurationMs + FLAGS_pg_client_extra_timeout_ms + kExtraTimeoutMs;
  ASSERT_EQ(conn2_session_lock_future.wait_for(
      std::chrono::milliseconds(expected_timeout_ms * kTimeMultiplier)), std::future_status::ready);

  // Verify the lock attempt fails with advisory lock timeout error.
  Status result = conn2_session_lock_future.get();
  ASSERT_NOK(result);
  // Most build types return "Timed out" while Mac returns "timed out". Ignore the 'T'.
  ASSERT_STR_CONTAINS(result.ToString(), "imed out waiting for Acquire Advisory Lock");
}

TEST_F(PgAdvisoryLockTest, ToggleAdvisoryLockFlag) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Fetch("SELECT pg_advisory_lock(10);"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = false;
  // Verify that lock attempts fail with advisory lock disabled error.
  auto s = conn.Fetch("SELECT pg_advisory_lock(10);");
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.status().message().ToBuffer(), "ERROR:  advisory locks are disabled");
  // Unlock should succeed, this because it ensures that any acquired advisory locks can
  // still be released even if user disables the ysql_yb_enable_advisory_locks flag at runtime
  ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>("SELECT pg_advisory_unlock(10);")));
  // Verify that unlock attempt return false because the lock is already released.
  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>("SELECT pg_advisory_unlock(10);")));
}

} // namespace yb::pgwrapper
