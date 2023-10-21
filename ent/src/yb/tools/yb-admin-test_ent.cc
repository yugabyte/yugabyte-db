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

// Tests for the EE yb-admin command-line tool.

#include <gflags/gflags.h>

#include "yb/client/client.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"

#include "yb/integration-tests/external_mini_cluster_ent.h"

#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/rpc/secure_stream.h"

#include "yb/tools/admin-test-base.h"
#include "yb/tools/yb-admin_util.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/client/table_alterer.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/date_time.h"
#include "yb/util/env_util.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

DECLARE_string(certs_dir);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_uint64(TEST_yb_inbound_big_calls_parse_delay_ms);
DECLARE_int64(rpc_throttle_threshold_bytes);
DECLARE_bool(parallelize_bootstrap_producer);
DECLARE_bool(check_bootstrap_required);
DECLARE_bool(TEST_create_table_with_empty_namespace_name);

namespace yb {
namespace tools {

using namespace std::literals;

using std::shared_ptr;
using std::string;

using client::YBTable;
using client::YBTableInfo;
using client::YBTableName;
using client::Transactional;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::ListSnapshotRestorationsRequestPB;
using master::ListSnapshotRestorationsResponsePB;
using master::MasterBackupProxy;
using master::MasterReplicationProxy;
using master::SysCDCStreamEntryPB;
using master::SysSnapshotEntryPB;
using rpc::RpcController;

class AdminCliTest : public client::KeyValueTableTest<MiniCluster> {
 protected:
  Result<MasterBackupProxy*> BackupServiceProxy() {
    if (!backup_service_proxy_) {
      backup_service_proxy_.reset(new MasterBackupProxy(
          &client_->proxy_cache(),
          VERIFY_RESULT(cluster_->GetLeaderMasterBoundRpcAddr())));
    }
    return backup_service_proxy_.get();
  }

  template <class... Args>
  Result<std::string> RunAdminToolCommand(MiniCluster* cluster, Args&&... args) {
    return tools::RunAdminToolCommand(cluster->GetMasterAddresses(), std::forward<Args>(args)...);
  }

  template <class... Args>
  Result<std::string> RunAdminToolCommand(Args&&... args) {
    return RunAdminToolCommand(cluster_.get(), std::forward<Args>(args)...);
  }

  template <class... Args>
  Status RunAdminToolCommandAndGetErrorOutput(string* error_msg, Args&&... args) {
    auto command = ToStringVector(
            GetToolPath("yb-admin"), "-master_addresses", cluster_->GetMasterAddresses(),
            std::forward<Args>(args)...);
    LOG(INFO) << "Run tool: " << AsString(command);
    return Subprocess::Call(command, error_msg, StdFdTypes{StdFdType::kErr});
  }

  template <class... Args>
  Result<rapidjson::Document> RunAdminToolCommandJson(Args&&... args) {
    auto raw = VERIFY_RESULT(RunAdminToolCommand(std::forward<Args>(args)...));
    rapidjson::Document result;
    if (result.Parse(raw.c_str(), raw.length()).HasParseError()) {
      return STATUS_FORMAT(
          InvalidArgument, "Failed to parse json output $0: $1", result.GetParseError(), raw);
    }
    return result;
  }

  Status WaitForRestoreSnapshot() {
    return WaitFor([this]() -> Result<bool> {
      const auto document = VERIFY_RESULT(RunAdminToolCommandJson("list_snapshot_restorations"));
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
        if (state_it->value.GetString() != "RESTORED"s) {
          return false;
        }
      }
      return true;
    },
    30s, "Waiting for snapshot restore to complete");
  }

  Result<ListSnapshotsResponsePB> WaitForAllSnapshots(
    MasterBackupProxy* const alternate_proxy = nullptr) {
    auto proxy = alternate_proxy;
    if (!proxy) {
      proxy = VERIFY_RESULT(BackupServiceProxy());
    }

    ListSnapshotsRequestPB req;
    ListSnapshotsResponsePB resp;
    RETURN_NOT_OK(
        WaitFor([proxy, &req, &resp]() -> Result<bool> {
                  RpcController rpc;
                  RETURN_NOT_OK(proxy->ListSnapshots(req, &resp, &rpc));
                  for (auto const& snapshot : resp.snapshots()) {
                    if (snapshot.entry().state() != SysSnapshotEntryPB::COMPLETE) {
                      return false;
                    }
                  }
                  return true;
                },
                30s, "Waiting for all snapshots to complete"));
    return resp;
  }

  Result<string> GetCompletedSnapshot(int num_snapshots = 1,
                                      int idx = 0,
                                      MasterBackupProxy* const proxy = nullptr) {
    auto resp = VERIFY_RESULT(WaitForAllSnapshots(proxy));

    if (resp.snapshots_size() != num_snapshots) {
      return STATUS_FORMAT(Corruption, "Wrong snapshot count $0", resp.snapshots_size());
    }

    return SnapshotIdToString(resp.snapshots(idx).id());
  }

