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

namespace yb {

// Test upgrade and rollback with a simple workload with updates and selects.
class BasicUpgradeTest : public UpgradeTestBase {
 public:
  explicit BasicUpgradeTest(const std::string& from_version) : UpgradeTestBase(from_version) {}
  virtual ~BasicUpgradeTest() = default;

  Status VerifyVersionFromDB(const std::string& expected_version) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    auto version = VERIFY_RESULT(conn.FetchRowAsString("SELECT version()"));
    LOG(INFO) << "Running version: " << version;

    SCHECK_STR_CONTAINS(version, expected_version);
    return Status::OK();
  }

  Status TestUpgrade() {
    RETURN_NOT_OK(StartClusterInOldVersion());
    RETURN_NOT_OK(VerifyVersionFromDB(old_version_info().version));

    RETURN_NOT_OK(PrepareTableAndStartWorkload());

    allow_errors_ = true;
    RETURN_NOT_OK(UpgradeClusterToCurrentVersion());
    allow_errors_ = false;

    RETURN_NOT_OK(VerifyVersionFromDB(current_version_info().version_number()));
    RETURN_NOT_OK(StopWorkloadAndCheckResults());

    return Status::OK();
  }

  Status TestRollback() {
    RETURN_NOT_OK(StartClusterInOldVersion());
    RETURN_NOT_OK(VerifyVersionFromDB(old_version_info().version));

    RETURN_NOT_OK(PrepareTableAndStartWorkload());

    const auto delay_between_nodes = 3s;

    allow_errors_ = true;
    RETURN_NOT_OK(UpgradeClusterToCurrentVersion(delay_between_nodes, /*auto_finalize=*/false));

    RETURN_NOT_OK(VerifyVersionFromDB(current_version_info().version_number()));

    RETURN_NOT_OK(RollbackClusterToOldVersion(delay_between_nodes));
    allow_errors_ = false;

    RETURN_NOT_OK(VerifyVersionFromDB(old_version_info().version));
    RETURN_NOT_OK(StopWorkloadAndCheckResults());

    return Status::OK();
  }

  static constexpr auto kAccountBalanceTable = "account_balance";
  static const int kNumUsers = 3;
  Status PrepareTableAndStartWorkload() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0(id INT, name TEXT, salary INT, PRIMARY KEY(id));", kAccountBalanceTable));

