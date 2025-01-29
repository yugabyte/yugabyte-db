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

#include "yb/common/snapshot.h"
#include "yb/gutil/strings/split.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/tools/yb-admin-test-base.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/flags.h"

#include "yb/client/client.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"

#include "yb/integration-tests/cql_test_util.h"

#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/rpc/secure_stream.h"

#include "yb/tools/admin-test-base.h"
#include "yb/tools/test_admin_client.h"
#include "yb/tools/yb-admin_util.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/client/table_alterer.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/date_time.h"
#include "yb/util/env_util.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(check_bootstrap_required);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);
DECLARE_bool(TEST_create_table_with_empty_namespace_name);
DECLARE_bool(TEST_simulate_long_restore);

namespace yb {
namespace tools {

using namespace std::literals;

using std::shared_ptr;
using std::string;

using client::Transactional;
using client::YBTable;
using client::YBTableInfo;
using client::YBTableName;
using master::ListSnapshotRestorationsRequestPB;
using master::ListSnapshotRestorationsResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::MasterBackupProxy;
using master::SysSnapshotEntryPB;
using rpc::RpcController;

const std::string kRestoredState = "RESTORED";
const std::string kFailedState = "FAILED";

class AdminCliTest : public AdminCliTestBase {
 protected:
  Result<MasterBackupProxy*> BackupServiceProxy() {
    if (!backup_service_proxy_) {
      backup_service_proxy_.reset(new MasterBackupProxy(
          &client_->proxy_cache(), VERIFY_RESULT(cluster_->GetLeaderMasterBoundRpcAddr())));
    }
    return backup_service_proxy_.get();
  }

  Status WaitForRestorationState(const std::string& state) {
    return WaitFor(
        [this, &state]() -> Result<bool> {
          const auto document =
              VERIFY_RESULT(RunAdminToolCommandJson("list_snapshot_restorations"));
          auto it = document.FindMember("restorations");
          if (it == document.MemberEnd()) {
            LOG(INFO) << "No restorations";
            return false;
          }
          auto value = it->value.GetArray();
          for (const auto& restoration : value) {
            auto state_it = restoration.FindMember("state");
            if (state_it == restoration.MemberEnd()) {
              return STATUS(NotFound, "'state' not found");
            }
            if (state_it->value.GetString() != state) {
              return false;
            }
          }
          return true;
        },
        30s, Format("Waiting for restoration state $0", state));
  }

  Status WaitForRestoreSnapshot() { return WaitForRestorationState(kRestoredState); }

  Result<master::ListSnapshotsResponsePB> WaitForAllSnapshots(
      master::MasterBackupProxy* const alternate_proxy = nullptr) {
    auto proxy = alternate_proxy;
    if (!proxy) {
      proxy = VERIFY_RESULT(BackupServiceProxy());
    }
    return tools::WaitForAllSnapshots(proxy);
  }

  Result<std::string> GetCompletedSnapshot(int num_snapshots = 1, int idx = 0) {
    auto* proxy = VERIFY_RESULT(BackupServiceProxy());
    return tools::GetCompletedSnapshot(proxy, num_snapshots, idx);
  }

  Result<SysSnapshotEntryPB::State> WaitForRestoration() {
    auto* proxy = VERIFY_RESULT(BackupServiceProxy());

    ListSnapshotRestorationsRequestPB req;
    ListSnapshotRestorationsResponsePB resp;
    RETURN_NOT_OK(WaitFor(
        [proxy, &req, &resp]() -> Result<bool> {
          RpcController rpc;
          RETURN_NOT_OK(proxy->ListSnapshotRestorations(req, &resp, &rpc));
          for (auto const& restoration : resp.restorations()) {
            if (restoration.entry().state() == SysSnapshotEntryPB::RESTORING) {
              return false;
            }
          }
          return true;
        },
        30s, "Waiting for all restorations to complete"));

    SCHECK_EQ(resp.restorations_size(), 1, IllegalState, "Expected only one restoration");
    return resp.restorations(0).entry().state();
  }

  Result<size_t> NumTables(const string& table_name) const;
  void ImportTableAs(const string& snapshot_file, const string& keyspace, const string& table_name);
  void ImportSelectiveTables(const string& snapshot_file, const string& keyspace,
      const YBTable *th1, const YBTable *th2);
  void CheckImportedTable(
      const YBTable* src_table, const YBTableName& yb_table_name, bool same_ids = false);
  void CheckAndDeleteImportedTable(
      const string& keyspace, const string& table_name, bool same_ids = false,
      const YBTable* src_table = nullptr);
  void CheckImportedTableWithIndex(
      const string& keyspace, const string& table_name, const string& index_name,
      bool same_ids = false);

  void DoTestImportSnapshot(const string& format = "");
  void DoTestExportImportIndexSnapshot(Transactional transactional);

