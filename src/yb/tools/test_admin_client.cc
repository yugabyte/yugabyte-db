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

#include "yb/common/wire_protocol.h"

#include <google/protobuf/repeated_field.h>

#include "yb/client/client.h"

#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_client.proxy.h"

#include "yb/tools/test_admin_client.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"

namespace yb {

using namespace std::chrono_literals;

Status TestAdminClient::SplitTablet(const TabletId& tablet_id) {
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_id);
  master::SplitTabletResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>();
  RETURN_NOT_OK(proxy.SplitTablet(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status TestAdminClient::SplitTablet(
    const client::YBTableName& table, const std::optional<TabletId>& tablet_id) {
  RETURN_NOT_OK(ybclient_->FlushTables({table}, false, 30, false));
  TabletId to_split;
  if (tablet_id) {
    to_split = *tablet_id;
  } else {
    auto tablets = VERIFY_RESULT(GetTabletLocations(table));
    to_split = tablets[0].tablet_id();
  }
  return SplitTablet(to_split);
}

Status TestAdminClient::SplitTablet(
    const std::string& ns, const std::string& table, const std::optional<TabletId>& tablet_id) {
  auto tname = VERIFY_RESULT(GetTableName(ns, table));
  return SplitTablet(tname, tablet_id);
}

Status TestAdminClient::SplitTabletAndWait(
    const client::YBTableName& table, bool wait_for_parent_deletion,
    const std::optional<TabletId>& tablet_id) {
  RETURN_NOT_OK(SplitTablet(table, tablet_id));
  return (WaitFor(
      [&]() -> Result<bool> { return IsTabletSplittingComplete(wait_for_parent_deletion); },
      60s * kTimeMultiplier, "Wait for ongoing splits to finish."));
}

Status TestAdminClient::SplitTabletAndWait(
    const std::string& ns, const std::string& table, bool wait_for_parent_deletion,
    const std::optional<TabletId>& tablet_id) {
  auto tname = VERIFY_RESULT(GetTableName(ns, table));
  return SplitTabletAndWait(tname, wait_for_parent_deletion, tablet_id);
}

Result<client::YBTableName> TestAdminClient::GetTableName(
    const std::string& ns, const std::string& table) {
  auto table_names = VERIFY_RESULT(ybclient_->ListTables(table));
  for (const auto& table_name : table_names) {
    if (table_name.namespace_name() == ns) {
      return table_name;
    }
  }
  return STATUS(NotFound, Format("Couldn't find table in namespace $0 with name $1", ns, table));
}

Result<bool> TestAdminClient::IsTabletSplittingComplete(bool wait_for_parent_deletion) {
  master::IsTabletSplittingCompleteRequestPB req;
  master::IsTabletSplittingCompleteResponsePB resp;
  rpc::RpcController rpc;
  req.set_wait_for_parent_deletion(wait_for_parent_deletion);
  rpc.set_timeout(30s * kTimeMultiplier);
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>();
  RETURN_NOT_OK(proxy.IsTabletSplittingComplete(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return resp.is_tablet_splitting_complete();
}

Result<std::vector<master::TabletLocationsPB>> TestAdminClient::GetTabletLocations(
    const client::YBTableName& table) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  RETURN_NOT_OK(ybclient_->GetTablets(table, 0, &tablets, nullptr));
  return std::vector<master::TabletLocationsPB>(tablets.begin(), tablets.end());
}

Result<std::vector<master::TabletLocationsPB>> TestAdminClient::GetTabletLocations(
    const std::string& ns, const std::string& table) {
  auto tname = VERIFY_RESULT(GetTableName(ns, table));
  return GetTabletLocations(tname);
}

Status TestAdminClient::WaitForTabletPostSplitCompacted(
    size_t tserver_idx, const TabletId& tablet_id) {
  const auto ts = cluster_->tablet_server(tserver_idx);
  return WaitFor(
      [&]() -> Result<bool> {
        auto resp = VERIFY_RESULT(cluster_->GetTabletStatus(*ts, tablet_id));
        if (resp.has_error()) {
          LOG(ERROR) << "Peer " << ts->uuid() << " tablet " << tablet_id
                     << " error: " << resp.error().status().ShortDebugString();
          return false;
        }
        return resp.tablet_status().parent_data_compacted();
      },
      30s * kTimeMultiplier,
      Format("Waiting for tablet $0 post split compacted on tserver $1", tablet_id, ts->id()));
}

Status TestAdminClient::FlushTable(const std::string& ns, const std::string& table) {
  auto tname = VERIFY_RESULT(GetTableName(ns, table));
  return ybclient_->FlushTables({tname}, false, 30, false);
}

Status TestAdminClient::DeleteSnapshotAndWait(const TxnSnapshotId& snapshot_id) {
  master::DeleteSnapshotRequestPB req;
  req.mutable_snapshot_id()->assign(snapshot_id.AsSlice().cdata(), snapshot_id.size());
  master::DeleteSnapshotResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterBackupProxy>();
  RETURN_NOT_OK(proxy.DeleteSnapshot(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  // Wait for snapshot to get deleted.
  return WaitForSnapshotComplete(snapshot_id, true /* check_deleted */);
}

Result<TxnSnapshotId> TestAdminClient::CreateSnapshotAndWait(
    const SnapshotScheduleId& schedule_id, const master::TableIdentifierPB& tables,
    const std::optional<int32_t> retention_duration_hours) {
  TxnSnapshotId snapshot_id = VERIFY_RESULT(CreateSnapshot(
      schedule_id, tables, retention_duration_hours));
  RETURN_NOT_OK(WaitForSnapshotComplete(snapshot_id));
  return snapshot_id;
}

Result<TxnSnapshotId> TestAdminClient::CreateSnapshot(
    const SnapshotScheduleId& schedule_id, const master::TableIdentifierPB& tables,
    const std::optional<int32_t> retention_duration_hours) {
  master::CreateSnapshotRequestPB req;
  master::CreateSnapshotResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  if (schedule_id) {
    req.set_schedule_id(schedule_id.data(), schedule_id.size());
  } else {
    req.add_tables()->CopyFrom(tables);
    req.set_transaction_aware(true);
    if (retention_duration_hours) {
      req.set_retention_duration_hours(*retention_duration_hours);
    }
  }
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterBackupProxy>();
  RETURN_NOT_OK(proxy.CreateSnapshot(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return FullyDecodeTxnSnapshotId(resp.snapshot_id());
}

Status TestAdminClient::WaitForSnapshotComplete(
    const TxnSnapshotId& snapshot_id, bool check_deleted) {
  return WaitFor(
      [&]() -> Result<bool> {
        master::ListSnapshotsRequestPB req;
        master::ListSnapshotsResponsePB resp;
        rpc::RpcController rpc;
        rpc.set_timeout(30s * kTimeMultiplier);
        req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
        auto proxy = cluster_->GetLeaderMasterProxy<master::MasterBackupProxy>();
        Status s = proxy.ListSnapshots(req, &resp, &rpc);
        // If snapshot is cleaned up and we are waiting for a delete
        // then succeed this call.
        if (check_deleted && !s.ok() && s.IsNotFound()) {
          return true;
        }
        if (resp.has_error()) {
          Status s = StatusFromPB(resp.error().status());
          if (check_deleted && s.IsNotFound()) {
            return true;
          }
          return s;
        }
        if (resp.snapshots_size() != 1) {
          return STATUS(
              IllegalState, Format("There should be exactly one snapshot of id $0", snapshot_id));
        }
        if (check_deleted) {
          return resp.snapshots(0).entry().state() == master::SysSnapshotEntryPB::DELETED;
        }
        return resp.snapshots(0).entry().state() == master::SysSnapshotEntryPB::COMPLETE;
      },
      30s * kTimeMultiplier, "Waiting for snapshot to complete");
}

}  // namespace yb
