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
//

#include "yb/client/txn-test-base.h"

#include "yb/master/master_backup.proxy.h"

using namespace std::literals;

DECLARE_uint64(max_clock_skew_usec);

namespace yb {
namespace client {

class BackupTxnTest : public TransactionTestBase {
 protected:
  void SetUp() override {
    SetIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);
    TransactionTestBase::SetUp();
  }

  master::MasterBackupServiceProxy MakeBackupServiceProxy() {
    return master::MasterBackupServiceProxy(
        &client_->proxy_cache(), cluster_->leader_mini_master()->bound_rpc_addr());
  }

  Result<TxnSnapshotId> StartSnapshot() {
    rpc::RpcController controller;
    controller.set_timeout(60s);
    master::CreateSnapshotRequestPB req;
    req.set_transaction_aware(true);
    auto id = req.add_tables();
    id->set_table_id(table_.table()->id());
    master::CreateSnapshotResponsePB resp;
    RETURN_NOT_OK(MakeBackupServiceProxy().CreateSnapshot(req, &resp, &controller));
    return FullyDecodeTxnSnapshotId(resp.snapshot_id());
  }

  Result<bool> IsSnapshotDone(const TxnSnapshotId& snapshot_id) {
    master::ListSnapshotsRequestPB req;
    master::ListSnapshotsResponsePB resp;

    rpc::RpcController controller;
    controller.set_timeout(60s);
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    RETURN_NOT_OK(MakeBackupServiceProxy().ListSnapshots(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    LOG(INFO) << "Snapshot state: " << resp.snapshots(0).ShortDebugString();
    if (resp.snapshots().size() != 1) {
      return STATUS_FORMAT(RuntimeError, "Wrong number of snapshots, one expected but $0 found",
                           resp.snapshots().size());
    }
    return resp.snapshots(0).entry().state() == master::SysSnapshotEntryPB::COMPLETE;
  }

  Result<TxnSnapshotRestorationId> StartRestoration(const TxnSnapshotId& snapshot_id) {
    master::RestoreSnapshotRequestPB req;
    master::RestoreSnapshotResponsePB resp;

    rpc::RpcController controller;
    controller.set_timeout(60s);
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    RETURN_NOT_OK(MakeBackupServiceProxy().RestoreSnapshot(req, &resp, &controller));
    return FullyDecodeTxnSnapshotRestorationId(resp.restoration_id());
  }

  Result<bool> IsRestorationDone(const TxnSnapshotRestorationId& restoration_id) {
    master::ListSnapshotRestorationsRequestPB req;
    master::ListSnapshotRestorationsResponsePB resp;

    rpc::RpcController controller;
    controller.set_timeout(60s);
    req.set_restoration_id(restoration_id.data(), restoration_id.size());
    RETURN_NOT_OK(MakeBackupServiceProxy().ListSnapshotRestorations(req, &resp, &controller));
    if (resp.has_status()) {
      return StatusFromPB(resp.status());
    }
    if (resp.restorations().size() != 1) {
      return STATUS_FORMAT(RuntimeError, "Wrong number of restorations, one expected but $0 found",
                           resp.restorations().size());
    }
    return resp.restorations(0).entry().state() == master::SysSnapshotEntryPB::COMPLETE;
  }
};

TEST_F(BackupTxnTest, CreateSnapshot) {
  SetAtomicFlag(
      std::chrono::duration_cast<std::chrono::microseconds>(1s).count() * kTimeMultiplier,
      &FLAGS_max_clock_skew_usec);
  ASSERT_NO_FATALS(WriteData());

  auto backup_service_proxy = MakeBackupServiceProxy();

  TxnSnapshotId snapshot_id = ASSERT_RESULT(StartSnapshot());

  bool has_pending = false;
  ASSERT_OK(WaitFor([this, &snapshot_id, &has_pending]() -> Result<bool> {
    if (!VERIFY_RESULT(IsSnapshotDone(snapshot_id))) {
      has_pending = true;
      return false;
    }
    return true;
  }, 10s, "Snapshot done"));

  ASSERT_TRUE(has_pending);

  {
    master::ListSnapshotsRequestPB req;
    master::ListSnapshotsResponsePB resp;

    rpc::RpcController controller;
    controller.set_timeout(60s);
    ASSERT_OK(MakeBackupServiceProxy().ListSnapshots(req, &resp, &controller));
    LOG(INFO) << "Snapshots: " << resp.ShortDebugString();
    ASSERT_EQ(resp.snapshots().size(), 1);
    const auto& snapshot = resp.snapshots(0);
    auto listed_snapshot_id = ASSERT_RESULT(FullyDecodeTxnSnapshotId(snapshot.id()));
    ASSERT_EQ(listed_snapshot_id, snapshot_id);
    size_t num_namespaces = 0, num_tables = 0, num_tablets = 0;
    for (const auto& entry : snapshot.entry().entries()) {
      switch (entry.type()) {
        case master::SysRowEntry::TABLET:
          ++num_tablets;
          break;
        case master::SysRowEntry::TABLE:
          ++num_tables;
          break;
        case master::SysRowEntry::NAMESPACE:
          ++num_namespaces;
          break;
        default:
          FAIL() << "Unexpected entry type: " << master::SysRowEntry::Type_Name(entry.type());
          break;
      }
    }
    ASSERT_EQ(num_namespaces, 1);
    ASSERT_EQ(num_tables, 1);
    ASSERT_EQ(num_tablets, table_.table()->GetPartitionCount());
  }

  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));
  ASSERT_NO_FATALS(VerifyData(1, WriteOpType::UPDATE));

  auto restoration_id = ASSERT_RESULT(StartRestoration(snapshot_id));

  ASSERT_OK(WaitFor([this, &restoration_id] {
    return IsRestorationDone(restoration_id);
  }, 10s, "Restoration done"));

  ASSERT_NO_FATALS(VerifyData(/* num_transactions=*/ 1, WriteOpType::INSERT));
}

} // namespace client
} // namespace yb
