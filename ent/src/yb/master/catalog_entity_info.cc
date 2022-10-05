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

const google::protobuf::RepeatedPtrField<std::string>& CDCStreamInfo::table_id() const {
  return LockForRead()->pb.table_id();
}

const NamespaceId& CDCStreamInfo::namespace_id() const {
  return LockForRead()->pb.namespace_id();
}

std::string CDCStreamInfo::ToString() const {
  auto l = LockForRead();
  if (l->pb.has_namespace_id()) {
    return Format(
        "$0 [namespace=$1] {metadata=$2} ", id(), l->pb.namespace_id(), l->pb.ShortDebugString());
  }
  return Format("$0 [table=$1] {metadata=$2} ", id(), l->pb.table_id(0), l->pb.ShortDebugString());
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

void UniverseReplicationInfo::StoreReplicationError(
    const TableId& consumer_table_id,
    const CDCStreamId& stream_id,
    const ReplicationErrorPb error,
    const std::string& error_detail) {
  std::lock_guard<decltype(lock_)> l(lock_);
  table_replication_error_map_[consumer_table_id][stream_id][error] = error_detail;
}

void UniverseReplicationInfo::ClearReplicationError(
    const TableId& consumer_table_id,
    const CDCStreamId& stream_id,
    const ReplicationErrorPb error) {
  std::lock_guard<decltype(lock_)> l(lock_);

  if (table_replication_error_map_.count(consumer_table_id) == 0 ||
      table_replication_error_map_[consumer_table_id].count(stream_id) == 0 ||
      table_replication_error_map_[consumer_table_id][stream_id].count(error) == 0) {
    return;
  }

  table_replication_error_map_[consumer_table_id][stream_id].erase(error);

  if (table_replication_error_map_[consumer_table_id][stream_id].empty()) {
    table_replication_error_map_[consumer_table_id].erase(stream_id);
  }

  if (table_replication_error_map_[consumer_table_id].empty()) {
    table_replication_error_map_.erase(consumer_table_id);
  }
}

UniverseReplicationInfo::TableReplicationErrorMap
UniverseReplicationInfo::GetReplicationErrors() const {
  SharedLock<decltype(lock_)> l(lock_);
  return table_replication_error_map_;
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

} // namespace master
} // namespace yb
