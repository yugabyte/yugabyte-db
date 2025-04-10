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

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_local_lock_manager.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(TEST_check_broadcast_address);
DECLARE_bool(TEST_enable_object_locking_for_table_locks);
DECLARE_bool(TEST_allow_wait_for_alter_table_to_finish);
DECLARE_bool(report_ysql_ddl_txn_status_to_master);
DECLARE_bool(ysql_ddl_transaction_wait_for_ddl_verification);

using namespace std::literals;

namespace yb::pgwrapper {

constexpr uint64_t kDefaultMasterYSQLLeaseTTLMilli = 5 * 1000;
constexpr uint64_t kDefaultYSQLLeaseRefreshIntervalMilli = 500;

class PgObjectLocksTestRF1 : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;  // GH #26281
    PgMiniTestBase::SetUp();
    lock_manager_ = cluster_->mini_tablet_server(0)->server()->ts_local_lock_manager();
  }

  size_t NumTabletServers() override {
    return 1;
  }

  size_t NumGrantedLocks() const {
    return lock_manager_->TEST_GrantedLocksSize();
  }

  size_t NumWaitingLocks() const {
    return lock_manager_->TEST_WaitingLocksSize();
  }

  Status AssertNumLocks(size_t granted_locks, size_t waiting_locks) {
    SCHECK_EQ(NumGrantedLocks(), granted_locks, IllegalState,
              Format("Found mum granted locks != Expected num granted locks"));
    SCHECK_EQ(NumWaitingLocks(), waiting_locks, IllegalState,
              Format("Found mum waiting locks != Expected num waiting locks"));
    return Status::OK();
  }

  void CreateTestTable() {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(1, 10), 0"));
  }

  tserver::TSLocalLockManagerPtr lock_manager_;
};

TEST_F(PgObjectLocksTestRF1, TestSanity) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));

  // A DDL shouldn't leave behind any locks on the local TS (since it is RF1).
  ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));

  // An auto commit distributed transaction shouldn't leave behind any locks.
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(1, 10), 0"));
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));

  // A fast-path transaction shouldn't leave behing any locks.
  ASSERT_OK(conn.Execute("UPDATE test SET v=1 where k=1"));
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));

  // A read-only transaction shouldn't leave behind any locks.
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Fetch("SELECT * FROM test"));
  ASSERT_GE(NumGrantedLocks(), 1);
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));

  // An explicit distributed transaction shouldn't leave behind any locks.
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE test SET v=v+1 WHERE k>=1"));
  ASSERT_GE(NumGrantedLocks(), 1);
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));
}

TEST_F(PgObjectLocksTestRF1, TestWaitingOnConflictingLocks) {
  CreateTestTable();

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Fetch("SELECT * FROM test"));

  auto status_future = std::async(std::launch::async, [&]() -> Status {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("ALTER TABLE test ADD COLUMN v1 INT"));
    return Status::OK();
  });

  EXPECT_OK(WaitFor([&]() {
    return NumWaitingLocks() >= 1;
  }, 5s * kTimeMultiplier, "Timed out waiting for num waiting locks == 1"));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(status_future.get());
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));
}

// Test that the exclusive locks are released only after the DDL schema changes have been applied
// at the docdb layer.
#ifndef NDEBUG
TEST_F(PgObjectLocksTestRF1, ExclusiveLocksRemovedAfterDocDBSchemaChange) {
  CreateTestTable();
  // Force the alter table txn to just return success on commit, and not really wait for
  // docdb to apply the schema changes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_report_ysql_ddl_txn_status_to_master) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_ddl_transaction_wait_for_ddl_verification) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_allow_wait_for_alter_table_to_finish) = false;

  yb::SyncPoint::GetInstance()->LoadDependency({
    {"ExclusiveLocksRemovedAfterDocDBSchemaChange", "DoReleaseObjectLocksIfNecessary"}});
  yb::SyncPoint::GetInstance()->ClearTrace();
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN v1 INT"));

  auto status_future = std::async(std::launch::async, [&conn]() -> Status {
    RETURN_NOT_OK(conn.Fetch("SELECT * FROM TEST WHERE k=1"));
    return Status::OK();
  });

  EXPECT_OK(WaitFor([&]() {
    return NumWaitingLocks() >= 1;
  }, 5s * kTimeMultiplier, "Timed out waiting for num waiting locks == 1"));
  DEBUG_ONLY_TEST_SYNC_POINT("ExclusiveLocksRemovedAfterDocDBSchemaChange");
  ASSERT_OK(status_future.get());
}
#endif

class PgObjectLocksTest : public LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.emplace_back("--TEST_enable_object_locking_for_table_locks=true");
    opts->extra_tserver_flags.emplace_back("--TEST_enable_ysql_operation_lease=true");
    opts->extra_tserver_flags.emplace_back("--TEST_tserver_enable_ysql_lease_refresh=true");
    opts->extra_tserver_flags.emplace_back(
        Format("--ysql_lease_refresher_interval_ms=$0", kDefaultYSQLLeaseRefreshIntervalMilli));

    opts->extra_master_flags.emplace_back("--TEST_enable_object_locking_for_table_locks=true");
    opts->extra_master_flags.emplace_back("--TEST_enable_ysql_operation_lease=true");
    opts->extra_master_flags.emplace_back(
        Format("--master_ysql_operation_lease_ttl_ms=$0", kDefaultMasterYSQLLeaseTTLMilli));
  }

  int GetNumTabletServers() const override {
    return 3;
  }
};