  Result<SysSnapshotEntryPB::State> WaitForRestoration() {
    auto* proxy = VERIFY_RESULT(BackupServiceProxy());

    ListSnapshotRestorationsRequestPB req;
    ListSnapshotRestorationsResponsePB resp;
    RETURN_NOT_OK(
        WaitFor([proxy, &req, &resp]() -> Result<bool> {
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

  Result<string> GetRecentStreamId(MiniCluster* cluster) {
    const int kStreamUuidLength = 32;
    string output = VERIFY_RESULT(RunAdminToolCommand(cluster, "list_cdc_streams"));
    string find_stream_id = "stream_id: \"";
    string::size_type pos = output.find(find_stream_id);
    return output.substr((pos + find_stream_id.size()), kStreamUuidLength);
  }

  Result<size_t> NumTables(const string& table_name) const;
  void ImportTableAs(const string& snapshot_file, const string& keyspace, const string& table_name);
  void CheckImportedTable(
      const YBTable* src_table, const YBTableName& yb_table_name, bool same_ids = false);
  void CheckAndDeleteImportedTable(
      const string& keyspace, const string& table_name, bool same_ids = false);
  void CheckImportedTableWithIndex(
      const string& keyspace, const string& table_name, const string& index_name,
      bool same_ids = false);

  void DoTestImportSnapshot(const string& format = "");
  void DoTestExportImportIndexSnapshot(Transactional transactional);

 private:
  std::unique_ptr<MasterBackupProxy> backup_service_proxy_;
};

TEST_F(AdminCliTest, TestNonTLS) {
  ASSERT_OK(RunAdminToolCommand("list_all_masters"));
}

// TODO: Enabled once ENG-4900 is resolved.
TEST_F(AdminCliTest, DISABLED_TestTLS) {
  const auto sub_dir = JoinPathSegments("ent", "test_certs");
  auto root_dir = env_util::GetRootDir(sub_dir) + "/../../";
  ASSERT_OK(RunAdminToolCommand(
    "--certs_dir_name", JoinPathSegments(root_dir, sub_dir), "list_all_masters"));
}

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

Result<size_t> AdminCliTest::NumTables(const string& table_name) const {
  auto tables = VERIFY_RESULT(
      client_->ListTables(/* filter */ table_name, /* exclude_ysql */ true));
  return tables.size();
}

void AdminCliTest::CheckImportedTable(const YBTable* src_table,
                                      const YBTableName& yb_table_name,
                                      bool same_ids) {
  shared_ptr<YBTable> table;
  ASSERT_OK(client_->OpenTable(yb_table_name, &table));

  ASSERT_EQ(same_ids, table->id() == src_table->id());
  ASSERT_EQ(table->table_type(), src_table->table_type());
  ASSERT_EQ(table->GetPartitionsCopy(), src_table->GetPartitionsCopy());
  ASSERT_TRUE(table->partition_schema().Equals(src_table->partition_schema()));
  ASSERT_TRUE(table->schema().Equals(src_table->schema()));
  ASSERT_EQ(table->schema().table_properties().is_transactional(),
            src_table->schema().table_properties().is_transactional());
}

void AdminCliTest::CheckAndDeleteImportedTable(const string& keyspace,
                                               const string& table_name,
                                               bool same_ids) {
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());

  const YBTableName yb_table_name(YQL_DATABASE_CQL, keyspace, table_name);
  CheckImportedTable(table_.get(), yb_table_name, same_ids);
  ASSERT_EQ(1, ASSERT_RESULT(NumTables(table_name)));
  ASSERT_OK(client_->DeleteTable(yb_table_name, /* wait */ true));
  ASSERT_EQ(0, ASSERT_RESULT(NumTables(table_name)));
}

void AdminCliTest::ImportTableAs(const string& snapshot_file,
                                 const string& keyspace,
                                 const string& table_name) {
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file, keyspace, table_name));
  CheckAndDeleteImportedTable(keyspace, table_name);
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
    ASSERT_OK(RunAdminToolCommand("export_snapshot", snapshot_id, snapshot_file,
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

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return SelectRow(CreateSession(), 1).ok();
  }, 20s, "Waiting for row from restored snapshot."));
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
  ASSERT_OK(RunAdminToolCommand("restore_snapshot", snapshot_id,
      std::to_string(hybrid_time.GetPhysicalValueMicros())));
  ASSERT_OK(WaitForRestoreSnapshot());

