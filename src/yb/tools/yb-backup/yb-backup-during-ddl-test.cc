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

#include <string>

#include "yb/rpc/rpc_controller.h"

#include "yb/util/test_util.h"

#include "yb/tools/yb-backup/yb-backup-test_base.h"
namespace yb {
namespace tools {

YB_DEFINE_ENUM(YsqlColocationConfig, (kNotColocated)(kDBColocated));
class YBBackupTestWithColocationParam : public YBBackupTest,
                                        public ::testing::WithParamInterface<YsqlColocationConfig> {
 public:
  void SetUp() override {
    YBBackupTest::SetUp();
    CreateDatabase(kBackupSourceDbName, GetParam());
    SetDbName(kBackupSourceDbName);  // Connect to the backup source database
  }

  void CreateDatabase(
      const std::string& namespace_name,
      YsqlColocationConfig colocated = YsqlColocationConfig::kNotColocated) {
    RunPsqlCommand(
        Format(
            "CREATE DATABASE $0$1", namespace_name,
            colocated == YsqlColocationConfig::kDBColocated ? " with colocation = true" : ""),
        "CREATE DATABASE");
  }
  const std::string kBackupSourceDbName = "backup_source_db";
  const std::string kRestoreTargetDbName = "restore_target_db";
};

INSTANTIATE_TEST_CASE_P(
    Colocation, YBBackupTestWithColocationParam,
    ::testing::Values(YsqlColocationConfig::kNotColocated, YsqlColocationConfig::kDBColocated));

TEST_P(
    YBBackupTestWithColocationParam,
    YB_DISABLE_TEST_IN_SANITIZERS(TestRestorePreserveRelfilenode)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE table_to_be_rewritten (k INT, v TEXT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (1, 'foo')"));
  ASSERT_NO_FATALS(RunPsqlCommand("", ""));
  // The following alters incure table rewrite, which means a new relfilenode is assigned
  // to the relation i.e., relfilenode != pg_class.oid in such a relation. Test that
  // relfilenode is preserved at restore side.
  ASSERT_NO_FATALS(RunPsqlCommand("TRUNCATE TABLE mytbl", "TRUNCATE TABLE"));
  ASSERT_NO_FATALS(
      RunPsqlCommand("ALTER TABLE table_to_be_rewritten ADD PRIMARY KEY (k)", "ALTER TABLE"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'foo')"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (101, 'bar')"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (102, 'cab')"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", kBackupSourceDbName),
       "create"}));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (999, 'foo')"));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", kRestoreTargetDbName),
       "restore"}));

  SetDbName(kRestoreTargetDbName);  // Connecting to the second DB from this moment.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
        k  |  v
        -----+-----
         100 | foo
         101 | bar
         102 | cab
        (3 rows)
    )#"));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}
}  // namespace tools
}  // namespace yb