TEST_F(PgObjectLocksTest, ExclusiveLockReleaseInvalidatesCatalogCache) {
  const auto ts1_idx = 1;
  const auto ts2_idx = 2;
  auto* ts1 = cluster_->tablet_server(ts1_idx);
  auto* ts2 = cluster_->tablet_server(ts2_idx);

  ts2->Shutdown();
  LogWaiter log_waiter(ts2, "Received new lease epoch");
  ASSERT_OK(ts2->Restart(
      ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      {std::make_pair("TEST_ysql_disable_transparent_cache_refresh_retry", "true")}));
  ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(kTimeMultiplier * 10)));

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));

  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));

  ASSERT_OK(conn2.FetchMatrix("SELECT * FROM test WHERE k=1", 1 /* rows */, 2 /* columns */));

  // Disable catalog cache invalidation on tserver-master heartbeat path. Set it after the tserver
  // boots up since setting it as part of initialization seems to error.
  ASSERT_OK(cluster_->SetFlag(
      ts2,
      "TEST_tserver_disable_catalog_refresh_on_heartbeat",
      "true"));

  // Release of exclusive locks of the below DDL should invalidate catalog cache on all tservers.
  ASSERT_OK(conn1.Execute("ALTER TABLE test ADD COLUMN v1 INT"));
  // The DML should now see the updated schema and not hit a catalog cache/schema version mismatch.
  ASSERT_OK(conn2.FetchMatrix("SELECT * FROM test WHERE k=1", 1 /* rows */, 3 /* columns */));
}

TEST_F(PgObjectLocksTest, ConsecutiveAltersSucceedWithoutCatalogVersionIssues) {
  const auto ts1_idx = 1;
  const auto ts2_idx = 2;
  auto* ts1 = cluster_->tablet_server(ts1_idx);
  auto* ts2 = cluster_->tablet_server(ts2_idx);

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));

  ASSERT_OK(conn1.Execute("CREATE TABLE t1(c1 INT, c2 INT)"));
  ASSERT_OK(conn2.Fetch("SELECT * FROM t1"));

  ASSERT_OK(cluster_->SetFlag(
      ts2,
      "TEST_tserver_disable_catalog_refresh_on_heartbeat",
      "true"));

  ASSERT_OK(conn1.Execute("ALTER TABLE t1 ADD COLUMN c3 INT"));
  ASSERT_OK(conn2.Execute("ALTER TABLE t1 ADD COLUMN c4 INT"));
}

TEST_F(PgObjectLocksTest, BackfillIndexSanityTest) {
  const auto ts1_idx = 1;
  const auto ts2_idx = 2;
  auto* ts1 = cluster_->tablet_server(ts1_idx);
  auto* ts2 = cluster_->tablet_server(ts2_idx);

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));

  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test select generate_series(1, 100000)"));
  ASSERT_OK(cluster_->SetFlag(
      ts2,
      "TEST_tserver_disable_catalog_refresh_on_heartbeat",
      "true"));

  ASSERT_OK(conn1.Execute("CREATE INDEX test_k ON test(k)"));

  std::string select_query = "SELECT * FROM test WHERE k=1";
  auto analyze_query = Format("EXPLAIN ANALYZE $0", Format(select_query));
  auto values = ASSERT_RESULT(conn2.FetchRows<std::string>(analyze_query));
  const auto index_only_scan_test = "Index Only Scan using test_k on test";
  const auto index_cond_text = "Index Cond: (k = 1)";
  bool found_index_only_scan = false;
  bool found_index_cond = false;
  for (const auto& value : values) {
    if (value.find(index_only_scan_test) != std::string::npos) {
      found_index_only_scan = true;
    } else if (value.find(index_cond_text) != std::string::npos) {
      found_index_cond = true;
    }
  }
  ASSERT_TRUE(found_index_only_scan && found_index_cond)
      << "Expected following text - \n"
      << "1. " << index_only_scan_test << "\n"
      << "2. " << index_cond_text << "\n"
      << "in " << analyze_query;
  ASSERT_OK(conn2.Execute("INSERT INTO test values(200000)"));
}

TEST_F(PgObjectLocksTest, ReleaseExpiredLocksInvalidatesCatalogCache) {
  const auto ts1_idx = 1;
  const auto ts2_idx = 2;
  auto* ts1 = cluster_->tablet_server(ts1_idx);
  auto* ts2 = cluster_->tablet_server(ts2_idx);

  ts2->Shutdown();
  {
    LogWaiter log_waiter(ts2, "Received new lease epoch");
    ASSERT_OK(ts2->Restart(
        ExternalMiniClusterOptions::kDefaultStartCqlProxy,
        {std::make_pair("TEST_ysql_disable_transparent_cache_refresh_retry", "true")}));
    ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(kTimeMultiplier * 10)));
  }

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));

  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));

  ASSERT_OK(conn2.FetchMatrix("SELECT * FROM test WHERE k=1", 1 /* rows */, 2 /* columns */));

  ASSERT_OK(cluster_->SetFlag(
      ts2,
      "TEST_tserver_disable_catalog_refresh_on_heartbeat",
      "true"));
  auto master_leader = cluster_->GetLeaderMaster();
  ASSERT_OK(cluster_->SetFlag(
      master_leader,
      "TEST_disable_release_object_locks_on_ddl_verification",
      "true"));

  {
    LogWaiter log_waiter(ts2,
                         "Invalidating db PgTableCache caches since catalog version incremented");
    ASSERT_OK(conn1.Execute("ALTER TABLE test ADD COLUMN v1 INT"));
    ts1->Shutdown();
    ASSERT_OK(log_waiter.WaitFor(2s * kTimeMultiplier * kDefaultMasterYSQLLeaseTTLMilli));
  }
  ASSERT_OK(conn2.FetchMatrix("SELECT * FROM test WHERE k=1", 1 /* rows */, 3 /* columns */));
  ASSERT_OK(ts1->Restart());
}

}  // namespace yb::pgwrapper
