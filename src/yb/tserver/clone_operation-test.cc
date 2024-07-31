// Copyright (c) YugaByte, Inc.
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

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.pb.h"
#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

DECLARE_bool(TEST_expect_clone_apply_failure);
DECLARE_bool(TEST_fail_apply_clone_op);

namespace yb {
namespace tserver {

using std::string;
using namespace std::literals;

using rpc::RpcController;
using tablet::TabletPeerPtr;

class CloneOperationTest : public TabletServerTestBase {
 public:
  CloneOperationTest() : TabletServerTestBase(TableType::YQL_TABLE_TYPE) {}

 protected:
  void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();
  }

  Status WriteSingleRow(int32_t key, int32_t int_val, const std::string& string_val) {
    RpcController rpc;
    WriteRequestPB write_req;
    WriteResponsePB write_resp;
    write_req.set_tablet_id(kTabletId);
    AddTestRowInsert(key, int_val, string_val, &write_req);
    RETURN_NOT_OK(proxy_->Write(write_req, &write_resp, &rpc));
    if (write_resp.has_error()) {
      return StatusFromPB(write_resp.error().status());
    }
    return Status::OK();
  }

  Status CreateSnapshot(const TxnSnapshotId& snapshot_id) {
    RpcController rpc;
    TabletSnapshotOpRequestPB req;
    TabletSnapshotOpResponsePB resp;
    req.set_operation(TabletSnapshotOpRequestPB::CREATE_ON_TABLET);
    req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    req.add_tablet_id(kTabletId);
    RETURN_NOT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Status Clone(uint32_t clone_request_seq_no) {
    SchemaPB schema_pb;
    SchemaToPB(schema_, &schema_pb);

    RpcController rpc;
    tablet::CloneTabletRequestPB req;
    CloneTabletResponsePB resp;

    req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
    req.set_tablet_id(kTabletId);
    req.set_target_tablet_id(kTargetTabletId);
    req.set_source_snapshot_id(kSourceSnapshotId.data(), kSourceSnapshotId.size());
    req.set_target_snapshot_id(kTargetSnapshotId.data(), kTargetSnapshotId.size());
    req.set_target_table_id(kTargetTableId);
    req.set_target_namespace_name("target_namespace_name");
    req.set_clone_request_seq_no(clone_request_seq_no);
    *req.mutable_target_schema() = schema_pb;
    req.set_target_pg_table_id("target_pg_table_id");
    RETURN_NOT_OK(admin_proxy_->CloneTablet(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Status Restore(
      const tablet::TabletPeerPtr& tablet, const TxnSnapshotId& snapshot_id,
      const HybridTime restore_time) {
    RETURN_NOT_OK(WaitFor([&tablet]() {
      return tablet->IsLeaderAndReady();
    }, 5s * kTimeMultiplier, "Wait for tablet to have a leader."));

    RpcController rpc;
    tserver::TabletSnapshotOpRequestPB req;
    tserver::TabletSnapshotOpResponsePB resp;
    req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    req.add_tablet_id(tablet->tablet_id());
    req.set_snapshot_hybrid_time(restore_time.ToUint64());
    req.set_operation(TabletSnapshotOpRequestPB::RESTORE_ON_TABLET);
    RETURN_NOT_OK(backup_proxy_->TabletSnapshotOp(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  string GetSnapshotDir(const TabletPeerPtr& tablet, const TxnSnapshotId& snapshot_id) {
    return JoinPathSegments( tablet->tablet_metadata()->snapshots_dir(), snapshot_id.ToString());
  }

  const TxnSnapshotId kSourceSnapshotId = TxnSnapshotId::GenerateRandom();
  const TxnSnapshotId kTargetSnapshotId = TxnSnapshotId::GenerateRandom();
  const string kTargetTableId = "target_table_id";
  const string kTargetTabletId = "target_tablet_id";
  const uint32_t kSeqNo = 1;
};

TEST_F(CloneOperationTest, Hardlink) {
  auto source_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  FsManager* const fs = source_tablet->tablet_metadata()->fs_manager();
  Env* const env = fs->env();

  // Write a row so we generate an SST file.
  ASSERT_OK(WriteSingleRow(1, 11, "key1"));
  VerifyRows(schema_, { KeyValue(1, 11) });

  ASSERT_OK(CreateSnapshot(kSourceSnapshotId));

  ASSERT_OK(Clone(kSeqNo));
  ASSERT_EQ(source_tablet->tablet_metadata()->LastAttemptedCloneSeqNo(), kSeqNo);

  auto target_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTargetTabletId));

  const string source_snapshot_dir = GetSnapshotDir(source_tablet, kSourceSnapshotId);
  const string target_snapshot_dir = GetSnapshotDir(target_tablet, kTargetSnapshotId);

  // Check that all the files from the source snapshot directory are hard-linked to the target
  // snapshot directory.
  for (auto& filename : ASSERT_RESULT(fs->ListDir(source_snapshot_dir))) {
    auto source_file = JoinPathSegments(source_snapshot_dir, filename);
    auto target_file = JoinPathSegments(target_snapshot_dir, filename);
    if (ASSERT_RESULT(env->IsDirectory(source_file))) {
      continue;
    }
    LOG(INFO) << "Found file " << filename;
    ASSERT_TRUE(fs->Exists(target_file));
    auto source_inode = ASSERT_RESULT(env->GetFileINode(source_file));
    auto target_inode = ASSERT_RESULT(env->GetFileINode(target_file));
    ASSERT_EQ(source_inode, target_inode);
  }
}

TEST_F(CloneOperationTest, CloneAndRestore) {
  auto source_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  ASSERT_OK(WriteSingleRow(1, 11, "key1"));
  SleepFor(2ms);
  auto restore_time = HybridTime::FromMicros(ASSERT_RESULT(WallClock()->Now()).time_point);
  SleepFor(2ms);
  ASSERT_OK(WriteSingleRow(2, 22, "key2"));
  VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) }, source_tablet);

  ASSERT_OK(CreateSnapshot(kSourceSnapshotId));

  ASSERT_OK(Clone(kSeqNo));
  auto target_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTargetTabletId));

