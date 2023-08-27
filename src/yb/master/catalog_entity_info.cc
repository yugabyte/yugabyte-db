// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/master/catalog_entity_info.h"

#include <string>

#include "yb/common/colocated_util.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/dockv/partition.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_error.h"
#include "yb/master/master_util.h"
#include "yb/master/ts_descriptor.h"

#include "yb/gutil/map-util.h"
#include "yb/util/atomic.h"
#include "yb/util/flags/auto_flags.h"
#include "yb/util/format.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

using std::string;

using strings::Substitute;

DECLARE_int32(tserver_unresponsive_timeout_ms);

DEFINE_RUNTIME_AUTO_bool(
    use_parent_table_id_field, kLocalPersisted, false, true,
    "Whether to use the new schema for colocated tables based on the parent_table_id field.");
TAG_FLAG(use_parent_table_id_field, advanced);

namespace yb {
namespace master {

// ================================================================================================
// TabletReplica
// ================================================================================================

string TabletReplica::ToString() const {
  return Format(
      "{ ts_desc: $0, "
      "state: $1, "
      "role: $2, "
      "member_type: $3, "
      "should_disable_lb_move: $4, "
      "fs_data_dir: $5, "
      "total_space_used: $6, "
      "full_compaction_state: $7, "
      "last_full_compaction_time: $8, "
      "time since update: $9ms }",
      ts_desc->permanent_uuid(),
      tablet::RaftGroupStatePB_Name(state),
      PeerRole_Name(role),
      consensus::PeerMemberType_Name(member_type),
      should_disable_lb_move,
      fs_data_dir,
      drive_info.sst_files_size + drive_info.wal_files_size,
      tablet::FullCompactionState_Name(full_compaction_status.full_compaction_state),
      full_compaction_status.last_full_compaction_time,
      MonoTime::Now().GetDeltaSince(time_updated).ToMilliseconds());
}

void TabletReplica::UpdateFrom(const TabletReplica& source) {
  state = source.state;
  role = source.role;
  member_type = source.member_type;
  should_disable_lb_move = source.should_disable_lb_move;
  fs_data_dir = source.fs_data_dir;
  full_compaction_status = source.full_compaction_status;
  time_updated = MonoTime::Now();
}

void TabletReplica::UpdateDriveInfo(const TabletReplicaDriveInfo& info) {
  drive_info = info;
}

void TabletReplica::UpdateLeaderLeaseInfo(const TabletLeaderLeaseInfo& info) {
  const bool initialized = leader_lease_info.initialized;
  const auto old_lease_exp = leader_lease_info.ht_lease_expiration;
  leader_lease_info = info;
  leader_lease_info.ht_lease_expiration = std::max(old_lease_exp, info.ht_lease_expiration);
  leader_lease_info.initialized = initialized || info.initialized;
}

bool TabletReplica::IsStale() const {
  MonoTime now(MonoTime::Now());
  if (now.GetDeltaSince(time_updated).ToMilliseconds() >=
      GetAtomicFlag(&FLAGS_tserver_unresponsive_timeout_ms)) {
    return true;
  }
  return false;
}

bool TabletReplica::IsStarting() const {
  return (state == tablet::NOT_STARTED || state == tablet::BOOTSTRAPPING);
}

// ================================================================================================
// TabletInfo
// ================================================================================================

class TabletInfo::LeaderChangeReporter {
 public:
  explicit LeaderChangeReporter(TabletInfo* info)
      : info_(info), old_leader_(info->GetLeaderUnlocked()) {
  }