 private:
  std::unique_ptr<MasterBackupProxy> backup_service_proxy_;
};

TEST_F(AdminCliTest, TestCreateSnapshot) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // There is custom table.
  const auto tables = ASSERT_RESULT(client_->ListTables(table_name, /* exclude_ysql */ true));
  ASSERT_EQ(1, tables.size());

  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RpcController rpc;
  ASSERT_OK(ASSERT_RESULT(BackupServiceProxy())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 0);

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));

  rpc.Reset();
  ASSERT_OK(ASSERT_RESULT(BackupServiceProxy())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 1);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestCreateSnapshotWithTtl) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  auto id1 = ASSERT_RESULT(RunAdminToolCommand(
      "create_snapshot", keyspace, table_name, 60 /* flush_timeout_secs */,
      3 /* retention_duration_hours */));
  auto id2 = ASSERT_RESULT(RunAdminToolCommand(
      "create_keyspace_snapshot", keyspace, 5 /* retention_duration_hours */));
  auto id3 = ASSERT_RESULT(RunAdminToolCommand(
      "create_keyspace_snapshot", keyspace, 0 /* retention_duration_hours */));
  auto id4 = ASSERT_RESULT(RunAdminToolCommand("create_keyspace_snapshot", keyspace));
  std::unordered_map<std::string, uint32_t> id_to_expected_retention;
  std::vector<string> split = strings::Split(id1, ": ");
  id_to_expected_retention[split[1].substr(0, split[1].size() - 1)] = 3;
  split = strings::Split(id2, ": ");
  id_to_expected_retention[split[1].substr(0, split[1].size() - 1)] = 5;
  split = strings::Split(id3, ": ");
  id_to_expected_retention[split[1].substr(0, split[1].size() - 1)] = -1;
  split = strings::Split(id4, ": ");
  id_to_expected_retention[split[1].substr(0, split[1].size() - 1)] = 24;
  LOG(INFO) << "Snashot Ids with expected retention: " << AsString(id_to_expected_retention);

  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RpcController rpc;
  ASSERT_OK(ASSERT_RESULT(BackupServiceProxy())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 4);
  for (const auto& entry : resp.snapshots()) {
    TxnSnapshotId id = TryFullyDecodeTxnSnapshotId(entry.id());
    ASSERT_TRUE(id_to_expected_retention.contains(id.ToString()));
    ASSERT_EQ(entry.entry().retention_duration_hours(), id_to_expected_retention[id.ToString()]);
  }

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

Result<size_t> AdminCliTest::NumTables(const string& table_name) const {
  auto tables =
      VERIFY_RESULT(client_->ListTables(/* filter */ table_name, /* exclude_ysql */ true));
  return tables.size();
}

void AdminCliTest::CheckImportedTable(
    const YBTable* src_table, const YBTableName& yb_table_name, bool same_ids) {
  shared_ptr<YBTable> table;
  ASSERT_OK(client_->OpenTable(yb_table_name, &table));

  ASSERT_EQ(same_ids, table->id() == src_table->id());
  ASSERT_EQ(table->table_type(), src_table->table_type());
  ASSERT_EQ(table->GetPartitionsCopy(), src_table->GetPartitionsCopy());
  ASSERT_TRUE(table->partition_schema().Equals(src_table->partition_schema()));
  ASSERT_TRUE(table->schema().Equals(src_table->schema()));
  ASSERT_EQ(
      table->schema().table_properties().is_transactional(),
      src_table->schema().table_properties().is_transactional());
}

void AdminCliTest::CheckAndDeleteImportedTable(
    const string& keyspace, const string& table_name, bool same_ids, const YBTable* src_table) {
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());

  const YBTableName yb_table_name(YQL_DATABASE_CQL, keyspace, table_name);
  if (src_table == nullptr) {
    CheckImportedTable(table_.get(), yb_table_name, same_ids);
  } else {
    CheckImportedTable(src_table, yb_table_name, same_ids);
  }
  ASSERT_EQ(1, ASSERT_RESULT(NumTables(table_name)));
  ASSERT_OK(client_->DeleteTable(yb_table_name, /* wait */ true));
  ASSERT_EQ(0, ASSERT_RESULT(NumTables(table_name)));
}

void AdminCliTest::ImportTableAs(
    const string& snapshot_file, const string& keyspace, const string& table_name) {
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file, keyspace, table_name));
  CheckAndDeleteImportedTable(keyspace, table_name);
}

void AdminCliTest::ImportSelectiveTables(const string& snapshot_file, const string& keyspace,
      const YBTable *th1, const YBTable *th2) {
  ASSERT_OK(RunAdminToolCommand("import_snapshot_selective", snapshot_file, keyspace,
        th1->name().table_name(), th2->name().table_name()));
  CheckAndDeleteImportedTable(keyspace, th1->name().table_name(), false, th1);
  CheckAndDeleteImportedTable(keyspace, th2->name().table_name(), false, th2);
}

void AdminCliTest::DoTestImportSnapshot(const string& format) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");

  if (format.empty()) {
    ASSERT_OK(RunAdminToolCommand("export_snapshot", snapshot_id, snapshot_file));
  } else {
    ASSERT_OK(RunAdminToolCommand(
        "export_snapshot", snapshot_id, snapshot_file,
        "-TEST_metadata_file_format_version=" + format));
  }

  // Import snapshot into the existing table.
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file));
  CheckAndDeleteImportedTable(keyspace, table_name, /* same_ids */ true);

  // Import snapshot into original table from the snapshot.
  // (The table was deleted by the call above.)
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file));
  CheckAndDeleteImportedTable(keyspace, table_name);

  // Import snapshot into non existing namespace.
  ImportTableAs(snapshot_file, keyspace + "_new", table_name);
  // Import snapshot into already existing namespace.
  ImportTableAs(snapshot_file, keyspace, table_name + "_new");
  // Import snapshot into already existing namespace and table.
  ImportTableAs(snapshot_file, keyspace, table_name);
}

