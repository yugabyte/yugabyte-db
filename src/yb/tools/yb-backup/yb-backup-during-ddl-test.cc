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

#include "yb/common/colocated_util.h"

#include "yb/gutil/strings/join.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/ysql_binary_runner.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using yb::client::Snapshots;
using yb::client::SnapshotTestUtil;
using yb::client::YBTableName;

DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(TEST_use_custom_varz);

namespace yb {
namespace tools {

YB_DEFINE_ENUM(YsqlColocationConfig, (kNotColocated)(kDBColocated));
class YBBackupTestWithColocationParam : public pgwrapper::PgMiniTestBase,
                                        public YBBackupTestBase,
                                        public ::testing::WithParamInterface<YsqlColocationConfig> {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
    // We need the following to be able to run yb-controller with MiniCluster.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_use_custom_varz) = true;
    TEST_SETUP_SUPER(PgMiniTestBase);
    YB_SKIP_TEST_IN_SANITIZERS();
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

  Status AreTablesIncludedInSnapshot(
      const master::ListTablesResponsePB& docdb_tables, const master::SnapshotInfoPB& snapshot) {
    std::unordered_set<std::string> snapshot_table_ids;
    for (const auto& backup_entry : snapshot.backup_entries()) {
      const auto& entry = backup_entry.entry();
      if (entry.type() == master::SysRowEntryType::TABLE) {
        snapshot_table_ids.insert(entry.id());
      }
    }
    for (const auto& table : docdb_tables.tables()) {
      SCHECK(
          snapshot_table_ids.contains(table.id()), NotFound,
          Format("DocDB table with id: $0 is not included in the snapshot", table.id()));
    }
    return Status::OK();
  }

  Status AreTabletsIncludedInSnapshot(
      const std::unordered_set<std::string>& tablet_ids, const master::SnapshotInfoPB& snapshot) {
    std::unordered_set<std::string> snapshot_tablet_ids;
    for (const auto& backup_entry : snapshot.backup_entries()) {
      const auto& entry = backup_entry.entry();
      if (entry.type() == master::SysRowEntryType::TABLET) {
        snapshot_tablet_ids.insert(entry.id());
      }
    }

    for (const auto& tablet_id : tablet_ids) {
      SCHECK(
          snapshot_tablet_ids.contains(tablet_id), NotFound,
          Format("Tablet with id: $0 is not included in the snapshot", tablet_id));
    }
    return Status::OK();
  }

  std::unique_ptr<SnapshotTestUtil> snapshot_util_;
  const std::string kBackupSourceDbName = "backup_source_db";
  const std::string kRestoreTargetDbName = "restore_target_db";
};

INSTANTIATE_TEST_CASE_P(
    Colocation, YBBackupTestWithColocationParam,
    ::testing::Values(YsqlColocationConfig::kNotColocated, YsqlColocationConfig::kDBColocated));

TEST_P(YBBackupTestWithColocationParam, TestRestorePreserveRelfilenode) {
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
TEST_P(YBBackupTestWithColocationParam, TestFailImportWithUnmatchedDocDBTable) {
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (1, '10')", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (2, '2')", table_name));
  auto ysql_dump_output =
      ASSERT_RESULT(DumpSchemaForRestoreAsOfTime(kBackupSourceDbName, kBackupSourceDbName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name + "_2"));