    for (int i = 0; i < kNumUsers; i++) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO $0 VALUES($1, 'user$1', 1000000)", kAccountBalanceTable, i));
    }

    LOG(INFO) << "Initial data inserted: ";
    RETURN_NOT_OK(PrintAccountBalanceTable());

    test_thread_holder_.AddThread([this]() {
      update_status_ = RunUpdateAccountBalanceWorkload(keep_running_, allow_errors_);
    });
    test_thread_holder_.AddThread(
        [this]() { scan_status_ = RunScanAccountBalanceWorkload(keep_running_, allow_errors_); });

    // Wait for a few runs.
    SleepFor(3s);

    return Status::OK();
  }

  Status StopWorkloadAndCheckResults() {
    // Wait for a few extra runs.
    SleepFor(10s);

    keep_running_ = false;
    test_thread_holder_.JoinAll();
    RETURN_NOT_OK_PREPEND(update_status_, "Failed the update workload");
    RETURN_NOT_OK_PREPEND(scan_status_, "Failed the scan workload");
    return Status::OK();
  }

  Status PrintAccountBalanceTable() {
    const auto select_all_query = Format("SELECT * FROM $0", kAccountBalanceTable);

    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    auto rows = VERIFY_RESULT((conn.FetchRows<int32_t, std::string, int32_t>(select_all_query)));
    std::stringstream result_string;
    result_string << "Account balance table: ";
    for (const auto& row : rows) {
      result_string << std::endl
                    << std::get<0>(row) << ", " << std::get<1>(row) << ", " << std::get<2>(row);
    }
    LOG(INFO) << result_string.str();

    return Status::OK();
  }

  // Setup the connection if needed. Return true if a valid connection is ready.
  // If we failed to create a connection and allowed_errors is set then returns false, else returns
  // bad Status.
  Result<bool> TrySetupConn(std::unique_ptr<pgwrapper::PGConn>& conn, bool allow_errors) {
    if (conn) {
      return true;
    }

    auto try_create_conn = [&]() -> Result<std::unique_ptr<pgwrapper::PGConn>> {
      return std::make_unique<pgwrapper::PGConn>(VERIFY_RESULT(cluster_->ConnectToDB()));
    };

    auto conn_result = try_create_conn();
    if (conn_result.ok()) {
      conn.swap(*conn_result);
      return true;
    }

    if (allow_errors) {
      LOG(ERROR) << "Failed to create new connection: " << conn_result.status();

      return false;
    }

    return conn_result.status();
  }

  // Move 500 from each user except the last one to the last user.
  Status RunUpdateAccountBalanceWorkload(
      std::atomic<bool>& keep_running, std::atomic<bool>& allow_errors) {
    std::ostringstream oss;
    oss << "BEGIN TRANSACTION;";
    for (int i = 0; i < kNumUsers - 1; i++) {
      oss << "UPDATE account_balance SET salary = salary - 500 WHERE name = 'user" << i << "';";
    }
    oss << "UPDATE account_balance SET salary = salary + " << 500 * (kNumUsers - 1)
        << " WHERE name = 'user" << kNumUsers - 1 << "';";
    oss << "COMMIT;";
    auto update_query = oss.str();

    std::unique_ptr<pgwrapper::PGConn> conn;
    LOG(INFO) << "Running update workload in a loop: " << update_query;

    while (keep_running) {
      SleepFor(100ms);

      if (!VERIFY_RESULT(TrySetupConn(conn, allow_errors))) {
        continue;
      }

      auto status = conn->Execute(update_query);
      if (!status.ok()) {
        LOG(WARNING) << "Failed to update: " << status;
        if (!allow_errors) {
          return status;
        }
        conn.reset();
        continue;
      }
    }

    return Status::OK();
  }

  // Make sure the total balance remains unchanged.
  Status RunScanAccountBalanceWorkload(
      std::atomic<bool>& keep_running, std::atomic<bool>& allow_errors) {
    const auto select_salary_sum_query = Format("SELECT SUM(salary) FROM $0", kAccountBalanceTable);
    int64_t total_salary = 0;
    {
      auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
      total_salary = VERIFY_RESULT(conn.FetchRow<int64_t>(select_salary_sum_query));
    }

    LOG(INFO) << "Running consumer workload in a loop: " << select_salary_sum_query;
    std::unique_ptr<pgwrapper::PGConn> conn;
    while (keep_running) {
      SleepFor(100ms);

      if (!VERIFY_RESULT(TrySetupConn(conn, allow_errors))) {
        continue;
      }

      auto salary_result = conn->FetchRow<int64_t>(select_salary_sum_query);
      if (!salary_result.ok()) {
        LOG(WARNING) << "Failed to fetch salary sum: " << salary_result.status();
        if (!allow_errors) {
          return salary_result.status();
        }
        conn.reset();
        continue;
      }

      if (total_salary != *salary_result) {
        LOG(ERROR) << "Invalid data: ";
        WARN_NOT_OK(PrintAccountBalanceTable(), "Failed to print account balance table");
        return STATUS_FORMAT(
            IllegalState, "Expected total $0, received total $1", total_salary, *salary_result);
      }
    }

    return Status::OK();
  }

 private:
  Status update_status_, scan_status_;
  std::atomic<bool> keep_running_{true}, allow_errors_{false};
};

class TestBasicUpgradeFrom_2_20_2_4 : public BasicUpgradeTest {
 public:
  TestBasicUpgradeFrom_2_20_2_4() : BasicUpgradeTest(kBuild_2_20_2_4) {}
};

TEST_F_EX(BasicUpgradeTest, TestUpgradeFrom_2_20_2_4, TestBasicUpgradeFrom_2_20_2_4) {
  ASSERT_OK(TestUpgrade());
}

TEST_F_EX(BasicUpgradeTest, TestRollbackTo_2_20_2_4, TestBasicUpgradeFrom_2_20_2_4) {
  ASSERT_OK(TestRollback());
}

class TestBasicUpgradeFrom_2024_1_0_1 : public BasicUpgradeTest {
 public:
  TestBasicUpgradeFrom_2024_1_0_1() : BasicUpgradeTest(kBuild_2024_1_0_1) {}
};

TEST_F_EX(BasicUpgradeTest, TestUpgradeFrom_2024_1_0_1, TestBasicUpgradeFrom_2024_1_0_1) {
  ASSERT_OK(TestUpgrade());
}

TEST_F_EX(BasicUpgradeTest, TestRollbackTo_2024_1_0_1, TestBasicUpgradeFrom_2024_1_0_1) {
  ASSERT_OK(TestRollback());
}

}  // namespace yb