  ~LeaderChangeReporter() {
    auto new_leader = info_->GetLeaderUnlocked();
    if (old_leader_ != new_leader) {
      LOG(INFO) << "T " << info_->tablet_id() << ": Leader changed from "
                << yb::ToString(old_leader_) << " to " << yb::ToString(new_leader);
    }
  }
 private:
  TabletInfo* info_;
  TSDescriptor* old_leader_;
};

TabletInfo::TabletInfo(const scoped_refptr<TableInfo>& table, TabletId tablet_id)
    : tablet_id_(std::move(tablet_id)),
      table_(table),
      last_update_time_(MonoTime::Now()),
      reported_schema_version_({}) {
  // Have to pre-initialize to an empty map, in case of access before the first setter is called.
  replica_locations_ = std::make_shared<TabletReplicaMap>();
}

TabletInfo::~TabletInfo() {
}

void TabletInfo::SetReplicaLocations(
    std::shared_ptr<TabletReplicaMap> replica_locations) {
  std::lock_guard l(lock_);
  LeaderChangeReporter leader_change_reporter(this);
  last_update_time_ = MonoTime::Now();
  replica_locations_ = replica_locations;
}

Status TabletInfo::CheckRunning() const {
  if (!table()->is_running()) {
    return STATUS_EC_FORMAT(Expired, MasterError(MasterErrorPB::TABLE_NOT_RUNNING),
                            "Table is not running: $0", table()->ToStringWithState());
  }

  return Status::OK();
}

void TabletInfo::SetTableIds(std::vector<TableId>&& table_ids) {
  std::lock_guard l(lock_);
  table_ids_ = std::move(table_ids);
}

void TabletInfo::AddTableId(const TableId& table_id) {
  std::lock_guard l(lock_);
  table_ids_.push_back(table_id);
}

std::vector<TableId> TabletInfo::GetTableIds() const {
  std::lock_guard l(lock_);
  return table_ids_;
}

void TabletInfo::RemoveTableIds(const std::unordered_set<TableId>& tables_to_remove) {
  std::lock_guard l(lock_);
  std::vector<TableId> new_table_ids;
  new_table_ids.reserve(table_ids_.size());
  for (auto& table_id : table_ids_) {
    if (!tables_to_remove.contains(table_id)) {
      new_table_ids.push_back(std::move(table_id));
    }
  }
  table_ids_ = std::move(new_table_ids);
}

Status TabletInfo::GetLeaderNotFoundStatus() const {
  RETURN_NOT_OK(CheckRunning());

  return STATUS_FORMAT(
      NotFound,
      "No leader found for tablet $0 with $1 replicas: $2.",
      ToString(), replica_locations_->size(), *replica_locations_);
}

Result<TSDescriptor*> TabletInfo::GetLeader() const {
  std::lock_guard l(lock_);
  auto result = GetLeaderUnlocked();
  if (result) {
    return result;
  }
  return GetLeaderNotFoundStatus();
}

Result<TabletReplicaDriveInfo> TabletInfo::GetLeaderReplicaDriveInfo() const {
  std::lock_guard l(lock_);

  for (const auto& pair : *replica_locations_) {
    if (pair.second.role == PeerRole::LEADER) {
      return pair.second.drive_info;
    }
  }
  return GetLeaderNotFoundStatus();
}

// Return leader lease info of the replica with ts_uuid if it's is the current leader.
Result<TabletLeaderLeaseInfo> TabletInfo::GetLeaderLeaseInfoIfLeader(
    const std::string& ts_uuid) const {
  std::lock_guard l(lock_);

  auto it = replica_locations_->find(ts_uuid);
  if (it == replica_locations_->end() || it->second.role != PeerRole::LEADER) {
    return GetLeaderNotFoundStatus();
  }
  return it->second.leader_lease_info;
}

TSDescriptor* TabletInfo::GetLeaderUnlocked() const {
  for (const auto& pair : *replica_locations_) {
    if (pair.second.role == PeerRole::LEADER) {
      return pair.second.ts_desc;
    }
  }
  return nullptr;
}

std::shared_ptr<const TabletReplicaMap> TabletInfo::GetReplicaLocations() const {
  std::lock_guard l(lock_);
  return replica_locations_;
}

void TabletInfo::UpdateReplicaLocations(const TabletReplica& replica) {
  std::lock_guard l(lock_);
  LeaderChangeReporter leader_change_reporter(this);
  last_update_time_ = MonoTime::Now();
  // Make a new shared_ptr, copying the data, to ensure we don't race against access to data from
  // clients that already have the old shared_ptr.
  replica_locations_ = std::make_shared<TabletReplicaMap>(*replica_locations_);
  auto it = replica_locations_->find(replica.ts_desc->permanent_uuid());
  if (it == replica_locations_->end()) {
    LOG(INFO) << Format("TS $0 reported replica $1 but it does not exist in the replica map. "
        "Adding it to the map. Replica map before adding new replica: $2",
        replica.ts_desc->permanent_uuid(), replica, replica_locations_);
    return;
  }
  it->second.UpdateFrom(replica);
}

void TabletInfo::UpdateReplicaInfo(const std::string& ts_uuid,
                                   const TabletReplicaDriveInfo& drive_info,
                                   const TabletLeaderLeaseInfo& leader_lease_info) {
  std::lock_guard l(lock_);
  // Make a new shared_ptr, copying the data, to ensure we don't race against access to data from
  // clients that already have the old shared_ptr.
  replica_locations_ = std::make_shared<TabletReplicaMap>(*replica_locations_);
  auto it = replica_locations_->find(ts_uuid);
  if (it == replica_locations_->end()) {
    return;
  }
  it->second.UpdateDriveInfo(drive_info);
  it->second.UpdateLeaderLeaseInfo(leader_lease_info);
}

std::unordered_map<xrepl::StreamId, uint64_t> TabletInfo::GetReplicationStatus() {
  std::lock_guard l(lock_);
  return replication_stream_to_status_bitmask_;
}

void TabletInfo::set_last_update_time(const MonoTime& ts) {
  std::lock_guard l(lock_);
  last_update_time_ = ts;
}

MonoTime TabletInfo::last_update_time() const {
  std::lock_guard l(lock_);
  return last_update_time_;
}

bool TabletInfo::set_reported_schema_version(const TableId& table_id, uint32_t version) {
  std::lock_guard l(lock_);
  if (reported_schema_version_.count(table_id) == 0 ||
      version > reported_schema_version_[table_id]) {
    reported_schema_version_[table_id] = version;
    return true;
  }
  return false;
}

uint32_t TabletInfo::reported_schema_version(const TableId& table_id) {
  std::lock_guard l(lock_);
  if (reported_schema_version_.count(table_id) == 0) {
    return 0;
  }
  return reported_schema_version_[table_id];
}

void TabletInfo::SetInitiaLeaderElectionProtege(const std::string& protege_uuid) {
  std::lock_guard l(lock_);
  initial_leader_election_protege_ = protege_uuid;
}

std::string TabletInfo::InitiaLeaderElectionProtege() {
  std::lock_guard l(lock_);
  return initial_leader_election_protege_;
}

bool TabletInfo::colocated() const {
  return LockForRead()->pb.colocated();
}

string TabletInfo::ToString() const {
  return Substitute("$0 (table $1)", tablet_id_,
                    (table_ != nullptr ? table_->ToString() : "MISSING"));
}

void TabletInfo::RegisterLeaderStepDownFailure(const TabletServerId& dest_leader,
                                               MonoDelta time_since_stepdown_failure) {
  std::lock_guard l(lock_);
  leader_stepdown_failure_times_[dest_leader] = MonoTime::Now() - time_since_stepdown_failure;
}

void TabletInfo::GetLeaderStepDownFailureTimes(MonoTime forget_failures_before,
                                               LeaderStepDownFailureTimes* dest) {
  std::lock_guard l(lock_);
  for (auto iter = leader_stepdown_failure_times_.begin();
       iter != leader_stepdown_failure_times_.end(); ) {
    if (iter->second < forget_failures_before) {
      iter = leader_stepdown_failure_times_.erase(iter);
    } else {
      iter++;
    }
  }
  *dest = leader_stepdown_failure_times_;
}

void TabletInfo::UpdateReplicaFullCompactionStatus(
    const TabletServerId& ts_uuid, const FullCompactionStatus& full_compaction_status) {
  std::lock_guard l(lock_);
  // Make a new shared_ptr, copying the data, to ensure we don't race against access to data from
  // clients that already have the old shared_ptr.
  replica_locations_ = std::make_shared<TabletReplicaMap>(*replica_locations_);
  auto it = replica_locations_->find(ts_uuid);
  if (it == replica_locations_->end()) {
    return;
  }
  it->second.full_compaction_status = full_compaction_status;
}

void PersistentTabletInfo::set_state(SysTabletsEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

// ================================================================================================
// TableInfo
// ================================================================================================

TableInfo::TableInfo(TableId table_id,
                     bool colocated,
                     scoped_refptr<TasksTracker> tasks_tracker)
    : table_id_(std::move(table_id)),
      tasks_tracker_(tasks_tracker),
      colocated_(colocated) {
}

TableInfo::~TableInfo() {
}

const TableName TableInfo::name() const {
  return LockForRead()->pb.name();
}

bool TableInfo::is_running() const {
  return LockForRead()->is_running();
}

bool TableInfo::is_deleted() const {
  return LockForRead()->is_deleted();
}

bool TableInfo::IsPreparing() const {
  return LockForRead()->IsPreparing();
}

string TableInfo::ToString() const {
  return Substitute("$0 [id=$1]", LockForRead()->pb.name(), table_id_);
}

string TableInfo::ToStringWithState() const {
  auto l = LockForRead();
  return Substitute("$0 [id=$1, state=$2]",
      l->pb.name(), table_id_, SysTablesEntryPB::State_Name(l->pb.state()));
}

const NamespaceId TableInfo::namespace_id() const {
  return LockForRead()->namespace_id();
}

// namespace_name can be null if table was created on version < 2.3.0 (see GH17713/GH17712 for more
// details)
const NamespaceName TableInfo::namespace_name() const {
  return LockForRead()->namespace_name();
}

ColocationId TableInfo::GetColocationId() const {
  return LockForRead()->schema().colocated_table_id().colocation_id();
}

const Status TableInfo::GetSchema(Schema* schema) const {
  return SchemaFromPB(LockForRead()->schema(), schema);
}

bool TableInfo::has_pgschema_name() const {
  return LockForRead()->schema().has_pgschema_name();
}

const string TableInfo::pgschema_name() const {
  return LockForRead()->schema().pgschema_name();
}

bool TableInfo::has_pg_type_oid() const {
  for (const auto& col : LockForRead()->schema().columns()) {
    if (!col.has_pg_type_oid()) {
      return false;
    }
  }
  return true;
}

TableId TableInfo::matview_pg_table_id() const {
  return LockForRead()->pb.matview_pg_table_id();
}

bool TableInfo::is_matview() const {
  return LockForRead()->pb.is_matview();
}

TableId TableInfo::indexed_table_id() const {
  return LockForRead()->indexed_table_id();
}

bool TableInfo::is_local_index() const {
  auto l = LockForRead();
  return l->pb.has_index_info() ? l->pb.index_info().is_local()
                                : l->pb.is_local_index();
}

bool TableInfo::is_unique_index() const {
  auto l = LockForRead();
  return l->pb.has_index_info() ? l->pb.index_info().is_unique()
                                : l->pb.is_unique_index();
}

TableType TableInfo::GetTableType() const {
  return LockForRead()->pb.table_type();
}

bool TableInfo::IsBeingDroppedDueToDdlTxn(const std::string& txn_id_pb, bool txn_success) const {
  auto l = LockForRead();
  if (l->pb_transaction_id() != txn_id_pb) {
    return false;
  }
  // The table can be dropped in 2 cases due to a DDL:
  // 1. This table was created by a transaction that subsequently aborted.
  // 2. This is a successful transaction that DROPs the table.
  return (l->is_being_created_by_ysql_ddl_txn() && !txn_success) ||
         (l->is_being_deleted_by_ysql_ddl_txn() && txn_success);
}

void TableInfo::AddTablet(const TabletInfoPtr& tablet) {
  std::lock_guard l(lock_);
  AddTabletUnlocked(tablet);
}

void TableInfo::ReplaceTablet(const TabletInfoPtr& old_tablet, const TabletInfoPtr& new_tablet) {
  std::lock_guard l(lock_);
  auto it = partitions_.find(old_tablet->metadata().dirty().pb.partition().partition_key_start());
  if (it != partitions_.end() && it->second == old_tablet.get()) {
    partitions_.erase(it);
  }
  AddTabletUnlocked(new_tablet);
}


void TableInfo::AddTablets(const TabletInfos& tablets) {
  std::lock_guard l(lock_);
  for (const auto& tablet : tablets) {
    AddTabletUnlocked(tablet);
  }
}

void TableInfo::ClearTabletMaps(DeactivateOnly deactivate_only) {
  std::lock_guard l(lock_);
  partitions_.clear();
  if (!deactivate_only) {
    tablets_.clear();
  }
}

Result<TabletWithSplitPartitions> TableInfo::FindSplittableHashPartitionForStatusTable() const {
  std::lock_guard l(lock_);

  for (const auto& entry : partitions_) {
    const auto& tablet = entry.second;
    const auto& metadata = tablet->LockForRead();
    dockv::Partition partition;
    dockv::Partition::FromPB(metadata->pb.partition(), &partition);
    auto result = dockv::PartitionSchema::SplitHashPartitionForStatusTablet(partition);
    if (result) {
      return TabletWithSplitPartitions{tablet, result->first, result->second};
    }
  }

  return STATUS_FORMAT(NotFound, "Table $0 has no splittable hash partition", table_id_);
}

void TableInfo::AddStatusTabletViaSplitPartition(
    TabletInfoPtr old_tablet, const dockv::Partition& partition, const TabletInfoPtr& new_tablet) {
  std::lock_guard l(lock_);

  const auto& new_dirty = new_tablet->metadata().dirty();
  if (new_dirty.is_deleted()) {
    return;
  }

  auto old_lock = old_tablet->LockForWrite();
  auto old_partition = old_lock.mutable_data()->pb.mutable_partition();
  partition.ToPB(old_partition);
  old_lock.Commit();

  tablets_.emplace(new_tablet->id(), new_tablet.get());

  if (!new_dirty.is_hidden()) {
    const auto& new_partition_key = new_dirty.pb.partition().partition_key_end();
    partitions_.emplace(new_partition_key, new_tablet.get());
  }
}

void TableInfo::AddTabletUnlocked(const TabletInfoPtr& tablet) {
  const auto& dirty = tablet->metadata().dirty();
  if (dirty.is_deleted()) {
    return;
  }
  const auto& tablet_meta = dirty.pb;
  tablets_.emplace(tablet->id(), tablet.get());

  if (dirty.is_hidden()) {
    return;
  }

  const auto& partition_key_start = tablet_meta.partition().partition_key_start();
  auto p = partitions_.emplace(partition_key_start, tablet.get());
  if (p.second) {
    return;
  }

  auto it = p.first;
  auto old_tablet_lock = it->second->LockForRead();
  const auto old_split_depth = old_tablet_lock->pb.split_depth();
  if (tablet_meta.split_depth() > old_split_depth || old_tablet_lock->is_deleted()) {
    VLOG(1) << "Replacing tablet " << it->second->tablet_id()
            << " (split_depth = " << old_split_depth << ")"
            << " with tablet " << tablet->tablet_id()
            << " (split_depth = " << tablet_meta.split_depth() << ")";
    it->second = tablet.get();
    return;
  }

  LOG_IF(DFATAL, tablet_meta.split_depth() == old_split_depth)
      << "Two tablets with the same partition key start and split depth: "
      << tablet_meta.ShortDebugString() << " and " << old_tablet_lock->pb.ShortDebugString();

  // TODO: can we assert that the replaced tablet is not in Running state?
  // May be a little tricky since we don't know whether to look at its committed or
  // uncommitted state.
}

bool TableInfo::RemoveTablet(const TabletId& tablet_id, DeactivateOnly deactivate_only) {
  std::lock_guard l(lock_);
  return RemoveTabletUnlocked(tablet_id, deactivate_only);
}

bool TableInfo::RemoveTablets(const TabletInfos& tablets, DeactivateOnly deactivate_only) {
  std::lock_guard l(lock_);
  bool all_were_removed = true;
  for (const auto& tablet : tablets) {
    if (!RemoveTabletUnlocked(tablet->id(), deactivate_only)) {
      all_were_removed = false;
    }
  }
  return all_were_removed;
}

bool TableInfo::RemoveTabletUnlocked(const TabletId& tablet_id, DeactivateOnly deactivate_only) {
  auto it = tablets_.find(tablet_id);
  if (it == tablets_.end()) {
    return false;
  }
  bool result = false;
  auto partitions_it = partitions_.find(
      it->second->metadata().dirty().pb.partition().partition_key_start());
  if (partitions_it != partitions_.end() && partitions_it->second == it->second) {
    partitions_.erase(partitions_it);
    result = true;
  }
  if (!deactivate_only) {
    tablets_.erase(it);
  }
  return result;
}

TabletInfos TableInfo::GetTabletsInRange(const GetTableLocationsRequestPB* req) const {
  if (req->has_include_inactive() && req->include_inactive()) {
    return GetInactiveTabletsInRange(
        req->partition_key_start(), req->partition_key_end(),
        req->max_returned_locations());
  } else {
    return GetTabletsInRange(
        req->partition_key_start(), req->partition_key_end(),
        req->max_returned_locations());
  }
}

TabletInfos TableInfo::GetTabletsInRange(
    const std::string& partition_key_start, const std::string& partition_key_end,
    const int32_t max_returned_locations) const {
  TabletInfos ret;
  SharedLock<decltype(lock_)> l(lock_);
  decltype(partitions_)::const_iterator it, it_end;
  if (partition_key_start.empty()) {
    it = partitions_.begin();
  } else {
    it = partitions_.upper_bound(partition_key_start);
    if (it != partitions_.begin()) {
      --it;
    }
  }
  if (partition_key_end.empty()) {
    it_end = partitions_.end();
  } else {
    it_end = partitions_.upper_bound(partition_key_end);
  }
  ret.reserve(
      std::min(static_cast<size_t>(std::max(max_returned_locations, 0)), partitions_.size()));
  int32_t count = 0;
  for (; it != it_end && count < max_returned_locations; ++it) {
    ret.push_back(it->second);
    count++;
  }
  return ret;
}

TabletInfos TableInfo::GetInactiveTabletsInRange(
    const std::string& partition_key_start, const std::string& partition_key_end,
    const int32_t max_returned_locations) const {
  TabletInfos ret;
  SharedLock<decltype(lock_)> l(lock_);
  int32_t count = 0;
  ret.reserve(std::min(static_cast<size_t>(std::max(max_returned_locations, 0)), tablets_.size()));
  for (const auto& p : tablets_) {
    if (count >= max_returned_locations) {
      break;
    }
    if (!partition_key_start.empty() &&
        p.second->metadata().dirty().pb.partition().partition_key_start() < partition_key_start) {
      continue;
    }
    if (!partition_key_end.empty() &&
        p.second->metadata().dirty().pb.partition().partition_key_start() > partition_key_end) {
      continue;
    }
    ret.push_back(p.second);
    count++;
  }
  return ret;
}

bool TableInfo::IsAlterInProgress(uint32_t version) const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& e : partitions_) {
    if (e.second->reported_schema_version(table_id_) < version) {
      VLOG_WITH_PREFIX_AND_FUNC(3)
          << "ALTER in progress due to tablet "
          << e.second->ToString() << " because reported schema "
          << e.second->reported_schema_version(table_id_) << " < expected " << version;
      return true;
    }
  }
  return false;
}

bool TableInfo::AreAllTabletsHidden() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& e : tablets_) {
    if (!e.second->LockForRead()->is_hidden()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Not hidden tablet: " << e.second->ToString();
      return false;
    }
  }
  return true;
}