TEST_F(AdminCliTest, TestImportSnapshot) {
  DoTestImportSnapshot();
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestImportSnapshotInOldFormat1) {
  DoTestImportSnapshot("1");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestImportSnapshotInOldFormatNoNamespaceName) {
  DoTestImportSnapshot("-1");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestImportSnapshotSelective) {
  const YBTableName t1(YQL_DATABASE_CQL, "my_keyspace", "selective_test_table_1");
  client::TableHandle t1h;
  client::kv_table_test::CreateTable(Transactional::kTrue, NumTablets(), client_.get(), &t1h, t1);

  YBTableName t2(YQL_DATABASE_CQL, "my_keyspace", "selective_test_table_2");
  client::TableHandle t2h;
  client::kv_table_test::CreateTable(Transactional::kFalse, NumTablets(), client_.get(), &t2h, t2);

  YBTableName t3(YQL_DATABASE_CQL, "my_keyspace", "selective_test_table_3");
  client::TableHandle t3h;
  client::kv_table_test::CreateTable(Transactional::kFalse, NumTablets(), client_.get(), &t3h, t3);

  const string& keyspace = t1h.name().namespace_name();

  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, t1h.name().table_name(),
        keyspace, t2h.name().table_name(), keyspace, t3h.name().table_name()));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");

  ASSERT_OK(RunAdminToolCommand("export_snapshot", snapshot_id, snapshot_file));

  // Import snapshot into the existing tables.
  ASSERT_OK(RunAdminToolCommand("import_snapshot_selective", snapshot_file));
  CheckAndDeleteImportedTable(keyspace, t1h.name().table_name(), /* same_ids */ true, t1h.get());
  CheckAndDeleteImportedTable(keyspace, t2h.name().table_name(), /* same_ids */ true, t2h.get());
  CheckAndDeleteImportedTable(keyspace, t3h.name().table_name(), /* same_ids */ true, t3h.get());

  // Import snapshot into original tables from the snapshot.
  // (The tables was deleted by the calls above.)
  ASSERT_OK(RunAdminToolCommand("import_snapshot_selective", snapshot_file));
  CheckAndDeleteImportedTable(keyspace, t1h.name().table_name(), false, t1h.get());
  CheckAndDeleteImportedTable(keyspace, t2h.name().table_name(), false, t2h.get());
  CheckAndDeleteImportedTable(keyspace, t3h.name().table_name(), false, t3h.get());

  // Import snapshot into non existing namespace.
  ImportSelectiveTables(snapshot_file, keyspace + "_new", t1h.get(), t2h.get());
  YBTableName t3new(YQL_DATABASE_CQL, keyspace + "_new", t3h.name().table_name());
  shared_ptr<YBTable> t3h_new;
  ASSERT_NOK(client_->OpenTable(t3new, &t3h_new));
  // Import snapshot into already existing namespace and tables.
  ImportSelectiveTables(snapshot_file, keyspace, t1h.get(), t2h.get());
  ASSERT_NOK(client_->OpenTable(t3, &t3h_new));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestExportImportSnapshot) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand("export_snapshot", snapshot_id, snapshot_file));
  // Import below will not create a new table - reusing the old one.
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file, keyspace, table_name));

  const YBTableName yb_table_name(YQL_DATABASE_CQL, keyspace, table_name);
  CheckImportedTable(table_.get(), yb_table_name, /* same_ids */ true);
  ASSERT_EQ(1, ASSERT_RESULT(NumTables(table_name)));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestRestoreSnapshotBasic) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  ASSERT_OK(WriteRow(CreateSession(), 1, 1));

  // Create snapshot of default table that gets created.
  LOG(INFO) << "Creating snapshot";
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());
  ASSERT_RESULT(WaitForAllSnapshots());

  ASSERT_OK(DeleteRow(CreateSession(), 1));
  ASSERT_NOK(SelectRow(CreateSession(), 1));

  // Restore snapshot into the existing table.
  LOG(INFO) << "Restoring snapshot";
  ASSERT_OK(RunAdminToolCommand("restore_snapshot", snapshot_id));
  ASSERT_OK(WaitForRestoreSnapshot());
  LOG(INFO) << "Restored snapshot";

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return SelectRow(CreateSession(), 1).ok(); }, 20s,
      "Waiting for row from restored snapshot."));
}

