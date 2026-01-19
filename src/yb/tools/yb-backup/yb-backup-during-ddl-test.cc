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

#include <future>
#include <memory>
#include <string>
#include <tuple>

#include "yb/common/colocated_util.h"
#include "yb/common/transaction.h"

#include "yb/gutil/strings/join.h"

#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"

#include "yb/master/master.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_snapshot_coordinator.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/ysql_binary_runner.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using yb::client::GetTableIdByTableName;
using yb::client::Snapshots;
using yb::client::SnapshotTestUtil;
using yb::client::YBTableName;

DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(TEST_mark_snapshot_as_failed);
DECLARE_bool(TEST_use_custom_varz);

namespace yb {
namespace tools {

YB_DEFINE_ENUM(YsqlColocationConfig, (kNotColocated)(kDBColocated));

// Base class for backup during DDL tests - contains all common functionality
// that doesn't depend on test parameters.
class YBBackupDuringDdl : public pgwrapper::PgMiniTestBase, public YBBackupTestBase {
 public:
  void SetUp() {
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

  // Log the contents of a directory recursively for debugging backup artifacts.
  void LogBackupDirContents(const std::string& dir) {
    LOG(INFO) << "Listing backup directory contents: " << dir;
    auto* env = Env::Default();
    if (!env->DirExists(dir)) {
      LOG(INFO) << "Directory does not exist: " << dir;
      return;
    }
    Status s = env->Walk(
        dir, Env::PRE_ORDER,
        [&](Env::FileType type, const std::string& dirname, const std::string& basename) -> Status {
          const std::string full = JoinPathSegments(dirname, basename);
          if (type == Env::DIRECTORY_TYPE) {
            LOG(INFO) << "dir  " << full;
          } else {
            auto size = env->GetFileSize(full);
            if (size.ok()) {
              LOG(INFO) << "file " << full << " (" << *size << " bytes)";
            } else {
              LOG(INFO) << "file " << full;
            }
          }
          return Status::OK();
        });
    if (!s.ok()) {
      LOG(WARNING) << "Failed to walk directory " << dir << ": " << s.ToString();
    }
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

  Result<std::unordered_set<std::string>> CollectTabletIdsFromTables(
      const master::ListTablesResponsePB& tables) {
    std::unordered_set<std::string> tablet_ids;
    // Check tablets for each DocDB table
    for (const auto& table : tables.tables()) {
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
      RETURN_NOT_OK(client_->GetTabletsFromTableId(
          table.id(),
          0,  // max_tablets = 0 means get all tablets
          &tablets));

      for (const auto& tablet : tablets) {
        tablet_ids.insert(tablet.tablet_id());
      }
    }
    return tablet_ids;
  }

  std::future<Result<TxnSnapshotId>> CreateSnapshotAsync(
      const master::CreateSnapshotRequestPB& req) {
    auto promise = std::make_shared<std::promise<Result<TxnSnapshotId>>>();
    auto future = promise->get_future();

    auto leader_addr_result = cluster_->GetLeaderMasterBoundRpcAddr();
    if (!leader_addr_result.ok()) {
      promise->set_value(STATUS(InternalError, leader_addr_result.status().ToString()));
      return future;
    }
    auto backup_proxy = master::MasterBackupProxy(&client_->proxy_cache(), *leader_addr_result);
    auto resp = std::make_shared<master::CreateSnapshotResponsePB>();
    auto controller = std::make_shared<rpc::RpcController>();
    controller->set_timeout(60s);

    backup_proxy.CreateSnapshotAsync(
        req, resp.get(), controller.get(), [promise, resp, controller]() {
          if (!controller->status().ok()) {
            promise->set_value(STATUS(InternalError, controller->status().ToString()));
          } else if (resp->has_error()) {
            promise->set_value(STATUS(InternalError, resp->error().ShortDebugString()));
          } else {
            auto snapshot_id_result = FullyDecodeTxnSnapshotId(resp->snapshot_id());
            if (snapshot_id_result.ok()) {
              promise->set_value(*snapshot_id_result);
            } else {
              promise->set_value(std::move(snapshot_id_result));
            }
          }
        });
    return future;
  }

 protected:
  std::unique_ptr<SnapshotTestUtil> snapshot_util_;
  const std::string kBackupSourceDbName = "backup_source_db";
  const std::string kRestoreTargetDbName = "restore_target_db";
};

// Parametrized test class for colocation-only tests
class YBBackupTestWithColocationParam : public YBBackupDuringDdl,
                                        public ::testing::WithParamInterface<YsqlColocationConfig> {
 public:
  void SetUp() override {
    YBBackupDuringDdl::SetUp();
    CreateDatabase(kBackupSourceDbName, GetParam());
  }
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
  LogBackupDirContents(backup_dir);
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
  ASSERT_OK(client_->GetNamespaceInfo(kBackupSourceDbName, YQL_DATABASE_PGSQL, &namespace_resp));
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
  ASSERT_OK(client_->GetNamespaceInfo(kBackupSourceDbName, YQL_DATABASE_PGSQL, &namespace_resp));
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
  std::unordered_set<std::string> tablet_ids =
      ASSERT_RESULT(CollectTabletIdsFromTables(base_docdb_tables));
  // Check if all tablets of all base tables are included in the snapshot
  ASSERT_OK(AreTabletsIncludedInSnapshot(tablet_ids, snapshot_infos[0]));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test that we retain the deleted tables of a namespace if CreateSnapshot RPC is being executed.
// Timeline of the test:
// Main Thread                    Master Leader
//   |                                 |
//   |1.Create test_table1,2           |
//   |2.Setup sync points              |
//   |3.CreateSnapshotAsync            |
//   |                                 |4.Receive CreateSnapshot RPC
//   |                                 |5.Select snapshot_hybrid_time
//   |                                 |6.Collect tables (including test_table2)
//   |7.Verify namespace retained      |
//   |8.Delete (hide) test_table       |
//   |                                 |9.Send CREATE_ON_TABLET operations
//   |                                 |10.Complete snapshot creation
//   |11.Wait for snapshot completion  |
//   |12.Verify test_table2 and its    |
//   |   tablets are included in       |
//   |   snapshot                      |
//   |13.Assert namespace is no longer |
//   |   retained                      |
TEST_P(YBBackupTestWithColocationParam, RetainTableDeletedDuringCreateSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table", table2_name = "test_table2";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table2_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (1, '10')", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (2, '2')", table_name));
  std::vector<SyncPoint::Dependency> dependencies;
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::CreateSnapshotEntriesCollected",
       "YBBackupTestWithColocationParam::StartDropSecondTable"});
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::DropSecondTableFinished",
       "YBBackupTestWithColocationParam::StartSnapshotSubmitCreate"});
  yb::SyncPoint::GetInstance()->LoadDependency(dependencies);
  yb::SyncPoint::GetInstance()->EnableProcessing();
  master::ListTablesResponsePB list_tables_resp =
      ASSERT_RESULT(client_->ListTables(table_name, /* ysql_db_filter */ kBackupSourceDbName));
  ASSERT_EQ(list_tables_resp.tables_size(), 2);