bool TableInfo::AreAllTabletsDeleted() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& e : tablets_) {
    if (!e.second->LockForRead()->is_deleted()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Not deleted tablet: " << e.second->ToString();
      return false;
    }
  }
  return true;
}

Status TableInfo::CheckAllActiveTabletsRunning() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& tablet_it : partitions_) {
    const auto& tablet = tablet_it.second;
    if (tablet->LockForRead()->pb.state() != SysTabletsEntryPB::RUNNING) {
      return STATUS_EC_FORMAT(IllegalState,
                              MasterError(MasterErrorPB::SPLIT_OR_BACKFILL_IN_PROGRESS),
                              "Found tablet that is not running, table_id: $0, tablet_id: $1",
                              id(), tablet->tablet_id());
    }
  }
  return Status::OK();
}

bool TableInfo::IsCreateInProgress() const {
  auto l = LockForRead();
  return l->pb.state() == SysTablesEntryPB::PREPARING;
}

bool TableInfo::AreAllTabletsRunning(const std::set<TabletId>& new_running_tablets) {
  SharedLock l(lock_);
  for (const auto& [key, tablet_info] : partitions_) {
    if (new_running_tablets.contains(tablet_info->id())) {
      continue;
    }
    if (!tablet_info->LockForRead()->is_running()) {
      return false;
    }
  }

  return true;
}

