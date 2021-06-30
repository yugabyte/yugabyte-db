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

#include <string>
#include <mutex>

#include "yb/master/catalog_entity_info.h"
#include "yb/util/format.h"
#include "yb/util/locks.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/wire_protocol.h"

using std::string;

using strings::Substitute;

DECLARE_int32(tserver_unresponsive_timeout_ms);

namespace yb {
namespace master {

// ================================================================================================
// TabletReplica
// ================================================================================================

string TabletReplica::ToString() const {
  return Format("{ ts_desc: $0, state: $1, role: $2, member_type: $3, "
                "should_disable_lb_move: $4, total_space_used: $5, time since update: $6ms }",
                ts_desc->permanent_uuid(),
                tablet::RaftGroupStatePB_Name(state),
                consensus::RaftPeerPB_Role_Name(role),
                consensus::RaftPeerPB::MemberType_Name(member_type),
                should_disable_lb_move,
                drive_info.sst_files_size + drive_info.wal_files_size,
                MonoTime::Now().GetDeltaSince(time_updated).ToMilliseconds());
}

void TabletReplica::UpdateFrom(const TabletReplica& source) {
  state = source.state;
  role = source.role;
  member_type = source.member_type;
  should_disable_lb_move = source.should_disable_lb_move;
  time_updated = MonoTime::Now();
}

void TabletReplica::UpdateDriveInfo(const TabletReplicaDriveInfo& info) {
  drive_info = info;
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
  replica_locations_ = std::make_shared<TabletInfo::ReplicaMap>();
}

TabletInfo::~TabletInfo() {
}

void TabletInfo::SetReplicaLocations(
    std::shared_ptr<TabletInfo::ReplicaMap> replica_locations) {
  std::lock_guard<simple_spinlock> l(lock_);
  LeaderChangeReporter leader_change_reporter(this);
  last_update_time_ = MonoTime::Now();
  replica_locations_ = replica_locations;
}

CHECKED_STATUS TabletInfo::CheckRunning() const {
  if (!table()->is_running()) {
    return STATUS_FORMAT(Expired, "Table is not running: $0", table()->ToStringWithState());
  }

  return Status::OK();
}

CHECKED_STATUS TabletInfo::GetLeaderNotFoundStatus() const {
  RETURN_NOT_OK(CheckRunning());

  return STATUS_FORMAT(
      NotFound,
      "No leader found for tablet $0 with $1 replicas: $2.",
      ToString(), replica_locations_->size(), *replica_locations_);
}

Result<TSDescriptor*> TabletInfo::GetLeader() const {
  std::lock_guard<simple_spinlock> l(lock_);
  auto result = GetLeaderUnlocked();
  if (result) {
    return result;
  }
  return GetLeaderNotFoundStatus();
}

Result<TabletReplicaDriveInfo> TabletInfo::GetLeaderReplicaDriveInfo() const {
  std::lock_guard<simple_spinlock> l(lock_);

  for (const auto& pair : *replica_locations_) {
    if (pair.second.role == consensus::RaftPeerPB::LEADER) {
      return pair.second.drive_info;
    }
  }
  return GetLeaderNotFoundStatus();
}

TSDescriptor* TabletInfo::GetLeaderUnlocked() const {
  for (const auto& pair : *replica_locations_) {
    if (pair.second.role == consensus::RaftPeerPB::LEADER) {
      return pair.second.ts_desc;
    }
  }
  return nullptr;
}

std::shared_ptr<const TabletInfo::ReplicaMap> TabletInfo::GetReplicaLocations() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return replica_locations_;
}

void TabletInfo::UpdateReplicaLocations(const TabletReplica& replica) {
  std::lock_guard<simple_spinlock> l(lock_);
  LeaderChangeReporter leader_change_reporter(this);
  last_update_time_ = MonoTime::Now();
  // Make a new shared_ptr, copying the data, to ensure we don't race against access to data from
  // clients that already have the old shared_ptr.
  replica_locations_ = std::make_shared<TabletInfo::ReplicaMap>(*replica_locations_);
  auto it = replica_locations_->find(replica.ts_desc->permanent_uuid());
  if (it == replica_locations_->end()) {
    replica_locations_->emplace(replica.ts_desc->permanent_uuid(), replica);
    return;
  }
  it->second.UpdateFrom(replica);
}

void TabletInfo::UpdateReplicaDriveInfo(const std::string& ts_uuid,
                                        const TabletReplicaDriveInfo& drive_info) {
  std::lock_guard<simple_spinlock> l(lock_);
  // Make a new shared_ptr, copying the data, to ensure we don't race against access to data from
  // clients that already have the old shared_ptr.
  replica_locations_ = std::make_shared<TabletInfo::ReplicaMap>(*replica_locations_);
  auto it = replica_locations_->find(ts_uuid);
  if (it == replica_locations_->end()) {
    return;
  }
  it->second.UpdateDriveInfo(drive_info);
}

void TabletInfo::set_last_update_time(const MonoTime& ts) {
  std::lock_guard<simple_spinlock> l(lock_);
  last_update_time_ = ts;
}

MonoTime TabletInfo::last_update_time() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return last_update_time_;
}

