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
#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

#include "yb/common/pgsql_error.h"
#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(ysql_yb_enable_advisory_locks);

namespace yb {

class PgAdvisoryLocksUpgradeTest : public UpgradeTestBase,
                                  public ::testing::WithParamInterface<uint32_t> {
 public:
  explicit PgAdvisoryLocksUpgradeTest(const std::string& from_version = kBuild_2025_1_1_0)
      : UpgradeTestBase(from_version) {}

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    AddUnDefOkAndSetFlag(
        opts.extra_master_flags, "num_advisory_locks_tablets", Format("$0", GetParam()));
    // A leader move while a backend is blocked on an advisory lock leaves the waiter in the old
    // leader's wait queue, where pg_locks no longer reports it (#36672). Leaders can change after
    // the load balancer has reported idle (e.g. via failure-detection elections after the rolling
    // restart), leaving them unbalanced, so waiting for idle cannot prevent a later leader
    // balancing move. Disable leader balancing until #36672 is fixed.
    AddUnDefOkAndSetFlag(opts.extra_master_flags, "load_balancer_max_concurrent_moves", "0");
    UpgradeTestBase::SetUpOptions(opts);
  }

  Status TestBasicFunctionality() {
    // The pg_advisory_locks tablets are created before every tserver has registered, so right after
    // startup the load balancer adds and removes replicas. Removing a leader replica steps the
    // leader down even with load_balancer_max_concurrent_moves=0, which would leave a lock waiter
    // in the old leader's wait queue, so let the cluster settle before taking any locks.
    RETURN_NOT_OK(cluster_->WaitForLoadBalancerToBecomeIdle(client_, 5min * kTimeMultiplier));

    auto conn1 = VERIFY_RESULT(cluster_->ConnectToDB());
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_lock(1)"));
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_lock(1, 1)"));
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_lock(2)"));

    // Assert behavior for individual unlock operations.
    SCHECK(
        !VERIFY_RESULT(conn1.FetchRow<bool>("select pg_advisory_unlock_shared(10)")), IllegalState,
        "Expected to error on unlock of non locked id");
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_lock(10)"));
    SCHECK(
        !VERIFY_RESULT(conn1.FetchRow<bool>("select pg_advisory_unlock_shared(10)")), IllegalState,
        "Expected to error on unlock of non locked id mode");
    SCHECK(
        VERIFY_RESULT(conn1.FetchRow<bool>("select pg_advisory_unlock(10)")), IllegalState,
        "Unexpected error on unlock for locked id");
    SCHECK(
        !VERIFY_RESULT(conn1.FetchRow<bool>("select pg_advisory_unlock(10)")), IllegalState,
        "Expected to error on unlock of already unlocked id");

    RETURN_NOT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_xact_lock(1)"));
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_xact_lock_shared(1, 2)"));

    auto conn2 = VERIFY_RESULT(cluster_->ConnectToDB());
    auto status_future_2 = std::async(std::launch::async, [&]() -> Status {
      RETURN_NOT_OK(conn2.Fetch("select pg_advisory_lock(1)"));
      return Status::OK();
    });
    // pg_locks fans out to every tserver, so it transiently fails while a transaction status tablet
    // replica is still bootstrapping. Poll it on a dedicated connection, so that such an error does
    // not abort conn1's transaction, and retry instead of failing the poll.
    auto conn_locks = VERIFY_RESULT(cluster_->ConnectToDB());
    auto assert_num_waiting_locks = [&](int expected_waiting_num_locks) -> Status {
      return WaitFor([&]() -> Result<bool> {
        auto num_waiting_locks =
            conn_locks.FetchRow<int64_t>("SELECT COUNT(*) FROM pg_locks WHERE NOT granted");
        WARN_NOT_OK(ResultToStatus(num_waiting_locks), "Failed to fetch waiting locks");
        return num_waiting_locks.ok() && *num_waiting_locks == expected_waiting_num_locks;
      },
      15s * kTimeMultiplier,
      Format("Timed out waiting for num waiting locks == $0", expected_waiting_num_locks));
    };
    RETURN_NOT_OK(assert_num_waiting_locks(1));

    auto conn3 = VERIFY_RESULT(cluster_->ConnectToDB());
    auto status_future_3 = std::async(std::launch::async, [&]() -> Status {
      RETURN_NOT_OK(conn3.Fetch("select pg_advisory_lock(1, 2)"));
      return Status::OK();
    });
    RETURN_NOT_OK(assert_num_waiting_locks(2));

    auto conn4 = VERIFY_RESULT(cluster_->ConnectToDB());
    auto status_future_4 = std::async(std::launch::async, [&]() -> Status {
      RETURN_NOT_OK(conn4.Fetch("select pg_advisory_lock(2)"));
      return Status::OK();
    });
    RETURN_NOT_OK(assert_num_waiting_locks(3));
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_unlock(2)"));
    RETURN_NOT_OK(assert_num_waiting_locks(2));
    RETURN_NOT_OK(status_future_4.get());

    RETURN_NOT_OK(conn1.CommitTransaction());
    RETURN_NOT_OK(assert_num_waiting_locks(1));
    RETURN_NOT_OK(status_future_3.get());
    RETURN_NOT_OK(conn1.Fetch("select pg_advisory_unlock_all()"));
    RETURN_NOT_OK(assert_num_waiting_locks(0));
    RETURN_NOT_OK(status_future_2.get());

    RETURN_NOT_OK(conn2.Fetch("select pg_advisory_unlock_all()"));
    RETURN_NOT_OK(conn3.Fetch("select pg_advisory_unlock_all()"));
    RETURN_NOT_OK(conn4.Fetch("select pg_advisory_unlock_all()"));
    return Status::OK();
  }
};