Status TableInfo::SetIsBackfilling() {
  const auto table_lock = LockForRead();
  std::lock_guard l(lock_);
  if (is_backfilling_) {
    return STATUS(AlreadyPresent, "Backfill already in progress", id(),
                  MasterError(MasterErrorPB::SPLIT_OR_BACKFILL_IN_PROGRESS));
  }

  is_backfilling_ = true;
  return Status::OK();
}

void TableInfo::SetCreateTableErrorStatus(const Status& status) {
  VLOG_WITH_FUNC(1) << status;
  std::lock_guard l(lock_);
  create_table_error_ = status;
}

Status TableInfo::GetCreateTableErrorStatus() const {
  SharedLock<decltype(lock_)> l(lock_);
  return create_table_error_;
}

std::size_t TableInfo::NumLBTasks() const {
  SharedLock<decltype(lock_)> l(lock_);
  return std::count_if(pending_tasks_.begin(),
                       pending_tasks_.end(),
                       [](auto task) { return task->started_by_lb(); });
}

std::size_t TableInfo::NumTasks() const {
  SharedLock<decltype(lock_)> l(lock_);
  return pending_tasks_.size();
}

bool TableInfo::HasTasks() const {
  SharedLock<decltype(lock_)> l(lock_);
  VLOG_WITH_PREFIX_AND_FUNC(3) << AsString(pending_tasks_);
  return !pending_tasks_.empty();
}

