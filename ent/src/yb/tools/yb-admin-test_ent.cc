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

#include "yb/client/client.h"
#include "yb/client/ql-dml-test-base.h"

#include "yb/integration-tests/external_mini_cluster_ent.h"

#include "yb/master/master_backup.proxy.h"

#include "yb/rpc/secure_stream.h"

#include "yb/tools/yb-admin_util.h"

#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

DECLARE_string(certs_dir);

namespace yb {
namespace tools {

using namespace std::literals;

using std::string;

using client::YBTableName;
using client::Transactional;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::MasterBackupServiceProxy;
using rpc::RpcController;

class AdminCliTest : public client::KeyValueTableTest {
 protected:
  MasterBackupServiceProxy& BackupServiceProxy() {
    if (!backup_service_proxy_) {
      backup_service_proxy_.reset(new MasterBackupServiceProxy(
          &client_->proxy_cache(), cluster_->leader_mini_master()->bound_rpc_addr()));
    }
    return *backup_service_proxy_.get();
  }

  Status RunAdminToolCommand(const std::initializer_list<string>& args) {
    std::stringstream command;
    command << GetToolPath("yb-admin") << " --master_addresses "
            << cluster_->GetMasterAddresses();
    for (const auto& a : args) {
      command << " " << a;
    }
    const auto command_str = command.str();
    LOG(INFO) << "Run tool: " << command_str;
    return Subprocess::Call(command_str);
  }

  void DoTestExportImportIndexSnapshot(Transactional transactional);

 private:
  std::unique_ptr<MasterBackupServiceProxy> backup_service_proxy_;
};

TEST_F(AdminCliTest, TestNonTLS) {
  ASSERT_OK(RunAdminToolCommand({"list_all_masters"}));
}

// TODO: Enabled once ENG-4900 is resolved.
TEST_F(AdminCliTest, DISABLED_TestTLS) {
  const auto sub_dir = JoinPathSegments("ent", "test_certs");
  auto root_dir = env_util::GetRootDir(sub_dir) + "/../../";
  ASSERT_OK(RunAdminToolCommand({
    "--certs_dir_name", JoinPathSegments(root_dir, sub_dir), "list_all_masters"}));
}

TEST_F(AdminCliTest, TestCreateSnapshot) {
  CreateTable(client::Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // There is custom table.
  const auto tables = ASSERT_RESULT(client_->ListTables(table_name, /* exclude_ysql */ true));
  ASSERT_EQ(1, tables.size());

  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RpcController rpc;
  ASSERT_OK(BackupServiceProxy().ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 0);

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand({"create_snapshot", keyspace, table_name}));

  rpc.Reset();
  ASSERT_OK(BackupServiceProxy().ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 1);

  LOG(INFO) << "Test TestCreateSnapshot finished.";
}

Result<ListSnapshotsResponsePB> WaitForAllSnapshots(MasterBackupServiceProxy* proxy) {
  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(
      WaitFor([proxy, &req, &resp]() -> Result<bool> {
                RpcController rpc;
                RETURN_NOT_OK(proxy->ListSnapshots(req, &resp, &rpc));
                for (auto const snapshot : resp.snapshots()) {
                  if (snapshot.entry().state() != master::SysSnapshotEntryPB_State_COMPLETE) {
                    return false;
                  }
                }
                return true;
              },
              30s, "Waiting for all snapshots to complete"));
  return resp;
}

Result<string> GetCompletedSnapshot(MasterBackupServiceProxy* proxy, int idx = 0) {
  auto resp = VERIFY_RESULT(WaitForAllSnapshots(proxy));

  if (resp.snapshots_size() != idx + 1) {
    return STATUS_FORMAT(Corruption, "Wrong snapshot count $0", resp.snapshots_size());
  }

  return SnapshotIdToString(resp.snapshots(idx).id());
}

TEST_F(AdminCliTest, TestImportSnapshot) {
  CreateTable(client::Transactional::kFalse);
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand({"create_snapshot", keyspace, table_name}));

  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(&BackupServiceProxy()));

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand({"export_snapshot", snapshot_id, snapshot_file}));

  auto importer = [this, &snapshot_file](const string& namespace_name,
                                         const string& table_name) -> Status {
    RETURN_NOT_OK(RunAdminToolCommand({
        "import_snapshot", snapshot_file, namespace_name, table_name}));
    // Wait for the new snapshot completion.
    VERIFY_RESULT(WaitForAllSnapshots(&BackupServiceProxy()));
    const auto tables = VERIFY_RESULT(client_->ListTables(/* filter */ "",
                                                          /* exclude_ysql */ true));
    for (const auto& t : tables) {
      if (t.namespace_name() == namespace_name && t.table_name() == table_name) {
        return Status::OK();
      }
    }
    return STATUS_FORMAT(NotFound, "Expected table $0.$1 not found", namespace_name, table_name);
  };

  // Import snapshot in non existed namespace.
  ASSERT_OK(importer(keyspace + "_new", table_name));
  // Import snapshot in already existed namespace.
  ASSERT_OK(importer(keyspace, table_name + "_new"));
  // Import snapshot in already existed namespace and table.
  ASSERT_OK(importer(keyspace, table_name));
}

