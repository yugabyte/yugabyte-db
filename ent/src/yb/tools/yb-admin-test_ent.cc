// Copyright (c) YugaByte, Inc.
//
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

static const char* const kAdminToolName = "yb-admin";

class AdminCliTest : public tserver::TabletServerIntegrationTestBase {
 protected:
  // Figure out where the admin tool is.
  string GetAdminToolPath() const {
    return GetToolPath(kAdminToolName);
  }

  string StartRF1CluterAndGetMasterAddrs(bool secure_server = false) {
    FLAGS_num_tablet_servers = secure_server ? 0 : 1;
    FLAGS_num_replicas = 1;

    vector<string> ts_flags, master_flags;
    master_flags.push_back("--replication_factor=1");
    if (secure_server) {
      const auto sub_dir = JoinPathSegments("ent", "test_certs");
      auto root_dir = env_util::GetRootDir(sub_dir) + "/../../";
      auto certs_dir = JoinPathSegments(root_dir, sub_dir);

      master_flags.push_back("--use_node_to_node_encryption=true");
      master_flags.push_back("--certs_dir=" + certs_dir);
      master_flags.push_back("--use_client_to_server_encryption=true");

      ts_flags.push_back("--use_node_to_node_encryption=true");
      ts_flags.push_back("--certs_dir=" + certs_dir);
      ts_flags.push_back("--use_client_to_server_encryption=true");
    }
    BuildAndStart(ts_flags, master_flags);
    return ToString(cluster_->master()->bound_rpc_addr());
  }
};

TEST_F(AdminCliTest, TestNonTLS) {
  string arg_str = Substitute("$0 -master_addresses $1 list_all_masters",
                              GetAdminToolPath(),
                              StartRF1CluterAndGetMasterAddrs());

  LOG(INFO) << "Run tool:" << arg_str;
  ASSERT_OK(Subprocess::Call(arg_str));
}

// TODO: Enabled once ENG-4900 is resolved.
TEST_F(AdminCliTest, DISABLED_TestTLS) {
  const auto sub_dir = JoinPathSegments("ent", "test_certs");
  auto root_dir = env_util::GetRootDir(sub_dir) + "/../../";
  string arg_str = Substitute("$0 -master_addresses $1 --certs_dir_name $2 list_all_masters",
                              GetAdminToolPath(),
                              StartRF1CluterAndGetMasterAddrs(true /* secure_server */),
                              JoinPathSegments(root_dir, sub_dir));

  LOG(INFO) << "Run tool:" << arg_str;
  ASSERT_OK(Subprocess::Call(arg_str));
}

TEST_F(AdminCliTest, TestCreateSnapshot) {
  string master_address = StartRF1CluterAndGetMasterAddrs();

  shared_ptr<YBClient> client;
  ASSERT_OK(YBClientBuilder()
            .add_master_server_addr(master_address)
            .Build(&client));

  // There is custom table.
  vector<YBTableName> tables;
  ASSERT_OK(client->ListTables(&tables));
  ASSERT_EQ(master::kNumSystemTables + 1, tables.size());

  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RpcController rpc;
  ASSERT_OK(master_backup_proxy(cluster_.get())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 0);

  // Default table that gets created.
  string table_name = kTableName.table_name();
  string keyspace = kTableName.namespace_name();

  string exe_path = GetAdminToolPath();
  string arg_str = Substitute("$0 -master_addresses $1 create_snapshot $2 $3",
                              exe_path,
                              master_address,
                              keyspace,
                              table_name);

  LOG(INFO) << "Run tool:" << arg_str;
  ASSERT_OK(Subprocess::Call(arg_str));

  rpc.Reset();
  ASSERT_OK(master_backup_proxy(cluster_.get())->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 1);

  LOG(INFO) << "Test TestCreateSnapshot finished.";
}

}  // namespace tools
}  // namespace yb
