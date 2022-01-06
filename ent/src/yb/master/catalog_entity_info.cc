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

#include "yb/master/catalog_entity_info.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/cdc_rpc_tasks.h"

#include "yb/util/result.h"
#include "yb/util/shared_lock.h"
#include "yb/util/trace.h"

using std::set;

namespace yb {
namespace master {

// ================================================================================================
// CDCStreamInfo
// ================================================================================================

const TableId& CDCStreamInfo::table_id() const {
  return LockForRead()->pb.table_id();
}

std::string CDCStreamInfo::ToString() const {
  auto l = LockForRead();
  return Format("$0 [table=$1] {metadata=$2} ", id(), l->pb.table_id(), l->pb.ShortDebugString());
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
  return strings::Substitute("$0 [data=$1] ", id(), l->pb.ShortDebugString());
}

void UniverseReplicationInfo::SetSetupUniverseReplicationErrorStatus(const Status& status) {
  std::lock_guard<decltype(lock_)> l(lock_);
  setup_universe_replication_error_ = status;
}

Status UniverseReplicationInfo::GetSetupUniverseReplicationErrorStatus() const {
  SharedLock<decltype(lock_)> l(lock_);
  return setup_universe_replication_error_;
}

////////////////////////////////////////////////////////////
// SnapshotInfo
////////////////////////////////////////////////////////////

SnapshotInfo::SnapshotInfo(SnapshotId id) : snapshot_id_(std::move(id)) {}

SysSnapshotEntryPB::State SnapshotInfo::state() const {
  return LockForRead()->state();
}

const std::string& SnapshotInfo::state_name() const {
  return LockForRead()->state_name();
}

std::string SnapshotInfo::ToString() const {
  return YB_CLASS_TO_STRING(snapshot_id);
}

bool SnapshotInfo::IsCreateInProgress() const {
  return LockForRead()->is_creating();
}

bool SnapshotInfo::IsRestoreInProgress() const {
  return LockForRead()->is_restoring();
}

bool SnapshotInfo::IsDeleteInProgress() const {
  return LockForRead()->is_deleting();
}

void SnapshotInfo::AddEntries(
    const TableDescription& table_description, std::unordered_set<NamespaceId>* added_namespaces) {
  SysSnapshotEntryPB& pb = mutable_metadata()->mutable_dirty()->pb;
  AddEntries(
      table_description, pb.mutable_entries(), pb.mutable_tablet_snapshots(), added_namespaces);
}

void SnapshotInfo::AddEntries(
    const TableDescription& table_description,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>* tablet_infos,
    std::unordered_set<NamespaceId>* added_namespaces) {
  // Note: SysSnapshotEntryPB includes PBs for stored (1) namespaces (2) tables (3) tablets.
  // Add namespace entry.
  if (added_namespaces->emplace(table_description.namespace_info->id()).second) {
    TRACE("Locking namespace");
    AddInfoEntry(table_description.namespace_info.get(), out);
  }

  // Add table entry.
  {
    TRACE("Locking table");
    AddInfoEntry(table_description.table_info.get(), out);
  }

  // Add tablet entries.
  for (const scoped_refptr<TabletInfo>& tablet : table_description.tablet_infos) {
    SysSnapshotEntryPB::TabletSnapshotPB* const tablet_info =
        tablet_infos ? tablet_infos->Add() : nullptr;

    TRACE("Locking tablet");
    auto l = AddInfoEntry(tablet.get(), out);

    if (tablet_info) {
      tablet_info->set_id(tablet->id());
      tablet_info->set_state(SysSnapshotEntryPB::CREATING);
    }
  }
}

} // namespace master
} // namespace yb