bool TabletInfo::set_reported_schema_version(const TableId& table_id, uint32_t version) {
  std::lock_guard<simple_spinlock> l(lock_);
  if (reported_schema_version_.count(table_id) == 0 ||
      version > reported_schema_version_[table_id]) {
    reported_schema_version_[table_id] = version;
    return true;
  }
  return false;
}

uint32_t TabletInfo::reported_schema_version(const TableId& table_id) {
  std::lock_guard<simple_spinlock> l(lock_);
  if (reported_schema_version_.count(table_id) == 0) {
    return 0;
  }
  return reported_schema_version_[table_id];
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
  std::lock_guard<simple_spinlock> l(lock_);
  leader_stepdown_failure_times_[dest_leader] = MonoTime::Now() - time_since_stepdown_failure;
}

void TabletInfo::GetLeaderStepDownFailureTimes(MonoTime forget_failures_before,
                                               LeaderStepDownFailureTimes* dest) {
  std::lock_guard<simple_spinlock> l(lock_);
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

void PersistentTabletInfo::set_state(SysTabletsEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

// ================================================================================================
// TableInfo
// ================================================================================================

TableInfo::TableInfo(TableId table_id, scoped_refptr<TasksTracker> tasks_tracker)
    : table_id_(std::move(table_id)),
      tasks_tracker_(tasks_tracker) {
}

TableInfo::~TableInfo() {
}

const TableName TableInfo::name() const {
  return LockForRead()->pb.name();
}

bool TableInfo::is_running() const {
  return LockForRead()->is_running();
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

const NamespaceName TableInfo::namespace_name() const {
  return LockForRead()->namespace_name();
}

const Status TableInfo::GetSchema(Schema* schema) const {
  return SchemaFromPB(LockForRead()->schema(), schema);
}

bool TableInfo::colocated() const {
  return LockForRead()->pb.colocated();
}

std::string TableInfo::indexed_table_id() const {
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

bool TableInfo::RemoveTablet(const string& partition_key_start) {
  std::lock_guard<decltype(lock_)> l(lock_);
  return EraseKeyReturnValuePtr(&tablet_map_, partition_key_start) != nullptr;
}

void TableInfo::AddTablet(TabletInfo *tablet) {
  std::lock_guard<decltype(lock_)> l(lock_);
  AddTabletUnlocked(tablet);
}

void TableInfo::AddTablets(const vector<TabletInfo*>& tablets) {
  std::lock_guard<decltype(lock_)> l(lock_);
  for (TabletInfo *tablet : tablets) {
    AddTabletUnlocked(tablet);
  }
}

void TableInfo::AddTabletUnlocked(TabletInfo* tablet) {
  const auto& tablet_meta = tablet->metadata().dirty().pb;
  const auto& partition_key_start = tablet_meta.partition().partition_key_start();
  auto it = tablet_map_.find(partition_key_start);
  if (it == tablet_map_.end()) {
    tablet_map_.emplace(partition_key_start, tablet);
  } else {
    const auto old_split_depth = it->second->LockForRead()->pb.split_depth();
    if (tablet_meta.split_depth() < old_split_depth) {
      return;
    }
    VLOG(1) << "Replacing tablet " << it->second->tablet_id()
            << " (split_depth = " << old_split_depth << ")"
            << " with tablet " << tablet->tablet_id()
            << " (split_depth = " << tablet_meta.split_depth() << ")";
    it->second = tablet;
    // TODO: can we assert that the replaced tablet is not in Running state?
    // May be a little tricky since we don't know whether to look at its committed or
    // uncommitted state.
  }
}

void TableInfo::GetTabletsInRange(const GetTableLocationsRequestPB* req, TabletInfos* ret) const {
  GetTabletsInRange(
      req->partition_key_start(), req->partition_key_end(), ret, req->max_returned_locations());
}

void TableInfo::GetTabletsInRange(
    const std::string& partition_key_start, const std::string& partition_key_end,
    TabletInfos* ret, const int32_t max_returned_locations) const {
  SharedLock<decltype(lock_)> l(lock_);

  TableInfo::TabletInfoMap::const_iterator it, it_end;
  if (partition_key_start.empty()) {
    it = tablet_map_.begin();
  } else {
    it = tablet_map_.upper_bound(partition_key_start);
    if (it != tablet_map_.begin()) {
      --it;
    }
  }
  if (partition_key_end.empty()) {
    it_end = tablet_map_.end();
  } else {
    it_end = tablet_map_.upper_bound(partition_key_end);
  }

  int32_t count = 0;
  for (; it != it_end && count < max_returned_locations; ++it) {
    ret->push_back(make_scoped_refptr(it->second));
    count++;
  }
}

bool TableInfo::IsAlterInProgress(uint32_t version) const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
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

bool TableInfo::HasTablets() const {
  SharedLock<decltype(lock_)> l(lock_);
  return !tablet_map_.empty();
}

bool TableInfo::AreAllTabletsHidden() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& e : tablet_map_) {
    if (!e.second->LockForRead()->is_hidden()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Not hidden tablet: " << e.second->ToString();
      return false;
    }
  }
  return true;
}

bool TableInfo::AreAllTabletsDeleted() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& e : tablet_map_) {
    if (!e.second->LockForRead()->is_deleted()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Not deleted tablet: " << e.second->ToString();
      return false;
    }
  }
  return true;
}

