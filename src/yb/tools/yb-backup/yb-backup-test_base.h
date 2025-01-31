// Copyright (c) YugaByte, Inc.
//
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

#pragma once

#include "yb/client/table_handle.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/tools/test_admin_client.h"
#include "yb/tools/tools_test_utils.h"

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

using std::string;
using std::unique_ptr;
using std::vector;

namespace yb {
namespace tools {

namespace helpers {
YB_DEFINE_ENUM(TableOp, (kKeepTable)(kDropTable)(kDropDB));

} // namespace helpers

class YBBackupTestBase {
 protected:
  string GetTempDir(const string& subdir);

  Status RunBackupCommand(const vector<string>& args, auto *cluster);

 private:
  TmpDirProvider tmp_dir_;
};

class YBBackupTest : public pgwrapper::PgCommandTestBase, public YBBackupTestBase {
 protected:
  YBBackupTest() : pgwrapper::PgCommandTestBase(false, false) {}

  void SetUp() override;

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override;

  Status RunBackupCommand(const vector<string>& args);

  void DropPsqlDatabase(const string& db);

  Result<client::YBTableName> GetTableName(
      const string& table_name, const string& log_prefix, const string& ns = string());

  Result<string> GetTableId(
      const string& table_name, const string& log_prefix, const string& ns = string());

  bool CheckPartitions(
      const std::vector<yb::master::TabletLocationsPB>& tablets,
      const vector<string>& expected_splits);

  Result<std::vector<std::string>> GetSplitPoints(
      const std::vector<yb::master::TabletLocationsPB>& tablets);

  void LogTabletsInfo(const std::vector<yb::master::TabletLocationsPB>& tablets);

  Status WaitForTabletPostSplitCompacted(size_t tserver_idx, const TabletId& tablet_id);
  void RestartClusterWithCatalogVersionMode(bool db_catalog_version_mode);
  void DoTestYEDISBackup(helpers::TableOp tableOp);
  void DoTestYSQLKeyspaceBackup();
  void DoTestYSQLMultiSchemaKeyspaceBackup();
  void DoTestYSQLKeyspaceWithHyphenBackupRestore(
      const string& backup_db, const string& restore_db);
  void DoTestYSQLRestoreBackup(std::optional<bool> db_catalog_version_mode);

  void TestColocatedDBBackupRestore();

  client::TableHandle table_;
  std::unique_ptr<TestAdminClient> test_admin_client_;
  std::unique_ptr<client::SnapshotTestUtil> snapshot_util_;
};

class YBBackupTestWithReadCommittedDisabled : public YBBackupTest {
 protected:
  YBBackupTestWithReadCommittedDisabled() : YBBackupTest() {}

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--yb_enable_read_committed_isolation=false");
  }
};

}  // namespace tools
}  // namespace yb
