// Copyright (c) YugaByte, Inc.

#include "yb/tools/yb-admin_client.h"

#include <iostream>

#include "yb/common/wire_protocol.h"
#include "yb/util/cast.h"
#include "yb/util/env.h"
#include "yb/util/protobuf_util.h"

namespace yb {
namespace tools {
namespace enterprise {

using std::cout;
using std::endl;
using std::string;

using google::protobuf::RepeatedPtrField;

using client::YBTableName;
using rpc::RpcController;

using master::CreateSnapshotRequestPB;
using master::CreateSnapshotResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::RestoreSnapshotRequestPB;
using master::RestoreSnapshotResponsePB;
using master::ImportSnapshotMetaRequestPB;
using master::ImportSnapshotMetaResponsePB;
using master::ImportSnapshotMetaResponsePB_TableMetaPB;
using master::SnapshotInfoPB;
using master::IdPairPB;
using master::IsCreateTableDoneRequestPB;
using master::IsCreateTableDoneResponsePB;

using namespace std::literals;

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

Status ClusterAdminClient::CreateSnapshotMetaFile(const string& snapshot_id,
                                                  const string& file_name) {
  RpcController rpc;
  rpc.set_timeout(timeout_);
  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  req.set_snapshot_id(snapshot_id);
  RETURN_NOT_OK(master_backup_proxy_->ListSnapshots(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  DCHECK_EQ(resp.snapshots_size(), 1);
  const SnapshotInfoPB& snapshot = resp.snapshots(0);

  DCHECK_EQ(snapshot.id(), snapshot_id);
  cout << "Exporting snapshot " << snapshot_id << " ("
       << snapshot.entry().state() << ") to file " << file_name << endl;

  string str;
  if (!snapshot.SerializeToString(&str)) {
    return STATUS_FORMAT(
        InternalError, "Failed snapshot serialization to string: $0", snapshot_id);
  }

  const Slice slice(util::to_uchar_ptr(str.data()), str.size());
  RETURN_NOT_OK(WriteStringToFile(Env::Default(), slice, file_name));
  cout << "Snapshot meta data was saved into file: " << file_name << endl;

  return Status::OK();
}

Status ClusterAdminClient::ImportSnapshotMetaFile(const string& file_name) {
  faststring fstr;
  RETURN_NOT_OK(ReadFileToString(Env::Default(), file_name, &fstr));

  cout << "Read snapshot meta file " << file_name << endl;

  ImportSnapshotMetaRequestPB req;
  ImportSnapshotMetaResponsePB resp;

  SnapshotInfoPB* const snapshot_info = req.mutable_snapshot();
  if (!snapshot_info->ParseFromString(fstr.ToString())) {
    return STATUS_FORMAT(
        RuntimeError, "Failed to parse snapshot meta file: $0", file_name);
  }

  cout << "Importing snapshot " << snapshot_info->id()
       << " (" << snapshot_info->entry().state() << ")" << endl;

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_backup_proxy_->ImportSnapshotMeta(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  cout << "Successfully applied snapshot." << endl
       << "Object\t\t Old ID\t\t\t\t\t New ID" << endl;

  const RepeatedPtrField<ImportSnapshotMetaResponsePB_TableMetaPB>& tables_meta =
      resp.tables_meta();
  IsCreateTableDoneRequestPB wait_req;
  IsCreateTableDoneResponsePB wait_resp;
  CreateSnapshotRequestPB snapshot_req;
  CreateSnapshotResponsePB snapshot_resp;

  for (int i = 0; i < tables_meta.size(); ++i) {
    const ImportSnapshotMetaResponsePB_TableMetaPB& table_meta = tables_meta.Get(i);
    const string& new_table_id = table_meta.table_ids().new_id();

    cout << "Keyspace\t " << table_meta.namespace_ids().old_id() << "\t "
         << table_meta.namespace_ids().new_id() << endl
         << "Table\t\t " << table_meta.table_ids().old_id()  << "\t " << new_table_id << endl;

    const RepeatedPtrField<IdPairPB>& tablets_map = table_meta.tablets_ids();
    for (int j = 0; j < tablets_map.size(); ++j) {
      const IdPairPB& pair = tablets_map.Get(j);
      cout << "Tablet" << j << "  \t " << pair.old_id() << "\t " << pair.new_id() << endl;
    }

    // Wait for table creation.
    wait_req.mutable_table()->set_table_id(new_table_id);

    for (int k = 0; k < 20; ++k) {
      rpc.Reset();
      RETURN_NOT_OK(master_proxy_->IsCreateTableDone(wait_req, &wait_resp, &rpc));

      if (wait_resp.done()) {
        break;
      } else {
        LOG(INFO) << "Waiting for table " << new_table_id << "...";
        std::this_thread::sleep_for(1s);
      }
    }

    if (!wait_resp.done()) {
      return STATUS_FORMAT(
          TimedOut, "Table creation timeout: table id $0", new_table_id);
    }

    snapshot_req.mutable_tables()->Add()->set_table_id(new_table_id);
  }

  // Create new snapshot.
  rpc.Reset();
  RETURN_NOT_OK(master_backup_proxy_->CreateSnapshot(snapshot_req, &snapshot_resp, &rpc));
  cout << "Snapshot\t " << snapshot_info->id() << "\t "
       << snapshot_resp.snapshot_id() << endl;

  return Status::OK();
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