TEST_F(AdminCliTest, TestAbortSnapshotRestoreBasic) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_long_restore) = true;

  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  ASSERT_OK(WriteRow(CreateSession(), 1, 1));

  // Create snapshot.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id =
      ASSERT_RESULT(TxnSnapshotIdFromString(ASSERT_RESULT(GetCompletedSnapshot())));
  ASSERT_RESULT(WaitForAllSnapshots());

  // Restore snapshot using proxy to get restoration ID.
  auto proxy = ASSERT_RESULT(BackupServiceProxy());
  RpcController rpc;
  master::RestoreSnapshotRequestPB req;
  master::RestoreSnapshotResponsePB resp;
  req.set_snapshot_id(snapshot_id.AsSlice().ToBuffer());
  ASSERT_OK(proxy->RestoreSnapshot(req, &resp, &rpc));

  auto restoration_id = ASSERT_RESULT(FullyDecodeTxnSnapshotRestorationId(resp.restoration_id()));

  // Abort snapshot restore.
  ASSERT_OK(RunAdminToolCommand("abort_snapshot_restore", restoration_id));
  ASSERT_OK(WaitForRestorationState(kFailedState));
}

TEST_F(AdminCliTest, TestRestoreSnapshotHybridTime) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  ASSERT_OK(WriteRow(CreateSession(), 1, 1));
  auto hybrid_time = cluster_->mini_tablet_server(0)->server()->Clock()->Now();
  ASSERT_OK(WriteRow(CreateSession(), 2, 2));

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());
  ASSERT_RESULT(WaitForAllSnapshots());

  // Restore snapshot into the existing table.
  ASSERT_OK(RunAdminToolCommand(
      "restore_snapshot", snapshot_id, std::to_string(hybrid_time.GetPhysicalValueMicros())));
  ASSERT_OK(WaitForRestoreSnapshot());

  // Row before HybridTime present, row after should be missing now.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return SelectRow(CreateSession(), 1).ok() && !SelectRow(CreateSession(), 2).ok();
      },
      20s, "Waiting for row from restored snapshot."));
}

TEST_F(AdminCliTest, TestRestoreSnapshotTimestamp) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  ASSERT_OK(WriteRow(CreateSession(), 1, 1));
  auto timestamp = DateTime::TimestampToString(DateTime::TimestampNow());
  LOG(INFO) << "Timestamp: " << timestamp;
  auto write_wait = 2s;
  std::this_thread::sleep_for(write_wait);
  ASSERT_OK(WriteRow(CreateSession(), 2, 2));

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());
  ASSERT_RESULT(WaitForAllSnapshots());

  // Restore snapshot into the existing table.
  ASSERT_OK(RunAdminToolCommand("restore_snapshot", snapshot_id, timestamp));
  ASSERT_OK(WaitForRestoreSnapshot());

  // Row before Timestamp present, row after should be missing now.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return SelectRow(CreateSession(), 1).ok() && !SelectRow(CreateSession(), 2).ok();
      },
      20s, "Waiting for row from restored snapshot."));
}

TEST_F(AdminCliTest, TestRestoreSnapshotInterval) {
  CreateTable(Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  auto clock = cluster_->mini_tablet_server(0)->server()->Clock();
  ASSERT_OK(WriteRow(CreateSession(), 1, 1));
  auto pre_sleep_ht = clock->Now();
  auto write_wait = 5s;
  std::this_thread::sleep_for(write_wait);
  ASSERT_OK(WriteRow(CreateSession(), 2, 2));

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());
  ASSERT_RESULT(WaitForAllSnapshots());

  // Restore snapshot into the existing table.
  auto restore_ht = clock->Now();
  auto interval = restore_ht.GetPhysicalValueMicros() - pre_sleep_ht.GetPhysicalValueMicros();
  auto i_str = std::to_string(interval / 1000000) + "s";
  ASSERT_OK(RunAdminToolCommand("restore_snapshot", snapshot_id, "minus", i_str));
  ASSERT_OK(WaitForRestoreSnapshot());

  ASSERT_OK(SelectRow(CreateSession(), 1));
  auto select2 = SelectRow(CreateSession(), 2);
  ASSERT_NOK(select2);
}

void AdminCliTest::CheckImportedTableWithIndex(
    const string& keyspace, const string& table_name, const string& index_name, bool same_ids) {
  const YBTableName yb_table_name(YQL_DATABASE_CQL, keyspace, table_name);
  const YBTableName yb_index_name(YQL_DATABASE_CQL, keyspace, index_name);

  CheckImportedTable(table_.get(), yb_table_name, same_ids);
  ASSERT_EQ(2, ASSERT_RESULT(NumTables(table_name)));
  CheckImportedTable(index_.get(), yb_index_name, same_ids);
  ASSERT_EQ(1, ASSERT_RESULT(NumTables(index_name)));

  YBTableInfo table_info = ASSERT_RESULT(client_->GetYBTableInfo(yb_table_name));
  YBTableInfo index_info = ASSERT_RESULT(client_->GetYBTableInfo(yb_index_name));
  // Check index ---> table relation.
  ASSERT_EQ(index_info.index_info->indexed_table_id(), table_info.table_id);
  // Check table ---> index relation.
  ASSERT_EQ(table_info.index_map.size(), 1);
  ASSERT_EQ(table_info.index_map.count(index_info.table_id), 1);
  ASSERT_EQ(table_info.index_map.begin()->first, index_info.table_id);
  ASSERT_EQ(table_info.index_map.begin()->second.table_id(), index_info.table_id);
  ASSERT_EQ(table_info.index_map.begin()->second.indexed_table_id(), table_info.table_id);

  ASSERT_OK(client_->DeleteTable(yb_table_name, /* wait */ true));
  ASSERT_EQ(0, ASSERT_RESULT(NumTables(table_name)));
}