  // Specifying only namespace identifier (ns id, name and type) means the created snapshot includes
  // all the user tables that belongs to the specified namespace.
  YBTableName namespace_info(YQL_DATABASE_PGSQL);
  namespace_info.set_namespace_name(kBackupSourceDbName);
  // Get the namespace ID for the database using the client API
  master::GetNamespaceInfoResponsePB namespace_resp;
  ASSERT_OK(client_->GetNamespaceInfo(
      "" /* namespace_id */, kBackupSourceDbName, YQL_DATABASE_PGSQL, &namespace_resp));
  auto namespace_id = namespace_resp.namespace_().id();
  namespace_info.set_namespace_id(namespace_id);
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

// Test that CreateSnapshot RPC includes the set of DocDB tables as of snapshot_hybrid_time.
// The test creates a race condition where a create table is issued in the window between the master
// leader receiving the CreateSnapshot RPC and selecting the snapshot_hybrid_time. The test ensures
// that the new table is included in the snapshot.
// Timeline of the test:
//
// Main Thread          Background Thread                 Master Leader
//      |                       |                              |
//      | 1. Create test_table  |                              |
//      | 2. Setup sync points  |                              |
//      | 3. Start bg thread    |                              |
//      |                       | 4. Wait until CreateSnapshot |
//      |                       |    RPC received              |
//      | 5. StartSnapshot      |                              |
//      |                       |                              | 6. Receive CreateSnapshot RPC
//      |                       | 7. Create test_table2        |
//      |                       | 8. Signal creation complete  |
//      | 9. Wait snapshot      |                              |
//      |                       |                              | 10. Select snapshot_hybrid_time
//      |                       |                              | 11. Complete snapshot creation
//      | 12. Verify both       |                              |
//      |     tables included   |                              |
// Sync Points:
// - StartCreateSecondTable: Blocks until snapshot RPC received
// - CreateSecondTableFinished: Signals table creation complete
TEST_P(YBBackupTestWithColocationParam, CreateConsistentMasterSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table", table2_name = "test_table2";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (1, '10')", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (2, '2')", table_name));
  std::vector<SyncPoint::Dependency> dependencies;
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::CreateSnapshotReceived",
       "YBBackupTestWithColocationParam::StartCreateSecondTable"});
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::CreateSecondTableFinished",
       "YBBackupTestWithColocationParam::ContinueSnapshotCreation"});
  yb::SyncPoint::GetInstance()->LoadDependency(dependencies);
  yb::SyncPoint::GetInstance()->EnableProcessing();
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    TEST_SYNC_POINT("YBBackupTestWithColocationParam::StartCreateSecondTable");

    LOG(INFO) << Format("Started creating table: $0", table2_name);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table2_name));

    TEST_SYNC_POINT("YBBackupTestWithColocationParam::CreateSecondTableFinished");
  });
  // Specifying only namespace identifier (ns id, name and type) means the created snapshot includes
  // all the user tables that belongs to the specified namespace.
  YBTableName namespace_info(YQL_DATABASE_PGSQL);
  namespace_info.set_namespace_name(kBackupSourceDbName);
  // Get the namespace ID for the database using the client API
  master::GetNamespaceInfoResponsePB namespace_resp;
  ASSERT_OK(client_->GetNamespaceInfo(
      "" /* namespace_id */, kBackupSourceDbName, YQL_DATABASE_PGSQL, &namespace_resp));
  auto namespace_id = namespace_resp.namespace_().id();
  namespace_info.set_namespace_id(namespace_id);

  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_util_->StartSnapshot(namespace_info));
  ASSERT_OK(snapshot_util_->WaitSnapshotDone(snapshot_id));
  Snapshots snapshot_infos = ASSERT_RESULT(snapshot_util_->ListSnapshots(
      snapshot_id, client::ListDeleted::kTrue, client::PrepareForBackup::kTrue,
      client::IncludeDdlInProgressTables::kTrue));
  ASSERT_EQ(snapshot_infos.size(), 1);
  LOG(INFO) << "SnapshotInfoPB is: " << snapshot_infos[0].ShortDebugString();
  thread_holder.JoinAll();

  master::ListTablesResponsePB base_docdb_tables =
      ASSERT_RESULT(client_->ListTables(table_name, /* ysql_db_filter */ kBackupSourceDbName));
  ASSERT_EQ(base_docdb_tables.tables_size(), 2);

  const bool is_colocated = GetParam() == YsqlColocationConfig::kDBColocated;
  TableId parent_colocation_table_id;
  if (is_colocated) {
    // Check that the Entry at index 1 is the parent table entry. The order of entries in the
    // SnapshotInfoPB is: namespace, parent table, parent tablet, all collocated tables entries..
    const auto& first_base_table = base_docdb_tables.tables(0);
    ASSERT_TRUE(first_base_table.colocated_info().colocated());
    parent_colocation_table_id = first_base_table.colocated_info().parent_table_id();
    ASSERT_GE(snapshot_infos[0].backup_entries_size(), 2);
    const auto& parent_entry = snapshot_infos[0].backup_entries(1).entry();
    ASSERT_EQ(parent_entry.type(), master::SysRowEntryType::TABLE);
    ASSERT_EQ(parent_entry.id(), parent_colocation_table_id) << Format(
        "Unexpected parent table id at index 1. Expected: $0, Found: $1",
        parent_colocation_table_id, parent_entry.id());
  }
  ASSERT_OK(AreTablesIncludedInSnapshot(base_docdb_tables, snapshot_infos[0]));
  std::unordered_set<std::string> tablet_ids;
  // Check tablets for each DocDB table
  for (const auto& table : base_docdb_tables.tables()) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(client_->GetTabletsFromTableId(
        table.id(),
        0, // max_tablets = 0 means get all tablets
        &tablets));

    for (const auto& tablet : tablets) {
      tablet_ids.insert(tablet.tablet_id());
    }
  }
  // Check if all tablets of all base tables are included in the snapshot
  ASSERT_OK(AreTabletsIncludedInSnapshot(tablet_ids, snapshot_infos[0]));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace tools
}  // namespace yb
