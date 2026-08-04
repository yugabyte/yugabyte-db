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

#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(ysql_yb_enable_advisory_locks);

namespace yb {

class PgAdvisoryLocksUpgradeTest : public UpgradeTestBase,
                                  public ::testing::WithParamInterface<uint32_t> {
 public:
  PgAdvisoryLocksUpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    AddUnDefOkAndSetFlag(
        opts.extra_master_flags, "num_advisory_locks_tablets", Format("$0", GetParam()));
    UpgradeTestBase::SetUpOptions(opts);
  }

  Status TestBasicFunctionality() {
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
    auto assert_num_waiting_locks = [&](int expected_waiting_num_locks) -> Status {
      return WaitFor([&]() -> Result<bool> {
        auto num_waiting_locks = VERIFY_RESULT(
            conn1.FetchRow<int64_t>("SELECT COUNT(*) FROM pg_locks WHERE NOT granted"));
        return num_waiting_locks == expected_waiting_num_locks;
      },
      5s * kTimeMultiplier,
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

} // namespace yb
