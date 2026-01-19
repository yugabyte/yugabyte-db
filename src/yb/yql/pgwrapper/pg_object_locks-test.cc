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

#include "yb/docdb/object_lock_shared_state_manager.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_local_lock_manager.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/object_lock_info_manager.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/monotime.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(enable_object_lock_fastpath);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(report_ysql_ddl_txn_status_to_master);
DECLARE_bool(ysql_ddl_transaction_wait_for_ddl_verification);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(TEST_allow_wait_for_alter_table_to_finish);
DECLARE_bool(TEST_check_broadcast_address);

DECLARE_string(TEST_block_alter_table);

DECLARE_uint64(pg_client_session_expiration_ms);
DECLARE_uint64(pg_client_heartbeat_interval_ms);
DECLARE_uint64(refresh_waiter_timeout_ms);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_uint64(master_ysql_operation_lease_ttl_ms);
DECLARE_bool(TEST_tserver_enable_ysql_lease_refresh);
DECLARE_int64(olm_poll_interval_ms);
DECLARE_string(vmodule);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_int32(pg_client_extra_timeout_ms);
DECLARE_bool(TEST_olm_serve_redundant_lock);

using namespace std::literals;

namespace yb::pgwrapper {

constexpr uint64_t kDefaultMasterYSQLLeaseTTLMilli = 5 * 1000;
constexpr uint64_t kDefaultYSQLLeaseRefreshIntervalMilli = 500;
constexpr uint64_t kDefaultLockManagerPollIntervalMs = 100;

class PgObjectLocksTestRF1 : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vmodule) = "libpq_utils=1";
    // Set reasonable high poll interavl to disable polling in tests., would help catch issues
    // with signaling logic if any.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_olm_poll_interval_ms) = 1000000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ysql_operation_lease_ttl_ms) =
        kDefaultMasterYSQLLeaseTTLMilli;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_enable_ysql_lease_refresh) =
        kDefaultYSQLLeaseRefreshIntervalMilli;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;  // GH #26281
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    PgMiniTestBase::SetUp();
    Init();
  }

  void Init() {
    ts_lock_manager_ = cluster_->mini_tablet_server(0)->server()->ts_local_lock_manager();
    master_lock_manager_ = cluster_->mini_master()
                               ->master()
                               ->catalog_manager_impl()
                               ->object_lock_info_manager()
                               ->TEST_ts_local_lock_manager();
  }

  size_t NumTabletServers() override {
    return 1;
  }

  size_t NumGrantedLocks() const {
    return ts_lock_manager_->TEST_GrantedLocksSize();
  }

  size_t NumWaitingLocks() const {
    return ts_lock_manager_->TEST_WaitingLocksSize();
  }

  size_t NumGrantedLocksOnMaster() const {
    return master_lock_manager_->TEST_GrantedLocksSize();
  }

  size_t NumWaitingLocksOnMaster() const {
    return master_lock_manager_->TEST_WaitingLocksSize();
  }

  Status AssertNumLocks(size_t granted_locks, size_t waiting_locks) {
    SCHECK_EQ(NumGrantedLocks(), granted_locks, IllegalState,
              Format("Found num granted locks != Expected num granted locks"));
    SCHECK_EQ(NumWaitingLocks(), waiting_locks, IllegalState,
              Format("Found num waiting locks != Expected num waiting locks"));
    return Status::OK();
  }

  void CreateTestTable() {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(1, 10), 0"));
  }

  void TestAllBlockingPairs(bool test_pg_locks) {
    static const std::vector<std::pair<std::string, std::string>> kBlockingPairs = {
      {"ACCESS EXCLUSIVE", "ACCESS SHARE"},
      {"ACCESS EXCLUSIVE", "ROW SHARE"},
      {"ACCESS EXCLUSIVE", "ROW EXCLUSIVE"},
      {"ACCESS EXCLUSIVE", "SHARE UPDATE EXCLUSIVE"},
      {"ACCESS EXCLUSIVE", "SHARE"},
      {"ACCESS EXCLUSIVE", "SHARE ROW EXCLUSIVE"},
      {"ACCESS EXCLUSIVE", "EXCLUSIVE"},
      {"ACCESS EXCLUSIVE", "ACCESS EXCLUSIVE"},
      {"EXCLUSIVE", "ROW SHARE"},
      {"EXCLUSIVE", "ROW EXCLUSIVE"},
      {"EXCLUSIVE", "SHARE UPDATE EXCLUSIVE"},
      {"EXCLUSIVE", "SHARE"},
      {"EXCLUSIVE", "SHARE ROW EXCLUSIVE"},
      {"EXCLUSIVE", "EXCLUSIVE"},
      {"SHARE ROW EXCLUSIVE", "ROW EXCLUSIVE"},
      {"SHARE ROW EXCLUSIVE", "SHARE UPDATE EXCLUSIVE"},
      {"SHARE ROW EXCLUSIVE", "SHARE"},
      {"SHARE ROW EXCLUSIVE", "SHARE ROW EXCLUSIVE"},
      {"SHARE", "ROW EXCLUSIVE"},
      {"SHARE", "SHARE UPDATE EXCLUSIVE"},
      {"SHARE UPDATE EXCLUSIVE", "SHARE UPDATE EXCLUSIVE"},
    };

    CreateTestTable();
    auto conn1 = ASSERT_RESULT(Connect());
    ASSERT_OK(conn1.ExecuteFormat("SET yb_locks_min_txn_age='0s'"));
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_OK(conn2.ExecuteFormat("SET yb_locks_min_txn_age='0s'"));
    for (const auto& lock_types : kBlockingPairs) {
      VerifyBlockingBehavior(conn1, conn2, lock_types.first, lock_types.second, test_pg_locks);
      VerifyBlockingBehavior(conn1, conn2, lock_types.second, lock_types.first, test_pg_locks);
    }
  }

  std::string ToMode(const std::string& table_lock_type_str) {
    static const std::map<std::string, std::string> kLockModeMap = {
      {"ACCESS SHARE", "AccessShareLock"},
      {"ROW SHARE", "RowShareLock"},
      {"ROW EXCLUSIVE", "RowExclusiveLock"},
      {"SHARE UPDATE EXCLUSIVE", "ShareUpdateExclusiveLock"},
      {"SHARE", "ShareLock"},
      {"SHARE ROW EXCLUSIVE", "ShareRowExclusiveLock"},
      {"EXCLUSIVE", "ExclusiveLock"},
      {"ACCESS EXCLUSIVE", "AccessExclusiveLock"}
    };
    auto mode_it = kLockModeMap.find(table_lock_type_str);
    if (mode_it == kLockModeMap.end()) {
      return "UnknownLockMode";
    }
    return mode_it->second;
  }

  void VerifyBlockingBehavior(
      PGConn& conn1, PGConn& conn2, const std::string& holder_lock_type,
      const std::string& waiter_lock_type, bool test_pg_locks) {
    google::SetVLOGLevel("object_lock*", 1);
    LOG(INFO) << "Checking blocking behavior: " << holder_lock_type << " and " << waiter_lock_type;
    static const std::string lock_query = "LOCK TABLE test IN $0 MODE";
    ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn1.ExecuteFormat(lock_query, holder_lock_type));

    auto log_waiter = StringWaiterLogSink("added to wait-queue on");
    auto conn2_lock_future = std::async(std::launch::async, [&]() -> Status {
      RETURN_NOT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      RETURN_NOT_OK(conn2.ExecuteFormat(lock_query, waiter_lock_type));

      if (test_pg_locks) {
        SleepFor(500ms * kTimeMultiplier);
        auto granted_lock_mode = VERIFY_RESULT(conn2.FetchRow<std::string>(
            "SELECT mode FROM pg_locks WHERE granted = true AND relation = 'test'::regclass"));
        // The waiter should have acquired the lock at this point.
        SCHECK_EQ(granted_lock_mode, ToMode(waiter_lock_type),
            IllegalState, "Expected the waiter to be unblocked");
      }

      RETURN_NOT_OK(conn2.Execute("INSERT INTO test (k, v) VALUES (11, 1)"));
      RETURN_NOT_OK(conn2.CommitTransaction());
      return Status::OK();
    });
    ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(5 * kTimeMultiplier)));

    if (test_pg_locks) {
      SleepFor(500ms * kTimeMultiplier);
      auto granted_lock_mode = ASSERT_RESULT(conn1.FetchRow<std::string>(
          "SELECT mode FROM pg_locks WHERE granted = true AND relation = 'test'::regclass"));
      ASSERT_EQ(granted_lock_mode, ToMode(holder_lock_type));
      auto waiting_lock_mode = ASSERT_RESULT(conn1.FetchRow<std::string>(
          "SELECT mode FROM pg_locks WHERE granted = false AND relation = 'test'::regclass"));
      ASSERT_EQ(waiting_lock_mode, ToMode(waiter_lock_type));
    }

    ASSERT_EQ(ASSERT_RESULT(conn1.FetchRow<PGUint64>("SELECT COUNT(*) FROM test WHERE v = 1")), 0);

    // After conn1 releases the lock, conn2 should be able to acquire it.
    ASSERT_OK(conn1.CommitTransaction());
    ASSERT_OK(conn2_lock_future.get());
    ASSERT_EQ(ASSERT_RESULT(conn1.FetchRow<PGUint64>("SELECT COUNT(*) FROM test WHERE v = 1")), 1);
    ASSERT_OK(conn2.Execute("DELETE FROM test WHERE v = 1"));
  }

  tserver::TSLocalLockManagerPtr ts_lock_manager_;
  tserver::TSLocalLockManagerPtr master_lock_manager_;
};