void AdminCliTest::DoTestExportImportIndexSnapshot(Transactional transactional) {
  CreateTable(transactional);
  CreateIndex(transactional);

  // Default tables that were created.
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();
  const string& index_name = index_.name().table_name();
  const YBTableName yb_table_name(YQL_DATABASE_CQL, keyspace, table_name);
  const YBTableName yb_index_name(YQL_DATABASE_CQL, keyspace, index_name);

  // Check there are 2 tables.
  ASSERT_EQ(2, ASSERT_RESULT(NumTables(table_name)));

  // Create snapshot of default table and the attached index that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand("export_snapshot", snapshot_id, snapshot_file));

  // Create snapshot of default table but skip the attached index that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name, "skip_indexes"));
  auto skip_index_snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(/* num_snapshots= */ 2));
  if (skip_index_snapshot_id == snapshot_id) {
    skip_index_snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(
        /* num_snapshots= */ 2, /* idx= */ 1));
  }

  const auto skip_index_snapshot_file =
      JoinPathSegments(tmp_dir, "exported_skip_index_snapshot.dat");
  ASSERT_OK(
      RunAdminToolCommand("export_snapshot", skip_index_snapshot_id, skip_index_snapshot_file));

  // Import table and index into the existing table and index.
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex(keyspace, table_name, index_name, /* same_ids */ true);

  // Import table and index with original names - not providing any names.
  // (The table was deleted by the call above.)
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex(keyspace, table_name, index_name);

  // Import table and index with original names - using the old names.
  ASSERT_OK(
      RunAdminToolCommand("import_snapshot", snapshot_file, keyspace, table_name, index_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex(keyspace, table_name, index_name);

  // Import table and index with original names - providing only old table name.
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file, keyspace, table_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex(keyspace, table_name, index_name);

  // Renaming table and index, but keeping the same keyspace.
  ASSERT_OK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, keyspace, "new_" + table_name, "new_" + index_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex(keyspace, "new_" + table_name, "new_" + index_name);

  // Keeping the same table and index names, but renaming the keyspace.
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file, "new_" + keyspace));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex("new_" + keyspace, table_name, index_name);

  // Repeat previous keyspace renaming case, but pass explicitly the same table name
  // (and skip index name).
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file, "new_" + keyspace, table_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex("new_" + keyspace, table_name, index_name);

  // Import table and index into a new keyspace with old table and index names.
  ASSERT_OK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, "new_" + keyspace, table_name, index_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex("new_" + keyspace, table_name, index_name);

  // Rename only index and keyspace, but keep the main table name.
  ASSERT_OK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, "new_" + keyspace, table_name, "new_" + index_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex("new_" + keyspace, table_name, "new_" + index_name);

  // Import table and index with renaming into a new keyspace.
  ASSERT_OK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, "new_" + keyspace, "new_" + table_name,
      "new_" + index_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex("new_" + keyspace, "new_" + table_name, "new_" + index_name);

  // Renaming table only, no new name for the index - expecting error.
  ASSERT_NOK(RunAdminToolCommand("import_snapshot", snapshot_file, keyspace, "new_" + table_name));
  ASSERT_NOK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, "new_" + keyspace, "new_" + table_name));

  // Import skip_index_snapshot_file should not import index.
  ASSERT_OK(RunAdminToolCommand(
      "import_snapshot", skip_index_snapshot_file));  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  // Verify that index is not imported and the table is imported correctly.
  ASSERT_EQ(1, ASSERT_RESULT(NumTables(table_name)));
  ASSERT_EQ(0, ASSERT_RESULT(NumTables(index_name)));
  CheckAndDeleteImportedTable(keyspace, table_name);
}