  auto master_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto* snapshot_coordinator = &master_leader->master()->snapshot_coordinator();

  // Get the namespace ID for the database using the client API.
  master::GetNamespaceInfoResponsePB namespace_resp;
  ASSERT_OK(client_->GetNamespaceInfo(kBackupSourceDbName, YQL_DATABASE_PGSQL, &namespace_resp));
  auto& namespace_id = namespace_resp.namespace_().id();
  // We will delete table test_table2 after starting the async create snapshot. Table deletion
  // happens after the master leader selects the snapshot_hybrid_time and collects the entries
  // included in the snapshot but before sending CREATE_ON_TABLET snapshot operation to all the
  // tablets. CreateSnapshot should retain the deleted table as it should be part of the snapshot.

  // Specifying only namespace identifier (ns id, name and type) means the created snapshot includes
  // all the user tables that belongs to the specified namespace.
  master::CreateSnapshotRequestPB req;
  auto table = req.add_tables();
  table->mutable_namespace_()->set_name(kBackupSourceDbName);
  table->mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  table->mutable_namespace_()->set_id(namespace_id);

  auto snapshot_future = CreateSnapshotAsync(req);

  TEST_SYNC_POINT("YBBackupTestWithColocationParam::StartDropSecondTable");

  // Verify that namespace is retained during snapshot creation.
  ASSERT_TRUE(snapshot_coordinator->IsNamespaceRetained(namespace_id))
      << "Namespace should be retained during snapshot creation";