bool TableInfo::HasTasks(server::MonitoredTaskType type) const {
  SharedLock<decltype(lock_)> l(lock_);
  for (auto task : pending_tasks_) {
    if (task->type() == type) {
      return true;
    }
  }
  return false;
}

void TableInfo::AddTask(std::shared_ptr<server::MonitoredTask> task) {
  bool abort_task = false;
  {
    std::lock_guard l(lock_);
    if (!closing_) {
      pending_tasks_.insert(task);
      if (tasks_tracker_) {
        tasks_tracker_->AddTask(task);
      }
    } else {
      abort_task = true;
    }
  }
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  if (abort_task) {
    task->AbortAndReturnPrevState(STATUS(Expired, "Table closing"));
  }
}

bool TableInfo::RemoveTask(const std::shared_ptr<server::MonitoredTask>& task) {
  bool result;
  {
    std::lock_guard l(lock_);
    pending_tasks_.erase(task);
    result = pending_tasks_.empty();
  }
  VLOG(1) << "Removed task " << task.get() << " " << task->description();
  return result;
}

// Aborts tasks which have their rpc in progress, rest of them are aborted and also erased
// from the pending list.
void TableInfo::AbortTasks() {
  AbortTasksAndCloseIfRequested( /* close */ false);
}

void TableInfo::AbortTasksAndClose() {
  AbortTasksAndCloseIfRequested( /* close */ true);
}

void TableInfo::AbortTasksAndCloseIfRequested(bool close) {
  std::vector<std::shared_ptr<server::MonitoredTask>> abort_tasks;
  {
    std::lock_guard l(lock_);
    if (close) {
      closing_ = true;
    }
    abort_tasks.reserve(pending_tasks_.size());
    abort_tasks.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
  }
  if (abort_tasks.empty()) {
    return;
  }
  auto status = close ? STATUS(Expired, "Table closing") : STATUS(Aborted, "Table closing");
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  for (const auto& task : abort_tasks) {
    VLOG_WITH_FUNC(1)
        << (close ? "Close and abort" : "Abort") << " task " << task.get() << " "
        << task->description();
    task->AbortAndReturnPrevState(status);
  }
}

void TableInfo::WaitTasksCompletion() {
  const int kMaxWaitMs = 30000;
  int wait_time_ms = 5;
  while (1) {
    std::vector<std::shared_ptr<server::MonitoredTask>> waiting_on_for_debug;
    bool at_max_wait = wait_time_ms >= kMaxWaitMs;
    {
      SharedLock<decltype(lock_)> l(lock_);
      if (pending_tasks_.empty()) {
        break;
      } else if (VLOG_IS_ON(1) || at_max_wait) {
        waiting_on_for_debug.reserve(pending_tasks_.size());
        waiting_on_for_debug.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
      }
    }
    for (const auto& task : waiting_on_for_debug) {
      if (at_max_wait) {
        LOG(WARNING) << "Long wait for aborting task " << task.get() << " " << task->description();
      } else {
        VLOG(1) << "Waiting for aborting task " << task.get() << " " << task->description();
      }
    }
    base::SleepForMilliseconds(wait_time_ms);
    wait_time_ms = std::min(wait_time_ms * 5 / 4, kMaxWaitMs);
  }
}

std::unordered_set<std::shared_ptr<server::MonitoredTask>> TableInfo::GetTasks() const {
  SharedLock<decltype(lock_)> l(lock_);
  return pending_tasks_;
}

std::size_t TableInfo::NumPartitions() const {
  SharedLock<decltype(lock_)> l(lock_);
  return partitions_.size();
}

bool TableInfo::HasPartitions(const std::vector<PartitionKey> other) const {
  SharedLock<decltype(lock_)> l(lock_);
  if (partitions_.size() != other.size()) {
    return false;
  }
  int i = 0;
  for (const auto& entry : partitions_) {
    if (entry.first != other[i++]) {
      return false;
    }
  }
  return true;
}

