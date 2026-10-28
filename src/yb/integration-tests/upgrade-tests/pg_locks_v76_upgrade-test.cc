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
  ASSERT_EQ(pg_locks_result, "NULL, NULL, NULL, ShareLock; "
                             "NULL, NULL, NULL, ExclusiveLock");
  pg_locks_result = ASSERT_RESULT(conn.FetchAllAsString(
      Format("$0 WHERE locktype = 'row';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "NULL, NULL, NULL, STRONG_READ,STRONG_WRITE");

  // Run the ysql_migration files and update the yb_lock_status and pg_locks view.
  ASSERT_OK(FinalizeUpgrade());

  auto conn2 = ASSERT_RESULT(cluster_->ConnectToDB());
  // Check that advisory locks from pg_locks now includes the new columns after the upgrade.
  pg_locks_result = ASSERT_RESULT(conn2.FetchAllAsString(
      Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "2, 2, 2, ShareLock; "
                             "0, 1, 1, ExclusiveLock");
  // Check that row locks from pg_locks still are NULL for the new columns.
  pg_locks_result = ASSERT_RESULT(conn2.FetchAllAsString(
      Format("$0 WHERE locktype = 'row';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "NULL, NULL, NULL, STRONG_READ,STRONG_WRITE");

  // conn's transaction was opened before FinalizeUpgrade(), and the enable_object_locking_infra
  // auto flag is latched once per transaction in StartTransaction (c60f1cba1cd), so this
  // transaction keeps the pre-finalize value no matter how enable_object_locking_for_table_locks
  // is set. AcceptInvalidationMessages() therefore skips the full cache invalidation and the
  // catcache stays stale (21 vs 24 columns).
  auto result = conn.FetchAllAsString(Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery));
  ASSERT_NOK_STR_CONTAINS(result, "Returned row contains 24 attributes, but query expects 21");

  // Once the transaction block ends, YBCheckSharedCatalogCacheVersion() stops bailing out on
  // IsTransactionOrTransactionBlock() and refreshes the catalog cache, so the same connection
  // renders the new columns. That path does not consult object locking; the latch above only
  // decides whether a promotion is honored mid-transaction.
  // Rolling back also released the xact-level advisory lock and reverted the yb_locks_min_txn_age
  // set inside the transaction, so only the session-level lock remains to report.
  ASSERT_OK(conn.RollbackTransaction());
  ASSERT_OK(conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));
  SleepFor(MonoDelta::FromSeconds(1 * kTimeMultiplier));
  pg_locks_result = ASSERT_RESULT(conn.FetchAllAsString(
      Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "0, 1, 1, ExclusiveLock");
}


} // namespace yb
