// Copyright (c) YugaByte, Inc.
//
// Tests for the EE yb-admin command-line tool.

#include <gflags/gflags.h>

#include "yb/client/client.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/ts_itest-base.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/util/subprocess.h"

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
};

TEST_F(AdminCliTest, TestCreateSnapshot) {
  google::FlagSaver saver;
  FLAGS_num_tablet_servers = 1;
  FLAGS_num_replicas = 1;

  vector<string> ts_flags, master_flags;
  master_flags.push_back("--replication_factor=1");
  BuildAndStart(ts_flags, master_flags);
  string master_address = ToString(cluster_->master()->bound_rpc_addr());

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
  ASSERT_OK(cluster_->master_backup_proxy()->ListSnapshots(req, &resp, &rpc));
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
  ASSERT_OK(cluster_->master_backup_proxy()->ListSnapshots(req, &resp, &rpc));
  ASSERT_EQ(resp.snapshots_size(), 1);

  LOG(INFO) << "Test TestCreateSnapshot finished.";
}

}  // namespace tools
}  // namespace yb
