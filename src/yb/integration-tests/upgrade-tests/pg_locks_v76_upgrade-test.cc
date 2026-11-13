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

// false turns enable_object_locking_for_table_locks off on every version; true keeps the build
// default, which is on in release builds. Without false, release builds would cover only the
// upgraded transaction that refreshes its catalog cache on a new lock, not the one that keeps its
// stale catalog cache. Debug builds default to off, so true would repeat false there and is
// skipped.
class PgLocksV76UpgradeTest : public UpgradeTestBase, public ::testing::WithParamInterface<bool> {
 public:
  PgLocksV76UpgradeTest() : UpgradeTestBase(kBuild_2_25_0_0) {}

 protected:
  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    if (!GetParam()) {
      for (auto* flags : {&opts.extra_master_flags, &opts.extra_tserver_flags}) {
        for (const auto* flag :
             {"enable_object_locking_for_table_locks", "ysql_yb_ddl_transaction_block_enabled",
              "ysql_enable_concurrent_ddl", "ysql_yb_enable_ddl_savepoint_support",
              "ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks"}) {
          AddUnDefOkAndSetFlag(*flags, flag, "false");
        }
      }
    }
    UpgradeTestBase::SetUpOptions(opts);
  }
};

// Verify that the pg_locks view correctly displays advisory locks and other types of locks
// during and after the upgrade.
TEST_P(PgLocksV76UpgradeTest, TestPgLocksViewAdvisoryLocksSupport) {
#ifndef NDEBUG
  if (GetParam()) {
    GTEST_SKIP() << "Object locking for table locks is off by default in debug builds";
  }
#endif
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
  // Node restarts during the upgrade trigger leader rebalancing, which may start only after the
  // locks below are taken. A transactions/pg_advisory_locks tablet leader move while the locks are
  // held can hide the transaction's locks from pg_locks.
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_load_balancing", "false"));
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("CREATE TABLE foo (id INT PRIMARY KEY, value TEXT);"));
  ASSERT_OK(conn.Execute("INSERT INTO foo VALUES (1, 'test');"));
  // Cache the pre-V76 row type of yb_lock_status (21 columns), which pg_locks expands to, in
  // conn's backend. The read in the transaction below after finalize then shows whether that
  // transaction refreshes its catalog cache: without a cached copy, the read would load the new
  // row type either way. Read before BEGIN: since #36572, a transaction that starts before
  // finalize takes local object locks, so a read in that transaction locks the view across
  // finalize, and V76 cannot replace the view.
  ASSERT_OK(conn.Fetch(kPgLocksQuery));
  // Acquire advisory locks and a row lock, and hold them across the upgrade.
  ASSERT_OK(conn.Fetch("SELECT pg_advisory_lock(1);"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn.Fetch("SELECT pg_advisory_xact_lock_shared(2,2);"));
  ASSERT_OK(conn.Fetch("SELECT * FROM foo WHERE id = 1 FOR UPDATE;"));
  // Ensure that the active locks are propagated.
  ASSERT_OK(conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));
  SleepFor(MonoDelta::FromSeconds(1 * kTimeMultiplier));

  // Check that the pg_locks view does not include the new columns,
  // classid, objid, objsubid, during the upgrade. Read from another connection, outside any
  // transaction: since #36572, a read in conn's transaction locks the view until that
  // transaction ends, and V76 cannot replace the view.
  auto observer = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(observer.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));
  auto pg_locks_result = ASSERT_RESULT(observer.FetchAllAsString(
      Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery)));
  ASSERT_EQ(pg_locks_result, "NULL, NULL, NULL, ShareLock; "
                             "NULL, NULL, NULL, ExclusiveLock");
  pg_locks_result = ASSERT_RESULT(observer.FetchAllAsString(
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

  // conn's transaction was opened before FinalizeUpgrade(), so it keeps the pre-finalize value of
  // the yb_enable_object_locking_infra auto flag, latched in StartTransaction. Its first pg_locks
  // read after finalize is a new lock for the transaction. With object locking on, a transaction
  // that started before the promotion takes local object locks and refreshes its catalog cache on
  // a new lock, as PG does (#36572), so it sees the new yb_lock_status row type. With object
  // locking off, it does not refresh mid-transaction, and the catcache stays stale (21 vs 24
  // columns).
  auto result = conn.FetchAllAsString(Format("$0 WHERE locktype = 'advisory';", kPgLocksQuery));
  if (ASSERT_RESULT(cluster_->GetFlag(
          cluster_->tablet_server(0), "enable_object_locking_for_table_locks")) == "true") {
    ASSERT_EQ(ASSERT_RESULT(std::move(result)), "2, 2, 2, ShareLock; 0, 1, 1, ExclusiveLock");
  } else {
    ASSERT_NOK_STR_CONTAINS(result, "Returned row contains 24 attributes, but query expects 21");
  }

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


INSTANTIATE_TEST_SUITE_P(
    TableLocks, PgLocksV76UpgradeTest, ::testing::Bool(), ::testing::PrintToStringParamName());

} // namespace yb
