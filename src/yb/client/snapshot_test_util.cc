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

#include "yb/client/snapshot_test_util.h"

#include "yb/client/client_fwd.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_fwd.h"
#include "yb/common/wire_protocol.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/status_format.h"

using namespace std::literals;

namespace yb {
namespace client {

Result<master::SysSnapshotEntryPB::State> SnapshotTestUtil::SnapshotState(
    const TxnSnapshotId& snapshot_id) {
  auto snapshots = VERIFY_RESULT(ListSnapshots(snapshot_id));
  if (snapshots.size() != 1) {
    return STATUS_FORMAT(RuntimeError, "Wrong number of snapshots, one expected but $0 found",
                         snapshots.size());
  }
  LOG(INFO) << "Snapshot state: " << snapshots[0].ShortDebugString();
  return snapshots[0].entry().state();
}

Result<bool> SnapshotTestUtil::IsSnapshotDone(const TxnSnapshotId& snapshot_id) {
  return VERIFY_RESULT(SnapshotState(snapshot_id)) == master::SysSnapshotEntryPB::COMPLETE;
}

Result<Snapshots> SnapshotTestUtil::ListSnapshots(
    const TxnSnapshotId& snapshot_id, ListDeleted list_deleted,
    PrepareForBackup prepare_for_backup) {
  master::ListSnapshotsRequestPB req;
  master::ListSnapshotsResponsePB resp;

  req.set_list_deleted_snapshots(list_deleted);
  req.set_prepare_for_backup(prepare_for_backup);
  if (!snapshot_id.IsNil()) {
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  }
  auto options = req.mutable_detail_options();
  options->set_show_namespace_details(true);
  options->set_show_udtype_details(true);
  options->set_show_table_details(true);
  options->set_show_tablet_details(true);

  rpc::RpcController controller;
  controller.set_timeout(60s);
  RETURN_NOT_OK(VERIFY_RESULT(MakeBackupServiceProxy()).ListSnapshots(req, &resp, &controller));
  RETURN_NOT_OK(ResponseStatus(resp));
  LOG(INFO) << "Snapshots: " << resp.ShortDebugString();
  return std::move(resp.snapshots());
}

Status SnapshotTestUtil::VerifySnapshot(
    const TxnSnapshotId& snapshot_id, master::SysSnapshotEntryPB::State state,
    size_t expected_num_tablets, size_t expected_num_namespaces, size_t expected_num_tables) {
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
      case master::SysRowEntryType::TABLET:
        ++num_tablets;
        break;
      case master::SysRowEntryType::TABLE:
        ++num_tables;
        break;
      case master::SysRowEntryType::NAMESPACE:
        ++num_namespaces;
        break;
      default:
        return STATUS_FORMAT(
            IllegalState, "Unexpected entry type: $0",
            master::SysRowEntryType_Name(entry.type()));
    }
  }
  SCHECK_EQ(num_namespaces, expected_num_namespaces, IllegalState,
            "Wrong number of namespaces");
  SCHECK_EQ(num_tables, expected_num_tables, IllegalState, "Wrong number of tables");
  SCHECK_EQ(num_tablets, expected_num_tablets, IllegalState,
            "Wrong number of tablets");

  return Status::OK();
}