TEST_P(PgAdvisoryLocksUpgradeTest, TestBasicFunctionality) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_NO_FATALS(ASSERT_OK(TestBasicFunctionality()));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  ASSERT_NO_FATALS(ASSERT_OK(TestBasicFunctionality()));
  ASSERT_OK(FinalizeUpgrade());
  ASSERT_NO_FATALS(ASSERT_OK(TestBasicFunctionality()));
}

INSTANTIATE_TEST_CASE_P(NumAdvisoryLockTablets, PgAdvisoryLocksUpgradeTest, testing::Values(1, 3));

// yb_silence_advisory_locks_not_supported_error only applies while advisory locks are unavailable.
// The source version already has them, so the GUC must not turn locks into no-ops at any point of
// the upgrade, including before finalize.
class PgAdvisoryLocksSilenceGucUpgradeTest : public PgAdvisoryLocksUpgradeTest {
 public:
  PgAdvisoryLocksSilenceGucUpgradeTest() : PgAdvisoryLocksUpgradeTest(kBuild_2026_1_2_0) {}

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    PgAdvisoryLocksUpgradeTest::SetUpOptions(opts);
    opts.extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=yb_silence_advisory_locks_not_supported_error=true");
  }
};

TEST_P(PgAdvisoryLocksSilenceGucUpgradeTest, LocksTakenBeforeFinalize) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_NO_FATALS(ASSERT_OK(TestBasicFunctionality()));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  ASSERT_NO_FATALS(ASSERT_OK(TestBasicFunctionality()));
  ASSERT_OK(FinalizeUpgrade());
  ASSERT_NO_FATALS(ASSERT_OK(TestBasicFunctionality()));
}

INSTANTIATE_TEST_CASE_P(
    NumAdvisoryLockTablets, PgAdvisoryLocksSilenceGucUpgradeTest, testing::Values(1));

