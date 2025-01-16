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

DECLARE_bool(enable_wait_queues);
DECLARE_bool(ysql_yb_enable_advisory_locks);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_uint32(num_advisory_locks_tablets);
DECLARE_uint64(pg_client_session_expiration_ms);
DECLARE_uint64(pg_client_heartbeat_interval_ms);

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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_advisory_locks_tablets) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_heartbeat_interval_ms) =
        kExpiredSessionCleanupMs / 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_session_expiration_ms) = kExpiredSessionCleanupMs;
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
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Fetch("select pg_advisory_lock(10)"));

  {
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_OK(conn2.Fetch("select pg_advisory_lock(11)"));

    auto status_future = std::async(std::launch::async, [&]() -> Status {
      RETURN_NOT_OK(conn1.Fetch("select pg_advisory_lock(11)"));
      return Status::OK();
    });
    ASSERT_NOK(conn2.Fetch("select pg_advisory_lock(10)"));
    ASSERT_NOK(status_future.get());
  }
  SleepFor(2 * kExpiredSessionCleanupMs * 1ms * kTimeMultiplier);
  ASSERT_OK(conn1.Fetch("select pg_advisory_lock(11)"));
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

} // namespace yb::pgwrapper
