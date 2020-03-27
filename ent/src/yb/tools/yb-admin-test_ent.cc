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
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/ts_itest-base.h"
#include "yb/integration-tests/external_mini_cluster_ent.h"

#include "yb/master/master_defaults.h"
#include "yb/master/master_backup.proxy.h"

#include "yb/rpc/secure_stream.h"

#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/subprocess.h"

DECLARE_string(certs_dir);

namespace yb {
namespace tools {

using std::shared_ptr;

using client::YBClient;
using client::YBClientBuilder;
using client::YBTableName;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using rpc::RpcController;
using strings::Substitute;

class AdminCliTest : public tserver::TabletServerIntegrationTestBase {
 protected:
  void StartRF1Cluter(bool secure_server = false) {
    FLAGS_num_tablet_servers = secure_server ? 0 : 1;
    FLAGS_num_replicas = 1;

    vector<string> ts_flags, master_flags;
    if (secure_server) {
      const auto sub_dir = JoinPathSegments("ent", "test_certs");
      auto root_dir = env_util::GetRootDir(sub_dir) + "/../../";
      auto certs_dir = JoinPathSegments(root_dir, sub_dir);

      master_flags.push_back("--use_node_to_node_encryption=true");
      master_flags.push_back("--certs_dir=" + JoinPathSegments(root_dir, sub_dir));
      master_flags.push_back("--use_client_to_server_encryption=true");
      ts_flags = master_flags;
    }
    master_flags.push_back("--replication_factor=1");
    BuildAndStart(ts_flags, master_flags);
  }

  CHECKED_STATUS RunAdminToolCommand(const std::initializer_list<string>& args) {
    std::stringstream command;
    command << GetToolPath("yb-admin") << " --master_addresses " <<
        ToString(cluster_->master()->bound_rpc_addr());
    for (const auto& a : args) {
      command << " " << a;
    }
    const auto command_str = command.str();
    LOG(INFO) << "Run tool: " << command_str;
    return Subprocess::Call(command_str);
  }
};

TEST_F(AdminCliTest, TestNonTLS) {
  StartRF1Cluter();
  ASSERT_OK(RunAdminToolCommand({"list_all_masters"}));
}

// TODO: Enabled once ENG-4900 is resolved.
TEST_F(AdminCliTest, DISABLED_TestTLS) {
  StartRF1Cluter();
  const auto sub_dir = JoinPathSegments("ent", "test_certs");
  auto root_dir = env_util::GetRootDir(sub_dir) + "/../../";
  ASSERT_OK(RunAdminToolCommand({
    "--certs_dir_name", JoinPathSegments(root_dir, sub_dir), "list_all_masters"}));
}

TEST_F(AdminCliTest, TestCreateSnapshot) {
  StartRF1Cluter();
  auto client = ASSERT_RESULT(CreateClient());

  // There is custom table.
  vector<YBTableName> tables;
  ASSERT_OK(client->ListTables(&tables, /* filter */ "", /* exclude_ysql */ true));
  ASSERT_EQ(master::kNumSystemTables + 1, tables.size());

  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RpcController rpc;
  ASSERT_OK(master_backup_proxy(cluster_.get())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 0);

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand({
    "create_snapshot", kTableName.namespace_name(), kTableName.table_name()}));

  rpc.Reset();
  ASSERT_OK(master_backup_proxy(cluster_.get())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 1);

  LOG(INFO) << "Test TestCreateSnapshot finished.";
}

Result<string> GetCompletedSnapshot(ExternalMiniCluster* cluster) {
  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RpcController rpc;

  auto timeout = MonoDelta::FromSeconds(10);
  MonoTime start = MonoTime::Now();
  do {
    rpc.Reset();
    RETURN_NOT_OK(master_backup_proxy(cluster)->ListSnapshots(req, &resp, &rpc));
    if (resp.snapshots_size() != 1) {
      return STATUS_FORMAT(Corruption, "Wrong snapshot count $0", resp.snapshots_size());
    }
    if (resp.snapshots(0).entry().state() == master::SysSnapshotEntryPB_State_COMPLETE) {
      return resp.snapshots(0).id();
    }
    SleepFor(MonoDelta::FromMilliseconds(100));
  } while (MonoTime::Now().GetDeltaSince(start).LessThan(timeout));
  return STATUS(TimedOut, "Snapshot has not been not completed in requested time");
}

TEST_F(AdminCliTest, TestImportSnapshot) {
  StartRF1Cluter();

  auto client = ASSERT_RESULT(CreateClient());

  // Create snapshot of default table that gets created.
  ASSERT_OK(RunAdminToolCommand({
    "create_snapshot", kTableName.namespace_name(), kTableName.table_name()}));

  const auto snapshot_id = CHECK_RESULT(GetCompletedSnapshot(cluster_.get()));

  std::string tmp_dir;
  ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));
  const auto snapshot_file = JoinPathSegments(tmp_dir, "exported_snapshot.dat");
  ASSERT_OK(RunAdminToolCommand({
    "export_snapshot", snapshot_id, snapshot_file}));

  auto importer = [this, &client, &snapshot_file](const string& namespace_name,
                                                  const string& table_name) -> Status {
    ListSnapshotsRequestPB req;
    ListSnapshotsResponsePB resp;
    RpcController rpc;
    RETURN_NOT_OK(master_backup_proxy(cluster_.get())->ListSnapshots(req, &resp, &rpc));
    const auto expected_snapshot_count = resp.snapshots_size() + 1;
    RETURN_NOT_OK(RunAdminToolCommand({
        "import_snapshot", snapshot_file, namespace_name, table_name}));
    rpc.Reset();
    RETURN_NOT_OK(master_backup_proxy(cluster_.get())->ListSnapshots(req, &resp, &rpc));
    if (resp.snapshots_size() != expected_snapshot_count) {
      return STATUS_FORMAT(
          Corruption, "Wrong snapshot count $0 expected but $1 found",
          expected_snapshot_count, resp.snapshots_size());
    }
    vector<YBTableName> tables;
    RETURN_NOT_OK(client->ListTables(&tables, /* filter */ "", /* exclude_ysql */ true));
    for (const auto& t : tables) {
      if (t.namespace_name() == namespace_name && t.table_name() == table_name) {
        return Status::OK();
      }
    }
    return STATUS_FORMAT(NotFound, "Expected table $0.$1 not found", namespace_name, table_name);
  };

  const string new_table_name = kTableName.table_name() + "_new";
  // Import snapshot in non existed namespace.
  ASSERT_OK(importer(kTableName.namespace_name() + "_new", new_table_name));
  // Import snapshot in already existed namespace.
  ASSERT_OK(importer(kTableName.namespace_name(), new_table_name));
  // Import snapshot in already existed namespace and table.
  ASSERT_OK(importer(kTableName.namespace_name(), kTableName.table_name()));
}

}  // namespace tools
}  // namespace yb