  LOG(INFO) << Format("Started dropping table: $0", table2_name);

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table2_name));
  TEST_SYNC_POINT("YBBackupTestWithColocationParam::DropSecondTableFinished");

  // Wait for the async create snapshot to complete.
  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_future.get());
  ASSERT_OK(snapshot_util_->WaitSnapshotDone(snapshot_id));
  Snapshots snapshot_infos = ASSERT_RESULT(snapshot_util_->ListSnapshots(
      snapshot_id, client::ListDeleted::kTrue, client::PrepareForBackup::kTrue,
      client::IncludeDdlInProgressTables::kTrue));
  ASSERT_EQ(snapshot_infos.size(), 1);
  LOG(INFO) << "SnapshotInfoPB is: " << snapshot_infos[0].ShortDebugString();
  // TODO(mhaddad): For colocated databases, adds checks that parent colocated tablets (1 per
  // tablespace) are included in the SnapshotInfoPB.
  ASSERT_OK(AreTablesIncludedInSnapshot(list_tables_resp, snapshot_infos[0]));
  std::unordered_set<std::string> tablet_ids =
      ASSERT_RESULT(CollectTabletIdsFromTables(list_tables_resp));
  // Check if all tablets of all base tables are included in the snapshot
  ASSERT_OK(AreTabletsIncludedInSnapshot(tablet_ids, snapshot_infos[0]));

  // Verify that namespace is no longer retained after snapshot creation completion
  ASSERT_FALSE(snapshot_coordinator->IsNamespaceRetained(namespace_id))
      << "Namespace should no longer be retained after snapshot creation completion";

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_P(YBBackupTestWithColocationParam, CleanNamespaceAnchoringAfterFailedSnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_mark_snapshot_as_failed) = true;
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table", table2_name = "test_table2";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table2_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (1, '10')", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (2, '2')", table_name));
  std::vector<SyncPoint::Dependency> dependencies;
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::CreateSnapshotEntriesCollected",
       "YBBackupTestWithColocationParam::StartDropSecondTable"});
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::DropSecondTableFinished",
       "YBBackupTestWithColocationParam::StartSnapshotSubmitCreate"});
  yb::SyncPoint::GetInstance()->LoadDependency(dependencies);
  yb::SyncPoint::GetInstance()->EnableProcessing();
  master::ListTablesResponsePB list_tables_resp =
      ASSERT_RESULT(client_->ListTables(table_name, /* ysql_db_fiter */ kBackupSourceDbName));
  ASSERT_EQ(list_tables_resp.tables_size(), 2);

  auto master_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto* snapshot_coordinator = &master_leader->master()->snapshot_coordinator();

  // Get the namespace ID for the database using the client API
  master::GetNamespaceInfoResponsePB namespace_resp;
  ASSERT_OK(client_->GetNamespaceInfo(kBackupSourceDbName, YQL_DATABASE_PGSQL, &namespace_resp));
  auto namespace_id = namespace_resp.namespace_().id();

  // Specifying only namespace identifier (ns id, name and type) means the created snapshot includes
  // all the user tables that belongs to the specified namespace.
  master::CreateSnapshotRequestPB req;
  auto table = req.add_tables();
  table->mutable_namespace_()->set_name(kBackupSourceDbName);
  table->mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  table->mutable_namespace_()->set_id(namespace_id);

  auto snapshot_future = CreateSnapshotAsync(req);

  TEST_SYNC_POINT("YBBackupTestWithColocationParam::StartDropSecondTable");

  // Verify that namespace is retained during snapshot creation
  ASSERT_TRUE(snapshot_coordinator->IsNamespaceRetained(namespace_id))
      << "Namespace should be retained during snapshot creation";

  LOG(INFO) << Format("Started dropping table: $0", table2_name);

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table2_name));
  TEST_SYNC_POINT("YBBackupTestWithColocationParam::DropSecondTableFinished");

  // Wait for the async create snapshot to complete.
  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_future.get());
  ASSERT_FALSE(snapshot_id.IsNil()) << "Snapshot ID should not be nil";

  // Wait for the snapshot to fail as expected.
  ASSERT_OK(snapshot_util_->WaitSnapshotInState(snapshot_id, master::SysSnapshotEntryPB::FAILED));

  // Verify that namespace is no longer retained after snapshot creation completion.
  ASSERT_FALSE(snapshot_coordinator->IsNamespaceRetained(namespace_id))
      << "Namespace should no longer be retained after snapshot creation completion";

  // Wait for the dropped table to be hard deleted, ListTables should return only 1 table.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        master::ListTablesResponsePB list_tables_resp =
            VERIFY_RESULT(client_->ListTables(table_name, /* ysql_db_fiter */ kBackupSourceDbName));
        return list_tables_resp.tables_size() == 1;
      },
      30s, "Wait for dropped table to be hard deleted"));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Base class for Backup creation during ALTER TABLE tests.
