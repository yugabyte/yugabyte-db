// Copyright (c) YugaByte, Inc.

#include "yb/common/wire_protocol.h"

#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/backup.proxy.h"

namespace yb {
namespace tserver {

using std::string;

using yb::rpc::RpcController;
using yb::tablet::TabletPeer;
using yb::tablet::enterprise::kSnapshotsDirName;

class BackupServiceTest : public TabletServerTestBase {
 public:
  BackupServiceTest() : TabletServerTestBase(TableType::YQL_TABLE_TYPE) {}

 protected:
  void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();

    backup_proxy_.reset(
        new TabletServerBackupServiceProxy(client_messenger_, mini_server_->bound_rpc_addr()));
  }

  gscoped_ptr<TabletServerBackupServiceProxy> backup_proxy_;
};

TEST_F(BackupServiceTest, TestCreateTabletSnapshot) {
  // Verify that the tablet exists.
  scoped_refptr<TabletPeer> tablet;
  ASSERT_TRUE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));
  FsManager* const fs = tablet->tablet_metadata()->fs_manager();

  const string snapshot_id = "00000000000000000000000000000000";
  const string rocksdb_dir = tablet->tablet_metadata()->rocksdb_dir();
  const string snapshots_dir = JoinPathSegments(rocksdb_dir, kSnapshotsDirName);
  const string tablet_dir = JoinPathSegments(snapshots_dir, snapshot_id);

  CreateTabletSnapshotRequestPB req;
  CreateTabletSnapshotResponsePB resp;

  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_snapshot_id(snapshot_id);

  // Test empty tablet list - expected error.
  // Send the call.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(backup_proxy_->CreateTabletSnapshot(req, &resp, &rpc));
    ASSERT_NOK(StatusFromPB(resp.error().status()));
  }

  req.set_tablet_id(kTabletId);

  ASSERT_TRUE(fs->Exists(rocksdb_dir));
  ASSERT_FALSE(fs->Exists(snapshots_dir));

  // Send the call.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(backup_proxy_->CreateTabletSnapshot(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  ASSERT_TRUE(fs->Exists(rocksdb_dir));
  ASSERT_TRUE(fs->Exists(snapshots_dir));
  ASSERT_TRUE(fs->Exists(tablet_dir));
  // Check existence of snapshot files:
  ASSERT_TRUE(fs->Exists(JoinPathSegments(tablet_dir, "CURRENT")));
  ASSERT_TRUE(fs->Exists(JoinPathSegments(tablet_dir, "MANIFEST-000001")));
}

} // namespace tserver
} // namespace yb
