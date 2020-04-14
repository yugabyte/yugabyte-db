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

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet_snapshots.h"

using namespace std::literals;

DECLARE_uint64(max_clock_skew_usec);
DECLARE_bool(flush_rocksdb_on_shutdown);

namespace yb {
namespace client {

using Snapshots = google::protobuf::RepeatedPtrField<master::SnapshotInfoPB>;
using ImportedSnapshotData = google::protobuf::RepeatedPtrField<
    master::ImportSnapshotMetaResponsePB::TableMetaPB>;

class BackupTxnTest : public TransactionTestBase {
 protected:
  void SetUp() override {
    SetIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);
    TransactionTestBase::SetUp();
  }

  void DoBeforeTearDown() override {
    FLAGS_flush_rocksdb_on_shutdown = false;
    ASSERT_OK(cluster_->RestartSync());

    TransactionTestBase::DoBeforeTearDown();
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
    auto snapshots = VERIFY_RESULT(ListSnapshots(snapshot_id));
    if (snapshots.size() != 1) {
      return STATUS_FORMAT(RuntimeError, "Wrong number of snapshots, one expected but $0 found",
                           snapshots.size());
    }
    LOG(INFO) << "Snapshot state: " << snapshots[0].ShortDebugString();
    return snapshots[0].entry().state() == master::SysSnapshotEntryPB::COMPLETE;
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

  Result<Snapshots> ListSnapshots(
      const TxnSnapshotId& snapshot_id = TxnSnapshotId::Nil()) {
    master::ListSnapshotsRequestPB req;
    master::ListSnapshotsResponsePB resp;

    if (!snapshot_id.IsNil()) {
      req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    }

    rpc::RpcController controller;
    controller.set_timeout(60s);
    RETURN_NOT_OK(MakeBackupServiceProxy().ListSnapshots(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    LOG(INFO) << "Snapshots: " << resp.ShortDebugString();
    return std::move(resp.snapshots());
  }

  CHECKED_STATUS VerifySnapshot(
      const TxnSnapshotId& snapshot_id, master::SysSnapshotEntryPB::State state) {
    auto snapshots = VERIFY_RESULT(ListSnapshots());
    SCHECK_EQ(snapshots.size(), 1, IllegalState, "Wrong number of snapshots");
    const auto& snapshot = snapshots[0];
    auto listed_snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id()));
    if (listed_snapshot_id != snapshot_id) {
      return STATUS_FORMAT(
          IllegalState, "Wrong snapshot id returned $0, expected $1", listed_snapshot_id,
          snapshot_id);
    }
    SCHECK_EQ(snapshot.entry().state(), state, IllegalState, "Wrong snapshot state");
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
          return STATUS_FORMAT(
              IllegalState, "Unexpected entry type: $0",
              master::SysRowEntry::Type_Name(entry.type()));
      }
    }
    SCHECK_EQ(num_namespaces, 1, IllegalState, "Wrong number of namespaces");
    SCHECK_EQ(num_tables, 1, IllegalState, "Wrong number of tables");
    SCHECK_EQ(num_tablets, table_.table()->GetPartitionCount(), IllegalState,
              "Wrong number of tablets");

    return Status::OK();
  }

  Result<TxnSnapshotId> CreateSnapshot() {
    TxnSnapshotId snapshot_id = VERIFY_RESULT(StartSnapshot());

    RETURN_NOT_OK(WaitFor([this, &snapshot_id]() {
      return IsSnapshotDone(snapshot_id);
    }, 5s * kTimeMultiplier, "Snapshot done"));

    return snapshot_id;
  }