// Releases without DocDB advisory locks turn every advisory lock function into a successful no-op
// when yb_silence_advisory_locks_not_supported_error is set. On a YSQL major upgrade from such a
// release, advisory locks stay unavailable until finalize, so the no-op has to continue until then.
class PgAdvisoryLocksSilencedMajorUpgradeTest : public YsqlMajorUpgradeTestBase {
 public:
  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    YsqlMajorUpgradeTestBase::SetUpOptions(opts);
    opts.extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=yb_silence_advisory_locks_not_supported_error=true");
  }

 protected:
  Status ValidateSilencedNoOp(std::optional<size_t> ts_id) {
    auto conn = VERIFY_RESULT(CreateConnToTs(ts_id));
    for (const auto* query : {
             "SELECT pg_advisory_lock(1)", "SELECT pg_advisory_lock(1, 1)",
             "SELECT pg_advisory_lock_shared(2)", "SELECT pg_advisory_unlock_all()"}) {
      RETURN_NOT_OK(conn.Fetch(query));
    }
    for (const auto* query : {
             "SELECT pg_try_advisory_lock(1)", "SELECT pg_try_advisory_lock_shared(2)",
             "SELECT pg_advisory_unlock(1)", "SELECT pg_advisory_unlock(1, 1)",
             "SELECT pg_advisory_unlock_shared(2)"}) {
      SCHECK(VERIFY_RESULT(conn.FetchRow<bool>(query)), IllegalState,
             Format("Expected $0 to return true", query));
    }
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn.Fetch("SELECT pg_advisory_xact_lock(3)"));
    SCHECK(VERIFY_RESULT(conn.FetchRow<bool>("SELECT pg_try_advisory_xact_lock_shared(3)")),
           IllegalState, "Expected pg_try_advisory_xact_lock_shared to return true");
    return conn.CommitTransaction();
  }

  Status ValidateUnavailableWithoutSilence(size_t ts_id) {
    auto conn = VERIFY_RESULT(CreateConnToTs(ts_id));
    RETURN_NOT_OK(conn.Execute("SET yb_silence_advisory_locks_not_supported_error = off"));
    for (const auto* query : {"SELECT pg_advisory_lock(1)", "SELECT pg_advisory_unlock(1)"}) {
      auto s = conn.Fetch(query);
      SCHECK(!s.ok(), IllegalState, Format("Expected $0 to fail without the silence GUC", query));
      SCHECK_STR_CONTAINS(s.status().message().ToBuffer(), "advisory locks are not available yet");
      SCHECK(
          PgsqlError(s.status()) == YBPgErrorCode::YB_PG_FEATURE_NOT_SUPPORTED, IllegalState,
          Format("Unexpected SQLSTATE: $0", s.status()));
    }
    return Status::OK();
  }
};

TEST_F(PgAdvisoryLocksSilencedMajorUpgradeTest, NoOpUntilFinalize) {
  ASSERT_OK(ValidateSilencedNoOp(kAnyTserver));

  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_OK(ValidateSilencedNoOp(kMixedModeTserverPg15));
  ASSERT_OK(ValidateSilencedNoOp(kMixedModeTserverPg11));
  ASSERT_OK(ValidateUnavailableWithoutSilence(kMixedModeTserverPg15));

  ASSERT_OK(UpgradeAllTserversFromMixedMode());
  ASSERT_OK(ValidateSilencedNoOp(kMixedModeTserverPg15));
  ASSERT_OK(ValidateSilencedNoOp(kMixedModeTserverPg11));

  // The no-op depends on the advisory locks table, not on ysql_yb_enable_advisory_locks: no node
  // can hold a DocDB advisory lock without the table, even with the flag turned on early.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server(kMixedModeTserverPg15), "ysql_yb_enable_advisory_locks", "true"));
  ASSERT_OK(ValidateSilencedNoOp(kMixedModeTserverPg15));
  ASSERT_OK(ValidateUnavailableWithoutSilence(kMixedModeTserverPg15));

  ASSERT_OK(FinalizeUpgrade());

  // Finalize promotes ysql_yb_enable_advisory_locks and the master then creates the advisory locks
  // table, both asynchronously. Once a tserver takes a lock without the GUC, the GUC has no effect
  // there and locks conflict across tservers.
  for (size_t ts_id = 0; ts_id < cluster_->num_tablet_servers(); ++ts_id) {
    auto conn = ASSERT_RESULT(CreateConnToTs(ts_id));
    ASSERT_OK(conn.Execute("SET yb_silence_advisory_locks_not_supported_error = off"));
    ASSERT_OK(WaitFor([&conn]() -> Result<bool> {
      auto s = conn.Fetch("SELECT pg_advisory_lock(1)");
      WARN_NOT_OK(ResultToStatus(s), "Advisory locks not available yet");
      return s.ok();
    }, 60s * kTimeMultiplier, "Wait for advisory locks to become available"));
    ASSERT_OK(conn.Fetch("SELECT pg_advisory_unlock(1)"));
  }
  auto conn1 = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
  auto conn2 = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
  ASSERT_OK(conn1.Fetch("SELECT pg_advisory_lock(1)"));
  ASSERT_FALSE(ASSERT_RESULT(conn2.FetchRow<bool>("SELECT pg_try_advisory_lock(1)")));
  ASSERT_TRUE(ASSERT_RESULT(conn1.FetchRow<bool>("SELECT pg_advisory_unlock(1)")));
  ASSERT_TRUE(ASSERT_RESULT(conn2.FetchRow<bool>("SELECT pg_try_advisory_lock(1)")));
}

} // namespace yb