// Test that import_snapshot restores the correct DocDB table schema in case a snapshot is created
// in the middle of alter table DDL.
// Timeline of the test:
// Main Thread                    Master Leader
//   |                                 |
//   |1.Create test_table              |
//   |2.Setup sync points              |
//   |3.Start AlterTable               |
//   |4.Create a Backup and            |
//   |  start CreateSnapshot           |
//   |                                 |5.Wait until new DocDB table schema is committed
//   |                                 |6.Select snapshot_ht
//   |                                 |7.Collect tables (including test_table)
//   |8.continue AlterTable, commit on pg|
//   |9.Run ysql_dump as of snapshot_ht|
//   |10.Wait for snapshot completion  |
//   |11.Export snapshot               |
//   |12.Restore the database          |
//   |13.Verify restore is successful  |
//   |14.Assert test_table has old     |
//   |   schema (before the ALTER)     |

class YBBackupDuringAlterTable : public YBBackupDuringDdl,
                                 public ::testing::WithParamInterface<YsqlColocationConfig> {
 public:
  void SetUp() override {
    YBBackupDuringDdl::SetUp();
    CreateDatabase(kBackupSourceDbName, GetParam());
  }

  static std::vector<SyncPoint::Dependency> DefaultAlterTableDuringBackupDependencies() {
    std::vector<SyncPoint::Dependency> dependencies;
    dependencies.push_back(
        {"YBBackupTestWithColocationParam::AlterTableDocDBTableCommitted",
         "YBBackupTestWithColocationParam::ContinueSnapshotCreation"});
    dependencies.push_back(
        {"YBBackupTestWithColocationParam::CreateSnapshotEntriesCollected",
         "YBBackupTestWithColocationParam::ContinueAlterTable"});
    return dependencies;
  }

  // Runs the DDL query during backup creation and returns the restored table info and connection.
  // The caller can optionally pass sync-point dependencies, and can optionally specify a "window"
  // (two test sync points) to run the DDL in between.
  //
  // If before_alter_sync_point is non-empty, we wait on that sync point before running the DDL.
  // If after_alter_sync_point is non-empty, we signal that sync point after running the DDL.
  Result<std::pair<client::YBTableInfo, pgwrapper::PGConn>> RunAlterDuringBackup(
      const std::string& table_name, const std::string& alter_query,
      const std::vector<SyncPoint::Dependency>& dependencies,
      const std::string& before_alter_sync_point = "",
      const std::string& after_alter_sync_point = "") {
    yb::SyncPoint::GetInstance()->LoadDependency(dependencies);
    yb::SyncPoint::GetInstance()->EnableProcessing();

    // Run the ALTER TABLE DDL while the backup is being created.
    // Creating the snapshot can be delayed using sync points to simulate races between:
    // - committing schema in DocDB
    // - selecting snapshot hybrid time / collecting snapshot entries
    // - tablet-level snapshot creation and restore
    auto conn = VERIFY_RESULT(ConnectToDB(kBackupSourceDbName));

    const string backup_dir = GetTempDir("backup");
    TestThreadHolder thread_holder;
    // Run the backup creation in a background thread so that we can interleave it with the ALTER.
    thread_holder.AddThreadFunctor([&] {
      LOG(INFO) << Format(
          "Starting backup create for keyspace ysql.$0 into dir $1", kBackupSourceDbName,
          backup_dir);
      ASSERT_OK(RunBackupCommand(
          {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", kBackupSourceDbName),
           "create"},
          cluster_.get()));
      LOG(INFO) << "Backup create finished";
    });

    if (!before_alter_sync_point.empty()) {
      TEST_SYNC_POINT(before_alter_sync_point.c_str());
    }
    LOG(INFO) << Format("Started Altering table: $0 with query: $1", table_name, alter_query);
    RETURN_NOT_OK(conn.ExecuteFormat(alter_query));
    if (!after_alter_sync_point.empty()) {
      TEST_SYNC_POINT(after_alter_sync_point.c_str());
    }

    thread_holder.JoinAll();

    LogBackupDirContents(backup_dir);
    RETURN_NOT_OK(RunBackupCommand(
        {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", kRestoreTargetDbName),
         "restore"},
        cluster_.get()));

    // Get the restored table info.
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    auto table_id =
        VERIFY_RESULT(GetTableIdByTableName(client.get(), kRestoreTargetDbName, table_name));
    auto table_info =
        VERIFY_RESULT(client->GetYBTableInfoById(table_id, false /* include_hidden */));
    LOG(INFO) << "Table info: " << table_info.schema.ToString();

    auto restored_conn = VERIFY_RESULT(ConnectToDB(kRestoreTargetDbName));
    return std::make_pair(std::move(table_info), std::move(restored_conn));
  }

  // The default "alter during backup" dependency sync points. Used in multiple tests.
  Result<std::pair<client::YBTableInfo, pgwrapper::PGConn>> RunAlterDuringBackup(
      const std::string& table_name, const std::string& alter_query) {
    return RunAlterDuringBackup(
        table_name, alter_query, DefaultAlterTableDuringBackupDependencies());
  }
};