  // Row before HybridTime present, row after should be missing now.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return SelectRow(CreateSession(), 1).ok() &&
           !SelectRow(CreateSession(), 2).ok();
  }, 20s, "Waiting for row from restored snapshot."));
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
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return SelectRow(CreateSession(), 1).ok() &&
           !SelectRow(CreateSession(), 2).ok();
  }, 20s, "Waiting for row from restored snapshot."));
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
  auto i_str = std::to_string(interval/1000000) + "s";
  ASSERT_OK(RunAdminToolCommand("restore_snapshot", snapshot_id, "minus", i_str));
  ASSERT_OK(WaitForRestoreSnapshot());

  ASSERT_OK(SelectRow(CreateSession(), 1));
  auto select2 = SelectRow(CreateSession(), 2);
  ASSERT_NOK(select2);
}

void AdminCliTest::CheckImportedTableWithIndex(const string& keyspace,
                                               const string& table_name,
                                               const string& index_name,
                                               bool same_ids) {
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
  ASSERT_OK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, keyspace, table_name, index_name));
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
      "import_snapshot", snapshot_file, "new_" + keyspace,
      "new_" + table_name, "new_" + index_name));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots());
  CheckImportedTableWithIndex("new_" + keyspace, "new_" + table_name, "new_" + index_name);

  // Renaming table only, no new name for the index - expecting error.
  ASSERT_NOK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, keyspace, "new_" + table_name));
  ASSERT_NOK(RunAdminToolCommand(
      "import_snapshot", snapshot_file, "new_" + keyspace, "new_" + table_name));
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

// Configures two clusters with clients for the producer and consumer side of xcluster replication.
class XClusterAdminCliTest : public AdminCliTest {
 public:
  virtual int num_tablet_servers() {
    return 3;
  }

  void SetUp() override {
    // Setup the default cluster as the consumer cluster.
    AdminCliTest::SetUp();
    // Only create a table on the consumer, producer table may differ in tests.
    CreateTable(Transactional::kTrue);
    FLAGS_check_bootstrap_required = false;

    // Create the producer cluster.
    opts.num_tablet_servers = num_tablet_servers();
    opts.cluster_id = kProducerClusterId;
    producer_cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(producer_cluster_->StartSync());
    ASSERT_OK(producer_cluster_->WaitForTabletServerCount(num_tablet_servers()));
    producer_cluster_client_ = ASSERT_RESULT(producer_cluster_->CreateClient());
  }

  void DoTearDown() override {
    if (producer_cluster_) {
      producer_cluster_->Shutdown();
    }
    AdminCliTest::DoTearDown();
  }

