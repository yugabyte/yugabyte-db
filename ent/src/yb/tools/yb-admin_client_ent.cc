// Copyright (c) YugaByte, Inc.

#include "yb/tools/yb-admin_client.h"

#include <iostream>

#include "yb/common/wire_protocol.h"
#include "yb/util/protobuf_util.h"

namespace yb {
namespace tools {
namespace enterprise {

using std::cout;
using std::endl;
using std::string;

using client::YBTableName;
using rpc::RpcController;

using master::CreateSnapshotRequestPB;
using master::CreateSnapshotResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::RestoreSnapshotRequestPB;
using master::RestoreSnapshotResponsePB;

PB_ENUM_FORMATTERS(yb::master::SysSnapshotEntryPB::State);

Status ClusterAdminClient::Init() {
  RETURN_NOT_OK(super::Init());
  DCHECK(initted_);

  master_backup_proxy_.reset(new master::MasterBackupServiceProxy(messenger_, leader_sock_));
  return Status::OK();
}

Status ClusterAdminClient::ListSnapshots() {
  RpcController rpc;
  rpc.set_timeout(timeout_);
  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(master_backup_proxy_->ListSnapshots(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (resp.has_current_snapshot_id()) {
    cout << "Current snapshot id: " << resp.current_snapshot_id() << endl;
  }

  if (resp.snapshots_size()) {
    cout << "\tSnapshot UUID\t\t  State" << endl;
  } else {
    cout << "No snapshots" << endl;
  }

  for (int i = 0; i < resp.snapshots_size(); ++i) {
    cout << resp.snapshots(i).id() << "  "
         << resp.snapshots(i).entry().state() << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::CreateSnapshot(const YBTableName& table_name) {
  RpcController rpc;
  rpc.set_timeout(timeout_);
  CreateSnapshotRequestPB req;
  CreateSnapshotResponsePB resp;
  table_name.SetIntoTableIdentifierPB(req.add_tables());
  RETURN_NOT_OK(master_backup_proxy_->CreateSnapshot(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  cout << "Started snapshot creation: " << resp.snapshot_id() << endl;
  return Status::OK();
}

Status ClusterAdminClient::RestoreSnapshot(const string& snapshot_id) {
  RpcController rpc;
  rpc.set_timeout(timeout_);

  RestoreSnapshotRequestPB req;
  RestoreSnapshotResponsePB resp;
  req.set_snapshot_id(snapshot_id);
  RETURN_NOT_OK(master_backup_proxy_->RestoreSnapshot(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  cout << "Started restoring snapshot: " << snapshot_id << endl;
  return Status::OK();
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