INSTANTIATE_TEST_CASE_P(
    AlterTable, YBBackupDuringAlterTable,
    ::testing::Values(YsqlColocationConfig::kNotColocated, YsqlColocationConfig::kDBColocated));

  // Test a race condition when a DDL is issued in the window between the master leader collecting
  // snapshot entries and submitting CREATE_ON_MASTER (and thus CREATE_ON_TABLET) operations.
TEST_P(YBBackupDuringAlterTable, AlterTableDuringCreateSnapshot) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Test requires YBC";
  }

  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table";

  // Set up table with initial schema.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k) VALUES (1)", table_name));

  // Orchestrate the following timeline:
  // 1. Backup creation starts and the master collects snapshot entries for the namespace.
  // 2. Before submitting CREATE_ON_MASTER (and thus CREATE_ON_TABLET) operations, we run
  //    "ALTER TABLE ... ADD COLUMN v INT" which bumps the DocDB schema.
  // 3. Snapshot creation then finishes and backup completes.
  // 4. We restore the backup into a new database and assert that restore succeeds.
  std::vector<SyncPoint::Dependency> dependencies;
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::CreateSnapshotEntriesCollected",
       "YBBackupTestWithColocationParam::StartAlterTableAfterCollectingSnapshotEntries"});
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::AlterTableDuringCreateSnapshotFinished",
       "YBBackupTestWithColocationParam::StartSnapshotSubmitCreate"});
  auto [_, restored_conn] = ASSERT_RESULT(RunAlterDuringBackup(
      table_name, Format("ALTER TABLE $0 ADD COLUMN v INT", table_name), dependencies,
      "YBBackupTestWithColocationParam::StartAlterTableAfterCollectingSnapshotEntries",
      "YBBackupTestWithColocationParam::AlterTableDuringCreateSnapshotFinished"));

  auto rows =
      ASSERT_RESULT(restored_conn.FetchRows<int32_t>(Format("SELECT k FROM $0", table_name)));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_EQ(rows[0], 1);
}

