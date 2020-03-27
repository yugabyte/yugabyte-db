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

#include "yb/common/wire_protocol.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/master.pb.h"

using std::set;

namespace yb {
namespace master {

// ================================================================================================
// CDCStreamInfo
// ================================================================================================

const TableId& CDCStreamInfo::table_id() const {
  auto l = LockForRead();
  return l->data().pb.table_id();
}

std::string CDCStreamInfo::ToString() const {
  auto l = LockForRead();
  return strings::Substitute("$0 [table=$1] {metadata=$2} ", id(), l->data().pb.table_id(),
                             l->data().pb.DebugString());
}

// ================================================================================================
// UniverseReplicationInfo
// ================================================================================================

Result<std::shared_ptr<CDCRpcTasks>> UniverseReplicationInfo::GetOrCreateCDCRpcTasks(
    google::protobuf::RepeatedPtrField<HostPortPB> producer_masters) {
  std::vector<HostPort> hp;
  HostPortsFromPBs(producer_masters, &hp);
  std::string master_addrs = HostPort::ToCommaSeparatedString(hp);

  std::lock_guard<decltype(lock_)> l(lock_);
  if (cdc_rpc_tasks_ != nullptr) {
    // Master Addresses changed, update YBClient with new retry logic.
    if (master_addrs_ != master_addrs) {
      if (cdc_rpc_tasks_->UpdateMasters(master_addrs).ok()) {
        master_addrs_ = master_addrs;
      }
    }
    return cdc_rpc_tasks_;
  }

  auto result = CDCRpcTasks::CreateWithMasterAddrs(producer_id_, master_addrs);
  if (result.ok()) {
    cdc_rpc_tasks_ = *result;
    master_addrs_ = master_addrs;
  }
  return result;
}

std::string UniverseReplicationInfo::ToString() const {
  auto l = LockForRead();
  return strings::Substitute("$0 [data=$1] ", id(),
                             l->data().pb.DebugString());
}

////////////////////////////////////////////////////////////
// SnapshotInfo
////////////////////////////////////////////////////////////

SnapshotInfo::SnapshotInfo(SnapshotId id) : snapshot_id_(std::move(id)) {}

SysSnapshotEntryPB::State SnapshotInfo::state() const {
  auto l = LockForRead();
  return l->data().state();
}

const std::string& SnapshotInfo::state_name() const {
  auto l = LockForRead();
  return l->data().state_name();
}

std::string SnapshotInfo::ToString() const {
  return YB_CLASS_TO_STRING(snapshot_id);
}

bool SnapshotInfo::IsCreateInProgress() const {
  auto l = LockForRead();
  return l->data().is_creating();
}

bool SnapshotInfo::IsRestoreInProgress() const {
  auto l = LockForRead();
  return l->data().is_restoring();
}

bool SnapshotInfo::IsDeleteInProgress() const {
  auto l = LockForRead();
  return l->data().is_deleting();
}

Status SnapshotInfo::AddEntries(const TableDescription& table_description) {
  SysSnapshotEntryPB& pb = mutable_metadata()->mutable_dirty()->pb;
  AddEntries(table_description, pb.mutable_entries(), pb.mutable_tablet_snapshots());
  return Status::OK();
}

void SnapshotInfo::AddEntries(
    const TableDescription& table_description,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>* tablet_infos) {
  // Note: SysSnapshotEntryPB includes PBs for stored (1) namespaces (2) tables (3) tablets.
  // Add namespace entry.
  SysRowEntry* entry = out->Add();
  {
    TRACE("Locking namespace");
    auto l = table_description.namespace_info->LockForRead();
    FillInfoEntry(*table_description.namespace_info, entry);
  }

  // Add table entry.
  entry = out->Add();
  {
    TRACE("Locking table");
    auto l = table_description.table_info->LockForRead();
    FillInfoEntry(*table_description.table_info, entry);
  }

  // Add tablet entries.
  for (const scoped_refptr<TabletInfo>& tablet : table_description.tablet_infos) {
    SysSnapshotEntryPB::TabletSnapshotPB* const tablet_info =
        tablet_infos ? tablet_infos->Add() : nullptr;
    entry = out->Add();

    TRACE("Locking tablet");
    auto l = tablet->LockForRead();

    if (tablet_info) {
      tablet_info->set_id(tablet->id());
      tablet_info->set_state(SysSnapshotEntryPB::CREATING);
    }

    FillInfoEntry(*tablet, entry);
  }
}

} // namespace master
} // namespace yb