TEST_F(PgObjectLocksTestRF1, TestLockTuple) {
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute(
      "CREATE OR REPLACE FUNCTION add1(integer, integer) RETURNS integer "
      "AS 'select $1 + $2 + 2;' LANGUAGE SQL IMMUTABLE RETURNS NULL ON NULL INPUT"));

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("ALTER FUNCTION add1(int, int) OWNER TO postgres"));

  auto status_future = std::async(std::launch::async, [&]() -> Status {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn.Execute("ALTER FUNCTION add1(int, int) OWNER TO postgres"));
    RETURN_NOT_OK(conn.CommitTransaction());
    return Status::OK();
  });

  EXPECT_OK(WaitFor([&]() {
    return NumWaitingLocks() >= 1 || NumWaitingLocksOnMaster() >= 1;
  }, 5s * kTimeMultiplier, "Timed out waiting for num waiting locks >= 1"));
  ASSERT_OK(conn.RollbackTransaction());
  ASSERT_OK(status_future.get());
}

TEST_F(PgObjectLocksTestRF1, TestSanity) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(AssertNumLocks(0 /* granted locks*/, 0 /* waiting locks */));

  // A DDL shouldn't leave behind any locks on the local TS (since it is RF1).
  ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));

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

TEST_F(PgObjectLocksTestRF1, VerifyTableLockBlockingBehavior) {
  TestAllBlockingPairs(/*test_pg_locks=*/false);
}

TEST_F(PgObjectLocksTestRF1, TestPgLocks) {
  TestAllBlockingPairs(/*test_pg_locks=*/true);
}

class PgObjectLocksTestRF1SessionExpiry : public PgObjectLocksTestRF1 {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_session_expiration_ms) = kSessionTimeoutMs;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_heartbeat_interval_ms) = kSessionTimeoutMs / 3;
    PgObjectLocksTestRF1::SetUp();

    conn_ = ASSERT_RESULT(Connect());
    LOG(INFO) << "Creating test table";
    ASSERT_OK(conn_->Execute("CREATE TABLE test_table (id INT PRIMARY KEY, value TEXT);"));
  }

  Status WaitToReleaseAllLocks(MonoDelta timeout) {
    LOG(INFO) << "Waiting for locks to be cleaned up";
    return LoggedWaitFor(
        [this]() -> bool {
          auto granted_locks = NumGrantedLocks();
          auto granted_master_locks = NumGrantedLocksOnMaster();
          LOG(INFO) << "Num granted locks: " << granted_locks;
          LOG(INFO) << "Num granted master locks: " << granted_master_locks;
          return granted_locks == 0 && granted_master_locks == 0;
        },
        timeout, "wait for locks to be cleaned up", timeout / 10);
  }

  void ExpireSession(pgwrapper::PGConn& target_conn) {
    auto pid = ASSERT_RESULT(target_conn.FetchRow<int32_t>("SELECT pg_backend_pid();"));
    ExpireSessionWithPid(pid);
  }

  void ExpireSessionWithPid(int32_t pid) {
    LOG(INFO) << "Killing pid " << pid;
    auto res =
        ASSERT_RESULT(conn_->FetchRowAsString(yb::Format("SELECT pg_terminate_backend($0);", pid)));
    LOG(INFO) << "Got " << res;
  }

  uint64_t kSessionTimeoutMs = 6000 * kTimeMultiplier;
  MonoDelta kSessionTimeout = MonoDelta::FromMilliseconds(kSessionTimeoutMs);

 private:
  std::optional<PGConn> conn_;
};

TEST_F(PgObjectLocksTestRF1SessionExpiry, CleanupSharedLocksOnExpiredSessions) {
  auto conn = ASSERT_RESULT(Connect());
  // Clean up a shared lock from the local TServer.
  LOG(INFO) << "Acquiring shared lock on test table in another session";
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION;"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'good');"));
  ASSERT_GE(NumGrantedLocks(), 1);
  LOG(INFO) << "Expiring connection";
  ExpireSession(conn);
  ASSERT_OK(WaitToReleaseAllLocks(kSessionTimeout * 2));
}