bool TableInfo::IsCreateInProgress() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    if (!e.second->LockForRead()->is_running()) {
      return true;
    }
  }
  return false;
}

Status TableInfo::SetIsBackfilling() {
  const auto table_lock = LockForRead();
  std::lock_guard<decltype(lock_)> l(lock_);
  if (is_backfilling_) {
    return STATUS(AlreadyPresent, "Backfill already in progress", id(),
                  MasterError(MasterErrorPB::SPLIT_OR_BACKFILL_IN_PROGRESS));
  }

  for (const auto& tablet_it : tablet_map_) {
    const auto& tablet = tablet_it.second;
    if (tablet->LockForRead()->pb.state() != SysTabletsEntryPB::RUNNING) {
      return STATUS_EC_FORMAT(IllegalState,
                              MasterError(MasterErrorPB::SPLIT_OR_BACKFILL_IN_PROGRESS),
                              "Some tablets are not running, table_id: $0 tablet_id: $1",
                              id(), tablet->tablet_id());
    }
  }
  is_backfilling_ = true;
  return Status::OK();
}

void TableInfo::SetCreateTableErrorStatus(const Status& status) {
  std::lock_guard<decltype(lock_)> l(lock_);
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

bool TableInfo::HasTasks(MonitoredTask::Type type) const {
  SharedLock<decltype(lock_)> l(lock_);
  for (auto task : pending_tasks_) {
    if (task->type() == type) {
      return true;
    }
  }
  return false;
}

void TableInfo::AddTask(std::shared_ptr<MonitoredTask> task) {
  bool abort_task = false;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
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

bool TableInfo::RemoveTask(const std::shared_ptr<MonitoredTask>& task) {
  bool result;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
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
  std::vector<std::shared_ptr<MonitoredTask>> abort_tasks;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
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
  int wait_time = 5;
  while (1) {
    std::vector<std::shared_ptr<MonitoredTask>> waiting_on_for_debug;
    {
      SharedLock<decltype(lock_)> l(lock_);
      if (pending_tasks_.empty()) {
        break;
      } else if (VLOG_IS_ON(1)) {
        waiting_on_for_debug.reserve(pending_tasks_.size());
        waiting_on_for_debug.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
      }
    }
    for (const auto& task : waiting_on_for_debug) {
      VLOG(1) << "Waiting for Aborting task " << task.get() << " " << task->description();
    }
    base::SleepForMilliseconds(wait_time);
    wait_time = std::min(wait_time * 5 / 4, 10000);
  }
}

std::unordered_set<std::shared_ptr<MonitoredTask>> TableInfo::GetTasks() {
  SharedLock<decltype(lock_)> l(lock_);
  return pending_tasks_;
}

std::size_t TableInfo::NumTablets() const {
  SharedLock<decltype(lock_)> l(lock_);
  return tablet_map_.size();
}

void TableInfo::GetAllTablets(TabletInfos *ret) const {
  ret->clear();
  SharedLock<decltype(lock_)> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    ret->push_back(make_scoped_refptr(e.second));
  }
}

TabletInfoPtr TableInfo::GetColocatedTablet() const {
  SharedLock<decltype(lock_)> l(lock_);
  if (colocated()) {
    for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
      return make_scoped_refptr(e.second);
    }
  }
  return nullptr;
}