Status SnapshotTestUtil::WaitSnapshotInState(
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

Status SnapshotTestUtil::WaitSnapshotDone(
    const TxnSnapshotId& snapshot_id, MonoDelta duration) {
  return WaitSnapshotInState(snapshot_id, master::SysSnapshotEntryPB::COMPLETE, duration);
}

Result<TxnSnapshotRestorationId> SnapshotTestUtil::StartRestoration(
    const TxnSnapshotId& snapshot_id, HybridTime restore_at) {
  master::RestoreSnapshotRequestPB req;
  master::RestoreSnapshotResponsePB resp;

  rpc::RpcController controller;
  controller.set_timeout(60s);
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  if (restore_at) {
    req.set_restore_ht(restore_at.ToUint64());
  }
  RETURN_NOT_OK(VERIFY_RESULT(MakeBackupServiceProxy()).RestoreSnapshot(req, &resp, &controller));
  RETURN_NOT_OK(ResponseStatus(resp));
  return FullyDecodeTxnSnapshotRestorationId(resp.restoration_id());
}

Result<bool> SnapshotTestUtil::IsRestorationDone(const TxnSnapshotRestorationId& restoration_id) {
  master::ListSnapshotRestorationsRequestPB req;
  master::ListSnapshotRestorationsResponsePB resp;
  req.set_restoration_id(restoration_id.data(), restoration_id.size());

  auto deadline = CoarseMonoClock::now() + 60s;
  for (;;) {
    rpc::RpcController controller;
    controller.set_deadline(deadline);
    RETURN_NOT_OK(
        VERIFY_RESULT(MakeBackupServiceProxy()).ListSnapshotRestorations(req, &resp, &controller));
    LOG(INFO) << "Restoration: " << resp.ShortDebugString();
    if (!resp.has_status()) {
      break;
    }
    auto status = StatusFromPB(resp.status());
    if (!status.IsServiceUnavailable()) {
      return status;
    }
  }
  if (resp.restorations().size() != 1) {
    return STATUS_FORMAT(RuntimeError, "Wrong number of restorations, one expected but $0 found",
                         resp.restorations().size());
  }
  return resp.restorations(0).entry().state() == master::SysSnapshotEntryPB::RESTORED;
}

Status SnapshotTestUtil::RestoreSnapshot(
    const TxnSnapshotId& snapshot_id, HybridTime restore_at) {
  auto restoration_id = VERIFY_RESULT(StartRestoration(snapshot_id, restore_at));

  return WaitFor([this, &restoration_id] {
    return IsRestorationDone(restoration_id);
  }, kWaitTimeout * kTimeMultiplier, Format("Restoration $0 done", restoration_id));
}

Result<TxnSnapshotId> SnapshotTestUtil::StartSnapshot(const YBTableName& table_name) {
  return DoStartSnapshot([&table_name](master::CreateSnapshotRequestPB* req) {
    auto table = req->add_tables();
    table->set_table_name(table_name.table_name());
    table->mutable_namespace_()->set_name(table_name.namespace_name());
    table->mutable_namespace_()->set_database_type(table_name.namespace_type());
  });
}

Result<TxnSnapshotId> SnapshotTestUtil::StartSnapshot(const TableHandle& table) {
  return StartSnapshot(table.table()->id());
}

Result<TxnSnapshotId> SnapshotTestUtil::StartSnapshot(const TableId& table_id, bool imported) {
  return DoStartSnapshot([&table_id, imported](master::CreateSnapshotRequestPB* req) {
    req->add_tables()->set_table_id(table_id);
    if (imported) {
      req->set_imported(true);
    }
  });
}

template <class F>
Result<TxnSnapshotId> SnapshotTestUtil::DoStartSnapshot(const F& fill_tables) {
  rpc::RpcController controller;
  controller.set_timeout(60s);
  master::CreateSnapshotRequestPB req;
  req.set_transaction_aware(true);
  fill_tables(&req);
  master::CreateSnapshotResponsePB resp;
  RETURN_NOT_OK(VERIFY_RESULT(MakeBackupServiceProxy()).CreateSnapshot(req, &resp, &controller));
  RETURN_NOT_OK(ResponseStatus(resp));
  return FullyDecodeTxnSnapshotId(resp.snapshot_id());
}

Result<TxnSnapshotId> SnapshotTestUtil::CreateSnapshot(const TableId& table_id, bool imported) {
  TxnSnapshotId snapshot_id = VERIFY_RESULT(StartSnapshot(table_id, imported));
  RETURN_NOT_OK(WaitSnapshotDone(snapshot_id));
  return snapshot_id;
}

Result<TxnSnapshotId> SnapshotTestUtil::CreateSnapshot(const TableHandle& table) {
  return CreateSnapshot(table.table()->id());
}

Status SnapshotTestUtil::DeleteSnapshot(const TxnSnapshotId& snapshot_id) {
  master::DeleteSnapshotRequestPB req;
  master::DeleteSnapshotResponsePB resp;

  rpc::RpcController controller;
  controller.set_timeout(60s);
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  RETURN_NOT_OK(VERIFY_RESULT(MakeBackupServiceProxy()).DeleteSnapshot(req, &resp, &controller));
  RETURN_NOT_OK(ResponseStatus(resp));
  return Status::OK();
}

Status SnapshotTestUtil::WaitAllSnapshotsDeleted() {
  RETURN_NOT_OK(WaitFor([this]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(ListSnapshots());
    SCHECK_EQ(snapshots.size(), 1, IllegalState, "Wrong number of snapshots");
    if (snapshots[0].entry().state() == master::SysSnapshotEntryPB::DELETED) {
      return true;
    }
    SCHECK_EQ(snapshots[0].entry().state(), master::SysSnapshotEntryPB::DELETING, IllegalState,
              "Wrong snapshot state");
    return false;
  }, kWaitTimeout * kTimeMultiplier, "Complete delete snapshot"));
  return Status::OK();
}

Status SnapshotTestUtil::WaitAllSnapshotsCleaned() {
  return WaitFor([this]() -> Result<bool> {
    return VERIFY_RESULT(ListSnapshots()).empty();
  }, kWaitTimeout * kTimeMultiplier, "Snapshot cleanup");
}

Result<ImportedSnapshotData> SnapshotTestUtil::StartImportSnapshot(
    const master::SnapshotInfoPB& snapshot) {
  master::ImportSnapshotMetaRequestPB req;
  master::ImportSnapshotMetaResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(60s);

  *req.mutable_snapshot() = snapshot;

  RETURN_NOT_OK(
      VERIFY_RESULT(MakeBackupServiceProxy()).ImportSnapshotMeta(req, &resp, &controller));
  RETURN_NOT_OK(ResponseStatus(resp));
  LOG(INFO) << "Imported snapshot metadata: " << resp.DebugString();
  return resp.tables_meta();
}

