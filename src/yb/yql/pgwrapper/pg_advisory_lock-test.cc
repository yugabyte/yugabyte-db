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
};

class PgAdvisoryLockTest : public PgAdvisoryLockTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_advisory_locks_tablets) = 1;
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

class PgAdvisoryLockNotSupportedTest : public PgAdvisoryLockTestBase {
 protected:
  void CheckStmtNotSupported(const std::string& stmt) {
    auto conn = ASSERT_RESULT(Connect());
    auto status = conn.Execute(stmt);
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), "advisory locks are not yet implemented");
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = false;
    PgAdvisoryLockTestBase::SetUp();
  }
};

TEST_F(PgAdvisoryLockNotSupportedTest, AdvisoryLockNotSupported) {
  for (const auto& session_level_lock : session_level_locks) {
    CheckStmtNotSupported(session_level_lock);
  }
  for (const auto& xact_level_lock : xact_level_locks) {
    CheckStmtNotSupported(xact_level_lock);
  }
}

} // namespace yb::pgwrapper