  ASSERT_OK(Restore(target_tablet, kTargetSnapshotId, restore_time));
  VerifyRows(schema_, { KeyValue(1, 11) }, target_tablet);

  // Both tablets should have the same data after restarting as they did before.
  ASSERT_OK(mini_server_->Restart());
  source_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  target_tablet = ASSERT_RESULT(
        mini_server_->server()->tablet_manager()->GetTablet(kTargetTabletId));
  ASSERT_OK(WaitFor([&]() {
    return source_tablet->IsLeaderAndReady() && target_tablet->IsLeaderAndReady();
  }, 5s * kTimeMultiplier, "Wait for tablets to have a leader."));
  VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) }, source_tablet);
  VerifyRows(schema_, { KeyValue(1, 11) }, target_tablet);
}

TEST_F(CloneOperationTest, FailedCloneAppliesSuccessfully) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_expect_clone_apply_failure) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_apply_clone_op) = true;

  auto source_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  ASSERT_OK(WriteSingleRow(1, 11, "key1"));
  ASSERT_OK(CreateSnapshot(kSourceSnapshotId));

  // Clone op should still succeed, but the target tablet should not be created.
  ASSERT_OK(Clone(kSeqNo));
  ASSERT_NOK(mini_server_->server()->tablet_manager()->GetTablet(kTargetTabletId));

  // The source tablet should still be able to apply more ops.
  ASSERT_OK(WriteSingleRow(2, 22, "key2"));
  VerifyRows(schema_, { KeyValue(1, 11), KeyValue(2, 22) }, source_tablet);
}

class CloneOperationMultiDriveTest : public CloneOperationTest {
 protected:
  virtual int NumDrives() override {
    return 2;
  }
};

TEST_F_EX(CloneOperationTest, CloneOnSameDrive, CloneOperationMultiDriveTest) {
  auto source_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  ASSERT_OK(CreateSnapshot(kSourceSnapshotId));
  ASSERT_OK(Clone(100 /* clone_request_seq_no */));
  auto target_tablet = ASSERT_RESULT(
      mini_server_->server()->tablet_manager()->GetTablet(kTargetTabletId));
  ASSERT_EQ(
      source_tablet->tablet_metadata()->wal_root_dir(),
      target_tablet->tablet_metadata()->wal_root_dir());
  ASSERT_EQ(
      source_tablet->tablet_metadata()->data_root_dir(),
      target_tablet->tablet_metadata()->data_root_dir());
}

} // namespace tserver
} // namespace yb