TEST_F(PgObjectLocksTestRF1SessionExpiry, CleanupExclusiveLocksOnExpiredSessions) {
  auto conn = ASSERT_RESULT(Connect());
  // Clean up an exclusive lock from the master and local TServer.
  LOG(INFO) << "Acquiring exclusive lock on test table";
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION;"));
  ASSERT_OK(conn.Execute("LOCK TABLE test_table IN ACCESS EXCLUSIVE MODE;"));
  ASSERT_GE(NumGrantedLocksOnMaster(), 1);
  ASSERT_GE(NumGrantedLocks(), 1);
  LOG(INFO) << "Expiring connection";
  ExpireSession(conn);
  ASSERT_OK(WaitToReleaseAllLocks(kSessionTimeout * 2));
}

TEST_F(PgObjectLocksTestRF1SessionExpiry, CleanupReadOnlyLocksOnExpiredSessions) {
  auto conn = ASSERT_RESULT(Connect());
  // Clean up a shared lock from the local TServer.
  LOG(INFO) << "Acquiring shared lock on test table in another session";
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION;"));
  ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) from test_table;"));
  ASSERT_GE(NumGrantedLocks(), 1);
  LOG(INFO) << "Expiring connection";
  ExpireSession(conn);
  ASSERT_OK(WaitToReleaseAllLocks(kSessionTimeout * 2));
}

class PgObjectLocksTestRF1SessionExpiryWithDdl
    : public PgObjectLocksTestRF1SessionExpiry,
      public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    allow_ddl_verification_task_ = GetParam();
    PgObjectLocksTestRF1SessionExpiry::SetUp();
  }
  bool allow_ddl_verification_task_;
};

TEST_P(PgObjectLocksTestRF1SessionExpiryWithDdl, CleanupDdlLocksOnExpiredSessions) {
  auto conn = ASSERT_RESULT(Connect());
  auto pid = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid();"));
  TestThreadHolder thread_holder;
  // Cause the alter to hang, so that we can kill the session while it is holding the locks.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) =
      (allow_ddl_verification_task_ ? "completion" : "alter_schema");
  thread_holder.AddThreadFunctor([&conn]() {
    LOG(INFO) << "Acquiring exclusive lock on test table";
    ASSERT_OK(conn.Execute("BEGIN TRANSACTION;"));
    auto res = conn.Execute("ALTER TABLE test_table ADD COLUMN bar TEXT");
    LOG(INFO) << "Got " << res;
    // Expect the Alter to fail because we are going to kill the session.
    ASSERT_NOK(res);
  });
  ASSERT_OK(WaitFor(
      [this]() { return NumGrantedLocks() >= 1; }, 5s * kTimeMultiplier,
      "Timed out waiting for num granted locks == 1"));
  LOG(INFO) << "Expiring connection 1";
  SleepFor(1s * kTimeMultiplier);
  ExpireSessionWithPid(pid);
  if (allow_ddl_verification_task_) {
    // Let the Alter proceed, so it creates the verifier task.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
    // Release request will be ignored.
    // ddl verifier will kick in after the txn expires.
    ASSERT_OK(WaitToReleaseAllLocks(60s));
  } else {
    ASSERT_OK(WaitToReleaseAllLocks(kSessionTimeout * 2));
    // Alter may proceed, only after the locks are released.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  }
}

std::string DdlVerificationTaskToName(
    const ::testing::TestParamInfo<bool>& info) {
  return yb::Format("$0", info.param ? "ReleaseByDdlVerifier" : "ReleaseByLocalTServer");
}

INSTANTIATE_TEST_CASE_P(
    VerifyTxnAtMaster, PgObjectLocksTestRF1SessionExpiryWithDdl,
    ::testing::Values(
      false,
      true
    ),
    DdlVerificationTaskToName);

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
    RETURN_NOT_OK(conn.Fetch("SELECT * FROM test WHERE k=1"));
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
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vmodule) =
        yb::Format("libpq_utils=1,ts_local_lock_manager=2,$0", FLAGS_vmodule);
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--enable_object_locking_for_table_locks=$0", EnableTableLocks()));
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--ysql_yb_ddl_transaction_block_enabled=$0", EnableTransactionalDdl()));
    opts->extra_tserver_flags.emplace_back("--enable_ysql_operation_lease=true");
    opts->extra_tserver_flags.emplace_back("--TEST_tserver_enable_ysql_lease_refresh=true");
    opts->extra_tserver_flags.emplace_back(
        Format("--ysql_lease_refresher_interval_ms=$0", kDefaultYSQLLeaseRefreshIntervalMilli));
    // yb_user_ddls_preempt_auto_analyze works only if enable_object_locking_for_table_locks is off,
    // so disable auto analyze in this test suite.
    opts->extra_tserver_flags.emplace_back("--ysql_enable_auto_analyze=false");

    opts->extra_master_flags.emplace_back("--enable_ysql_operation_lease=true");
    opts->extra_master_flags.emplace_back(
        Format("--master_ysql_operation_lease_ttl_ms=$0", kDefaultMasterYSQLLeaseTTLMilli));
  }

  int GetNumTabletServers() const override {
    return 3;
  }

  virtual bool EnableTableLocks() const {
    return true;
  }

  void testConcurrentAlterSelect(bool fresh_catalog_cache) {
    const auto ts1_idx = 0;
    const auto ts2_idx = 1;
    auto* ts1 = cluster_->tablet_server(ts1_idx);
    auto* ts2 = cluster_->tablet_server(ts2_idx);

    auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
    auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));

    ASSERT_OK(conn1.Execute("CREATE TABLE t1(c1 INT, c2 INT)"));
    ASSERT_OK(conn2.Execute("INSERT INTO t1 VALUES (1, 2)"));

    if (fresh_catalog_cache) {
      // Reset to new conns so we don't retain any catalog cache entries
      conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
      conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));
    } else {
      auto row1 = ASSERT_RESULT(conn2.FetchRowAsString("SELECT * FROM t1"));
      LOG(INFO) << "Row was " << row1;
    }
    ASSERT_OK(cluster_->SetFlag(
        ts2,
        "TEST_tserver_disable_catalog_refresh_on_heartbeat",
        "true"));

    ASSERT_OK(conn1.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
    ASSERT_OK(conn2.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
    LOG(INFO) << "conn1: Acquiring exclusive lock on test table";
    ASSERT_OK(conn1.Execute("ALTER TABLE t1 DROP COLUMN c2"));

    TestThreadHolder thread_holder;
    thread_holder.AddThreadFunctor([&conn2]() {
      LOG(INFO) << "conn2: going to wait on conn1's table lock";
      // without propagation of inval msgs on lock acquire, this statement would hit a
      // schema mismatch error
      auto row2 = ASSERT_RESULT(conn2.FetchRowAsString("SELECT * FROM t1"));
      LOG(INFO) << "Row was " << row2;
    });

    ASSERT_OK(conn1.Execute("COMMIT"));
    thread_holder.JoinAll();
    ASSERT_OK(conn2.Execute("COMMIT"));

    ASSERT_OK(cluster_->SetFlag(
        ts2,
        "TEST_tserver_disable_catalog_refresh_on_heartbeat",
        "false"));
    LOG(INFO) << "Verifying new conns go through";
    auto conn4 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));
    ASSERT_OK(conn4.Fetch("SELECT * FROM t1"));
  }

  virtual bool EnableTransactionalDdl() const {
    return true;
  }
};