Result<SnapshotScheduleId> SnapshotTestUtil::CreateSchedule(
    const TableHandle& table, const YQLDatabase db_type, const std::string& db_name,
    const MonoDelta interval, const MonoDelta retention) {
  return CreateSchedule(table, db_type, db_name, WaitSnapshot::kFalse, interval, retention);
}

Result<SnapshotScheduleId> SnapshotTestUtil::CreateSchedule(
    const TableHandle& table, const YQLDatabase db_type, const std::string& db_name,
    const WaitSnapshot wait_snapshot, const MonoDelta interval, const MonoDelta retention) {
  return CreateSchedule(table.table(), db_type, db_name, wait_snapshot, interval, retention);
}

Result<SnapshotScheduleId> SnapshotTestUtil::CreateSchedule(
    const YBTablePtr table, const YQLDatabase db_type, const std::string& db_name,
    const WaitSnapshot wait_snapshot, const MonoDelta interval, const MonoDelta retention) {
  rpc::RpcController controller;
  controller.set_timeout(60s);
  master::CreateSnapshotScheduleRequestPB req;
  auto& options = *req.mutable_options();
  options.set_interval_sec(interval.ToSeconds());
  options.set_retention_duration_sec(retention.ToSeconds());
  auto& tables = *options.mutable_filter()->mutable_tables()->mutable_tables();
  master::TableIdentifierPB* table_identifier = tables.Add();
  if (table != nullptr) {
    table_identifier->set_table_id(table->id());
  }
  master::NamespaceIdentifierPB* namespace_identifier = table_identifier->mutable_namespace_();
  namespace_identifier->set_database_type(db_type);
  namespace_identifier->set_name(db_name);
  master::CreateSnapshotScheduleResponsePB resp;
  RETURN_NOT_OK(
      VERIFY_RESULT(MakeBackupServiceProxy()).CreateSnapshotSchedule(req, &resp, &controller));
  RETURN_NOT_OK(ResponseStatus(resp));
  auto id = VERIFY_RESULT(FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id()));
  if (wait_snapshot) {
    RETURN_NOT_OK(WaitScheduleSnapshot(id));
  }
  return id;
}

Result<Schedules> SnapshotTestUtil::ListSchedules(const SnapshotScheduleId& id) {
  master::ListSnapshotSchedulesRequestPB req;
  master::ListSnapshotSchedulesResponsePB resp;

  if (!id.IsNil()) {
    req.set_snapshot_schedule_id(id.data(), id.size());
  }

  rpc::RpcController controller;
  controller.set_timeout(60s);
  RETURN_NOT_OK(
      VERIFY_RESULT(MakeBackupServiceProxy()).ListSnapshotSchedules(req, &resp, &controller));
  RETURN_NOT_OK(ResponseStatus(resp));
  LOG(INFO) << "Schedules: " << resp.ShortDebugString();
  return std::move(resp.schedules());
}

Result<TxnSnapshotId> SnapshotTestUtil::PickSuitableSnapshot(
      const SnapshotScheduleId& schedule_id, HybridTime hybrid_time) {
  auto schedules = VERIFY_RESULT(ListSchedules(schedule_id));
  SCHECK_EQ(schedules.size(), 1, IllegalState,
            Format("Expected exactly one schedule with id $0", schedule_id));
  const auto& schedule = schedules[0];
  for (const auto& snapshot : schedule.snapshots()) {
    auto prev_ht = HybridTime::FromPB(snapshot.entry().previous_snapshot_hybrid_time());
    auto cur_ht = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
    auto id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id()));
    if (hybrid_time > prev_ht && hybrid_time <= cur_ht) {
      return id;
    }
    LOG(INFO) << __func__ << " rejected " << id << " (" << prev_ht << "-" << cur_ht << "] for "
              << hybrid_time;
  }
  return STATUS_FORMAT(NotFound, "Not found suitable snapshot for $0", hybrid_time);
}

Status SnapshotTestUtil::WaitScheduleSnapshot(
    const SnapshotScheduleId& schedule_id, HybridTime min_hybrid_time) {
  return WaitScheduleSnapshot(schedule_id, std::numeric_limits<int>::max(), min_hybrid_time);
}

Status SnapshotTestUtil::WaitScheduleSnapshot(
    const SnapshotScheduleId& schedule_id, int max_snapshots,
    HybridTime min_hybrid_time) {
  return WaitFor([this, schedule_id, max_snapshots, min_hybrid_time]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(ListSnapshots());
    EXPECT_LE(snapshots.size(), max_snapshots);
    LOG(INFO) << "Snapshots: " << AsString(snapshots);
    for (const auto& snapshot : snapshots) {
      EXPECT_EQ(TryFullyDecodeSnapshotScheduleId(snapshot.entry().schedule_id()), schedule_id);
      if (snapshot.entry().state() == master::SysSnapshotEntryPB::COMPLETE
          && HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time()) >= min_hybrid_time) {
        return true;
      }
    }
      return false;
    },
    ((max_snapshots == 1) ? 0s : kSnapshotInterval) + kSnapshotInterval / 2,
    "Schedule snapshot");
}

} // namespace client
} // namespace yb