TEST_F(AdminCliTest, TestExportImportIndexSnapshot) {
  // Test non-transactional table.
  DoTestExportImportIndexSnapshot(Transactional::kFalse);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestExportImportIndexSnapshot_ForTransactional) {
  // Test the recreated transactional table.
  DoTestExportImportIndexSnapshot(Transactional::kTrue);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestFailedRestoration) {
  CreateTable(Transactional::kTrue);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot());
  LOG(INFO) << "Created snapshot: " << snapshot_id;

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand("export_snapshot", snapshot_id, snapshot_file));
  // Import below will not create a new table - reusing the old one.
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file));

  const YBTableName yb_table_name(YQL_DATABASE_CQL, keyspace, table_name);
  CheckImportedTable(table_.get(), yb_table_name, /* same_ids */ true);
  ASSERT_EQ(1, ASSERT_RESULT(NumTables(table_name)));

  auto new_snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(2));
  if (new_snapshot_id == snapshot_id) {
    new_snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(2, 1));
  }
  LOG(INFO) << "Imported snapshot: " << new_snapshot_id;

  ASSERT_OK(RunAdminToolCommand("restore_snapshot", new_snapshot_id));

  const SysSnapshotEntryPB::State state = ASSERT_RESULT(WaitForRestoration());
  LOG(INFO) << "Restoration: " << SysSnapshotEntryPB::State_Name(state);
  ASSERT_EQ(state, SysSnapshotEntryPB::FAILED);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(AdminCliTest, TestSetPreferredZone) {
  const std::string c1z1 = "c1.r1.z1";
  const std::string c1z2 = "c1.r1.z2";
  const std::string c2z1 = "c2.r1.z1";
  const std::string c1z1_json =
      "{\"placementCloud\":\"c1\",\"placementRegion\":\"r1\",\"placementZone\":\"z1\"}";
  const std::string c1z2_json =
      "{\"placementCloud\":\"c1\",\"placementRegion\":\"r1\",\"placementZone\":\"z2\"}";
  const std::string c2z1_json =
      "{\"placementCloud\":\"c2\",\"placementRegion\":\"r1\",\"placementZone\":\"z1\"}";
  const std::string affinitized_leaders_json_Start = "\"affinitizedLeaders\"";
  const std::string multi_affinitized_leaders_json_start =
      "\"multiAffinitizedLeaders\":[{\"zones\":[";
  const std::string json_end = "]}]";

  ASSERT_OK(RunAdminToolCommand(
      "modify_placement_info", strings::Substitute("$0,$1,$2", c1z1, c1z2, c2z1), 5, ""));

  ASSERT_NOK(RunAdminToolCommand("set_preferred_zones", ""));
  auto output = ASSERT_RESULT(RunAdminToolCommand("get_universe_config"));
  ASSERT_EQ(output.find(affinitized_leaders_json_Start), string::npos);
  ASSERT_EQ(output.find(multi_affinitized_leaders_json_start), string::npos);

  ASSERT_OK(RunAdminToolCommand("set_preferred_zones", c1z1));
  output = ASSERT_RESULT(RunAdminToolCommand("get_universe_config"));
  ASSERT_EQ(output.find(affinitized_leaders_json_Start), string::npos);
  ASSERT_NE(output.find(multi_affinitized_leaders_json_start + c1z1_json + json_end), string::npos);

  ASSERT_OK(RunAdminToolCommand("set_preferred_zones", c1z1, c1z2, c2z1));
  output = ASSERT_RESULT(RunAdminToolCommand("get_universe_config"));
  ASSERT_EQ(output.find(affinitized_leaders_json_Start), string::npos);
  ASSERT_NE(
      output.find(
          multi_affinitized_leaders_json_start + c1z1_json + "," + c1z2_json + "," + c2z1_json +
          json_end),
      string::npos);

  ASSERT_OK(RunAdminToolCommand("set_preferred_zones", strings::Substitute("$0:1", c1z1)));
  output = ASSERT_RESULT(RunAdminToolCommand("get_universe_config"));
  ASSERT_EQ(output.find(affinitized_leaders_json_Start), string::npos);
  ASSERT_NE(output.find(multi_affinitized_leaders_json_start + c1z1_json + json_end), string::npos);

  ASSERT_OK(RunAdminToolCommand("set_preferred_zones", strings::Substitute("$0:1", c1z1), c1z2));
  output = ASSERT_RESULT(RunAdminToolCommand("get_universe_config"));
  ASSERT_EQ(output.find(affinitized_leaders_json_Start), string::npos);
  ASSERT_NE(
      output.find(multi_affinitized_leaders_json_start + c1z1_json + "," + c1z2_json + json_end),
      string::npos);

  ASSERT_OK(RunAdminToolCommand(
      "set_preferred_zones", strings::Substitute("$0:1", c1z1), strings::Substitute("$0:2", c1z2),
      strings::Substitute("$0:3", c2z1)));
  output = ASSERT_RESULT(RunAdminToolCommand("get_universe_config"));
  ASSERT_EQ(output.find(affinitized_leaders_json_Start), string::npos);
  ASSERT_NE(
      output.find(
          multi_affinitized_leaders_json_start + c1z1_json + "]},{\"zones\":[" + c1z2_json +
          "]},{\"zones\":[" + c2z1_json + json_end),
      string::npos);

  ASSERT_OK(RunAdminToolCommand(
      "set_preferred_zones", strings::Substitute("$0:1", c1z1), strings::Substitute("$0:1", c1z2),
      strings::Substitute("$0:2", c2z1)));
  output = ASSERT_RESULT(RunAdminToolCommand("get_universe_config"));
  ASSERT_EQ(output.find(affinitized_leaders_json_Start), string::npos);
  ASSERT_NE(
      output.find(
          multi_affinitized_leaders_json_start + c1z1_json + "," + c1z2_json + "]},{\"zones\":[" +
          c2z1_json + json_end),
      string::npos);

  ASSERT_NOK(RunAdminToolCommand("set_preferred_zones", strings::Substitute("$0:", c1z1)));
  ASSERT_NOK(RunAdminToolCommand("set_preferred_zones", strings::Substitute("$0:0", c1z1)));
  ASSERT_NOK(RunAdminToolCommand("set_preferred_zones", strings::Substitute("$0:-13", c1z1)));
  ASSERT_NOK(RunAdminToolCommand("set_preferred_zones", strings::Substitute("$0:2", c1z1)));
  ASSERT_NOK(RunAdminToolCommand(
      "set_preferred_zones", strings::Substitute("$0:1", c1z1), strings::Substitute("$0:3", c1z2)));
  ASSERT_NOK(RunAdminToolCommand(
      "set_preferred_zones", strings::Substitute("$0:2", c1z1), strings::Substitute("$0:2", c1z2),
      strings::Substitute("$0:3", c2z1)));
}

