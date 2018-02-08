// Copyright (c) YugaByte, Inc.

#include "yb/tools/yb-admin_client.h"

#include <iostream>

#include "yb/common/wire_protocol.h"
#include "yb/util/cast.h"
#include "yb/util/env.h"
#include "yb/util/pb_util.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/string_util.h"

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
using master::SysNamespaceEntryPB;
using master::SysRowEntry;
using master::SysTablesEntryPB;
using master::IdPairPB;
using master::IsCreateTableDoneRequestPB;
using master::IsCreateTableDoneResponsePB;
using yb::util::to_uchar_ptr;

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
    cout << RightPadToUuidWidth("Snapshot UUID") << kColumnSep
         << "State" << endl;
  } else {
    cout << "No snapshots" << endl;
  }

  for (int i = 0; i < resp.snapshots_size(); ++i) {
    cout << resp.snapshots(i).id() << kColumnSep
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

  const SnapshotInfoPB* snapshot = nullptr;
  for (const auto& snapshot_entry : resp.snapshots()) {
    if (snapshot_entry.id() == snapshot_id) {
      snapshot = &snapshot_entry;
      break;
    }
  }
  if (!snapshot) {
    return STATUS_FORMAT(
        InternalError, "Response contained $0 entries but no entry for snapshot '$1'",
        resp.snapshots_size(), snapshot_id);
  }
  if (resp.snapshots_size() > 1) {
    LOG(WARNING) << "Requested snapshot metadata for snapshot '" << snapshot_id << "', but got "
                 << resp.snapshots_size() << " snapshots in the response";
  }

  cout << "Exporting snapshot " << snapshot_id << " ("
       << snapshot->entry().state() << ") to file " << file_name << endl;

  string str;
  if (!snapshot->SerializeToString(&str)) {
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

  string keyspace_name;
  string table_name;
  for (const SysRowEntry& entry : snapshot_info->entry().entries()) {
    switch (entry.type()) {
      case SysRowEntry::NAMESPACE: {
        SysNamespaceEntryPB meta;
        const string &data = entry.data();
        RETURN_NOT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));
        keyspace_name = meta.name();
        break;
      }
      case SysRowEntry::TABLE: {
        SysTablesEntryPB meta;
        const string &data = entry.data();
        RETURN_NOT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));
        table_name = meta.name();
        break;
      }
      default:
        break;
    }
  }

  if (keyspace_name.empty() || table_name.empty()) {
    return STATUS(IllegalState,
                  "Could not find table name or keyspace name from snapshot metadata");
  }

  cout << "Table being imported: " << keyspace_name << "." << table_name << endl;

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_backup_proxy_->ImportSnapshotMeta(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  const int kObjectColumnWidth = 16;
  const auto pad_object_type = [](const string& s) {
    return RightPadToWidth(s, kObjectColumnWidth);
  };

  cout << "Successfully applied snapshot." << endl
       << pad_object_type("Object") << kColumnSep
       << RightPadToUuidWidth("Old ID") << kColumnSep
       << RightPadToUuidWidth("New ID") << endl;

  const RepeatedPtrField<ImportSnapshotMetaResponsePB_TableMetaPB>& tables_meta =
      resp.tables_meta();
  IsCreateTableDoneRequestPB wait_req;
  IsCreateTableDoneResponsePB wait_resp;
  CreateSnapshotRequestPB snapshot_req;
  CreateSnapshotResponsePB snapshot_resp;

  for (int i = 0; i < tables_meta.size(); ++i) {
    const ImportSnapshotMetaResponsePB_TableMetaPB& table_meta = tables_meta.Get(i);
    const string& new_table_id = table_meta.table_ids().new_id();

    cout << pad_object_type("Keyspace") << kColumnSep
         << table_meta.namespace_ids().old_id() << kColumnSep
         << table_meta.namespace_ids().new_id() << endl;

    cout << pad_object_type("Table") << kColumnSep
         << table_meta.table_ids().old_id() << kColumnSep
         << new_table_id << endl;

    const RepeatedPtrField<IdPairPB>& tablets_map = table_meta.tablets_ids();
    for (int j = 0; j < tablets_map.size(); ++j) {
      const IdPairPB& pair = tablets_map.Get(j);
      cout << pad_object_type(Format("Tablet $0", j)) << kColumnSep
           << pair.old_id() << kColumnSep
           << pair.new_id() << endl;
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
  cout << pad_object_type("Snapshot") << kColumnSep
       << snapshot_info->id() << kColumnSep
       << snapshot_resp.snapshot_id() << endl;

  return Status::OK();
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