class PgObjectLocksTestAbortTxns : public PgObjectLocksTest,
                                   public ::testing::WithParamInterface<bool> {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    PgObjectLocksTest::UpdateMiniClusterOptions(opts);
    opts->extra_master_flags.emplace_back("--ysql_colocate_database_by_default=true");
    opts->extra_tserver_flags.emplace_back("--ysql_colocate_database_by_default=true");
  }

  bool EnableTableLocks() const override {
    return GetParam();
  }

  bool EnableTransactionalDdl() const override {
    return GetParam();
  }
};

TEST_P(PgObjectLocksTestAbortTxns, TestDDLAbortsTxns) {
  auto conn = ASSERT_RESULT(ConnectToDB("yugabyte"));
  ASSERT_OK(conn.Execute("CREATE DATABASE testdb with colocation=true"));

  auto conn1 = ASSERT_RESULT(ConnectToDB("testdb"));
  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT) with (colocated=true)"));
  ASSERT_OK(conn1.Execute("CREATE TABLE test2(k INT PRIMARY KEY, v INT) with (colocated=true)"));

  ASSERT_OK(conn1.Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));

  auto conn2 = ASSERT_RESULT(ConnectToDB("testdb"));
  ASSERT_OK(conn2.Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn2.Execute("ALTER TABLE test2 ADD COLUMN v1 INT"));
  ASSERT_OK(conn2.Execute("COMMIT"));

  if (EnableTableLocks()) {
    ASSERT_OK(conn1.Execute("COMMIT"));
  } else {
    ASSERT_NOK(conn1.Execute("COMMIT"));
  }
}

INSTANTIATE_TEST_CASE_P(
    TableLocksEnabled, PgObjectLocksTestAbortTxns, ::testing::Bool(),
    ::testing::PrintToStringParamName());

class PgObjectLocksTestAbortTxnsInMixedMode : public PgObjectLocksTestAbortTxns {
 protected:
  bool EnableTransactionalDdl() const override {
    // Always enable transactional DDL for this test. At least one of the nodes will be enabling
    // the table locks.
    return true;
  }
};

// We create connections to 2 different nodes.
// And try combinations where the nodes may or may not be using table locks.
TEST_P(PgObjectLocksTestAbortTxnsInMixedMode, TestDDLAbortsTxnsInMixedMode) {
  {
    auto conn = ASSERT_RESULT(ConnectToDB("yugabyte"));
    ASSERT_OK(conn.Execute("CREATE DATABASE testdb with colocation=true"));
    conn = ASSERT_RESULT(ConnectToDB("testdb"));
    ASSERT_OK(conn.Execute("CREATE TABLE test1(k INT PRIMARY KEY, v INT) with (colocated=true)"));
    ASSERT_OK(conn.Execute("CREATE TABLE test2(k INT PRIMARY KEY, v INT) with (colocated=true)"));
  }

  const bool ddl_using_table_locks = GetParam();
  const bool ts1_should_use_table_locks = !ddl_using_table_locks;

  auto* ts1 = cluster_->tablet_server(0);
  // Restart TServer 1. To start with table locks in the opposite mode of ddl_using_table_locks.
  ts1->Shutdown();
  LogWaiter log_waiter(ts1, "Received new lease epoch");
  // Append the flag here rather than pass it as an argument to Restart to ensure that this takes
  // precedence over value set by GetParam()
  ts1->mutable_flags()->push_back(yb::Format(
      "--enable_object_locking_for_table_locks=$0", ts1_should_use_table_locks ? "true" : "false"));
  ASSERT_OK(ts1->Restart(ExternalMiniClusterOptions::kDefaultStartCqlProxy));
  ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(kTimeMultiplier * 10)));

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts1, "testdb"));
  ASSERT_OK(conn1.Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn1.Execute("INSERT INTO test1 SELECT generate_series(1,11), 0"));

  auto* ts2 = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts2, "testdb"));
  ASSERT_OK(conn2.Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn2.Execute("ALTER TABLE test2 ADD COLUMN v1 INT"));
  ASSERT_OK(conn2.Execute("COMMIT"));

  // The DML txn should be aborted because at least one of the DDL/DML is not
  // using table locks.
  ASSERT_NOK(conn1.Execute("COMMIT"));
}

INSTANTIATE_TEST_CASE_P(
    TableLocksEnabled, PgObjectLocksTestAbortTxnsInMixedMode, ::testing::Bool(),
    ::testing::PrintToStringParamName());

class PgObjectLocksTestMixModeDuringPromotion : public PgObjectLocksTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    PgObjectLocksTest::UpdateMiniClusterOptions(opts);
    // Start off with the flag disabled.
    opts->extra_tserver_flags.emplace_back("--ysql_enable_object_locking_infra=false");
  }
};

TEST_F(PgObjectLocksTestMixModeDuringPromotion, TestMixModeDuringPromotion) {
  {
    auto conn = ASSERT_RESULT(ConnectToDB("yugabyte"));
    ASSERT_OK(conn.Execute("CREATE DATABASE testdb;"));
    conn = ASSERT_RESULT(ConnectToDB("testdb"));
    ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  }

  // Promote ts1 to use table locks before the DDL.
  auto* ts1 = cluster_->tablet_server(0);
  ASSERT_OK(cluster_->SetFlag(ts1, "ysql_enable_object_locking_infra", "true"));

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts1, "testdb"));
  ASSERT_OK(conn1.Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn1.Execute("ALTER TABLE test ADD COLUMN v1 INT"));

  auto* ts2 = cluster_->tablet_server(1);
  ASSERT_OK(cluster_->SetFlag(ts2, "ysql_enable_object_locking_infra", "true"));
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts2, "testdb"));
  const auto kStatementTimeoutSec = 2;
  ASSERT_OK(conn2.ExecuteFormat("SET statement_timeout = '$0s';", kStatementTimeoutSec));

  // The DML should fail because it cannot get the table lock.
  ASSERT_NOK(conn2.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));

  ASSERT_OK(conn1.Execute("COMMIT"));

  // The DML should now succeed.
  ASSERT_OK(conn2.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));
}

TEST_F(
    PgObjectLocksTestMixModeDuringPromotion, TestLocksTakenAfterPromotionWithExistingConnections) {
  {
    auto conn = ASSERT_RESULT(ConnectToDB("yugabyte"));
    ASSERT_OK(conn.Execute("CREATE DATABASE testdb;"));
    conn = ASSERT_RESULT(ConnectToDB("testdb"));
    ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  }

  // Promote ts1 to use table locks before the DDL.
  auto* ts1 = cluster_->tablet_server(0);
  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts1, "testdb"));
  auto* ts2 = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts2, "testdb"));

  ASSERT_OK(cluster_->SetFlag(ts1, "ysql_enable_object_locking_infra", "true"));
  ASSERT_OK(cluster_->SetFlag(ts2, "ysql_enable_object_locking_infra", "true"));

  ASSERT_OK(conn1.Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn1.Execute("ALTER TABLE test ADD COLUMN v1 INT"));

  const auto kStatementTimeoutSec = 2;
  ASSERT_OK(conn2.ExecuteFormat("SET statement_timeout = '$0s';", kStatementTimeoutSec));

  // The DML should fail because it cannot get the table lock.
  ASSERT_NOK(conn2.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));
}