TabletInfos TableInfo::GetTablets(IncludeInactive include_inactive) const {
  TabletInfos result;
  SharedLock<decltype(lock_)> l(lock_);
  if (include_inactive) {
    result.reserve(tablets_.size());
    for (const auto& p : tablets_) {
      result.push_back(p.second);
    }
  } else {
    result.reserve(partitions_.size());
    for (const auto& p : partitions_) {
      result.push_back(p.second);
    }
  }
  return result;
}

bool TableInfo::HasOutstandingSplits(bool wait_for_parent_deletion) const {
  SharedLock<decltype(lock_)> l(lock_);
  DCHECK(!colocated());
  std::unordered_set<TabletId> partitions_tablets;
  for (const auto& p : partitions_) {
    auto tablet_lock = p.second->LockForRead();
    if (tablet_lock->pb.has_split_parent_tablet_id() && !tablet_lock->is_running()) {
      YB_LOG_EVERY_N_SECS(INFO, 10) << "Tablet Splitting: Child tablet " << p.second->tablet_id()
                                   << " belonging to table " << id() << " is not yet running";
      return true;
    }
    if (wait_for_parent_deletion) {
      partitions_tablets.insert(p.second->tablet_id());
    }
  }
  if (!wait_for_parent_deletion) {
    return false;
  }

  for (const auto& p : tablets_) {
    // If any parents have not been deleted yet, the split is not yet complete.
    if (!partitions_tablets.contains(p.second->tablet_id())) {
      auto tablet_lock = p.second->LockForRead();
      if (!tablet_lock->is_deleted() && !tablet_lock->is_hidden()) {
        YB_LOG_EVERY_N_SECS(INFO, 10) << "Tablet Splitting: Parent tablet " << p.second->tablet_id()
                                     << " belonging to table " << id()
                                     << " is not yet deleted or hidden";
        return true;
      }
    }
  }
  YB_LOG_EVERY_N_SECS(INFO, 10) << "Tablet Splitting: Table "
                               << id() << " does not have any outstanding splits";
  return false;
}

TabletInfoPtr TableInfo::GetColocatedUserTablet() const {
  if (!IsColocatedUserTable()) {
    return nullptr;
  }
  SharedLock<decltype(lock_)> l(lock_);
  if (!tablets_.empty()) {
    return tablets_.begin()->second;
  }
  LOG(INFO) << "Colocated Tablet not found for table " << name();
  return nullptr;
}

qlexpr::IndexInfo TableInfo::GetIndexInfo(const TableId& index_id) const {
  auto l = LockForRead();
  for (const auto& index_info_pb : l->pb.indexes()) {
    if (index_info_pb.table_id() == index_id) {
      return qlexpr::IndexInfo(index_info_pb);
    }
  }
  return qlexpr::IndexInfo();
}

bool TableInfo::UsesTablespacesForPlacement() const {
  auto l = LockForRead();
  // Global transaction table is excluded due to not having a tablespace id set.
  bool is_transaction_table_using_tablespaces =
      l->pb.table_type() == TRANSACTION_STATUS_TABLE_TYPE &&
      l->pb.has_transaction_table_tablespace_id();
  bool is_regular_ysql_table =
      l->pb.table_type() == PGSQL_TABLE_TYPE &&
      l->namespace_id() != kPgSequencesDataNamespaceId &&
      !IsColocatedUserTable() &&
      !IsColocationParentTable();
  return is_transaction_table_using_tablespaces ||
         is_regular_ysql_table ||
         IsTablegroupParentTable();
}

bool TableInfo::IsColocationParentTable() const {
  return IsColocationParentTableId(table_id_);
}

bool TableInfo::IsColocatedDbParentTable() const {
  return IsColocatedDbParentTableId(table_id_);
}

bool TableInfo::IsTablegroupParentTable() const {
  return IsTablegroupParentTableId(table_id_);
}

bool TableInfo::IsColocatedUserTable() const {
  return colocated() && !IsColocationParentTable();
}

TablespaceId TableInfo::TablespaceIdForTableCreation() const {
  SharedLock<decltype(lock_)> l(lock_);
  return tablespace_id_for_table_creation_;
}

void TableInfo::SetTablespaceIdForTableCreation(const TablespaceId& tablespace_id) {
  std::lock_guard l(lock_);
  tablespace_id_for_table_creation_ = tablespace_id;
}

google::protobuf::RepeatedField<int> TableInfo::GetHostedStatefulServices() const {
  auto l = LockForRead();
  return l->pb.hosted_stateful_services();
}

bool TableInfo::AttachedYCQLIndexDeletionInProgress(const TableId& index_table_id) const {
  auto l = LockForRead();
  const auto& indices = l->pb.indexes();
  const auto index_info_it = std::find_if(
      indices.begin(), indices.end(), [&index_table_id](const IndexInfoPB& index_info) {
        return index_info.table_id() == index_table_id;
      });
  return // If the index has been already detached from the table:
         index_info_it == indices.end() ||
         // OR if the index is in the deletion process - it's visible via the permissions:
         index_info_it->index_permissions() >=
             IndexPermissions::INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING;
}

void PersistentTableInfo::set_state(SysTablesEntryPB::State state, const string& msg) {
  VLOG_WITH_FUNC(2) << "Setting state for " << name() << " to "
                    << SysTablesEntryPB::State_Name(state) << " reason: " << msg;
  pb.set_state(state);
  pb.set_state_msg(msg);
}

bool PersistentTableInfo::is_index() const {
  return !indexed_table_id().empty();
}

const std::string& PersistentTableInfo::indexed_table_id() const {
  static const std::string kEmptyString;
  return pb.has_index_info()
             ? pb.index_info().indexed_table_id()
             : pb.has_indexed_table_id() ? pb.indexed_table_id() : kEmptyString;
}

Result<bool> PersistentTableInfo::is_being_modified_by_ddl_transaction(
  const TransactionId& txn) const {
  return has_ysql_ddl_txn_verifier_state() &&
    txn == VERIFY_RESULT(FullyDecodeTransactionId(pb_transaction_id()));
}