class SnapshotAdminCliTest : public AdminCliTest {
 public:
  void SetUp() override {
    AdminCliTest::SetUp();
    snapshot_util_ = std::make_unique<client::SnapshotTestUtil>();
    snapshot_util_->SetProxy(&client_->proxy_cache());
    snapshot_util_->SetCluster(cluster_.get());
  }

  Result<master::SysTablesEntryPB> GetTableEntryFromSnapshot(
      const client::Snapshots& snapshots, const TxnSnapshotId& snapshot_id) {
    for (const auto& snapshot : snapshots) {
      if (VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id())) == snapshot_id) {
        for (const auto& entry : snapshot.entry().entries()) {
          if (entry.type() == master::SysRowEntryType::TABLE) {
            return pb_util::ParseFromSlice<master::SysTablesEntryPB>(entry.data());
          }
        }
      }
    }
    return STATUS(NotFound, "Could not find TABLE entry");
  }

 protected:
  std::unique_ptr<client::SnapshotTestUtil> snapshot_util_;
};

TEST_F_EX(AdminCliTest, TestListSnapshotWithNamespaceNameMigration, SnapshotAdminCliTest) {
  // Start with a table missing its namespace_name (as if created before 2.3).
  FLAGS_TEST_create_table_with_empty_namespace_name = true;
  CreateTable(Transactional::kFalse);
  FLAGS_TEST_create_table_with_empty_namespace_name = false;
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  LOG(INFO) << ASSERT_RESULT(RunAdminToolCommand("create_snapshot", keyspace, table_name));

  auto snapshots = ASSERT_RESULT(snapshot_util_->ListSnapshots());
  ASSERT_EQ(snapshots.size(), 1);
  auto snapshot_id = ASSERT_RESULT(FullyDecodeTxnSnapshotId(snapshots[0].id()));

  // Old behaviour, snapshot doesn't have namespace_name.
  auto table_meta = ASSERT_RESULT(GetTableEntryFromSnapshot(snapshots, snapshot_id));
  ASSERT_FALSE(table_meta.has_namespace_name());

  // Restart cluster, run namespace_name migration to populate the namespace_name field.
  ASSERT_OK(cluster_->RestartSync());

  // Create a new snapshot.
  LOG(INFO) << ASSERT_RESULT(RunAdminToolCommand("create_snapshot", keyspace, table_name));
  snapshots = ASSERT_RESULT(snapshot_util_->ListSnapshots());
  ASSERT_EQ(snapshots.size(), 2);

  // Find the new snapshot id.
  TxnSnapshotId new_snapshot_id = ASSERT_RESULT(FullyDecodeTxnSnapshotId(snapshots[0].id()));
  if (new_snapshot_id == snapshot_id) {
    new_snapshot_id = ASSERT_RESULT(FullyDecodeTxnSnapshotId(snapshots[1].id()));
  }

  // Ensure that the namespace_name field is now populated.
  table_meta = ASSERT_RESULT(GetTableEntryFromSnapshot(snapshots, new_snapshot_id));
  ASSERT_TRUE(table_meta.has_namespace_name());
  ASSERT_EQ(table_meta.namespace_name(), keyspace);
}

// An external mini cluster with libpq capabilities to be able to run
// ysqlsh commands as well as invoke yb-admin snapshot related commands.
const std::string kClusterName = "yugacluster";

class YbAdminSnapshotTest : public AdminTestBase {
 public:
  Status PrepareCommon() {
    LOG(INFO) << "Create cluster";
    CreateCluster(kClusterName, ExtraTSFlags(), ExtraMasterFlags());

    LOG(INFO) << "Create client";
    client_ = VERIFY_RESULT(CreateClient());
    test_admin_client_ = std::make_unique<TestAdminClient>(cluster_.get(), client_.get());

    return Status::OK();
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    opts->enable_ysql = true;
    opts->num_masters = 3;
  }

  virtual std::vector<std::string> ExtraTSFlags() {
    return {};
  }

  virtual std::vector<std::string> ExtraMasterFlags() {
    // To speed up tests.
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=500" };
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

  Result<master::ListTablesResponsePB::TableInfo> GetTableByName(const std::string& table_name) {
    auto proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
    master::ListTablesRequestPB req;
    req.set_include_not_running(true);
    req.set_name_filter(table_name);
    master::ListTablesResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.ListTables(req, &resp, &rpc));
    if (resp.tables_size() != 1) {
      return STATUS_FORMAT(IllegalState, "Expected only one table with name $0", table_name);
    }
    return resp.tables(0);
  }

