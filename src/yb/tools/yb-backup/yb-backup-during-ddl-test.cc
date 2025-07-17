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
#include "yb/util/ysql_binary_runner.h"

#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

using yb::client::Snapshots;
using yb::client::SnapshotTestUtil;
using yb::client::YBTableName;

namespace yb {
namespace tools {

YB_DEFINE_ENUM(YsqlColocationConfig, (kNotColocated)(kDBColocated));
class YBBackupTestWithColocationParam : public pgwrapper::LibPqTestBase,
                                        public YBBackupTestBase,
                                        public ::testing::WithParamInterface<YsqlColocationConfig> {
 public:
  void SetUp() override {
    LibPqTestBase::SetUp();
    ASSERT_OK(CreateClient());
    // Start Yb Controllers for backup/restore.
    if (UseYbController()) {
      CHECK_OK(cluster_->StartYbControllerServers());
    }
    CreateDatabase(kBackupSourceDbName, GetParam());
    snapshot_util_ = std::make_unique<SnapshotTestUtil>();
    snapshot_util_->SetProxy(&client_->proxy_cache());
    snapshot_util_->SetCluster(cluster_.get());
  }

  void CreateDatabase(
      const std::string& namespace_name,
      YsqlColocationConfig colocated = YsqlColocationConfig::kNotColocated) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE DATABASE $0$1", namespace_name,
        colocated == YsqlColocationConfig::kDBColocated ? " with colocation = true" : ""));
  }

  Result<std::string> DumpSchemaForRestoreAsOfTime(
      std::string source_db_name, std::string target_db_name,
      const std::optional<HybridTime>& read_time = std::nullopt) {
    YsqlDumpRunner ysql_dump_runner =
        VERIFY_RESULT(YsqlDumpRunner::GetYsqlDumpRunner(cluster_->YsqlHostport()));
    std::string dump_output =
        VERIFY_RESULT(ysql_dump_runner.DumpSchemaAsOfTime(source_db_name, read_time));
    std::string modified_dump = ysql_dump_runner.ModifyDbNameInScript(dump_output, target_db_name);
    LOG(INFO) << "Tool output: " << modified_dump;
    return modified_dump;
  }

  Result<std::string> ExecuteSqlScript(
      const std::string& sql_script, const std::string& tmp_file_prefix) {
    YsqlshRunner ysqlsh_runner =
        VERIFY_RESULT(YsqlshRunner::GetYsqlshRunner(cluster_->YsqlHostport()));
    return ysqlsh_runner.ExecuteSqlScript(sql_script, "ysql_dump" /* tmp_file_prefix */);
  }

  std::unique_ptr<SnapshotTestUtil> snapshot_util_;
  const std::string kBackupSourceDbName = "backup_source_db";
  const std::string kRestoreTargetDbName = "restore_target_db";
};

INSTANTIATE_TEST_CASE_P(
    Colocation, YBBackupTestWithColocationParam,
    ::testing::Values(YsqlColocationConfig::kNotColocated, YsqlColocationConfig::kDBColocated));

TEST_P(
    YBBackupTestWithColocationParam,
    YB_DISABLE_TEST_IN_SANITIZERS(TestRestorePreserveRelfilenode)) {
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE table_to_be_rewritten (k INT, v TEXT)"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO mytbl (k, v) VALUES (1, 'foo')"));
  // The following alters incure table rewrite, which means a new relfilenode is assigned
  // to the relation i.e., relfilenode != pg_class.oid in such a relation. Test that
  // relfilenode is preserved at restore side.
  ASSERT_OK(conn.ExecuteFormat("TRUNCATE TABLE mytbl"));
  ASSERT_OK(
      conn.ExecuteFormat("ALTER TABLE table_to_be_rewritten ADD PRIMARY KEY (k)", "ALTER TABLE"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO mytbl (k, v) VALUES (100, 'foo')"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO mytbl (k, v) VALUES (101, 'bar')"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO mytbl (k, v) VALUES (102, 'cab')"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", kBackupSourceDbName),
       "create"},
      cluster_.get()));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO mytbl (k, v) VALUES (999, 'foo')"));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", kRestoreTargetDbName),
       "restore"},
      cluster_.get()));

  conn = ASSERT_RESULT(ConnectToDB(kRestoreTargetDbName));
  auto rows =
      ASSERT_RESULT((conn.FetchRows<int32_t, std::string>("SELECT k, v FROM mytbl ORDER BY k")));
  ASSERT_EQ(rows, (decltype(rows){{100, "foo"}, {101, "bar"}, {102, "cab"}}));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test that import_snapshot step fails in case one of the DocDB tables in the snapshot doesn't have
// a corresponding pg table at restore side.
TEST_P(
    YBBackupTestWithColocationParam,
    YB_DISABLE_TEST_IN_SANITIZERS(TestFailImportWithUnmatchedDocDBTable)) {
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (1, '10')", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (2, '2')", table_name));
  auto ysql_dump_output =
      ASSERT_RESULT(DumpSchemaForRestoreAsOfTime(kBackupSourceDbName, kBackupSourceDbName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name + "_2"));

  // Specifying only namespace type and name means the created snapshot includes all the user tables
  // that belongs to the specified namespace.
  YBTableName namespace_info(YQL_DATABASE_PGSQL);
  namespace_info.set_namespace_name(kBackupSourceDbName);
  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(namespace_info));
  Snapshots snapshot_infos = ASSERT_RESULT(snapshot_util_->ListSnapshots(
      snapshot_id, client::ListDeleted::kTrue, client::PrepareForBackup::kTrue,
      client::IncludeDdlInProgressTables::kTrue));
  ASSERT_EQ(snapshot_infos.size(), 1);
  // Drop the DB so that we can restore the dump in the same cluster.
  conn = ASSERT_RESULT(ConnectToDB("yugabyte"));
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kBackupSourceDbName));
  ASSERT_RESULT(ExecuteSqlScript(ysql_dump_output, "restore" /* tmp_file_prefix */));
  conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  auto import_snapshot_result = snapshot_util_->StartImportSnapshot(snapshot_infos[0]);
  ASSERT_FALSE(import_snapshot_result.ok());
  ASSERT_STR_CONTAINS(
      import_snapshot_result.status().ToString(),
      master::MasterErrorPB::Code_Name(master::MasterErrorPB::OBJECT_NOT_FOUND));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace tools
}  // namespace yb
