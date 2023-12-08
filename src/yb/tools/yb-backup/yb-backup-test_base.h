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

class YBBackupTest : public pgwrapper::PgCommandTestBase {
 protected:
  YBBackupTest() : pgwrapper::PgCommandTestBase(false, false) {}

  void SetUp() override;

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override;

  string GetTempDir(const string& subdir);

  Status RunBackupCommand(const vector<string>& args);

  void RecreateDatabase(const string& db);

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

  void DoTestYEDISBackup(helpers::TableOp tableOp);
  void DoTestYSQLKeyspaceBackup(helpers::TableOp tableOp);
  void DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp tableOp);
  void DoTestYSQLKeyspaceWithHyphenBackupRestore(
      const string& backup_db, const string& restore_db);

  void TestColocatedDBBackupRestore();

  client::TableHandle table_;
  TmpDirProvider tmp_dir_;
  std::unique_ptr<TestAdminClient> test_admin_client_;
  std::unique_ptr<client::SnapshotTestUtil> snapshot_util_;
};

}  // namespace tools
}  // namespace yb