  Result<std::vector<tserver::ListTabletsResponsePB::StatusAndSchemaPB>> GetTabletsOnTserver(
      size_t tserver_idx, const std::string& namespace_name, const std::string& table_name_substr) {
    auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(tserver_idx);
    tserver::ListTabletsRequestPB req;
    tserver::ListTabletsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s);
    RETURN_NOT_OK(proxy.ListTablets(req, &resp, &controller));
    std::vector<tserver::ListTabletsResponsePB::StatusAndSchemaPB> res;
    for (const auto& tablet : resp.status_and_schema()) {
      if (tablet.tablet_status().namespace_name() == namespace_name
          && tablet.tablet_status().table_name().find(table_name_substr) != string::npos) {
        LOG(INFO) << "Tablet on tserver " << tablet.tablet_status().ShortDebugString();
        res.push_back(tablet);
      }
    }
    return res;
  }

  std::unique_ptr<TestAdminClient> test_admin_client_;
};

TEST_F(YbAdminSnapshotTest, SnapshotHidesColocatedTables) {
  ASSERT_OK(PrepareCommon());
  // Create a colocated db.
  auto conn = ASSERT_RESULT(PgConnect());
  ASSERT_OK(conn.Execute("CREATE DATABASE demo WITH colocation=true"));
  auto conn_demo = ASSERT_RESULT(PgConnect("demo"));
  // Create a table.
  ASSERT_OK(conn_demo.Execute("CREATE TABLE t1 (id INT PRIMARY KEY)"));
  // Create a snapshot.
  master::TableIdentifierPB ns_identifier;
  ns_identifier.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  ns_identifier.mutable_namespace_()->set_name("demo");
  auto snapshot_id = ASSERT_RESULT(test_admin_client_->CreateSnapshotAndWait(
      SnapshotScheduleId(Uuid::Nil()), ns_identifier));
  LOG(INFO) << "Created snapshot with id " << snapshot_id;
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
  // Now drop table t1. It should get hidden.
  ASSERT_OK(conn_demo.Execute("DROP TABLE t1"));
  auto table = ASSERT_RESULT(GetTableByName("t1"));
  ASSERT_EQ(table.state(), master::SysTablesEntryPB::RUNNING);
  ASSERT_TRUE(table.hidden());
  LOG(INFO) << "Table is hidden on the master";
  // Table should continue to exist in the tablet metadata on the tservers.
  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    auto tablets = ASSERT_RESULT(GetTabletsOnTserver(i, "demo", "colocation.parent"));
    ASSERT_EQ(tablets.size(), 1);
    ASSERT_EQ(tablets[0].tablet_status().colocated_table_ids_size(), 2);
  }
  // Now delete the snapshot, the cleanup of this table should get triggered that should remove
  // it from the parent tablet.
  ASSERT_OK(test_admin_client_->DeleteSnapshotAndWait(snapshot_id));
  LOG(INFO) << "Snapshot is deleted";
  // Table should be deleted now.
  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto table = VERIFY_RESULT(GetTableByName("t1"));
    if (table.state() != master::SysTablesEntryPB::DELETED) {
      return false;
    }
    LOG(INFO) << "Table is deleted on the master";
    // Table should be deleted from the tablet metadata on the tservers.
    // Thus next compaction will clear out the data for this table.
    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      auto tablets = VERIFY_RESULT(GetTabletsOnTserver(i, "demo", "colocation.parent"));
      if (tablets.size() != 1 || tablets[0].tablet_status().colocated_table_ids_size() != 1) {
        return false;
      }
    }
    LOG(INFO) << "Table is deleted from tserver";
    return true;
  }, 120s, "Wait for table to be deleted"));
}

// Tests that if master leader does not have the auto flag
// enable_object_retention_due_to_snapshots set to true then
// it does not expire snapshots and deletes objects instead of hiding them.
TEST_F(YbAdminSnapshotTest, TtlDuringUpgrade) {
  ASSERT_OK(PrepareCommon());
  // Disable the flag on the master leader.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->GetLeaderMaster(), "enable_object_retention_due_to_snapshots", "false"));
  // Create a db.
  auto conn = ASSERT_RESULT(PgConnect());
  ASSERT_OK(conn.Execute("CREATE DATABASE demo"));
  auto conn_demo = ASSERT_RESULT(PgConnect("demo"));
  // Create a table.
  ASSERT_OK(conn_demo.Execute("CREATE TABLE t1 (id INT PRIMARY KEY)"));
  // Create a snapshot.
  master::TableIdentifierPB ns_identifier;
  ns_identifier.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  ns_identifier.mutable_namespace_()->set_name("demo");
  ASSERT_RESULT(test_admin_client_->CreateSnapshotAndWait(
      SnapshotScheduleId(Uuid::Nil()), ns_identifier, 24 /* retention_duration_hours */));
  // Drop table. It should get deleted now.
  ASSERT_OK(conn_demo.Execute("DROP TABLE t1"));
  auto table = ASSERT_RESULT(GetTableByName("t1"));
  ASSERT_EQ(table.state(), master::SysTablesEntryPB::DELETED);
}

}  // namespace tools
}  // namespace yb