TEST_F(PgObjectLocksTestMixModeDuringPromotion, TestConcurrentTxnsMayNotTakeTableLocks) {
  {
    auto conn = ASSERT_RESULT(ConnectToDB("yugabyte"));
    ASSERT_OK(conn.Execute("CREATE DATABASE testdb;"));
    conn = ASSERT_RESULT(ConnectToDB("testdb"));
    ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  }

  // Promote ts1 to use table locks before the DDL.
  auto* ts1 = cluster_->tablet_server(0);
  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts1, "testdb"));
  auto* ts2 = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTsForDB(*ts2, "testdb"));

  ASSERT_OK(conn1.Execute("BEGIN TRANSACTION"));

  ASSERT_OK(cluster_->SetFlag(ts1, "ysql_enable_object_locking_infra", "true"));
  ASSERT_OK(cluster_->SetFlag(ts2, "ysql_enable_object_locking_infra", "true"));

  ASSERT_OK(conn1.Execute("ALTER TABLE test ADD COLUMN v1 INT"));

  const auto kStatementTimeoutSec = 2;
  ASSERT_OK(conn2.ExecuteFormat("SET statement_timeout = '$0s';", kStatementTimeoutSec));

  // The DML should NOT fail because it can get the table lock.
  ASSERT_OK(conn2.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));
}

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

TEST_F(PgObjectLocksTest, ConcurrentAlterSelectFreshCache) {
  testConcurrentAlterSelect(true);
}

TEST_F(PgObjectLocksTest, ConcurrentAlterSelect) {
  testConcurrentAlterSelect(false);
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

TEST_F(PgObjectLocksTest, RetryExclusiveLockOnTserverLeaseRefresh) {
  const auto ts1_idx = 1;
  const auto ts2_idx = 2;
  auto* ts1 = cluster_->tablet_server(ts1_idx);
  auto* ts2 = cluster_->tablet_server(ts2_idx);

  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));
  ASSERT_OK(conn2.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn2.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.Execute("LOCK TABLE test IN ACCESS SHARE MODE"));

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(
      cluster_->SetFlag(ts2, "vmodule", yb::Format("object_lock_manager=1,$0", FLAGS_vmodule)));
  LogWaiter log_waiter(ts2, "added to wait-queue");
  auto status_future = std::async(std::launch::async, [&]() -> Status {
    return conn1.Execute("LOCK TABLE test IN ACCESS EXCLUSIVE MODE");
  });
  ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(kTimeMultiplier * 10)));

  // The exclusive lock request should still go through and not error due to membership changes.
  ts2->Shutdown(SafeShutdown::kTrue);
  ASSERT_EQ(
      status_future.wait_for(2s * kTimeMultiplier * kDefaultMasterYSQLLeaseTTLMilli),
      std::future_status::ready);
  ASSERT_OK(status_future.get());
  ASSERT_OK(conn1.CommitTransaction());
}

TEST_F(PgObjectLocksTest, VerifyLockTimeout) {
  constexpr int kLockTimeoutDurationMs = 3000;
  constexpr int kExtraTimeoutMs = 1000;

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO t VALUES (1, 1)"));

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1.Execute("LOCK TABLE t IN ACCESS EXCLUSIVE MODE;"));

  ASSERT_OK(conn2.ExecuteFormat("SET lock_timeout='$0ms';", kLockTimeoutDurationMs));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  std::future<Status> conn2_lock_future = std::async(std::launch::async, [&]() -> Status {
    return conn2.Execute("LOCK TABLE t IN ACCESS EXCLUSIVE MODE;");
  });

  // Wait briefly to confirm that conn2 is actually blocking on the lock.
  ASSERT_EQ(conn2_lock_future.wait_for(
      std::chrono::seconds(1)), std::future_status::timeout);

  auto expected_timeout_ms =
      kLockTimeoutDurationMs + FLAGS_pg_client_extra_timeout_ms + kExtraTimeoutMs;
  ASSERT_EQ(conn2_lock_future.wait_for(
      std::chrono::milliseconds(expected_timeout_ms * kTimeMultiplier)), std::future_status::ready);

  // Verify the lock attempt fails with lock timeout error.
  Status result = conn2_lock_future.get();
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.ToString(), "canceling statement due to lock timeout");

  // Conn1 should still be able to commit
  ASSERT_OK(conn1.CommitTransaction());
}

TEST_F(PgObjectLocksTest, BootstrapLocksHasStatusTabetIdForTxns) {
  const auto ts1_idx = 1;
  const auto ts2_idx = 2;
  auto* ts1 = cluster_->tablet_server(ts1_idx);
  auto* ts2 = cluster_->tablet_server(ts2_idx);

  auto conn1 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(1,11), 0"));

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1.Execute("ALTER TABLE test ADD COLUMN v1 INT DEFAULT 0"));

  ts2->Shutdown();
  {
    LogWaiter log_waiter(ts2, "BootstrapDdlObjectLocks: success");
    ASSERT_OK(ts2->Restart());
    ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(kTimeMultiplier * 10)));
  }

  ASSERT_OK(conn1.CommitTransaction());

  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));
  ASSERT_OK(conn2.FetchMatrix("SELECT * FROM test WHERE k=1", 1 /* rows */, 3 /* columns */));
}

YB_STRONGLY_TYPED_BOOL(DoMasterFailover);
YB_STRONGLY_TYPED_BOOL(UseExplicitLocksInsteadOfDdl);
class PgObjecLocksTestOutOfOrderMessageHandling
    : public PgObjectLocksTest,
      public ::testing::WithParamInterface<
          std::tuple<DoMasterFailover, UseExplicitLocksInsteadOfDdl>> {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    PgObjectLocksTest::UpdateMiniClusterOptions(opts);
    opts->num_masters = (ShouldDoMasterFailover() ? 3 : 1);
    opts->extra_master_flags.emplace_back(
        yb::Format("--pg_client_extra_timeout_ms=$0", kPgClientExtraTimeoutMs));
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--vmodule=ts_local_lock_manager=2,$0", FLAGS_vmodule));
  }

  DoMasterFailover ShouldDoMasterFailover() const {
    return std::get<0>(GetParam());
  }

  UseExplicitLocksInsteadOfDdl ShouldUseExplicitLocksInsteadOfDdl() const {
    return std::get<1>(GetParam());
  }

  static constexpr auto kPgClientExtraTimeoutMs = 2000;
};