TEST_P(YBBackupDuringAlterTable, AddColumnWithDefaultValueDuringCreateSnapshot) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Test requires YBC";
  }

  const std::string table_name = "test_table";

  // Set up table with initial schema.
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k) VALUES (1)", table_name));

  // Orchestrate the following timeline:
  // 1. Backup creation starts and the master collects snapshot entries for the namespace.
  // 2. Before submitting CREATE_ON_MASTER (and thus CREATE_ON_TABLET) operations, we run
  //    "ALTER TABLE ... ADD COLUMN v TEXT DEFAULT 'foo'" which bumps the DocDB schema and sets
  //    missing_value for the new column.
  // 3. Snapshot creation then finishes and backup completes.
  // 4. We restore the backup into a new database and assert that restore succeeds.
  std::vector<SyncPoint::Dependency> dependencies;
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::CreateSnapshotEntriesCollected",
       "YBBackupTestWithColocationParam::StartAlterTableAfterCollectingSnapshotEntries"});
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::AlterTableDuringCreateSnapshotFinished",
       "YBBackupTestWithColocationParam::StartSnapshotSubmitCreate"});
  auto [_, restored_conn] = ASSERT_RESULT(RunAlterDuringBackup(
      table_name, Format("ALTER TABLE $0 ADD COLUMN v TEXT DEFAULT 'foo'", table_name),
      dependencies,
      "YBBackupTestWithColocationParam::StartAlterTableAfterCollectingSnapshotEntries",
      "YBBackupTestWithColocationParam::AlterTableDuringCreateSnapshotFinished"));

  auto rows = ASSERT_RESULT(
      restored_conn.FetchRows<int32_t>(Format("SELECT k FROM $0 ORDER BY k", table_name)));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_EQ(rows[0], 1);

  // The restored schema should match the dump schema (as-of snapshot creation time), i.e. only
  // column k. Verify we can add column v again.
  ASSERT_OK(restored_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN v TEXT", table_name));
}

