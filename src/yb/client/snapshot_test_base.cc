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

#include "yb/client/snapshot_test_base.h"

using namespace std::literals;

namespace yb {
namespace client {

master::MasterBackupServiceProxy SnapshotTestBase::MakeBackupServiceProxy() {
  return master::MasterBackupServiceProxy(
      &client_->proxy_cache(), cluster_->leader_mini_master()->bound_rpc_addr());
}

Result<master::SysSnapshotEntryPB::State> SnapshotTestBase::SnapshotState(
    const TxnSnapshotId& snapshot_id) {
  auto snapshots = VERIFY_RESULT(ListSnapshots(snapshot_id));
  if (snapshots.size() != 1) {
    return STATUS_FORMAT(RuntimeError, "Wrong number of snapshots, one expected but $0 found",
                         snapshots.size());
  }
  LOG(INFO) << "Snapshot state: " << snapshots[0].ShortDebugString();
  return snapshots[0].entry().state();
}

Result<bool> SnapshotTestBase::IsSnapshotDone(const TxnSnapshotId& snapshot_id) {
  return VERIFY_RESULT(SnapshotState(snapshot_id)) == master::SysSnapshotEntryPB::COMPLETE;
}

Result<Snapshots> SnapshotTestBase::ListSnapshots(
    const TxnSnapshotId& snapshot_id, bool list_deleted) {
  master::ListSnapshotsRequestPB req;
  master::ListSnapshotsResponsePB resp;

  req.set_list_deleted_snapshots(list_deleted);
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

CHECKED_STATUS SnapshotTestBase::VerifySnapshot(
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
  if (snapshot.entry().state() != state) {
    return STATUS_FORMAT(
        IllegalState, "Wrong snapshot state: $0 vs $1",
        master::SysSnapshotEntryPB::State_Name(snapshot.entry().state()),
        master::SysSnapshotEntryPB::State_Name(state));
  }
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

CHECKED_STATUS SnapshotTestBase::WaitSnapshotInState(
    const TxnSnapshotId& snapshot_id, master::SysSnapshotEntryPB::State state,
    MonoDelta duration) {
  auto state_name = master::SysSnapshotEntryPB::State_Name(state);
  master::SysSnapshotEntryPB::State last_state = master::SysSnapshotEntryPB::UNKNOWN;
  auto status = WaitFor([this, &snapshot_id, state, &last_state]() -> Result<bool> {
    last_state = VERIFY_RESULT(SnapshotState(snapshot_id));
    return last_state == state;
  }, duration * kTimeMultiplier, "Snapshot in state " + state_name);

  if (!status.ok() && status.IsTimedOut()) {
    return STATUS_FORMAT(
      IllegalState, "Wrong snapshot state: $0, while $1 expected",
      master::SysSnapshotEntryPB::State_Name(last_state), state_name);
  }
  return status;
}

CHECKED_STATUS SnapshotTestBase::WaitSnapshotDone(
    const TxnSnapshotId& snapshot_id, MonoDelta duration) {
  return WaitSnapshotInState(snapshot_id, master::SysSnapshotEntryPB::COMPLETE, duration);
}


} // namespace client
} // namespace yb