  Status WaitForSetupUniverseReplicationCleanUp(string producer_uuid) {
    auto proxy = std::make_shared<master::MasterReplicationProxy>(
        &client_->proxy_cache(),
        VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

    master::GetUniverseReplicationRequestPB req;
    master::GetUniverseReplicationResponsePB resp;
    return WaitFor([proxy, &req, &resp, producer_uuid]() -> Result<bool> {
      req.set_producer_id(producer_uuid);
      RpcController rpc;
      Status s = proxy->GetUniverseReplication(req, &resp, &rpc);

      return resp.has_error();
    }, 20s, "Waiting for universe to delete");
  }

 protected:
  Status CheckTableIsBeingReplicated(
    const std::vector<TableId>& tables,
    SysCDCStreamEntryPB::State target_state = SysCDCStreamEntryPB::ACTIVE) {
    string output = VERIFY_RESULT(RunAdminToolCommand(producer_cluster_.get(), "list_cdc_streams"));
    string state_search_str = Format(
      "value: \"$0\"",
      SysCDCStreamEntryPB::State_Name(target_state));

    for (const auto& table_id : tables) {
      // Ensure a stream object with table_id exists.
      size_t table_id_pos = output.find(table_id);
      if (table_id_pos == string::npos) {
        return STATUS_FORMAT(
          NotFound,
          "Table id '$0' not found in output: $1",
          table_id, output);
      }

      // Ensure that the strem object has the expected state value.
      size_t state_pos = output.find(state_search_str, table_id_pos);
      if (state_pos == string::npos) {
        return STATUS_FORMAT(
          NotFound,
          "Table id '$0' has the incorrect state value in output: $1",
          table_id, output);
      }

      // Ensure that the state value we captured earlier did not belong
      // to different stream object.
      size_t next_stream_obj_pos = output.find("streams {", table_id_pos);
      if (next_stream_obj_pos != string::npos && next_stream_obj_pos <= state_pos) {
        return STATUS_FORMAT(
          NotFound,
          "Table id '$0' has no state value in output: $1",
          table_id, output);
      }
    }
    return Status::OK();
  }

  Result<MasterBackupProxy*> ProducerBackupServiceProxy() {
    if (!producer_backup_service_proxy_) {
      producer_backup_service_proxy_.reset(new MasterBackupProxy(
          &producer_cluster_client_->proxy_cache(),
          VERIFY_RESULT(producer_cluster_->GetLeaderMasterBoundRpcAddr())));
    }
    return producer_backup_service_proxy_.get();
  }

  std::pair<client::TableHandle, client::TableHandle> CreateAdditionalTableOnBothClusters() {
    const YBTableName kTableName2(YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table2");
    client::TableHandle consumer_table2;
    client::TableHandle producer_table2;
    client::kv_table_test::CreateTable(
        Transactional::kTrue, NumTablets(), client_.get(), &consumer_table2, kTableName2);
    client::kv_table_test::CreateTable(
        Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table2,
        kTableName2);
    return std::make_pair(consumer_table2, producer_table2);
  }

  const string kProducerClusterId = "producer";
  std::unique_ptr<client::YBClient> producer_cluster_client_;
  std::unique_ptr<MiniCluster> producer_cluster_;
  MiniClusterOptions opts;

 private:
  std::unique_ptr<MasterBackupProxy> producer_backup_service_proxy_;
};

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplication) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand("setup_universe_replication",
                                kProducerClusterId,
                                producer_cluster_->GetMasterAddresses(),
                                producer_cluster_table->id()));

  // Check that the stream was properly created for this table.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationChecksForColumnIdMismatch) {
  client::TableHandle producer_table;
  client::TableHandle consumer_table;
  const YBTableName table_name(YQL_DATABASE_CQL,
                               "my_keyspace",
                               "column_id_mismatch_test_table");

  client::kv_table_test::CreateTable(Transactional::kTrue,
                                     NumTablets(),
                                     producer_cluster_client_.get(),
                                     &producer_table,
                                     table_name);
  client::kv_table_test::CreateTable(Transactional::kTrue,
                                     NumTablets(),
                                     client_.get(),
                                     &consumer_table,
                                     table_name);

  // Drop a column from the consumer table.
  {
    auto table_alterer = client_.get()->NewTableAlterer(table_name);
    ASSERT_OK(table_alterer->DropColumn(kValueColumn)->Alter());
  }

  // Add the same column back into the producer table. This results in a schema mismatch
  // between the producer and consumer versions of the table.
  {
    auto table_alterer = client_.get()->NewTableAlterer(table_name);
    table_alterer->AddColumn(kValueColumn)->Type(INT32);
    ASSERT_OK(table_alterer->timeout(MonoDelta::FromSeconds(60 * kTimeMultiplier))->Alter());
  }

  // Try setting up replication, this should fail due to the schema mismatch.
  ASSERT_NOK(RunAdminToolCommand("setup_universe_replication",
                                 kProducerClusterId,
                                 producer_cluster_->GetMasterAddresses(),

                                 producer_table->id()));

  // Make a snapshot of the producer table.
  auto timestamp = DateTime::TimestampToString(DateTime::TimestampNow());
  auto producer_backup_proxy = ASSERT_RESULT(ProducerBackupServiceProxy());
  ASSERT_OK(RunAdminToolCommand(
      producer_cluster_.get(), "create_snapshot", producer_table.name().namespace_name(),
      producer_table.name().table_name()));

  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(1, 0, producer_backup_proxy));
  ASSERT_RESULT(WaitForAllSnapshots(producer_backup_proxy));

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_producer_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand(
      producer_cluster_.get(), "export_snapshot", snapshot_id, snapshot_file));

  // Delete consumer table, then import snapshot of producer table into the existing
  // consumer table. This should fix the schema mismatch issue.
  ASSERT_OK(client_->DeleteTable(table_name, /* wait */ true));
  ASSERT_OK(RunAdminToolCommand("import_snapshot", snapshot_file));

  // Try running SetupUniverseReplication again, this time it should succeed.
  ASSERT_OK(RunAdminToolCommand("setup_universe_replication",
                                kProducerClusterId,
                                producer_cluster_->GetMasterAddresses(),
                                producer_table->id()));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationFailsWithInvalidSchema) {
  client::TableHandle producer_cluster_table;

  // Create a table with a different schema on the producer.
  client::kv_table_test::CreateTable(Transactional::kFalse, // Results in different schema!
                                     NumTablets(),
                                     producer_cluster_client_.get(),
                                     &producer_cluster_table);

  // Try to setup universe replication, should return with a useful error.
  string error_msg;
  // First provide a non-existant table id.
  // ASSERT_NOK since this should fail.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  producer_cluster_table->id() + "-BAD"));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Now try with the correct table id.
  // Note that SetupUniverseReplication should call DeleteUniverseReplication to
  // clean up the environment on failure, so we don't need to explicitly call
  // DeleteUniverseReplication here.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  producer_cluster_table->id()));

  // Verify that error message has relevant information.
  ASSERT_TRUE(error_msg.find("Source and target schemas don't match") != string::npos);
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationFailsWithInvalidBootstrapId) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  // Try to setup universe replication with a fake bootstrap id, should return with a useful error.
  string error_msg;
  // ASSERT_NOK since this should fail.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  producer_cluster_table->id(),
                                                  "fake-bootstrap-id"));

  // Verify that error message has relevant information.
  ASSERT_TRUE(error_msg.find(
      "Could not find CDC stream: stream_id: \"fake-bootstrap-id\"") != string::npos);
}