TEST_P(YBBackupDuringAlterTable, DropColumnWithDefaultValueDuringCreateSnapshot) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Test requires YBC";
  }

  const std::string table_name = "test_table";

  // Set up table with initial schema (includes default value column).
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k) VALUES (1)", table_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN v TEXT DEFAULT 'foo'", table_name));
  // Orchestrate the following timeline:
  // 1. Backup creation starts and the master collects snapshot entries for the namespace.
  // 2. Before submitting CREATE_ON_MASTER (and thus CREATE_ON_TABLET) operations, we run
  //    "ALTER TABLE ... DROP COLUMN v" which bumps the DocDB schema.
  // 3. Snapshot creation then finishes and backup completes.
  // 4. We restore the backup into a new database and assert that restore succeeds.
  std::vector<SyncPoint::Dependency> dependencies;
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::CreateSnapshotEntriesCollected",
       "YBBackupTestWithColocationParam::StartAlterTableAfterCollectingSnapshotEntries"});
  dependencies.push_back(
      {"YBBackupTestWithColocationParam::AlterTableDuringCreateSnapshotFinished",
       "YBBackupTestWithColocationParam::StartSnapshotSubmitCreate"});

  auto [_, restored_conn] = ASSERT_RESULT(RunAlterDuringBackup(
      table_name, Format("ALTER TABLE $0 DROP COLUMN v", table_name), dependencies,
      "YBBackupTestWithColocationParam::StartAlterTableAfterCollectingSnapshotEntries",
      "YBBackupTestWithColocationParam::AlterTableDuringCreateSnapshotFinished"));

  // The restored schema should match the dump schema (as-of snapshot creation time), i.e. it
  // should still have column v with default 'foo' applied to existing row.
  auto rows = ASSERT_RESULT((restored_conn.FetchRows<int32_t, std::string>(
      Format("SELECT k, v FROM $0 ORDER BY k", table_name))));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_EQ(std::get<0>(rows[0]), 1);
  ASSERT_EQ(std::get<1>(rows[0]), "foo");

  // Verify v exists by dropping it successfully.
  ASSERT_OK(restored_conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN v", table_name));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_P(YBBackupDuringAlterTable, AddColumn) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Test requires YBC";
  }
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table";

  // Set up table with initial schema
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k) VALUES (1)", table_name));
  // Add and remove some columns so that we have multiple schema packings
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c1 INT", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k,c1) VALUES (2, 2)", table_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN c1", table_name));
  // Add and remove c2
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c2 INT", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k,c2) VALUES (3, 3)", table_name));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN c2", table_name));
  // Add column c3 and keep it. This is to test different column_ids in backup/restore side for
  // the same column name. i.e c3 will have column_id= [1,3] in the [backup,restore] side.
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c3 INT", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k,c3) VALUES (4, 4)", table_name));

  // Run the ALTER TABLE DDL during backup
  auto [table_info, restored_conn] = ASSERT_RESULT(RunAlterDuringBackup(
      table_name, Format("ALTER TABLE $0 ADD COLUMN v TEXT", table_name),
      DefaultAlterTableDuringBackupDependencies()));

  // Verify that the restored table has the old schema (before the ALTER).
  // Should have old schema: just k and c3 columns
  ASSERT_EQ(table_info.schema.columns().size(), 2);
  ASSERT_EQ(table_info.schema.columns()[0].name(), "k");
  ASSERT_EQ(table_info.schema.columns()[1].name(), "c3");
  auto result =
      ASSERT_RESULT(restored_conn.FetchRows<int32_t>(Format("SELECT k FROM $0", table_name)));
  ASSERT_EQ(result.size(), 4);
  // Try to add a new column with the name v and verify it works successfully
  ASSERT_OK(restored_conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN v TEXT", table_name));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_P(YBBackupDuringAlterTable, DropColumn) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Test requires YBC";
  }
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table";

  // Set up table with initial schema
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (1, 'test')", table_name));

  // Run the ALTER TABLE DDL during backup
  auto [table_info, restored_conn] = ASSERT_RESULT(RunAlterDuringBackup(
      table_name, Format("ALTER TABLE $0 DROP COLUMN v", table_name),
      DefaultAlterTableDuringBackupDependencies()));

  // Verify that the restored table has the old schema (before the ALTER).
  // Should have old schema: both k and v columns
  ASSERT_EQ(table_info.schema.columns().size(), 2);
  ASSERT_EQ(table_info.schema.columns()[0].name(), "k");
  ASSERT_EQ(table_info.schema.columns()[1].name(), "v");
  auto result = ASSERT_RESULT(
      (restored_conn.FetchRows<int32_t, std::string>(Format("SELECT k, v FROM $0", table_name))));
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(std::get<0>(result[0]), 1);
  ASSERT_EQ(std::get<1>(result[0]), "test");

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_P(YBBackupDuringAlterTable, RenameColumn) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Test requires YBC";
  }
  auto conn = ASSERT_RESULT(ConnectToDB(kBackupSourceDbName));
  const std::string table_name = "test_table";

  // Set up table with initial schema
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (k, v) VALUES (1, 'test')", table_name));

  // Run the ALTER TABLE DDL during backup
  auto [table_info, restored_conn] = ASSERT_RESULT(RunAlterDuringBackup(
      table_name, Format("ALTER TABLE $0 RENAME COLUMN v TO v2", table_name),
      DefaultAlterTableDuringBackupDependencies()));

  // Verify that the restored table has the old schema (before the ALTER).
  // Should have old schema: k and v (not v2)
  ASSERT_EQ(table_info.schema.columns().size(), 2);
  ASSERT_EQ(table_info.schema.columns()[0].name(), "k");
  ASSERT_EQ(table_info.schema.columns()[1].name(), "v");
  auto result = ASSERT_RESULT(
      (restored_conn.FetchRows<int32_t, std::string>(Format("SELECT k, v FROM $0", table_name))));
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(std::get<0>(result[0]), 1);
  ASSERT_EQ(std::get<1>(result[0]), "test");

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace tools
}  // namespace yb