TEST_F(AdminCliTest, TestExportImportSnapshot) {
  CreateTable(client::Transactional::kFalse);

  // Default table that gets created.
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand({"create_snapshot", keyspace, table_name}));
  const auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(&BackupServiceProxy()));

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand({"export_snapshot", snapshot_id, snapshot_file}));

  ASSERT_OK(client_->DeleteTable(
      YBTableName(YQL_DATABASE_CQL, keyspace, table_name), /* wait */ true));
  auto tables = ASSERT_RESULT(client_->ListTables(table_name, /* exclude_ysql */ true));
  ASSERT_EQ(0, tables.size());

  ASSERT_OK(RunAdminToolCommand({"import_snapshot", snapshot_file, keyspace, table_name}));

  tables = ASSERT_RESULT(client_->ListTables(table_name, /* exclude_ysql */ true));
  ASSERT_EQ(1, tables.size());

  LOG(INFO) << "Test TestExportImportSnapshot finished.";
}

void AdminCliTest::DoTestExportImportIndexSnapshot(Transactional transactional) {
  CreateTable(transactional);
  CreateIndex(transactional);

  // Default table that gets created.
  const string& table_name = table_.name().table_name();
  const string& keyspace = table_.name().namespace_name();
  const string& index_name = index_.name().table_name();

  // Check there are 2 tables.
  auto tables = ASSERT_RESULT(client_->ListTables(table_name, true /* exclude_ysql */));
  ASSERT_EQ(2, tables.size());

  // Create snapshot of default table and the attached index that gets created.
  ASSERT_OK(RunAdminToolCommand({"create_snapshot", keyspace, table_name}));
  auto snapshot_id = ASSERT_RESULT(GetCompletedSnapshot(&BackupServiceProxy()));

  string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand({"export_snapshot", snapshot_id, snapshot_file}));

  ASSERT_OK(client_->DeleteTable(
      YBTableName(YQL_DATABASE_CQL, keyspace, table_name), /* wait */ true));
  tables = ASSERT_RESULT(client_->ListTables(table_name /* filter */, true /* exclude_ysql */));
  ASSERT_EQ(0, tables.size());

  // Import table and index with original names - using the old names.
  ASSERT_OK(RunAdminToolCommand({
      "import_snapshot", snapshot_file, keyspace, table_name, keyspace, index_name}));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots(&BackupServiceProxy()));

  tables = ASSERT_RESULT(client_->ListTables(table_name /* filter */, true /* exclude_ysql */));
  ASSERT_EQ(2, tables.size());
  ASSERT_OK(client_->DeleteTable(
      YBTableName(YQL_DATABASE_CQL, keyspace, table_name), /* wait */ true));

  // Import table and index with original names - not providing any names.
  ASSERT_OK(RunAdminToolCommand({"import_snapshot", snapshot_file}));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots(&BackupServiceProxy()));

  tables = ASSERT_RESULT(client_->ListTables(table_name /* filter */, true /* exclude_ysql */));
  ASSERT_EQ(2, tables.size());
  ASSERT_OK(client_->DeleteTable(
      YBTableName(YQL_DATABASE_CQL, keyspace, table_name), /* wait */ true));

  // Import table and index with original names - providing only old table name.
  ASSERT_OK(RunAdminToolCommand({"import_snapshot", snapshot_file, keyspace, table_name}));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots(&BackupServiceProxy()));

  tables = ASSERT_RESULT(client_->ListTables(table_name /* filter */, true /* exclude_ysql */));
  ASSERT_EQ(2, tables.size());

  // Import table and index with renaming.
  ASSERT_OK(RunAdminToolCommand({
      "import_snapshot", snapshot_file,
      keyspace, table_name + "_new", keyspace, index_name + "_new"}));
  // Wait for the new snapshot completion.
  ASSERT_RESULT(WaitForAllSnapshots(&BackupServiceProxy()));

  tables = ASSERT_RESULT(client_->ListTables(table_name /* filter */, true /* exclude_ysql */));
  ASSERT_EQ(4, tables.size());
}

TEST_F(AdminCliTest, TestExportImportIndexSnapshot) {
  // Test non-transactional table.
  DoTestExportImportIndexSnapshot(Transactional::kFalse);
  LOG(INFO) << "Test TestExportImportIndexSnapshot finished.";
}

TEST_F(AdminCliTest, TestExportImportIndexSnapshot_ForTransactional) {
  // Test the recreated transactional table.
  DoTestExportImportIndexSnapshot(Transactional::kTrue);
  LOG(INFO) << "Test TestExportImportIndexSnapshot_ForTransactional finished.";
}

}  // namespace tools
}  // namespace yb
