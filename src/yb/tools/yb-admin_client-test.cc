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

#include <gmock/gmock.h>

#include "yb/common/wire_protocol.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/pb_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

using namespace std::literals;

DECLARE_bool(TEST_hang_on_namespace_transition);

namespace yb {
namespace tools {

// Tests for the client. Used to verify behaviour that cannot be verified by using yb-admin as an
// external process.
class ClusterAdminClientTest : public pgwrapper::PgCommandTestBase {
 public:
  std::unique_ptr<ClusterAdminClient> cluster_admin_client_;

 protected:
  ClusterAdminClientTest() : pgwrapper::PgCommandTestBase(false, false) {}

  void SetUp() override {
    pgwrapper::PgCommandTestBase::SetUp();
    ASSERT_OK(CreateClient());
    cluster_admin_client_ = std::make_unique<ClusterAdminClient>(
        cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(60));
    ASSERT_OK(cluster_admin_client_->Init());
  }

  Result<pgwrapper::PGConn> PgConnect(const std::string& db_name = std::string()) {
    auto* ts = cluster_->tablet_server(
        RandomUniformInt<size_t>(0, cluster_->num_tablet_servers() - 1));
    return pgwrapper::PGConnBuilder({
      .host = ts->bind_host(),
      .port = ts->ysql_port(),
      .dbname = db_name
    }).Connect();
  }

  Status WaitForSnapshotComplete(const TxnSnapshotId& snapshot_id) {
    return WaitFor(
        [&]() -> Result<bool> {
          master::ListSnapshotsResponsePB resp = VERIFY_RESULT(
              cluster_admin_client_->ListSnapshots(EnumBitSet<ListSnapshotsFlag>(), snapshot_id));
          if (resp.has_error()) {
            return StatusFromPB(resp.error().status());
          }
          if (resp.snapshots_size() != 1) {
            return STATUS(
                IllegalState, Format("There should be exactly one snapshot of id $0", snapshot_id));
          }
          return resp.snapshots(0).entry().state() == master::SysSnapshotEntryPB::COMPLETE;
        },
        30s * kTimeMultiplier, "Waiting for snapshot to complete");
  }