TEST_F(XClusterAdminCliTest, TestSetupUniverseReplicationCleanupOnFailure) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  string error_msg;
  // Try to setup universe replication with a fake bootstrap id, should result in failure.
  // ASSERT_NOK since this should fail. We should be able to make consecutive calls to
  // SetupUniverseReplication without having to call DeleteUniverseReplication first.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  producer_cluster_table->id(),
                                                  "fake-bootstrap-id"));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Try to setup universe replication with fake producer master address.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  "fake-producer-address",
                                                  producer_cluster_table->id()));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Try to setup universe replication with fake producer table id.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  "fake-producer-table-id"));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Test when producer and local table have different schema.
  client::TableHandle producer_cluster_table2;
  client::TableHandle consumer_table2;
  const YBTableName kTableName2(YQL_DATABASE_CQL, "my_keyspace", "different_schema_test_table");

  client::kv_table_test::CreateTable(Transactional::kFalse, // Results in different schema!
                                     NumTablets(),
                                     producer_cluster_client_.get(),
                                     &producer_cluster_table2,
                                     kTableName2);
  client::kv_table_test::CreateTable(Transactional::kTrue,
                                     NumTablets(),
                                     client_.get(),
                                     &consumer_table2,
                                     kTableName2);

  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  producer_cluster_table2->id()));

  // Wait for the universe to be cleaned up
  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Verify that the environment is cleaned up correctly after failure.
  // A valid call to SetupUniverseReplication after the failure should succeed
  // without us having to first call DeleteUniverseReplication.
  ASSERT_OK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                 "setup_universe_replication",
                                                 kProducerClusterId,
                                                 producer_cluster_->GetMasterAddresses(),
                                                 producer_cluster_table->id()));
  // Verify table is being replicated.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Try calling SetupUniverseReplication again. This should fail as the producer
  // is already present. However, in this case, DeleteUniverseReplication should
  // not be called since the error was due to failing a sanity check.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  producer_cluster_table->id()));
  // Verify the universe replication has not been deleted is still there.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Delete universe.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestListCdcStreamsWithBootstrappedStreams) {
  const int kStreamUuidLength = 32;
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  string output = ASSERT_RESULT(RunAdminToolCommand(producer_cluster_.get(), "list_cdc_streams"));
  // First check that the table and bootstrap status are not present.
  ASSERT_EQ(output.find(producer_cluster_table->id()), string::npos);
  ASSERT_EQ(output.find(SysCDCStreamEntryPB::State_Name(SysCDCStreamEntryPB::INITIATED)),
            string::npos);

  // Bootstrap the producer.
  output = ASSERT_RESULT(RunAdminToolCommand(
      producer_cluster_.get(), "bootstrap_cdc_producer", producer_cluster_table->id()));
  // Get the bootstrap id (output format is "table id: 123, CDC bootstrap id: 123\n").
  string bootstrap_id = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);

  // Check list_cdc_streams again for the table and the status INITIATED.
  ASSERT_OK(CheckTableIsBeingReplicated(
      {producer_cluster_table->id()}, SysCDCStreamEntryPB::INITIATED));

  // Setup universe replication using the bootstrap_id
  ASSERT_OK(RunAdminToolCommand("setup_universe_replication",
                                kProducerClusterId,
                                producer_cluster_->GetMasterAddresses(),
                                producer_cluster_table->id(),
                                bootstrap_id));


  // Check list_cdc_streams again for the table and the status ACTIVE.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Try restarting the producer to ensure that the status persists.
  ASSERT_OK(producer_cluster_->RestartSync());
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAdminCliTest, TestRenameUniverseReplication) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand("setup_universe_replication",
                                kProducerClusterId,
                                producer_cluster_->GetMasterAddresses(),
                                producer_cluster_table->id()));

  // Check that the stream was properly created for this table.
  ASSERT_OK(CheckTableIsBeingReplicated({producer_cluster_table->id()}));

  // Now rename the replication group and then try to perform operations on it.
  std::string new_replication_id = "new_replication_id";
  ASSERT_OK(RunAdminToolCommand("alter_universe_replication",
                                kProducerClusterId,
                                "rename_id",
                                new_replication_id));

  // Assert that using old universe id fails.
  ASSERT_NOK(RunAdminToolCommand("set_universe_replication_enabled",
                                 kProducerClusterId,
                                 0));
  // But using correct name should succeed.
  ASSERT_OK(RunAdminToolCommand("set_universe_replication_enabled",
                                new_replication_id,
                                0));

  // Also create a second stream so we can verify name collisions.
  std::string collision_id = "collision_id";
  // Need to create new tables so that we don't hit "N:1 replication topology not supported" errors.
  auto [consumer_table2, producer_table2] = CreateAdditionalTableOnBothClusters();
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      collision_id,
      producer_cluster_->GetMasterAddresses(),
      producer_table2->id()));
  ASSERT_NOK(RunAdminToolCommand("alter_universe_replication",
                                 new_replication_id,
                                 "rename_id",
                                 collision_id));

  // Using correct name should still succeed.
  ASSERT_OK(RunAdminToolCommand("set_universe_replication_enabled",
                                new_replication_id,
                                1));

  // Also test that we can rename again.
  std::string new_replication_id2 = "new_replication_id2";
  ASSERT_OK(RunAdminToolCommand("alter_universe_replication",
                                new_replication_id,
                                "rename_id",
                                new_replication_id2));

  // Assert that using old universe ids fails.
  ASSERT_NOK(RunAdminToolCommand("set_universe_replication_enabled",
                                 kProducerClusterId,
                                 1));
  ASSERT_NOK(RunAdminToolCommand("set_universe_replication_enabled",
                                 new_replication_id,
                                 1));
  // But using new correct name should succeed.
  ASSERT_OK(RunAdminToolCommand("set_universe_replication_enabled",
                                new_replication_id2,
                                1));

  // Delete this universe so shutdown can proceed.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", new_replication_id2));
  // Also delete second one too.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", collision_id));
}