TEST_P(PgObjecLocksTestOutOfOrderMessageHandling, TestOutOfOrderMessageHandling) {
  const auto ts1_idx = 1;
  const auto ts2_idx = 2;
  auto* ts1 = cluster_->tablet_server(ts1_idx);
  auto* ts2 = cluster_->tablet_server(ts2_idx);

  auto conn = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts1));
  auto conn2 = ASSERT_RESULT(LibPqTestBase::ConnectToTs(*ts2));

  LOG(INFO) << "Creating test table";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (id INT PRIMARY KEY, value TEXT);"));

  const auto kStatementTimeoutSec = 8;
  ASSERT_OK(conn.ExecuteFormat("SET statement_timeout = '$0s';", kStatementTimeoutSec));

  ASSERT_OK(cluster_->SetFlag(ts2, "TEST_block_acquires_to_simulate_out_of_order", "true"));
  LogWaiter log_waiter(ts2, "Blocking acquire request to simulate out-of-order requests");

  if (!ShouldDoMasterFailover()) {
    // This will cause the master to timeout the Acquire Rpc to the tserver, and retry
    // the Acquire request to simulate out-of-order requests, along with the original blocked
    // request.
    ASSERT_OK(cluster_->SetFlagOnMasters(
        "master_ts_rpc_timeout_ms", yb::Format("$0", (kStatementTimeoutSec * 1000 / 10))));
  }

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION;"));
  auto lock_request_start = CoarseMonoClock::Now();
  auto status_future = std::async(std::launch::async, [&]() -> Status {
    if (ShouldUseExplicitLocksInsteadOfDdl()) {
      LOG(INFO) << "Acquiring EXCLUSIVE lock on test_table using explicit locks";
      RETURN_NOT_OK(conn.Execute("LOCK TABLE test_table IN ACCESS EXCLUSIVE MODE;"));
      LOG(INFO) << "Releasing EXCLUSIVE lock on test_table";
    } else {
      LOG(INFO) << "Acquiring EXCLUSIVE lock on test_table using a DDL -- Alter Table";
      RETURN_NOT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN dummy TEXT;"));
    }
    return conn.Execute("COMMIT;");
  });

  const auto kTimeout = MonoDelta::FromSeconds(3 * kTimeMultiplier);
  ASSERT_OK(log_waiter.WaitFor(kTimeout));

  LOG(INFO) << "Have at least one Tserver blocking the acquire request";
  ASSERT_OK(cluster_->SetFlag(ts2, "TEST_block_acquires_to_simulate_out_of_order", "false"));
  LOG(INFO) << "Will no longer block acquires at TServers";

  if (ShouldDoMasterFailover()) {
    auto old_master = cluster_->GetLeaderMaster();
    LOG(INFO) << "Stepping down master leader";
    ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
    LOG(INFO) << "Killing old master to reset the network connection";
    old_master->Shutdown();
  }

  LOG(INFO) << "Waiting for LOCK TABLE to succeed and release it";
  ASSERT_OK(status_future.get());

  LOG(INFO) << "Taking a SHARED lock to ensure that the asynchronous release is infact done.";
  ASSERT_OK(conn2.Execute("BEGIN TRANSACTION;"));
  ASSERT_OK(conn2.Execute("LOCK TABLE test_table IN ACCESS SHARE MODE;"));
  ASSERT_OK(conn2.Execute("COMMIT;"));
  LOG(INFO) << "Acquired and Released shared lock. So the exclusive lock release was processed by "
               "the TServer.";

  LogWaiter log_waiter2(ts2, "Unblocking acquire request.");
  LOG(INFO) << "Allowing blocked acquire request to now proceed.";
  ASSERT_OK(
      cluster_->SetFlag(ts2, "TEST_release_blocked_acquires_to_simulate_out_of_order", "true"));
  ASSERT_OK(log_waiter2.WaitFor(kTimeout));

  auto get_locks_count_on_tserver = [&ts2, this]() -> Result<int64_t> {
    return VERIFY_RESULT(cluster_->GetObjectLockStatus(*ts2)).object_lock_infos_size();
  };

  auto kGracePeriod = MonoDelta::FromMilliseconds(500);
  ASSERT_LE(CoarseMonoClock::Now() + kGracePeriod, lock_request_start + kStatementTimeoutSec * 1s);
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        auto locks_count = VERIFY_RESULT(get_locks_count_on_tserver());
        LOG(INFO) << "Number of locks on test_table: " << locks_count;
        return locks_count > 0;
      },
      kGracePeriod, "Wait for unblocked acquire request to complete"));

  auto expected_cleanup_time = lock_request_start + MonoDelta::FromSeconds(kStatementTimeoutSec) +
                               kPgClientExtraTimeoutMs * 1ms;
  ASSERT_OK(LoggedWait(
      [&]() -> Result<bool> {
        auto locks_count = VERIFY_RESULT(get_locks_count_on_tserver());
        LOG(INFO) << "Number of locks on test_table: " << locks_count;
        return locks_count == 0;
      },
      expected_cleanup_time + kGracePeriod, "Wait for the out-of-order acquire to get cleaned up"));

  LOG(INFO) << "Re-Acquiring an exclusive lock on test table";
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION;"));
  ASSERT_OK(conn.Execute("LOCK TABLE test_table IN ACCESS EXCLUSIVE MODE;"));
  ASSERT_OK(conn.Execute("COMMIT;"));
  LOG(INFO) << "Re-acquire succeeded. Done";
}

INSTANTIATE_TEST_SUITE_P(
    , PgObjecLocksTestOutOfOrderMessageHandling,
    ::testing::Values(
        std::make_tuple(
            DoMasterFailover::kTrue, UseExplicitLocksInsteadOfDdl::kTrue),
        std::make_tuple(
            DoMasterFailover::kFalse, UseExplicitLocksInsteadOfDdl::kTrue),
        std::make_tuple(
            DoMasterFailover::kTrue, UseExplicitLocksInsteadOfDdl::kFalse),
        std::make_tuple(
            DoMasterFailover::kFalse, UseExplicitLocksInsteadOfDdl::kFalse)),
    [](const ::testing::TestParamInfo<
        std::tuple<DoMasterFailover, UseExplicitLocksInsteadOfDdl>>& info) {
      return Format("$0_$1",
          (std::get<0>(info.param) ? "WithMasterFailover" : "NoMasterFailover"),
          (std::get<1>(info.param) ? "UseExplicitLocks" : "UseDdlForLocks"));
    });