IndexInfo TableInfo::GetIndexInfo(const TableId& index_id) const {
  auto l = LockForRead();
  for (const auto& index_info_pb : l->pb.indexes()) {
    if (index_info_pb.table_id() == index_id) {
      return IndexInfo(index_info_pb);
    }
  }
  return IndexInfo();
}

bool TableInfo::UsesTablespacesForPlacement() const {
  auto l = LockForRead();
  return l->pb.table_type() == PGSQL_TABLE_TYPE && !l->pb.colocated() &&
         l->namespace_id() != kPgSequencesDataNamespaceId;
}

TablespaceId TableInfo::TablespaceIdForTableCreation() const {
  SharedLock<decltype(lock_)> l(lock_);
  return tablespace_id_for_table_creation_;
}

void TableInfo::SetTablespaceIdForTableCreation(const TablespaceId& tablespace_id) {
  std::lock_guard<decltype(lock_)> l(lock_);
  tablespace_id_for_table_creation_ = tablespace_id;
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


// ================================================================================================
// DeletedTableInfo
// ================================================================================================

DeletedTableInfo::DeletedTableInfo(const TableInfo* table) : table_id_(table->id()) {
  vector<scoped_refptr<TabletInfo>> tablets;
  table->GetAllTablets(&tablets);

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto tablet_lock = tablet->LockForRead();
    auto replica_locations = tablet->GetReplicaLocations();

    for (const TabletInfo::ReplicaMap::value_type& r : *replica_locations) {
      tablet_set_.insert(TabletSet::value_type(
          r.second.ts_desc->permanent_uuid(), tablet->id()));
    }
  }
}

std::size_t DeletedTableInfo::NumTablets() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return tablet_set_.size();
}

bool DeletedTableInfo::HasTablets() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return !tablet_set_.empty();
}

void DeletedTableInfo::DeleteTablet(const TabletKey& key) {
  std::lock_guard<simple_spinlock> l(lock_);
  tablet_set_.erase(key);
}

void DeletedTableInfo::AddTabletsToMap(DeletedTabletMap* tablet_map) {
  std::lock_guard<simple_spinlock> l(lock_);
  for (const TabletKey& key : tablet_set_) {
    tablet_map->insert(DeletedTabletMap::value_type(key, this));
  }
}

// ================================================================================================
// NamespaceInfo
// ================================================================================================

NamespaceInfo::NamespaceInfo(NamespaceId ns_id) : namespace_id_(std::move(ns_id)) {}

const NamespaceName& NamespaceInfo::name() const {
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
// TablegroupInfo
// ================================================================================================

TablegroupInfo::TablegroupInfo(TablegroupId tablegroup_id, NamespaceId namespace_id) :
                               tablegroup_id_(tablegroup_id), namespace_id_(namespace_id) {}

void TablegroupInfo::AddChildTable(const TableId& table_id) {
  std::lock_guard<simple_spinlock> l(lock_);
  if (table_set_.find(table_id) != table_set_.end()) {
    LOG(WARNING) << "Table ID " << table_id << " already in Tablegroup " << tablegroup_id_;
  } else {
    table_set_.insert(table_id);
  }
}

void TablegroupInfo::DeleteChildTable(const TableId& table_id) {
  std::lock_guard<simple_spinlock> l(lock_);
  if (table_set_.find(table_id) != table_set_.end()) {
    table_set_.erase(table_id);
  } else {
    LOG(WARNING) << "Table ID " << table_id << " not found in Tablegroup " << tablegroup_id_;
  }
}

bool TablegroupInfo::HasChildTables() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return !table_set_.empty();
}


std::size_t TablegroupInfo::NumChildTables() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return table_set_.size();
}

// ================================================================================================
// UDTypeInfo
// ================================================================================================

UDTypeInfo::UDTypeInfo(UDTypeId udtype_id) : udtype_id_(std::move(udtype_id)) { }

const UDTypeName& UDTypeInfo::name() const {
  return LockForRead()->pb.name();
}

const NamespaceName& UDTypeInfo::namespace_id() const {
  return LockForRead()->pb.namespace_id();
}

int UDTypeInfo::field_names_size() const {
  return LockForRead()->pb.field_names_size();
}

const string& UDTypeInfo::field_names(int index) const {
  return LockForRead()->pb.field_names(index);
}

int UDTypeInfo::field_types_size() const {
  return LockForRead()->pb.field_types_size();
}

const QLTypePB& UDTypeInfo::field_types(int index) const {
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

}  // namespace master
}  // namespace yb