class XClusterAlterUniverseAdminCliTest : public XClusterAdminCliTest {
 public:
  void SetUp() override {
    // Use more masters so we can test set_master_addresses
    opts.num_masters = 3;

    XClusterAdminCliTest::SetUp();
  }
};

TEST_F(XClusterAlterUniverseAdminCliTest, TestAlterUniverseReplication) {
  YB_SKIP_TEST_IN_TSAN();
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Create an additional table to test with as well.
  const YBTableName kTableName2(YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table2");
  client::TableHandle consumer_table2;
  client::TableHandle producer_table2;
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), client_.get(), &consumer_table2, kTableName2);
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table2,
      kTableName2);

  // Setup replication with both tables, this should only return once complete.
  // Only use the leader master address initially.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      ASSERT_RESULT(producer_cluster_->GetLeaderMiniMaster())->bound_rpc_addr_str(),
      producer_table->id() + "," + producer_table2->id()));

  // Test set_master_addresses, use all the master addresses now.
  ASSERT_OK(RunAdminToolCommand("alter_universe_replication",
                                kProducerClusterId,
                                "set_master_addresses",
                                producer_cluster_->GetMasterAddresses()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id(), producer_table2->id()}));

  // Test removing a table.
  ASSERT_OK(RunAdminToolCommand("alter_universe_replication",
                                kProducerClusterId,
                                "remove_table",
                                producer_table->id()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table2->id()}));
  ASSERT_NOK(CheckTableIsBeingReplicated({producer_table->id()}));

  // Test adding a table.
  ASSERT_OK(RunAdminToolCommand("alter_universe_replication",
                                kProducerClusterId,
                                "add_table",
                                producer_table->id()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id(), producer_table2->id()}));

  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(XClusterAlterUniverseAdminCliTest, TestAlterUniverseReplicationWithBootstrapId) {
  YB_SKIP_TEST_IN_TSAN();
  const int kStreamUuidLength = 32;
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Create an additional table to test with as well.
  const YBTableName kTableName2(YQL_DATABASE_CQL, "my_keyspace", "ql_client_test_table2");
  client::TableHandle consumer_table2;
  client::TableHandle producer_table2;
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), client_.get(), &consumer_table2, kTableName2);
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table2,
      kTableName2);

  // Get bootstrap ids for both producer tables and get bootstrap ids.
  string output = ASSERT_RESULT(RunAdminToolCommand(
      producer_cluster_.get(), "bootstrap_cdc_producer", producer_table->id()));
  string bootstrap_id1 = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);
  ASSERT_OK(CheckTableIsBeingReplicated(
    {producer_table->id()},
    master::SysCDCStreamEntryPB_State_INITIATED));

  output = ASSERT_RESULT(RunAdminToolCommand(
      producer_cluster_.get(), "bootstrap_cdc_producer", producer_table2->id()));
  string bootstrap_id2 = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);
  ASSERT_OK(CheckTableIsBeingReplicated(
    {producer_table2->id()},
    master::SysCDCStreamEntryPB_State_INITIATED));

  // Setup replication with first table, this should only return once complete.
  // Only use the leader master address initially.
  ASSERT_OK(RunAdminToolCommand(
      "setup_universe_replication",
      kProducerClusterId,
      ASSERT_RESULT(producer_cluster_->GetLeaderMiniMaster())->bound_rpc_addr_str(),
      producer_table->id(),
      bootstrap_id1));

  // Test adding the second table with bootstrap id
  ASSERT_OK(RunAdminToolCommand("alter_universe_replication",
                                kProducerClusterId,
                                "add_table",
                                producer_table2->id(),
                                bootstrap_id2));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id(), producer_table2->id()}));

  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

