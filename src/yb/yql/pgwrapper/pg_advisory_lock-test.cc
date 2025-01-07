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

} // namespace yb::pgwrapper
