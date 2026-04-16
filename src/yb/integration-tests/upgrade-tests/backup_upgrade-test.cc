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
#include "yb/tools/tools_test_utils.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

class BackupUpgradeTest : public UpgradeTestBase {
 public:
  BackupUpgradeTest() : UpgradeTestBase(kBuild_2_25_0_0) {}

  void SetUp() {
    TEST_SETUP_SUPER(UpgradeTestBase);

    if (!UseYbController()) {
      GTEST_SKIP() << "This test does not work without YBC";
    }
  }

  Status RunBackupCommand(const std::vector<std::string>& args) {
    return tools::RunYbControllerCommand(cluster_.get(), *tmp_dir_, args);
  }

  Status StartClusterInOldVersion() {
    RETURN_NOT_OK(UpgradeTestBase::StartClusterInOldVersion());
    // Start Yb Controllers for backup/restore.
    if (UseYbController()) {
      RETURN_NOT_OK(cluster_->StartYbControllerServers());
    }
    return Status::OK();
  }

  std::string GetTempDir(const std::string& subdir) { return tmp_dir_ / subdir; }

 protected:
  std::string kSourceDbName = "src_db";
  std::string kRestoreDbName = "restore_db";
  std::string kTableName = "t";
  tools::TmpDirProvider tmp_dir_;
};

// Verify that a backup created during the monitoring phase is restorable after rollback.
TEST_F(BackupUpgradeTest, TestRestoreBackupAfterRollback) {
  ASSERT_OK(StartClusterInOldVersion());
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0;", kSourceDbName));
  conn = ASSERT_RESULT(cluster_->ConnectToDB(kSourceDbName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName));
  ASSERT_OK(
      conn.ExecuteFormat("INSERT INTO $0 SELECT i,i FROM generate_series(1,10) i", kTableName));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", GetTempDir(kSourceDbName), "--keyspace",
       Format("ysql.$0", kSourceDbName), "create"}));
  ASSERT_OK(RollbackClusterToOldVersion());
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", GetTempDir(kSourceDbName), "--keyspace",
       Format("ysql.$0", kRestoreDbName), "restore"}));
  conn = ASSERT_RESULT(cluster_->ConnectToDB(kSourceDbName));
  auto restore_conn = ASSERT_RESULT(cluster_->ConnectToDB(kRestoreDbName));
  const auto expected_rows =
      ASSERT_RESULT((conn.FetchAllAsString(Format("SELECT * FROM $0", kTableName))));
  const auto restored_row =
      ASSERT_RESULT((restore_conn.FetchAllAsString(Format("SELECT * FROM $0", kTableName))));
  ASSERT_EQ(expected_rows, restored_row);
}
}  // namespace yb