// delete_cdc_stream tests
TEST_F(XClusterAdminCliTest, TestDeleteCDCStreamWithConsumerSetup) {
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand("setup_universe_replication",
                                kProducerClusterId,
                                producer_cluster_->GetMasterAddresses(),
                                producer_table->id()));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id()}));

  string stream_id = ASSERT_RESULT(GetRecentStreamId(producer_cluster_.get()));

  // Should fail as it should meet the conditions to be stopped.
  ASSERT_NOK(RunAdminToolCommand(producer_cluster_.get(), "delete_cdc_stream", stream_id));
  // Should pass as we force it.
  ASSERT_OK(RunAdminToolCommand(producer_cluster_.get(), "delete_cdc_stream", stream_id,
                                "force_delete"));
  // Delete universe should fail as we've force deleted the stream.
  ASSERT_NOK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication",
                                kProducerClusterId,
                                "ignore-errors"));
}

TEST_F(XClusterAdminCliTest, TestDeleteCDCStreamWithBootstrap) {
  const int kStreamUuidLength = 32;
  client::TableHandle producer_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_table);

  string output = ASSERT_RESULT(RunAdminToolCommand(
      producer_cluster_.get(), "bootstrap_cdc_producer", producer_table->id()));
  // Get the bootstrap id (output format is "table id: 123, CDC bootstrap id: 123\n").
  string bootstrap_id = output.substr(output.find_last_of(' ') + 1, kStreamUuidLength);

  // Setup universe replication, this should only return once complete.
  ASSERT_OK(RunAdminToolCommand("setup_universe_replication",
                                kProducerClusterId,
                                producer_cluster_->GetMasterAddresses(),
                                producer_table->id(),
                                bootstrap_id));
  ASSERT_OK(CheckTableIsBeingReplicated({producer_table->id()}));

  // Should fail as it should meet the conditions to be stopped.
  ASSERT_NOK(RunAdminToolCommand(producer_cluster_.get(), "delete_cdc_stream", bootstrap_id));
  // Delete should work fine from deleting from universe.
  ASSERT_OK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));
}

TEST_F(AdminCliTest, TestDeleteCDCStreamWithCreateCDCStream) {

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), client_.get(), &table_);

  // Create CDC stream
  ASSERT_OK(RunAdminToolCommand(cluster_.get(),
                                "create_cdc_stream",
                                table_->id()));

  string stream_id = ASSERT_RESULT(GetRecentStreamId(cluster_.get()));

  // Should be deleted.
  ASSERT_OK(RunAdminToolCommand(cluster_.get(), "delete_cdc_stream", stream_id));
}