bool IsReplicationInfoSet(const ReplicationInfoPB& replication_info) {
  const auto& live_placement_info = replication_info.live_replicas();
  if (!(live_placement_info.placement_blocks().empty() && live_placement_info.num_replicas() <= 0 &&
        live_placement_info.placement_uuid().empty()) ||
      !replication_info.read_replicas().empty() ||
      !replication_info.affinitized_leaders().empty() ||
      !replication_info.multi_affinitized_leaders().empty()) {
    return true;
  }
  return false;
}

// ================================================================================================
// DeletedTableInfo
// ================================================================================================

DeletedTableInfo::DeletedTableInfo(const TableInfo* table) : table_id_(table->id()) {
  auto tablets = table->GetTablets();

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto tablet_lock = tablet->LockForRead();
    auto replica_locations = tablet->GetReplicaLocations();

    for (const TabletReplicaMap::value_type& r : *replica_locations) {
      tablet_set_.insert(TabletSet::value_type(
          r.second.ts_desc->permanent_uuid(), tablet->id()));
    }
  }
}

std::size_t DeletedTableInfo::NumTablets() const {
  std::lock_guard l(lock_);
  return tablet_set_.size();
}

bool DeletedTableInfo::HasTablets() const {
  std::lock_guard l(lock_);
  return !tablet_set_.empty();
}

void DeletedTableInfo::DeleteTablet(const TabletKey& key) {
  std::lock_guard l(lock_);
  tablet_set_.erase(key);
}

void DeletedTableInfo::AddTabletsToMap(DeletedTabletMap* tablet_map) {
  std::lock_guard l(lock_);
  for (const TabletKey& key : tablet_set_) {
    tablet_map->insert(DeletedTabletMap::value_type(key, this));
  }
}

// ================================================================================================
// NamespaceInfo
// ================================================================================================

NamespaceInfo::NamespaceInfo(NamespaceId ns_id) : namespace_id_(std::move(ns_id)) {}

const NamespaceName NamespaceInfo::name() const {
  return LockForRead()->pb.name();
}

YQLDatabase NamespaceInfo::database_type() const {
  return LockForRead()->pb.database_type();
}

bool NamespaceInfo::colocated() const {
  return LockForRead()->pb.colocated();
}

::yb::master::SysNamespaceEntryPB_State NamespaceInfo::state() const {
  return LockForRead()->pb.state();
}

string NamespaceInfo::ToString() const {
  return Substitute("$0 [id=$1]", name(), namespace_id_);
}

// ================================================================================================
// UDTypeInfo
// ================================================================================================

UDTypeInfo::UDTypeInfo(UDTypeId udtype_id) : udtype_id_(std::move(udtype_id)) { }

const UDTypeName UDTypeInfo::name() const {
  return LockForRead()->pb.name();
}

const NamespaceId UDTypeInfo::namespace_id() const {
  return LockForRead()->pb.namespace_id();
}

int UDTypeInfo::field_names_size() const {
  return LockForRead()->pb.field_names_size();
}

const string UDTypeInfo::field_names(int index) const {
  return LockForRead()->pb.field_names(index);
}

int UDTypeInfo::field_types_size() const {
  return LockForRead()->pb.field_types_size();
}

const QLTypePB UDTypeInfo::field_types(int index) const {
  return LockForRead()->pb.field_types(index);
}

string UDTypeInfo::ToString() const {
  auto l = LockForRead();
  return Format("$0 [id=$1] {metadata=$2} ", name(), udtype_id_, l->pb);
}

DdlLogEntry::DdlLogEntry(
    HybridTime time, const TableId& table_id, const SysTablesEntryPB& table,
    const std::string& action) {
  pb_.set_time(time.ToUint64());
  pb_.set_table_type(table.table_type());
  pb_.set_namespace_name(table.namespace_name());
  pb_.set_namespace_id(table.namespace_id());
  pb_.set_table_name(table.name());
  pb_.set_table_id(table_id);
  pb_.set_action(action);
}

const DdlLogEntryPB& DdlLogEntry::old_pb() const {
  // Since DDL log entry are always added, we don't have previous PB for the same entry.
  static const DdlLogEntryPB kEmpty;
  return kEmpty;
}

const DdlLogEntryPB& DdlLogEntry::new_pb() const {
  return pb_;
}

std::string DdlLogEntry::id() const {
  return DocHybridTime(HybridTime(pb_.time()), kMaxWriteId).EncodedInDocDbFormat();
}

void XClusterSafeTimeInfo::Clear() {
  auto l = LockForWrite();
  l.mutable_data()->pb.Clear();
  l.Commit();
}

// ================================================================================================
// CDCStreamInfo
// ================================================================================================

const google::protobuf::RepeatedPtrField<std::string> CDCStreamInfo::table_id() const {
  return LockForRead()->pb.table_id();
}