  CHECKED_STATUS DeleteSnapshot(const TxnSnapshotId& snapshot_id) {
    master::DeleteSnapshotRequestPB req;
    master::DeleteSnapshotResponsePB resp;

    rpc::RpcController controller;
    controller.set_timeout(60s);
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    RETURN_NOT_OK(MakeBackupServiceProxy().DeleteSnapshot(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Result<ImportedSnapshotData> StartImportSnapshot(const master::SnapshotInfoPB& snapshot) {
    master::ImportSnapshotMetaRequestPB req;
    master::ImportSnapshotMetaResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(60s);

    *req.mutable_snapshot() = snapshot;

    RETURN_NOT_OK(MakeBackupServiceProxy().ImportSnapshotMeta(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    LOG(INFO) << "Imported snapshot metadata: " << resp.DebugString();

    return resp.tables_meta();
  }

  Result<bool> IsSnapshotImportDone(const ImportedSnapshotData& data) {
    for (const auto& table : data) {
      RETURN_NOT_OK(client_->OpenTable(table.table_ids().new_id()));
    }

    return true;
  }
};

TEST_F(BackupTxnTest, Simple) {
  SetAtomicFlag(
      std::chrono::duration_cast<std::chrono::microseconds>(1s).count() * kTimeMultiplier,
      &FLAGS_max_clock_skew_usec);
  ASSERT_NO_FATALS(WriteData());

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

  ASSERT_OK(VerifySnapshot(snapshot_id, master::SysSnapshotEntryPB::COMPLETE));

  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));
  ASSERT_NO_FATALS(VerifyData(1, WriteOpType::UPDATE));

  auto restoration_id = ASSERT_RESULT(StartRestoration(snapshot_id));

  ASSERT_OK(WaitFor([this, &restoration_id] {
    return IsRestorationDone(restoration_id);
  }, 10s, "Restoration done"));

  ASSERT_NO_FATALS(VerifyData(/* num_transactions=*/ 1, WriteOpType::INSERT));
}

TEST_F(BackupTxnTest, Persistence) {
  LOG(INFO) << "Write data";

  ASSERT_NO_FATALS(WriteData());

  LOG(INFO) << "Create snapshot";

  auto snapshot_id = ASSERT_RESULT(CreateSnapshot());

  LOG(INFO) << "First restart";

  ASSERT_OK(cluster_->leader_mini_master()->Restart());
  ASSERT_OK(VerifySnapshot(snapshot_id, master::SysSnapshotEntryPB::COMPLETE));

  LOG(INFO) << "Create namespace";

  // Create namespace and flush, to avoid replaying logs in the master tablet containing the
  // CREATE_ON_MASTER operation for the snapshot.
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name() + "_Test",
                                                kTableName.namespace_type()));

  LOG(INFO) << "Flush";

  auto catalog_manager = cluster_->leader_mini_master()->master()->catalog_manager();
  tablet::TabletPeerPtr tablet_peer;
  ASSERT_OK(catalog_manager->GetTabletPeer(master::kSysCatalogTabletId, &tablet_peer));
  ASSERT_OK(tablet_peer->tablet()->Flush(tablet::FlushMode::kSync));

  LOG(INFO) << "Second restart";

  ASSERT_OK(cluster_->leader_mini_master()->Restart());

  LOG(INFO) << "Verify";

  ASSERT_OK(VerifySnapshot(snapshot_id, master::SysSnapshotEntryPB::COMPLETE));
}

TEST_F(BackupTxnTest, Delete) {
  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(CreateSnapshot());
  ASSERT_OK(VerifySnapshot(snapshot_id, master::SysSnapshotEntryPB::COMPLETE));
  ASSERT_OK(DeleteSnapshot(snapshot_id));

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(ListSnapshots());
    SCHECK_EQ(snapshots.size(), 1, IllegalState, "Wrong number of snapshots");
    if (snapshots[0].entry().state() == master::SysSnapshotEntryPB::DELETED) {
      return true;
    }
    SCHECK_EQ(snapshots[0].entry().state(), master::SysSnapshotEntryPB::DELETING, IllegalState,
              "Wrong snapshot state");
    return false;
  }, 10s * kTimeMultiplier, "Complete delete snapshot"));

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      auto db = peer->tablet()->doc_db().regular;
      if (!db) {
        continue;
      }
      auto dir = tablet::TabletSnapshots::SnapshotsDirName(db->GetName());
      auto children = VERIFY_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
      if (!children.empty()) {
        LOG(INFO) << peer->LogPrefix() << "Children: " << AsString(children);
        return false;
      }
    }
    return true;
  }, 10s * kTimeMultiplier, "Delete on tablets"));
}

TEST_F(BackupTxnTest, ImportMeta) {
  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(CreateSnapshot());
  ASSERT_OK(VerifySnapshot(snapshot_id, master::SysSnapshotEntryPB::COMPLETE));

  ASSERT_OK(client_->DeleteTable(kTableName));
  ASSERT_OK(client_->DeleteNamespace(kTableName.namespace_name()));

  auto snapshots = ASSERT_RESULT(ListSnapshots());
  ASSERT_EQ(snapshots.size(), 1);

  auto import_data = ASSERT_RESULT(StartImportSnapshot(snapshots[0]));

  ASSERT_OK(WaitFor([this, import_data] {
    return IsSnapshotImportDone(import_data);
  }, 10s * kTimeMultiplier, "Complete import snapshot"));

  ASSERT_OK(table_.Open(kTableName, client_.get()));

  ASSERT_NO_FATALS(WriteData());
}

} // namespace client
} // namespace yb