// This test relies on the OLM poller to run frequently and timedout waiters, in order to
// check for deadlocks/transaction aborts and retry from pg_client_session if necessary.
TEST_F(PgObjectLocksTestRF1, YB_DISABLE_TEST_IN_TSAN(TestDeadlock)) {
  // Restart the cluster_ with the poll interval set to default.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_olm_poll_interval_ms) = kDefaultLockManagerPollIntervalMs;
  ASSERT_OK(cluster_->RestartSync());
  Init();

  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(0, 10), 0"));
  ASSERT_OK(conn1.Execute("CREATE TABLE test1(k INT)"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_refresh_waiter_timeout_ms) = 5000;
  auto conn2 = ASSERT_RESULT(Connect());

  // Deadlock at the tserver's object lock manager
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  ASSERT_OK(conn1.Execute("LOCK TABLE test in ACCESS SHARE mode"));
  ASSERT_OK(conn2.Execute("LOCK TABLE test1 in ACCESS EXCLUSIVE mode"));

  auto status_future = std::async(std::launch::async, [&]() -> Status {
    auto s = conn1.Execute("LOCK TABLE test1 in ACCESS SHARE mode");
    EXPECT_OK(conn1.RollbackTransaction());
    return s;
  });

  auto s = conn2.Execute("LOCK TABLE test in ACCESS EXCLUSIVE mode");
  ASSERT_OK(conn2.RollbackTransaction());
  if (s.ok()) {
    s = status_future.get();
  } else {
    ASSERT_OK(status_future.get());
  }
  ASSERT_STR_CONTAINS(s.ToString(), "aborted due to a deadlock");

  // Deadlock between row-level locks and object locks.
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  ASSERT_OK(conn1.Execute("UPDATE test SET v=v+1 where k=1"));
  ASSERT_OK(conn2.Execute("LOCK TABLE test1 in ACCESS EXCLUSIVE mode"));

  status_future = std::async(std::launch::async, [&]() -> Status {
    auto s = conn1.Execute("LOCK TABLE test1 in ACCESS SHARE mode");
    SleepFor(FLAGS_transaction_heartbeat_usec * 1us * kTimeMultiplier * 2);
    if (s.ok()) {
      s = conn1.Execute("UPDATE test SET v=v+1 where k=1");
    }
    EXPECT_OK(conn1.RollbackTransaction());
    return s;
  });
  s = conn2.Execute("UPDATE test SET v=v+1 where k=1");
  if (s.ok()) {
    // Give time for the transaction heartheat to realize the abort. That way, conn1's
    // lock request would fail. If not, it will falsely report success, but fail the
    // subsequent statements.
    SleepFor(FLAGS_transaction_heartbeat_usec * 1us * kTimeMultiplier * 2);
    s = conn2.Execute("UPDATE test SET v=v+1 where k=1");
  }
  ASSERT_OK(conn2.RollbackTransaction());
  if (s.ok()) {
    s = status_future.get();
  } else {
    ASSERT_OK(status_future.get());
  }
  ASSERT_STR_CONTAINS(s.ToString(), "aborted due to a deadlock");

  // Deadlock at the master's object lock manager
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  ASSERT_OK(conn1.Execute("LOCK TABLE test in ACCESS EXCLUSIVE mode"));
  ASSERT_OK(conn2.Execute("LOCK TABLE test1 in ACCESS EXCLUSIVE mode"));
  status_future = std::async(std::launch::async, [&]() -> Status {
    auto s = conn1.Execute("LOCK TABLE test1 in ACCESS EXCLUSIVE mode");
    EXPECT_OK(conn1.RollbackTransaction());
    return s;
  });
  s = conn2.Execute("LOCK TABLE test in ACCESS EXCLUSIVE mode");
  ASSERT_OK(conn2.RollbackTransaction());
  if (s.ok()) {
    s = status_future.get();
  } else {
    ASSERT_OK(status_future.get());
  }
  ASSERT_STR_CONTAINS(s.ToString(), "aborted due to a deadlock");
}

TEST_F(PgObjectLocksTestRF1, TestShutdownWithWaiters) {
  constexpr auto kNumWaiters = 3;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(0, 10), 0"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("LOCK TABLE test in ACCESS EXCLUSIVE mode"));

  TestThreadHolder thread_holder;
  for (int i = 0 ; i < kNumWaiters; i++) {
    thread_holder.AddThreadFunctor([&]() {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.ExecuteFormat("SET statement_timeout=$0", 1000 * kTimeMultiplier));
      ASSERT_NOK(conn.Execute("UPDATE test SET v=v+1 WHERE k=1"));
    });
  }
  auto log_waiter = StringWaiterLogSink("has just lost its lease");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_enable_ysql_lease_refresh) = false;
  ASSERT_OK(log_waiter.WaitFor(
      MonoDelta::FromMilliseconds(2 * kDefaultMasterYSQLLeaseTTLMilli * kTimeMultiplier)));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_enable_ysql_lease_refresh) = true;
  thread_holder.JoinAll();
}

TEST_F(PgObjectLocksTestRF1, YB_DISABLE_TEST_IN_TSAN(TestDeadlockFastPath)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(0, 10), 0"));

  google::SetVLOGLevel("conflict_resolution*", 3);
  {
    RegexWaiterLogSink log_waiter(R"#(.*ResolveTransactionConflicts.*object_locking_txn_meta.*)#");
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.Execute("UPDATE test SET v=v+1 WHERE k=1"));
    ASSERT_FALSE(log_waiter.IsEventOccurred());
  }

  SyncPoint::GetInstance()->LoadDependency(
      {{"WaitQueue::Impl::SetupWaiterUnlocked:1", "TestDeadlockFastPath"}});
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  RegexWaiterLogSink log_waiter(R"#(.*ResolveOperationConflicts.*object_locking_txn_meta.*)#");
  auto status_future = std::async(std::launch::async, [&]() -> Status {
    auto conn = VERIFY_RESULT(Connect());
    return conn.Execute("UPDATE test SET v=v+1 WHERE k=1");
  });
  TEST_SYNC_POINT("TestDeadlockFastPath");
  ASSERT_TRUE(log_waiter.IsEventOccurred());
  auto s = conn.Execute("ALTER TABLE test ADD COLUMN v1 INT");
  if (s.ok()) {
    s = conn.Execute("UPDATE test SET v=v+1 WHERE k=1");
  }
  if (s.ok()) {
    ASSERT_NOK(status_future.get());
    ASSERT_OK(conn.CommitTransaction());
  } else {
    ASSERT_OK(status_future.get());
    ASSERT_OK(conn.RollbackTransaction());
  }
}

TEST_F(PgObjectLocksTestRF1, TestDisableReuseAbortedPlainTxn) {
  constexpr auto kStatementTimeoutMs = 1000 * kTimeMultiplier;
  // Restart the cluster_ with the poll interval set to 5x of statment timeout
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_olm_poll_interval_ms) = 5 * kStatementTimeoutMs;
  google::SetVLOGLevel("pg_client_session*", 1);
  ASSERT_OK(cluster_->RestartSync());
  Init();

  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(1, 10), 0"));
  ASSERT_OK(conn1.Execute("CREATE TABLE test1(k INT)"));

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1.Execute("LOCK TABLE test in ACCESS EXCLUSIVE mode"));

  auto conn2 = ASSERT_RESULT(Connect());
  // TODO(#26792): Change to LOCK_TIMEOUT once GH is addressed.
  ASSERT_OK(conn2.ExecuteFormat("SET statement_timeout=$0", kStatementTimeoutMs));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto log_waiter = StringWaiterLogSink("Consuming re-usable kPlain txn");
  ASSERT_OK(conn2.Execute("LOCK TABLE test1 in ACCESS SHARE mode"));
  // Would timeout before the waiting lock at the OLM is released.
  ASSERT_NOK(conn2.Execute("LOCK TABLE test in ACCESS SHARE mode"));
  ASSERT_OK(conn2.RollbackTransaction());
  ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(2 * kTimeMultiplier)));
  ASSERT_OK(conn1.Execute("COMMIT"));
  ASSERT_EQ(ASSERT_RESULT(conn2.FetchRow<PGUint64>("SELECT COUNT(*) FROM test")), 10);
  ASSERT_OK(AssertNumLocks(0, 0));
}

