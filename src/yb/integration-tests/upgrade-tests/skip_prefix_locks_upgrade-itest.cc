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

static constexpr auto kTable = "test_table";
static constexpr auto kKeyCol = "id";
static constexpr auto kValueCol = "value";

class SkipPrefixLocksUpgradeTest : public UpgradeTestBase,
                                   public ::testing::WithParamInterface<std::string> {
 public:
  SkipPrefixLocksUpgradeTest() : UpgradeTestBase(GetParam()) {}

  void SetUp() override {
    LOG(INFO) << "SetUp test";
    TEST_SETUP_SUPER(UpgradeTestBase);

    std::vector<std::string> extra_tserver_flags = {
      "--undefok=TEST_skip_prefix_locks_invariance_check",
      "--TEST_skip_prefix_locks_invariance_check=true"
    };
    ExternalMiniClusterOptions opts;
    opts.num_masters = 3;
    opts.num_tablet_servers = 3;
    opts.extra_tserver_flags = extra_tserver_flags;

    ASSERT_OK(StartClusterInOldVersion(opts));

    // Create test table
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute(Format(
        "CREATE TABLE $0 ($1 TEXT PRIMARY KEY, $2 TEXT)", kTable, kKeyCol, kValueCol)));
  }

  void TearDown() override {
    UpgradeTestBase::TearDown();
  }

  void TestSkipPrefixLocksUpgrade(bool ysql_enable_packed_row_before_upgrade = true) {
    LOG(INFO) << "TestSkipPrefixLocksUpgrade. ysql_enable_packed_row_before_upgrade: "
              << ysql_enable_packed_row_before_upgrade;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      auto* tserver = cluster_->tablet_server(i);
      if (tserver) {
        ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server(0), "ysql_enable_packed_row",
                                    ysql_enable_packed_row_before_upgrade ? "true" : "false"));
      }
    }

    // Start two threads: one with REPEATABLE READ, one with SERIALIZABLE
    TestThreadHolder thread_holder;
    std::atomic<int64_t> repeatable_read_rows{0};
    std::atomic<int64_t> serializable_rows{0};

    thread_holder.AddThread([&]() {
      InsertRowsWithIsolationLevel(
          thread_holder.stop_flag(),
          repeatable_read_rows,
          IsolationLevel::SNAPSHOT_ISOLATION);
    });

    thread_holder.AddThread([&]() {
      InsertRowsWithIsolationLevel(
          thread_holder.stop_flag(),
          serializable_rows,
          IsolationLevel::SERIALIZABLE_ISOLATION);
    });

    // Let threads run for a bit before upgrade
    SleepFor(5s);
    ASSERT_GT(repeatable_read_rows.load(), 0);
    ASSERT_GT(serializable_rows.load(), 0);

    // Perform upgrade
    ASSERT_OK(UpgradeClusterToCurrentVersion());
    LOG(INFO) << "Upgrade completed";

    // Stop threads and check results
    thread_holder.Stop();
    SleepFor(5s);

    LOG(INFO) << "REPEATABLE READ thread inserted: " << repeatable_read_rows.load() << " rows";
    LOG(INFO) << "SERIALIZABLE thread inserted: " << serializable_rows.load() << " rows";

    // Verify all rows are present
    VerifyAllRows(repeatable_read_rows.load(), serializable_rows.load());
  }

 private:
  void InsertRowsWithIsolationLevel(
      std::atomic<bool>& stop_flag,
      std::atomic<int64_t>& rows_inserted,
      IsolationLevel isolation_level) {

    std::string isolation_level_name = IsolationLevel_Name(isolation_level);
    while (!stop_flag) {
      auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
      while (!stop_flag) {
        // Set isolation level and insert a row
        auto status = conn.StartTransaction(isolation_level);
        if (!status.ok()) {
          LOG(WARNING) << "Failed to begin transaction: " << status;
          SleepFor(1s);
          break;
        }

        int64_t current_id = rows_inserted.load();
        status = conn.ExecuteFormat(
            "INSERT INTO $0 ($1, $2) VALUES ('$3', '$4') ON CONFLICT DO NOTHING",
            kTable, kKeyCol, kValueCol,
            Format("$0-$1", isolation_level_name, current_id),
            Format("$0_row_$1", isolation_level_name, current_id));
        if (!status.ok()) {
          LOG(WARNING) << "Failed to insert row with " << isolation_level_name
                       << " isolation: " << status;
          status = conn.RollbackTransaction();
          if (status.ok()) {
            SleepFor(1s);
            continue;
          }
        } else {
          status = conn.CommitTransaction();
        }
        if (!status.ok()) {
          LOG(WARNING) << "Failed to commit/abort transaction: " << status;
          SleepFor(1s);
          break;
        }

        rows_inserted++;
        SleepFor(100ms);
      }
    }

    LOG(INFO) << "Inserted " << rows_inserted << " rows with "
              << isolation_level_name << " isolation";
  }

  void VerifyAllRows(int64_t expected_repeatable_read_rows, int64_t expected_serializable_rows) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

    // Count total rows
    auto total_rows = ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT COUNT(*) FROM $0", kTable)));

    // Count rows by isolation level (based on value pattern)
    auto repeatable_read_count = ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT COUNT(*) FROM $0 WHERE $1 LIKE 'SNAPSHOT_ISOLATION_row_%'",
               kTable, kValueCol)));
    auto serializable_count = ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT COUNT(*) FROM $0 WHERE $1 LIKE 'SERIALIZABLE_ISOLATION_row_%'",
               kTable, kValueCol)));

    LOG(INFO) << "Verification results:";
    LOG(INFO) << "  Total rows: " << total_rows;
    LOG(INFO) << "  REPEATABLE READ rows: " << repeatable_read_count;
    LOG(INFO) << "  SERIALIZABLE rows: " << serializable_count;
    LOG(INFO) << "  Expected REPEATABLE READ: " << expected_repeatable_read_rows;
    LOG(INFO) << "  Expected SERIALIZABLE: " << expected_serializable_rows;

    // Verify counts match expectations
    CHECK_EQ(total_rows, repeatable_read_count + serializable_count);
    CHECK_EQ(repeatable_read_count, expected_repeatable_read_rows);
    CHECK_EQ(serializable_count, expected_serializable_rows);
  }
};

TEST_P(SkipPrefixLocksUpgradeTest, TestSkipPrefixLocksUpgrade_PackedRowEnabled) {
  TestSkipPrefixLocksUpgrade(true);
}

TEST_P(SkipPrefixLocksUpgradeTest, TestSkipPrefixLocksUpgrade_PackedRowDisabled) {
  TestSkipPrefixLocksUpgrade(false);
}

INSTANTIATE_TEST_SUITE_P(
    UpgradeFrom_2024_2_4_0, SkipPrefixLocksUpgradeTest, ::testing::Values(kBuild_2024_2_4_0));
INSTANTIATE_TEST_SUITE_P(
    UpgradeFrom_2_25_0_0, SkipPrefixLocksUpgradeTest, ::testing::Values(kBuild_2_25_0_0));
}  // namespace yb