const NamespaceId CDCStreamInfo::namespace_id() const {
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
// UniverseReplicationInfoBase
// ================================================================================================
Result<std::shared_ptr<CDCRpcTasks>> UniverseReplicationInfoBase::GetOrCreateCDCRpcTasks(
    google::protobuf::RepeatedPtrField<HostPortPB> producer_masters) {
  std::vector<HostPort> hp;
  HostPortsFromPBs(producer_masters, &hp);
  std::string master_addrs = HostPort::ToCommaSeparatedString(hp);

  std::lock_guard l(lock_);
  if (cdc_rpc_tasks_ != nullptr) {
    // Master Addresses changed, update YBClient with new retry logic.
    if (master_addrs_ != master_addrs) {
      RETURN_NOT_OK(cdc_rpc_tasks_->UpdateMasters(master_addrs));
      master_addrs_ = master_addrs;
    }
    return cdc_rpc_tasks_;
  }

  auto rpc_task =
      VERIFY_RESULT(CDCRpcTasks::CreateWithMasterAddrs(replication_group_id_, master_addrs));
  cdc_rpc_tasks_ = rpc_task;
  master_addrs_ = master_addrs;
  return rpc_task;
}

// ================================================================================================
// UniverseReplicationInfo
// ================================================================================================
std::string UniverseReplicationInfo::ToString() const {
  auto l = LockForRead();
  return strings::Substitute("$0 [data=$1] ", id(), l->pb.ShortDebugString());
}

void UniverseReplicationInfo::SetSetupUniverseReplicationErrorStatus(const Status& status) {
  std::lock_guard l(lock_);
  setup_universe_replication_error_ = status;
}

Status UniverseReplicationInfo::GetSetupUniverseReplicationErrorStatus() const {
  SharedLock<decltype(lock_)> l(lock_);
  return setup_universe_replication_error_;
}

void UniverseReplicationInfo::StoreReplicationError(
    const TableId& consumer_table_id,
    const xrepl::StreamId& stream_id,
    const ReplicationErrorPb error,
    const std::string& error_detail) {
  std::lock_guard l(lock_);
  table_replication_error_map_[consumer_table_id][stream_id][error] = error_detail;
}

void UniverseReplicationInfo::ClearReplicationError(
    const TableId& consumer_table_id,
    const xrepl::StreamId& stream_id,
    const ReplicationErrorPb error) {
  std::lock_guard l(lock_);

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

// ================================================================================================
// PersistentUniverseReplicationBootstrapInfo
// ================================================================================================
void PersistentUniverseReplicationBootstrapInfo::set_new_snapshot_objects(
    const NamespaceMap& namespace_map, const UDTypeMap& type_map,
    const ExternalTableSnapshotDataMap& tables_data) {
  SysUniverseReplicationBootstrapEntryPB::NewSnapshotObjectsPB new_snapshot_objects;
  for (const ExternalTableSnapshotDataMap::value_type& entry : tables_data) {
    const auto& data = entry.second;
    const TableId& old_id = entry.first;
    const TableId& new_id = data.new_table_id;
    const TableType& type = data.table_entry_pb.table_type();
    const SysTablesEntryPB& table_entry_pb = data.table_entry_pb;
    const auto& indexed_table_id =
        table_entry_pb.has_indexed_table_id() ? table_entry_pb.indexed_table_id() : "";

    SysUniverseReplicationBootstrapEntryPB::IdPairPB ids;
    ids.set_old_id(old_id);
    ids.set_new_id(new_id);

    auto table_data = new_snapshot_objects.mutable_tables()->Add();
    table_data->set_table_type(type);
    table_data->set_indexed_table_id(indexed_table_id);
    table_data->mutable_ids()->CopyFrom(ids);
  }

  for (const UDTypeMap::value_type& entry : type_map) {
    const UDTypeId& old_id = entry.first;
    const UDTypeId& new_id = entry.second.new_type_id;
    const bool existing = !entry.second.just_created;

    SysUniverseReplicationBootstrapEntryPB::IdPairPB ids;
    ids.set_old_id(old_id);
    ids.set_new_id(new_id);

    auto ud_type_data = new_snapshot_objects.mutable_ud_types()->Add();
    ud_type_data->set_existing(existing);
    ud_type_data->mutable_ids()->CopyFrom(ids);
  }

  for (const NamespaceMap::value_type& entry : namespace_map) {
    const NamespaceId& old_id = entry.first;
    const NamespaceId& new_id = entry.second.new_namespace_id;
    const YQLDatabase& db_type = entry.second.db_type;
    const bool existing = !entry.second.just_created;

    SysUniverseReplicationBootstrapEntryPB::IdPairPB ids;
    ids.set_old_id(old_id);
    ids.set_new_id(new_id);

    auto namespace_data = new_snapshot_objects.mutable_namespaces()->Add();
    namespace_data->set_existing(existing);
    namespace_data->set_db_type(db_type);
    namespace_data->mutable_ids()->CopyFrom(ids);
  }

  pb.mutable_new_snapshot_objects()->CopyFrom(new_snapshot_objects);
}

void PersistentUniverseReplicationBootstrapInfo::set_into_namespace_map(
    NamespaceMap* namespace_map) const {
  for (const auto& entry : pb.new_snapshot_objects().namespaces()) {
    const auto& ids = entry.ids();
    auto& namespace_entry = (*namespace_map)[ids.old_id()];
    namespace_entry.new_namespace_id = ids.new_id();
    namespace_entry.db_type = entry.db_type();
    namespace_entry.just_created = entry.existing();
  }
}

void PersistentUniverseReplicationBootstrapInfo::set_into_ud_type_map(UDTypeMap* type_map) const {
  for (const auto& entry : pb.new_snapshot_objects().ud_types()) {
    const auto& ids = entry.ids();
    auto& type_entry = (*type_map)[ids.old_id()];
    type_entry.new_type_id = ids.new_id();
    type_entry.just_created = entry.existing();
  }
}
void PersistentUniverseReplicationBootstrapInfo::set_into_tables_data(
    ExternalTableSnapshotDataMap* tables_data) const {
  for (const auto& entry : pb.new_snapshot_objects().tables()) {
    const auto& ids = entry.ids();
    auto& tables_entry = (*tables_data)[ids.old_id()];
    tables_entry.new_table_id = ids.new_id();
    tables_entry.table_entry_pb.set_table_type(entry.table_type());
    tables_entry.table_entry_pb.set_indexed_table_id(entry.indexed_table_id());
  }
}

// ================================================================================================
// UniverseReplicationBootstrapInfo
// ================================================================================================
std::string UniverseReplicationBootstrapInfo::ToString() const {
  auto l = LockForRead();
  return strings::Substitute("$0 [data=$1] ", id(), l->pb.ShortDebugString());
}

void UniverseReplicationBootstrapInfo::SetReplicationBootstrapErrorStatus(const Status& status) {
  std::lock_guard l(lock_);
  replication_bootstrap_error_ = status;
}


Status UniverseReplicationBootstrapInfo::GetReplicationBootstrapErrorStatus() const {
  SharedLock<decltype(lock_)> l(lock_);
  return replication_bootstrap_error_;
}

////////////////////////////////////////////////////////////
// SnapshotInfo
////////////////////////////////////////////////////////////

SnapshotInfo::SnapshotInfo(SnapshotId id) : snapshot_id_(std::move(id)) {}

SysSnapshotEntryPB::State SnapshotInfo::state() const {
  return LockForRead()->state();
}

const std::string SnapshotInfo::state_name() const {
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

}  // namespace master
}  // namespace yb