TEST_F(XClusterAdminCliTest, TestFailedSetupUniverseWithDeletion) {
  client::TableHandle producer_cluster_table;

  // Create an identical table on the producer.
  client::kv_table_test::CreateTable(
      Transactional::kTrue, NumTablets(), producer_cluster_client_.get(), &producer_cluster_table);

  string error_msg;
  // Setup universe replication, this should only return once complete.
  // First provide a non-existant table id.
  // ASSERT_NOK since this should fail.
  ASSERT_NOK(RunAdminToolCommandAndGetErrorOutput(&error_msg,
                                                  "setup_universe_replication",
                                                  kProducerClusterId,
                                                  producer_cluster_->GetMasterAddresses(),
                                                  producer_cluster_table->id() + "-BAD"));

  ASSERT_OK(WaitForSetupUniverseReplicationCleanUp(kProducerClusterId));

  // Universe should be deleted by BG cleanup
  ASSERT_NOK(RunAdminToolCommand("delete_universe_replication", kProducerClusterId));

  ASSERT_OK(RunAdminToolCommand("setup_universe_replication",
                                kProducerClusterId,
                                producer_cluster_->GetMasterAddresses(),
                                producer_cluster_table->id()));
  std::this_thread::sleep_for(5s);
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

class XClusterAdminCliTest_Large : public XClusterAdminCliTest {
 public:
  int num_tablet_servers() override {
    return 5;
  }
};

TEST_F(XClusterAdminCliTest_Large, TestBootstrapProducerPerformance) {
  // Skip this test in TSAN since the test will time out waiting
  // for table creation to finish.
  YB_SKIP_TEST_IN_TSAN();

  const int table_count = 10;
  const int tablet_count = 5;
  const int expected_runtime_seconds = 15 * kTimeMultiplier;
  const std::string keyspace = "my_keyspace";
  std::vector<client::TableHandle> tables;

  for (int i = 0; i < table_count; i++) {
    client::TableHandle th;
    tables.push_back(th);

    // Build the table.
    client::YBSchemaBuilder builder;
    builder.AddColumn(kKeyColumn)->Type(INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn(kValueColumn)->Type(INT32);

    TableProperties table_properties;
    table_properties.SetTransactional(true);
    builder.SetTableProperties(table_properties);

    const YBTableName table_name(YQL_DATABASE_CQL, keyspace,
      Format("bootstrap_producer_performance_test_table_$0", i));
    ASSERT_OK(producer_cluster_client_.get()->CreateNamespaceIfNotExists(
      table_name.namespace_name(),
      table_name.namespace_type()));

    ASSERT_OK(tables.at(i).Create(
      table_name, tablet_count, producer_cluster_client_.get(), &builder));
  }

  std::string table_ids = tables.at(0)->id();
  for (size_t i = 1; i < tables.size(); ++i) {
    table_ids += "," + tables.at(i)->id();
  }

  // Wait for load balancer to be idle until we call bootstrap_cdc_producer.
  // This prevents TABLET_DATA_TOMBSTONED errors when we make rpc calls.
  // Todo: need to improve bootstrap behaviour with load balancer.
  ASSERT_OK(WaitFor(
    [this, table_ids]() -> Result<bool> {
      return producer_cluster_client_->IsLoadBalancerIdle();
    },
    MonoDelta::FromSeconds(120 * kTimeMultiplier),
    "Waiting for load balancer to be idle"));

  // Add delays to all rpc calls to simulate live environment.
  FLAGS_TEST_yb_inbound_big_calls_parse_delay_ms = 5;
  FLAGS_rpc_throttle_threshold_bytes = 0;
  // Enable parallelized version of BootstrapProducer.
  FLAGS_parallelize_bootstrap_producer = true;

  // Check that bootstrap_cdc_producer returns within time limit.
  ASSERT_OK(WaitFor(
    [this, table_ids]() -> Result<bool> {
      auto res = RunAdminToolCommand(producer_cluster_.get(),
        "bootstrap_cdc_producer", table_ids);
      return res.ok();
    },
    MonoDelta::FromSeconds(expected_runtime_seconds),
    "Waiting for bootstrap_cdc_producer to complete"));
}

TEST_F(AdminCliTest, TestListSnapshotWithNamespaceNameMigration) {
  // Start with a table missing its namespace_name (as if created before 2.3).
  FLAGS_TEST_create_table_with_empty_namespace_name = true;
  CreateTable(Transactional::kFalse);
  FLAGS_TEST_create_table_with_empty_namespace_name = false;
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  LOG(INFO) << ASSERT_RESULT(RunAdminToolCommand("create_snapshot", keyspace, table_name));

  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RpcController rpc;
  ASSERT_OK(ASSERT_RESULT(BackupServiceProxy())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 1);
  auto snapshot_id = resp.snapshots(0).id();
  LOG(INFO) << "Created snapshot " << snapshot_id;
  auto get_table_entry = [&]() -> Result<master::SysTablesEntryPB> {
    for (auto& entry : resp.snapshots(0).entry().entries()) {
      if (entry.type() == master::SysRowEntryType::TABLE) {
        return pb_util::ParseFromSlice<master::SysTablesEntryPB>(entry.data());
      }
    }
    return STATUS(NotFound, "Could not find TABLE entry");
  };

  // Old behaviour, snapshot doesn't have namespace_name.
  master::SysTablesEntryPB table_meta = ASSERT_RESULT(get_table_entry());
  ASSERT_FALSE(table_meta.has_namespace_name());

  // Delete snapshot.
  LOG(INFO) << ASSERT_RESULT(RunAdminToolCommand("delete_snapshot", snapshot_id));
  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    ListSnapshotsRequestPB req;
    ListSnapshotsResponsePB resp;
    RpcController rpc;
    RETURN_NOT_OK(VERIFY_RESULT(BackupServiceProxy())->ListSnapshots(req, &resp, &rpc));
    return resp.snapshots_size() == 0;
  }, 30s, "Complete delete snapshot"));

  // Restart cluster, run namespace_name migration to populate the namespace_name field.
  ASSERT_OK(cluster_->RestartSync());

  // Create a new snapshot.
  LOG(INFO) << ASSERT_RESULT(RunAdminToolCommand("create_snapshot", keyspace, table_name));

  rpc.Reset();
  ASSERT_OK(ASSERT_RESULT(BackupServiceProxy())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 1);

  // Ensure that the namespace_name field is now populated.
  table_meta = ASSERT_RESULT(get_table_entry());
  ASSERT_TRUE(table_meta.has_namespace_name());
  ASSERT_EQ(table_meta.namespace_name(), keyspace);
}

}  // namespace tools
}  // namespace yb