#ifndef NDEBUG
TEST_F(PgObjectLocksTestRF1, TestDisableReuseOfFailedTxn) {
  google::SetVLOGLevel("pg_client_session*", 1);
  google::SetVLOGLevel("transaction*", 1);
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(1, 10), 0"));

  TransactionId txn_id = TransactionId::Nil();
  yb::SyncPoint::GetInstance()->SetCallBack(
      "PgClientSession::Impl::DoAcquireObjectLock",
      [&](void* arg) { txn_id = *(static_cast<TransactionId*>(arg)); });
  SyncPoint::GetInstance()->ClearTrace();
  SyncPoint::GetInstance()->EnableProcessing();

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn1.Execute("LOCK TABLE test in ACCESS SHARE mode"));
  ASSERT_TRUE(!txn_id.Nil());
  SyncPoint::GetInstance()->DisableProcessing();

  auto conn2 = ASSERT_RESULT(Connect());
  auto log_waiter1 = RegexWaiterLogSink(Format(".*$0.*Heartbeat failed.*", txn_id));
  ASSERT_TRUE(ASSERT_RESULT(
      conn2.FetchRow<bool>(Format("SELECT yb_cancel_transaction('$0')", txn_id))));
  ASSERT_OK(log_waiter1.WaitFor(MonoDelta::FromSeconds(5 * kTimeMultiplier)));
  auto log_waiter2 = StringWaiterLogSink("Consuming re-usable kPlain txn");
  ASSERT_OK(conn1.Execute("COMMIT"));
  ASSERT_OK(log_waiter2.WaitFor(MonoDelta::FromSeconds(2 * kTimeMultiplier)));
  ASSERT_EQ(ASSERT_RESULT(conn1.FetchRow<PGUint64>("SELECT COUNT(*) FROM test")), 10);
  ASSERT_OK(AssertNumLocks(0, 0));
}
#endif

TEST_F(PgObjectLocksTestRF1, TestDisableReuseOfBlockerTxn) {
  google::SetVLOGLevel("object_lock_manager*", 1);
  google::SetVLOGLevel("pg_client_session*", 1);
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test SELECT generate_series(1, 10), 0"));

  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.Fetch("SELECT * FROM test"));

  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn2.Execute("BEGIN"));
  auto log_waiter1 = StringWaiterLogSink("added to wait-queue on");
  auto status_future1 = std::async(std::launch::async, [&] -> Status {
    return conn2.Execute("ALTER TABLE test ADD COLUMN v1 INT DEFAULT 0");
  });
  ASSERT_OK(log_waiter1.WaitFor(MonoDelta::FromSeconds(10 * kTimeMultiplier)));

  auto log_waiter2 = StringWaiterLogSink("Consuming re-usable kPlain txn");
  ASSERT_OK(conn1.Execute("COMMIT"));
  ASSERT_OK(log_waiter2.WaitFor(MonoDelta::FromSeconds(10 * kTimeMultiplier)));
  ASSERT_OK(status_future1.get());

  auto log_waiter3 = StringWaiterLogSink("added to wait-queue on");
  auto status_future2 = std::async(std::launch::async, [&] -> Status {
    return ResultToStatus(conn1.Fetch("SELECT * FROM test"));
  });
  ASSERT_OK(log_waiter3.WaitFor(MonoDelta::FromSeconds(10 * kTimeMultiplier)));
  SleepFor(2s * kTimeMultiplier);

  ASSERT_OK(conn2.Execute("COMMIT"));
  ASSERT_OK(status_future2.get());
  ASSERT_OK(AssertNumLocks(0, 0));
}

TEST_F(PgObjectLocksTestRF1, TestRedundantLockIsServedLocallyInYsql) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_olm_serve_redundant_lock) = true;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE pk(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE fk(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO pk SELECT generate_series(1, 1000), 0"));
  ASSERT_OK(conn.Execute("INSERT INTO fk SELECT i,i FROM generate_series(1, 1000) AS i"));

  // ALTER ADD CONSTRAINT requests RowShare on pk for each row in fk. But it should be
  // a no-op since the transaction already holds RowShare (or greater) on the object.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("ALTER TABLE fk ADD CONSTRAINT fk_rule FOREIGN KEY (v) REFERENCES pk(k)"));
  ASSERT_LT(NumGrantedLocks(), 50);
  ASSERT_OK(conn.Execute("COMMIT"));

  // Assert redundant table_open on pk is skipped.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO pk VALUES(2000, 0)"));
  auto num_initial_locks = NumGrantedLocks();
  ASSERT_OK(conn.Execute("INSERT INTO pk VALUES(3000, 0)"));
  ASSERT_EQ(num_initial_locks, NumGrantedLocks());
  ASSERT_OK(conn.Execute("COMMIT"));
}

class PgObjectLocksFastpathTest : public PgObjectLocksTestRF1 {
 protected:
  auto TServerSharedObject() {
    return cluster_->mini_tablet_server(0)->server()->shared_object();
  }

  docdb::ObjectLockSharedStateManager& ObjectLockSharedStateManager() {
    return *cluster_->mini_tablet_server(0)->server()->ObjectLockSharedStateManager();
  }

  void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_lock_fastpath) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_use_shared_memory) = true;
  }

  void SetUp() override {
    PgObjectLocksTestRF1::SetUp();
    auto& shared_object = *TServerSharedObject().get();
    ASSERT_OK(shared_object.WaitAllocatorsInitialized());
  }

  TransactionId LastOwner() {
    return ObjectLockSharedStateManager().TEST_last_owner();
  }
};

TEST_F(PgObjectLocksFastpathTest, TestSimple) {
  CreateTestTable();

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Fetch("SELECT * FROM test"));

  auto txn_id = LastOwner();

  ASSERT_OK(conn.Fetch("SELECT * FROM test"));
  ASSERT_EQ(LastOwner(), txn_id);

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (101, 1)"));
  ASSERT_EQ(LastOwner(), txn_id);

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Fetch("SELECT * FROM test"));
  ASSERT_EQ(LastOwner(), txn_id);
  ASSERT_OK(conn.CommitTransaction());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (102, 2)"));
  ASSERT_EQ(LastOwner(), txn_id);
  ASSERT_OK(conn.CommitTransaction());

  ASSERT_OK(conn.Fetch("SELECT * FROM test"));
  ASSERT_NE(LastOwner(), txn_id);
}

}  // namespace yb::pgwrapper