  Result<TxnSnapshotId> CreateAndWaitForSnapshotToComplete(const TypedNamespaceName database) {
    RETURN_NOT_OK(
        cluster_admin_client_->CreateNamespaceSnapshot(database, 0 /* retention_duration_hours */));
    auto resp = VERIFY_RESULT(cluster_admin_client_->ListSnapshots({}));
    SCHECK_EQ(
        resp.snapshots_size(), 1, IllegalState,
        Format("Expected 1 snapshot but found: $0", resp.snapshots_size()));
    TxnSnapshotId snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(resp.snapshots(0).id()));
    RETURN_NOT_OK(WaitForSnapshotComplete(snapshot_id));
    LOG(INFO) << Format("Finished creating snapshot with id: $0", snapshot_id);
    return snapshot_id;
  }
};

TEST_F(ClusterAdminClientTest, YB_DISABLE_TEST_IN_SANITIZERS(ListSnapshotsWithDetails)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE test_table (k INT PRIMARY KEY, v TEXT)"));
  const TypedNamespaceName database {
    .db_type = YQL_DATABASE_PGSQL,
    .name = "yugabyte"};
  ASSERT_OK(cluster_admin_client_->CreateNamespaceSnapshot(
      database, 0 /* retention_duration_hours */));
  EnumBitSet<ListSnapshotsFlag> flags{ListSnapshotsFlag::SHOW_DETAILS};
  auto resp = ASSERT_RESULT(cluster_admin_client_->ListSnapshots(flags));
  EXPECT_EQ(resp.snapshots_size(), 1);
  std::unordered_set<master::SysRowEntryType> expected_types = {
      master::SysRowEntryType::NAMESPACE, master::SysRowEntryType::TABLE};
  std::unordered_set<master::SysRowEntryType> missing_types = {
      master::SysRowEntryType::NAMESPACE, master::SysRowEntryType::TABLE};
  for (const auto& entry : resp.snapshots(0).entry().entries()) {
    EXPECT_THAT(expected_types, testing::Contains(entry.type()));
    missing_types.erase(entry.type());
    switch (entry.type()) {
      case master::SysRowEntryType::NAMESPACE: {
        auto meta =
            ASSERT_RESULT(pb_util::ParseFromSlice<master::SysNamespaceEntryPB>(entry.data()));
        EXPECT_EQ(meta.name(), "yugabyte");
        EXPECT_EQ(meta.database_type(), YQL_DATABASE_PGSQL);
        EXPECT_EQ(
            meta.state(), master::SysNamespaceEntryPB_State::SysNamespaceEntryPB_State_RUNNING);
        break;
      }
      case master::SysRowEntryType::TABLE: {
        auto meta = ASSERT_RESULT(pb_util::ParseFromSlice<master::SysTablesEntryPB>(entry.data()));
        EXPECT_EQ(meta.name(), "test_table");
        EXPECT_EQ(meta.table_type(), yb::TableType::PGSQL_TABLE_TYPE);
        EXPECT_EQ(meta.namespace_name(), "yugabyte");
        EXPECT_EQ(meta.state(), master::SysTablesEntryPB_State::SysTablesEntryPB_State_RUNNING);
        break;
      }
      default:
        break;
    }
  }
  EXPECT_THAT(missing_types, testing::IsEmpty());
}

TEST_F(ClusterAdminClientTest, YB_DISABLE_TEST_IN_SANITIZERS(ListSnapshotsWithoutDetails)) {
  CreateTable("CREATE TABLE test_table (k INT PRIMARY KEY, v TEXT)");
  const TypedNamespaceName database {
    .db_type = YQL_DATABASE_PGSQL,
    .name = "yugabyte"
  };
  ASSERT_OK(cluster_admin_client_->CreateNamespaceSnapshot(
      database, 0 /* retention_duration_hours */));
  auto resp = ASSERT_RESULT(cluster_admin_client_->ListSnapshots(EnumBitSet<ListSnapshotsFlag>()));
  EXPECT_EQ(resp.snapshots_size(), 1);
  EXPECT_EQ(resp.snapshots(0).entry().entries_size(), 0);
}

// Test the list snapshot parameter `include_ddl_in_progress_tables` which allows exporting a
// snapshot even if one of its tables is undergoing DDL verification.
TEST_F(ClusterAdminClientTest, ExportSnapshotWithDdlInProgressTables) {
  const std::string db_name = "yugabyte";
  const std::string table_name = "test_table";
  CreateTable(Format("CREATE TABLE $0 (k INT, v TEXT)", table_name));
  // Start a long-running table-rewrite DDL.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));
  auto conn = ASSERT_RESULT(PgConnect(db_name));
  ASSERT_OK(conn.TestFailDdl(Format("ALTER TABLE $0 ADD PRIMARY KEY (k)", table_name)));
  // Verify that the uncommitted DocDB table still exists.
  master::ListTablesResponsePB docdb_tables =
      ASSERT_RESULT(client_->ListTables(table_name, /* ysql_db_fiter */ db_name));
  ASSERT_EQ(docdb_tables.tables_size(), 2);

  // Create a snapshot.
  const TypedNamespaceName database{.db_type = YQL_DATABASE_PGSQL, .name = db_name};
  TxnSnapshotId snapshot_id = ASSERT_RESULT(CreateAndWaitForSnapshotToComplete(database));
  // Exporting a snapshot means listing the snapshot with prepare_for_backup = true.
  // ListSnapshot should fail as one table is still undergoing DDL verification.
  EnumBitSet<ListSnapshotsFlag> flags{ListSnapshotsFlag::SHOW_DETAILS};
  auto s = cluster_admin_client_->ListSnapshots(
      flags, snapshot_id, /*prepare_for_backup*/ true,
      /*include_ddl_in_progress_tables*/ false);
  ASSERT_NOK_STR_CONTAINS(s, "undergoing DDL verification");
  // Should be able to export the snapshot when include_ddl_in_progress_tables = true.
  auto resp = ASSERT_RESULT(cluster_admin_client_->ListSnapshots(
      flags, snapshot_id, /*prepare_for_backup*/ true,
      /*include_ddl_in_progress_tables*/ true));
  ASSERT_EQ(resp.snapshots_size(), 1);
  // Expect to have 2 backup_entries: 1 for namespace and 1 for the old committed DocDB table.
  ASSERT_EQ(resp.snapshots(0).backup_entries_size(), 2);
  ASSERT_EQ(resp.snapshots(0).backup_entries(1).entry().id(), docdb_tables.tables(0).id());
  // Re-enable DDL rolback so that the test exists gracefully.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "false"));
}

// Test that the option include_ddl_in_progress_tables bumps the format_version of the
// SnapshotInfoPB to kUseRelfilenodeFormatVersion when set to true and
// kUseBackupRowEntryFormatVersion otherwise.
TEST_F(ClusterAdminClientTest, ExportSnapshotHasCorrectFormatVersion) {
  const std::string db_name = "yugabyte";
  const std::string table_name = "test_table";
  CreateTable(Format("CREATE TABLE $0 (k INT, v TEXT)", table_name));
  // Create a snapshot.
  const TypedNamespaceName database{.db_type = YQL_DATABASE_PGSQL, .name = db_name};
  TxnSnapshotId snapshot_id = ASSERT_RESULT(CreateAndWaitForSnapshotToComplete(database));
  // Check that format_version=2 when include_ddl_in_progress_tables = false.
  EnumBitSet<ListSnapshotsFlag> flags{ListSnapshotsFlag::SHOW_DETAILS};
  auto resp = ASSERT_RESULT(
      cluster_admin_client_->ListSnapshots(flags, snapshot_id, /*prepare_for_backup*/ true));
  ASSERT_EQ(resp.snapshots_size(), 1);
  ASSERT_EQ(resp.snapshots(0).format_version(), master::kUseBackupRowEntryFormatVersion);
  // Check that format_version=3 when include_ddl_in_progress_tables = true.
  resp = ASSERT_RESULT(cluster_admin_client_->ListSnapshots(
      flags, snapshot_id, /*prepare_for_backup*/ true, /*include_ddl_in_progress_tables*/ true));
  ASSERT_EQ(resp.snapshots_size(), 1);
  ASSERT_EQ(resp.snapshots(0).format_version(), master::kUseRelfilenodeFormatVersion);
}

// Test that export_snapshot includes only the committed DocDB tables as of snapshot_hybrid_time.
// This behavior is introduced to support backups during DDLs.
TEST_F(ClusterAdminClientTest, ExportCommittedDocDbTablesAsOfSnapshotHt) {
  const std::string db_name = "yugabyte";
  const std::string table_name = "test_table";
  auto conn = ASSERT_RESULT(PgConnect(db_name));
  CreateTable(Format("CREATE TABLE $0 (k INT, v TEXT)", table_name));
  // Keep the old DocDB table in the next table rewrite to check that export_snapshot doesn't skip
  // including it.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_yb_test_table_rewrite_keep_old_table", "true"));
  ASSERT_OK(conn.ExecuteFormat("SET yb_test_table_rewrite_keep_old_table TO TRUE"));
  ASSERT_OK(
      conn.ExecuteFormat("ALTER TABLE $0 ALTER COLUMN v TYPE INT USING v::INTEGER", table_name));
  // Verify that the uncommitted DocDB table still exists.
  master::ListTablesResponsePB base_docdb_tables =
      ASSERT_RESULT(client_->ListTables(table_name, /* ysql_db_fiter */ db_name));
  ASSERT_EQ(base_docdb_tables.tables_size(), 2);

  // Create a snapshot.
  const TypedNamespaceName database{.db_type = YQL_DATABASE_PGSQL, .name = db_name};
  TxnSnapshotId snapshot_id = ASSERT_RESULT(CreateAndWaitForSnapshotToComplete(database));

  // Drop the table after completing the snapshot. Export_snapshot should still be able to
  // export the committed DocDB table as of snapshot_hybid_time.
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  // Exporting a snapshot means listing the snapshot with prepare_for_backup = true.
  EnumBitSet<ListSnapshotsFlag> flags{ListSnapshotsFlag::SHOW_DETAILS};
  auto resp = ASSERT_RESULT(cluster_admin_client_->ListSnapshots(
      flags, snapshot_id, /*prepare_for_backup*/ true,
      /*include_ddl_in_progress_tables*/ true));
  ASSERT_EQ(resp.snapshots_size(), 1);
  // Expect to have 2 backup_entries: 1 for namespace and 1 for the new committed DocDB table.
  ASSERT_EQ(resp.snapshots(0).backup_entries_size(), 2);
  ASSERT_EQ(resp.snapshots(0).backup_entries(1).entry().id(), base_docdb_tables.tables(1).id());
}

TEST_F(
    ClusterAdminClientTest,
    CreateSnapshotAfterMultiNamespaceCreateIssuedWithSameName) {
  std::string db_name = "test_pgsql";
  TestThreadHolder threads;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_on_namespace_transition) = true;

