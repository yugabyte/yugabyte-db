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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(ysql_yb_enable_advisory_locks);

namespace yb {

class PgLocksV76UpgradeTest : public UpgradeTestBase {
 public:
  PgLocksV76UpgradeTest() : UpgradeTestBase(kBuild_2_25_0_0) {}
};

// Verify that the pg_locks view correctly displays advisory locks and other types of locks
// during and after the upgrade.
TEST_F(PgLocksV76UpgradeTest, TestPgLocksViewAdvisoryLocksSupport) {
  constexpr int kMinTxnAgeMs = 0;
  const std::string kPgLocksQuery = "SELECT classid, objid, objsubid, mode FROM pg_locks";
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));

  std::vector<std::pair<std::string, std::string>> flag_pairs = {
    {"ysql_yb_enable_advisory_locks", "true"}
  };
  for (const auto& [flag, value] : flag_pairs) {
    ASSERT_OK(cluster_->AddAndSetExtraFlag(flag, value));
  }
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("CREATE TABLE foo (id INT PRIMARY KEY, value TEXT);"));
  ASSERT_OK(conn.Execute("INSERT INTO foo VALUES (1, 'test');"));
  // Acquire advisory locks and a row lock.
  ASSERT_OK(conn.Fetch("SELECT pg_advisory_lock(1);"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn.Fetch("SELECT pg_advisory_xact_lock_shared(2,2);"));
  ASSERT_OK(conn.Fetch("SELECT * FROM foo WHERE id = 1 FOR UPDATE;"));
  // Ensure that the active locks are propagated.
  ASSERT_OK(conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));
  SleepFor(MonoDelta::FromSeconds(1 * kTimeMultiplier));

  // Check that the pg_locks view does not include the new columns,
  // classid, objid, objsubid, during the upgrade.
  auto pg_locks_result = ASSERT_RESULT(conn.FetchAllAsString(
      Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "NULL, NULL, NULL, ExclusiveLock; "
                             "NULL, NULL, NULL, ShareLock");
  pg_locks_result = ASSERT_RESULT(conn.FetchAllAsString(
      Format("$0 WHERE locktype = 'row';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "NULL, NULL, NULL, STRONG_READ,STRONG_WRITE");

  // Run the ysql_migration files and update the yb_lock_status and pg_locks view.
  ASSERT_OK(FinalizeUpgrade());

  auto conn2 = ASSERT_RESULT(cluster_->ConnectToDB());
  // Check that advisory locks from pg_locks now includes the new columns after the upgrade.
  pg_locks_result = ASSERT_RESULT(conn2.FetchAllAsString(
      Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "0, 1, 1, ExclusiveLock; "
                             "2, 2, 2, ShareLock");
  // Check that row locks from pg_locks still are NULL for the new columns.
  pg_locks_result = ASSERT_RESULT(conn2.FetchAllAsString(
      Format("$0 WHERE locktype = 'row';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "NULL, NULL, NULL, STRONG_READ,STRONG_WRITE");

  // Using the existing connection will fail and cause the transaction to be aborted.
  // Explain: After the YSQL upgrade, the existing connection still uses stale catalog cache,
  // because the ongoing transaction prevents the catalog cache from refreshing.
  // With the new code, yb_lock_status returns 24 columns, but the connection expects 21 columns.
  auto result = conn.FetchAllAsString(Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery));
  ASSERT_NOK_STR_CONTAINS(
    result, "Returned row contains 24 attributes, but query expects 21");
}


} // namespace yb
