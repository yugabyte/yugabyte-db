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

#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

namespace yb {
namespace tserver {

using std::string;

using yb::rpc::RpcController;

class BackupServiceTest : public TabletServerTestBase {
 public:
  BackupServiceTest() : TabletServerTestBase(TableType::YQL_TABLE_TYPE) {}

  Status WriteSingleRow(
      const std::string& tablet_id, int32_t key, int32_t int_val, const std::string& string_val);

  Status CreateSnapshot(const std::string& tablet_id, const TxnSnapshotId& snapshot_id);

  Status RestoreSnapshot(
      const std::string& tablet_id, const TxnSnapshotId& snapshot_id,
      const TxnSnapshotRestorationId& restoration_id);

 protected:
  void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();
  }
};

TEST_F(BackupServiceTest, TestCreateTabletSnapshot) {
  // Verify that the tablet exists.
  auto tablet = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  FsManager* const fs = tablet->tablet_metadata()->fs_manager();

  const string snapshot_id = "00000000000000000000000000000000";
  const string rocksdb_dir = tablet->tablet_metadata()->rocksdb_dir();
  const string top_snapshots_dir = tablet->tablet_metadata()->snapshots_dir();
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir, snapshot_id);

  TabletSnapshotOpRequestPB req;
  TabletSnapshotOpResponsePB resp;

  req.set_operation(TabletSnapshotOpRequestPB::CREATE_ON_TABLET);
  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_snapshot_id(snapshot_id);

  // Test empty tablet list - expected error.
  // Send the call.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
    ASSERT_NOK(StatusFromPB(resp.error().status()));
  }

  req.add_tablet_id(kTabletId);

  ASSERT_TRUE(fs->Exists(rocksdb_dir));
  ASSERT_TRUE(fs->Exists(top_snapshots_dir));

  // Send the call.
  {
    RpcController rpc;
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  ASSERT_TRUE(fs->Exists(rocksdb_dir));
  ASSERT_TRUE(fs->Exists(top_snapshots_dir));
  ASSERT_TRUE(fs->Exists(snapshot_dir));
  // Check existence of snapshot files:
  ASSERT_TRUE(fs->Exists(JoinPathSegments(snapshot_dir, "CURRENT")));
  ASSERT_TRUE(fs->Exists(JoinPathSegments(snapshot_dir, "MANIFEST-000001")));
}

TEST_F(BackupServiceTest, TestSnapshotData) {
  // Verify that the tablet exists.
  ASSERT_OK(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11) });

  auto snapshot_id = TxnSnapshotId::GenerateRandom();
  ASSERT_OK(CreateSnapshot(kTabletId, snapshot_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "CREATED SNAPSHOT. UPDATING THE TABLET DATA..";

  ASSERT_OK(WriteSingleRow(kTabletId, 2, 22, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) });

  // Send the restore snapshot request.
  auto restoration_id = TxnSnapshotRestorationId::GenerateRandom();
  ASSERT_OK(RestoreSnapshot(kTabletId, snapshot_id, restoration_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "RESTORED SNAPSHOT. CHECKING THE TABLET DATA..";

  // Expected the first row only from the snapshot.
  VerifyRows(schema_, { KeyValue(1, 11) });

  LOG(INFO) << "THE TABLET DATA IS VALID. Test TestSnapshotData finished.";
}

TEST_F(BackupServiceTest, RepeatedRestoreRequest) {
  // Verify that the tablet exists.
  ASSERT_OK(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  ASSERT_OK(WriteSingleRow(kTabletId, 1, 11, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11) });

  auto snapshot_id = TxnSnapshotId::GenerateRandom();
  ASSERT_OK(CreateSnapshot(kTabletId, snapshot_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "CREATED SNAPSHOT. UPDATING THE TABLET DATA..";

  ASSERT_OK(WriteSingleRow(kTabletId, 2, 22, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) });

  // Send the restore snapshot request.
  auto restoration_id = TxnSnapshotRestorationId::GenerateRandom();
  ASSERT_OK(RestoreSnapshot(kTabletId, snapshot_id, restoration_id));
  SleepFor(MonoDelta::FromMilliseconds(500));

  // Repeat the restoration attempt with the same restoration_id.
  ASSERT_OK(RestoreSnapshot(kTabletId, snapshot_id, restoration_id));
  SleepFor(MonoDelta::FromMilliseconds(500));
  LOG(INFO) << "SENT SNAPSHOT RESTORATION REQUEST TWICE. CHECKING TABLET METADATA..";

  // Verify there is only a single active restoration id in the metadata.
  tablet::RaftGroupReplicaSuperBlockPB super_block;
  ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId))
      ->tablet_metadata()
      ->ToSuperBlock(&super_block);
  ASSERT_EQ(super_block.active_restorations_size(), 1);
  auto recorded_restoration_id =
      ASSERT_RESULT(FullyDecodeTxnSnapshotRestorationId(super_block.active_restorations(0)));
  ASSERT_EQ(recorded_restoration_id, restoration_id);

  // Expected only the first row only from the snapshot.
  VerifyRows(schema_, { KeyValue(1, 11) });
}

Status BackupServiceTest::WriteSingleRow(
    const std::string& tablet_id, int32_t key, int32_t int_val, const std::string& string_val) {
  WriteRequestPB req;
  req.set_tablet_id(tablet_id);
  AddTestRowInsert(key, int_val, string_val, &req);
  RpcController rpc;
  SCOPED_TRACE(req.DebugString());
  WriteResponsePB resp;
  RETURN_NOT_OK(proxy_->Write(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  return ResponseStatus(resp);
}

Status BackupServiceTest::CreateSnapshot(
    const std::string& tablet_id, const TxnSnapshotId& snapshot_id) {
  TabletSnapshotOpRequestPB req;
  TabletSnapshotOpResponsePB resp;
  req.set_operation(TabletSnapshotOpRequestPB::CREATE_ON_TABLET);
  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  req.add_tablet_id(tablet_id);
  RpcController rpc;
  SCOPED_TRACE(req.DebugString());
  RETURN_NOT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  return ResponseStatus(resp);
}

Status BackupServiceTest::RestoreSnapshot(
    const std::string& tablet_id, const TxnSnapshotId& snapshot_id,
    const TxnSnapshotRestorationId& restoration_id) {
  TabletSnapshotOpRequestPB req;
  TabletSnapshotOpResponsePB resp;
  req.set_operation(TabletSnapshotOpRequestPB::RESTORE_ON_TABLET);
  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  req.add_tablet_id(tablet_id);
  req.set_restoration_id(restoration_id.data(), restoration_id.size());
  RpcController rpc;
  SCOPED_TRACE(req.DebugString());
  RETURN_NOT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  return ResponseStatus(resp);
}

} // namespace tserver
} // namespace yb