  auto create_database = [this, &db_name] {
    auto conn = ASSERT_RESULT(PgConnect());
    auto res = conn.ExecuteFormat("CREATE DATABASE $0", db_name);
    if (!res.ok()) {
      ASSERT_THAT(
          res.ToString(),
          ::testing::ContainsRegex(Format("('$0'|\"$0\") already exists", db_name)));
    }
  };

  threads.AddThreadFunctor(create_database);
  threads.AddThreadFunctor(create_database);
  threads.Stop();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_on_namespace_transition) = false;

  ASSERT_OK(WaitFor([this, &db_name]() -> Result<bool> {
    bool create_in_progress = true;
    RETURN_NOT_OK(client_->IsCreateNamespaceInProgress(
        db_name, YQL_DATABASE_PGSQL, "" /* namespace_id */, &create_in_progress));

    return !create_in_progress;
  }, 15s, "Wait for create namespace to finish async setup tasks."));

  auto conn = ASSERT_RESULT(PgConnect(db_name));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  const TypedNamespaceName database {
    .db_type = YQL_DATABASE_PGSQL,
    .name = db_name
  };
  ASSERT_OK(cluster_admin_client_->CreateNamespaceSnapshot(
      database, 0 /* retention_duration_hours */));
}

}  // namespace tools
}  // namespace yb
