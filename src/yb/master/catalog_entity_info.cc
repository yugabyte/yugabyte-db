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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/cdc/xcluster_types.h"

#include "yb/common/colocated_util.h"
#include "yb/common/common_consensus_util.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/opid_util.h"

#include "yb/dockv/partition.h"

#include "yb/master/catalog_manager_util.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_error.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster_rpc_tasks.h"

#include "yb/util/atomic.h"
#include "yb/util/flags/auto_flags.h"
#include "yb/util/format.h"
#include "yb/util/oid_generator.h"
#include "yb/util/status_format.h"

using std::string;

using strings::Substitute;

DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_bool(cdcsdk_enable_dynamic_tables_disable_option);
DECLARE_uint64(master_ysql_operation_lease_ttl_ms);

DEFINE_RUNTIME_AUTO_bool(
    use_parent_table_id_field, kLocalPersisted, false, true,
    "Whether to use the new schema for colocated tables based on the parent_table_id field.");
TAG_FLAG(use_parent_table_id_field, advanced);

namespace yb::master {

namespace {

Result<TabletInfoPtr> PromoteTabletPointer(const std::weak_ptr<TabletInfo>& tablet) {
  if (auto p = tablet.lock()) {
    return p;
  } else {
    return STATUS(
        IllegalState,
        "Tablet objects backing this table have been freed. This table object is possibly stale, "
        "from a previous load of the sys catalog.");
  }
}

} // namespace

// ================================================================================================
// TabletReplica
// ================================================================================================

string TabletReplica::ToString() const {
  auto shared_desc_p = ts_desc.lock();
  const auto& ts_uuid = shared_desc_p ? shared_desc_p->id() : "<TS_REMOVED>";
  return Format(
      "{ ts_desc: $0, "
      "state: $1, "
      "role: $2, "
      "member_type: $3, "
      "should_disable_lb_move: $4, "
      "fs_data_dir: $5, "
      "total_space_used (excluding snapshots): $6, "
      "total_space_used (including snapshots): $7, "
      "full_compaction_state: $8, "
      "last_full_compaction_time: $9, "
      "time since update: $10ms }",
      ts_uuid,
      tablet::RaftGroupStatePB_Name(state),
      PeerRole_Name(role),
      consensus::PeerMemberType_Name(member_type),
      should_disable_lb_move,
      fs_data_dir,
      drive_info.sst_files_size + drive_info.wal_files_size,
      drive_info.total_size,
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
  leader_lease_info.ht_lease_expiration = 0;
  if (info.leader_lease_status == consensus::LeaderLeaseStatus::HAS_LEASE) {
    if (old_lease_exp == consensus::kInfiniteHybridTimeLeaseExpiration) {
      // It's originally RF-1, so there are two possibilities:
      // 1. It's still RF-1, and the new lease expiration is the same as the old one.
      // 2. It's changed to RF>1, and the new lease expiration is less than the old one.
      // In both cases, we can accept the new lease expiration.
      leader_lease_info.ht_lease_expiration = info.ht_lease_expiration;
    } else {
      leader_lease_info.ht_lease_expiration = std::max(info.ht_lease_expiration, old_lease_exp);
    }
  }
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
    if (new_leader && old_leader_) {
      if (old_leader_.get()->id() != new_leader.get()->id()) {
        LOG(INFO) << "T " << info_->tablet_id() << ": Leader changed from "
                  << yb::ToString(old_leader_) << " to " << yb::ToString(new_leader);
      }
    } else {
      auto old_leader_freed = !old_leader_.ok() && old_leader_.status().IsIllegalState();
      auto new_leader_freed = !new_leader.ok() && new_leader.status().IsIllegalState();
      if (old_leader_freed || new_leader_freed) {
        LOG(WARNING) << Format(
                            "TSDescriptor object freed but references to this TS remain in tablet "
                            "$0 ($1 leader of this tablet)",
                            info_->id(), old_leader_freed ? "old" : "new");
      }
    }
  }

 private:
  TabletInfo* info_;
  Result<TSDescriptorPtr> old_leader_;
};

TabletInfo::TabletInfo(const TableInfoPtr& table, TabletId tablet_id)
    : tablet_id_(std::move(tablet_id)),
      table_(table),
      last_update_time_(MonoTime::Now()),
      last_time_with_valid_leader_(last_update_time_) {
}

TabletInfo::~TabletInfo() = default;

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
  VLOG_WITH_FUNC(2) << "Tablet " << tablet_id_ << " table ids: " << AsString(table_ids_);
}

void TabletInfo::AddTableId(const TableId& table_id) {
  std::lock_guard l(lock_);
  table_ids_.push_back(table_id);
  VLOG_WITH_FUNC(2) << "Tablet " << tablet_id_ << " table ids: " << AsString(table_ids_);
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

Result<TSDescriptorPtr> TabletInfo::GetLeader() const {
  std::lock_guard l(lock_);
  return GetLeaderUnlocked();
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

Result<TSDescriptorPtr> TabletInfo::GetLeaderUnlocked() const {
  for (const auto& pair : *replica_locations_) {
    if (pair.second.role == PeerRole::LEADER) {
      auto desc = pair.second.ts_desc.lock();
      if (desc) {
        return desc;
      }
      return STATUS_FORMAT(
          IllegalState, "TSDescriptor for ts $0, currently leader of tablet $1, has been freed",
          pair.first, id());
    }
  }
  return GetLeaderNotFoundStatus();
}

std::shared_ptr<const TabletReplicaMap> TabletInfo::GetReplicaLocations() const {
  std::lock_guard l(lock_);
  return replica_locations_;
}

void TabletInfo::UpdateReplicaLocations(const std::string& ts_uuid, const TabletReplica& replica) {
  std::lock_guard l(lock_);
  LeaderChangeReporter leader_change_reporter(this);
  last_update_time_ = MonoTime::Now();
  // Make a new shared_ptr, copying the data, to ensure we don't race against access to data from
  // clients that already have the old shared_ptr.
  replica_locations_ = std::make_shared<TabletReplicaMap>(*replica_locations_);
  auto it = replica_locations_->find(ts_uuid);
  if (it == replica_locations_->end()) {
    LOG(INFO) << Format("TS $0 reported replica $1 but it does not exist in the replica map. "
        "Adding it to the map. Replica map before adding new replica: $2",
        ts_uuid, replica, replica_locations_);
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
  VLOG_WITH_FUNC(3) << "T " << id() << " P " << ts_uuid << ": " << drive_info.ToString();
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

void TabletInfo::UpdateLastTimeWithValidLeader() {
  std::lock_guard l(lock_);
  last_time_with_valid_leader_ = MonoTime::Now();
}

MonoTime TabletInfo::last_time_with_valid_leader() const {
  std::lock_guard l(lock_);
  return last_time_with_valid_leader_;
}

void TabletInfo::TEST_set_last_time_with_valid_leader(const MonoTime& time) {
  std::lock_guard l(lock_);
  last_time_with_valid_leader_ = time;
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

TableInfo::TableInfo(TableId table_id, bool colocated, scoped_refptr<TasksTracker> tasks_tracker)
    : CatalogEntityWithTasks(std::move(tasks_tracker)),
      table_id_(std::move(table_id)),
      colocated_(colocated) {}

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

bool TableInfo::is_hidden() const {
  return LockForRead()->is_hidden();
}

HybridTime TableInfo::hide_hybrid_time() const {
  return LockForRead()->hide_hybrid_time();
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

NamespaceId TableInfo::namespace_id() const {
  return LockForRead()->namespace_id();
}

// namespace_name can be null if table was created on version < 2.3.0 (see GH17713/GH17712 for more
// details)
NamespaceName TableInfo::namespace_name() const {
  return LockForRead()->namespace_name();
}

ColocationId TableInfo::GetColocationId() const {
  return LockForRead()->schema().colocated_table_id().colocation_id();
}

Result<Schema> TableInfo::GetSchema() const {
  return LockForRead()->GetSchema();
}

bool TableInfo::has_pgschema_name() const {
  return LockForRead()->schema().has_deprecated_pgschema_name();
}

const string TableInfo::pgschema_name() const {
  return LockForRead()->schema().deprecated_pgschema_name();
}

bool TableInfo::has_pg_type_oid() const {
  return LockForRead()->has_pg_type_oid();
}

TableId TableInfo::pg_table_id() const {
  return LockForRead()->pb.pg_table_id();
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

bool TableInfo::is_vector_index() const {
  auto l = LockForRead();
  return l->is_vector_index();
}

Result<uint32_t> TableInfo::GetPgRelfilenodeOid() const {
  return GetPgsqlTableOid(id());
}

Result<uint32_t> TableInfo::GetPgTableOid() const {
  return LockForRead()->GetPgTableOid(id());
}

Result<PgTableAllOids> TableInfo::GetPgTableAllOids() const {
  PgTableAllOids oids;
  auto l = LockForRead();
  RSTATUS_DCHECK_EQ(
      l->table_type(), PGSQL_TABLE_TYPE, InternalError,
      Format("Invalid table type of table $0", table_id_));
  oids.database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(l->namespace_id()));
  oids.relfilenode_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id_));
  oids.pg_table_oid = VERIFY_RESULT(l->GetPgTableOid(table_id_));
  return oids;
}

TableType TableInfo::GetTableType() const {
  return LockForRead()->pb.table_type();
}

bool TableInfo::IsBeingDroppedDueToDdlTxn(const std::string& pb_txn_id, bool txn_success) const {
  auto l = LockForRead();
  if (l->pb_transaction_id() != pb_txn_id) {
    return false;
  }
  // The table can be dropped in 2 cases due to a DDL:
  // 1. This table was created by a transaction that subsequently aborted.
  // 2. This is a successful transaction that DROPs the table.
  return (l->is_being_created_by_ysql_ddl_txn() && !txn_success) ||
         (l->is_being_deleted_by_ysql_ddl_txn() && txn_success);
}

bool TableInfo::IsBeingDroppedDueToSubTxnRollback(
    const std::string& pb_txn_id, const SubTransactionId sub_transaction_id) const {
  auto l = LockForRead();
  if (l->pb_transaction_id() != pb_txn_id) {
    return false;
  }

  auto idx = l->ysql_first_ddl_state_at_or_after_sub_txn(sub_transaction_id);
  return idx < l->ysql_ddl_txn_verifier_state().size() &&
         l->ysql_ddl_txn_verifier_state()[idx].contains_create_table_op();
}

Status TableInfo::AddTablet(const TabletInfoPtr& tablet) {
  std::lock_guard l(lock_);
  return AddTabletUnlocked(tablet);
}

Status TableInfo::ReplaceTablet(const TabletInfoPtr& old_tablet, const TabletInfoPtr& new_tablet) {
  std::lock_guard l(lock_);
  auto it = partitions_.find(old_tablet->metadata().dirty().pb.partition().partition_key_start());
  if (it != partitions_.end() &&
      VERIFY_RESULT(PromoteTabletPointer(it->second)) == old_tablet) {
    partitions_.erase(it);
  }
  return AddTabletUnlocked(new_tablet);
}

Status TableInfo::AddTablets(const TabletInfos& tablets) {
  std::lock_guard l(lock_);
  for (const auto& tablet : tablets) {
    RETURN_NOT_OK(AddTabletUnlocked(tablet));
  }
  return Status::OK();
}

void TableInfo::ClearTabletMaps() {
  DCHECK(IsSecondaryTable());
  std::lock_guard l(lock_);
  partitions_.clear();
  tablets_.clear();
}

std::map<TabletId, std::weak_ptr<TabletInfo>> TableInfo::TakeTablets() {
  std::lock_guard l(lock_);
  partitions_.clear();
  return std::move(tablets_);
}

Result<TabletWithSplitPartitions> TableInfo::FindSplittableHashPartitionForStatusTable() const {
  std::lock_guard l(lock_);

  for (const auto& [_, tablet_weak_ptr] : partitions_) {
    const auto& tablet = VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr));
    const auto& metadata = tablet->LockForRead();
    dockv::Partition partition;
    dockv::Partition::FromPB(metadata->pb.partition(), &partition);
    auto result = dockv::PartitionSchema::SplitHashPartitionForStatusTablet(partition);
    if (result) {
      return TabletWithSplitPartitions{
          .tablet = tablet, .left = result->first, .right = result->second};
    }
  }

  return STATUS_FORMAT(NotFound, "Table $0 has no splittable hash partition", table_id_);
}

void TableInfo::AddStatusTabletViaSplitPartition(
    TabletInfoPtr old_tablet, const dockv::Partition& partition, const TabletInfoPtr& new_tablet) {
  const auto& new_dirty = new_tablet->metadata().dirty();
  if (new_dirty.is_deleted()) {
    return;
  }

  auto old_lock = old_tablet->LockForWrite();
  auto old_partition = old_lock.mutable_data()->pb.mutable_partition();
  partition.ToPB(old_partition);
  old_lock.Commit();

  std::lock_guard l(lock_);
  tablets_.emplace(new_tablet->id(), new_tablet);

  if (!new_dirty.is_hidden()) {
    const auto& new_partition_key = new_dirty.pb.partition().partition_key_end();
    partitions_.emplace(new_partition_key, new_tablet);
  }
}

Status TableInfo::AddTabletUnlocked(const TabletInfoPtr& tablet) {
  const auto& tablet_dirty = tablet->metadata().dirty();
  if (tablet_dirty.is_deleted()) {
    // todo(zdrudi): for github issue 18257 this function's return type changed from void to Status.
    // To avoid changing existing behaviour we return OK here.
    // But silently passing over this case could cause bugs.
    return Status::OK();
  }
  const auto& tablet_meta = tablet_dirty.pb;
  tablets_.emplace(tablet->id(), tablet);

  if (tablet_dirty.is_hidden()) {
    // todo(zdrudi): for github issue 18257 this function's return type changed from void to Status.
    // To avoid changing existing behaviour we return OK here.
    // But silently passing over this case could cause bugs.

    // Hidden tablets of live tables should not be included in partitions_
    // as they are either split parents or children that are inactive.
    // Including them will result in overlapping partition ranges
    if (!is_hidden()) {
      VLOG(1) << Format("Tablet $0 is hidden but table $1 is not, skip add to partitions_",
          tablet->id(), id());
      return Status::OK();
    }
  }

  // Include hidden tablets in partitions_ only for hidden tables to support features
  // such as CLONE, PITR, and SELECT AS-OF that query previously dropped tables.
  const auto& partition_key_start = tablet_meta.partition().partition_key_start();
  auto [it, inserted] = partitions_.emplace(partition_key_start, tablet);
  if (inserted) {
    return Status::OK();
  }

  auto old_tablet = VERIFY_RESULT(PromoteTabletPointer(it->second));
  auto old_tablet_lock = old_tablet->LockForRead();
  const auto old_split_depth = old_tablet_lock->pb.split_depth();
  if (tablet_meta.split_depth() > old_split_depth || old_tablet_lock->is_deleted()) {
    VLOG(1) << "Replacing tablet " << old_tablet->tablet_id()
            << " (split_depth = " << old_split_depth << ")"
            << " with tablet " << tablet->tablet_id()
            << " (split_depth = " << tablet_meta.split_depth() << ")";
    it->second = tablet;
    return Status::OK();
  }

  if (tablet_meta.split_depth() == old_split_depth) {
    std::string msg = Format(
        "Two tablets $0, $1 with the same partition key start and split depth: $2 and $3",
        tablet->id(), old_tablet->tablet_id(),
        tablet_meta.ShortDebugString(), old_tablet_lock->pb.ShortDebugString());
    LOG_WITH_PREFIX(DFATAL) << msg;
    return STATUS(IllegalState, msg);
  }
  return Status::OK();
  // TODO: can we assert that the replaced tablet is not in Running state?
  // May be a little tricky since we don't know whether to look at its committed or
  // uncommitted state.
}

Result<bool> TableInfo::RemoveTablet(const TabletId& tablet_id, DeactivateOnly deactivate_only) {
  std::lock_guard l(lock_);
  return RemoveTabletUnlocked(tablet_id, deactivate_only);
}

Result<bool> TableInfo::RemoveTablets(const TabletInfos& tablets, DeactivateOnly deactivate_only) {
  std::lock_guard l(lock_);
  bool all_were_removed = true;
  for (const auto& tablet : tablets) {
    if (!VERIFY_RESULT(RemoveTabletUnlocked(tablet->id(), deactivate_only))) {
      all_were_removed = false;
    }
  }
  return all_were_removed;
}

Result<bool> TableInfo::RemoveTabletUnlocked(
    const TabletId& tablet_id, DeactivateOnly deactivate_only) {
  auto it = tablets_.find(tablet_id);
  if (it == tablets_.end()) return false;
  auto tablet = VERIFY_RESULT(PromoteTabletPointer(it->second));
  bool result = false;
  auto partitions_it = partitions_.find(
      tablet->metadata().dirty().pb.partition().partition_key_start());
  if (partitions_it != partitions_.end()) {
    auto partitions_tablet = VERIFY_RESULT(PromoteTabletPointer(partitions_it->second));
    if (partitions_tablet == tablet) {
      partitions_.erase(partitions_it);
      result = true;
    }
  }
  if (!deactivate_only) {
    tablets_.erase(it);
  }
  return result;
}

Result<TabletInfos> TableInfo::GetTabletsInRange(const GetTableLocationsRequestPB* req) const {
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

Result<TabletInfos> TableInfo::GetTabletsInRange(
    const std::string& partition_key_start, const std::string& partition_key_end,
    const size_t max_returned_locations) const {
  TabletInfos ret;
  SharedLock<decltype(lock_)> l(lock_);
  auto it = partition_key_start.empty() ? partitions_.begin()
                                        : partitions_.upper_bound(partition_key_start);
  if (it != partitions_.begin()) {
    --it;
  }
  auto it_end =
      partition_key_end.empty() ? partitions_.end() : partitions_.upper_bound(partition_key_end);
  ret.reserve(std::min(max_returned_locations, partitions_.size()));
  for (; it != it_end && ret.size() < max_returned_locations; ++it) {
    ret.push_back(VERIFY_RESULT(PromoteTabletPointer(it->second)));
  }
  return ret;
}

Result<TabletInfos> TableInfo::GetInactiveTabletsInRange(
    const std::string& partition_key_start, const std::string& partition_key_end,
    const size_t max_returned_locations) const {
  TabletInfos ret;
  SharedLock<decltype(lock_)> l(lock_);
  ret.reserve(std::min(max_returned_locations, tablets_.size()));
  for (const auto& [_, weak_tablet_ptr] : tablets_) {
    if (ret.size() >= max_returned_locations) {
      break;
    }
    auto tablet = VERIFY_RESULT(PromoteTabletPointer(weak_tablet_ptr));
    if (!partition_key_start.empty() &&
        tablet->metadata().dirty().pb.partition().partition_key_start() < partition_key_start) {
      continue;
    }
    if (!partition_key_end.empty() &&
        tablet->metadata().dirty().pb.partition().partition_key_start() > partition_key_end) {
      continue;
    }
    ret.push_back(std::move(tablet));
  }
  return ret;
}

Result<bool> TableInfo::IsAlterInProgress(uint32_t version) const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& [_, tablet_weak_ptr] : partitions_) {
    auto tablet = VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr));
    if (tablet->reported_schema_version(table_id_) < version) {
      VLOG_WITH_PREFIX_AND_FUNC(3)
          << "ALTER in progress due to tablet "
          << tablet->ToString() << " because reported schema "
          << tablet->reported_schema_version(table_id_) << " < expected " << version;
      return true;
    }
  }
  return false;
}

Result<bool> TableInfo::AreAllTabletsHidden() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& [_, tablet_weak_ptr] : tablets_) {
    auto tablet = VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr));
    if (!tablet->LockForRead()->is_hidden()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Not hidden tablet: " << tablet->ToString();
      return false;
    }
  }
  return true;
}

Result<bool> TableInfo::AreAllTabletsDeleted() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& [_, tablet_weak_ptr] : tablets_) {
    auto tablet = VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr));
    if (!tablet->LockForRead()->is_deleted()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Not deleted tablet: " << tablet->ToString();
      return false;
    }
  }
  return true;
}

Status TableInfo::CheckAllActiveTabletsRunning() const {
  SharedLock<decltype(lock_)> l(lock_);
  for (const auto& tablet_it : partitions_) {
    const auto& tablet = VERIFY_RESULT(PromoteTabletPointer(tablet_it.second));
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

Result<bool> TableInfo::AreAllTabletsRunning(const std::set<TabletId>& new_running_tablets) {
  SharedLock l(lock_);
  for (const auto& [_, weak_tablet_info] : partitions_) {
    auto tablet_info = VERIFY_RESULT(PromoteTabletPointer(weak_tablet_info.lock()));
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

Result<TabletInfos> TableInfo::GetTabletsIncludeInactive() const {
  TabletInfos result;
  SharedLock<decltype(lock_)> l(lock_);
  result.reserve(tablets_.size());
  for (const auto& [_, tablet_weak_ptr] : tablets_) {
    result.push_back(VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr)));
  }
  return result;
}

Result<TabletInfos> TableInfo::GetTablets(GetTabletsMode mode) const {
  TabletInfos result;
  {
    SharedLock<decltype(lock_)> l(lock_);
    result.reserve(partitions_.size());
    for (const auto& [_, tablet_weak_ptr] : partitions_) {
      result.push_back(VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr)));
    }
  }
  if (mode == GetTabletsMode::kOrderByTabletId) {
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
      return lhs->id() < rhs->id();
    });
  }
  return result;
}

size_t TableInfo::TabletCount(IncludeInactive include_inactive) const {
  SharedLock<decltype(lock_)> l(lock_);
  return include_inactive ? tablets_.size() : partitions_.size();
}

Result<bool> TableInfo::HasOutstandingSplits(bool wait_for_parent_deletion) const {
  SharedLock<decltype(lock_)> l(lock_);
  DCHECK(!colocated());
  std::unordered_set<TabletId> partitions_tablets;
  for (const auto& [_, tablet_weak_ptr] : partitions_) {
    auto tablet = VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr));
    auto tablet_lock = tablet->LockForRead();
    if (tablet_lock->pb.has_split_parent_tablet_id() && !tablet_lock->is_running()) {
      YB_LOG_EVERY_N_SECS(INFO, 10) << "Tablet Splitting: Child tablet " << tablet->tablet_id()
                                   << " belonging to table " << id() << " is not yet running";
      return true;
    }
    if (wait_for_parent_deletion) {
      partitions_tablets.insert(tablet->tablet_id());
    }
  }
  if (!wait_for_parent_deletion) {
    return false;
  }

  for (const auto& [_, tablet_weak_ptr] : tablets_) {
    auto tablet = VERIFY_RESULT(PromoteTabletPointer(tablet_weak_ptr));
    // If any parents have not been deleted yet, the split is not yet complete.
    if (!partitions_tablets.contains(tablet->tablet_id())) {
      auto tablet_lock = tablet->LockForRead();
      if (!tablet_lock->is_deleted() && !tablet_lock->is_hidden()) {
        YB_LOG_EVERY_N_SECS(INFO, 10) << "Tablet Splitting: Parent tablet " << tablet->tablet_id()
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

qlexpr::IndexInfo TableInfo::GetIndexInfo(const TableId& index_id) const {
  auto l = LockForRead();
  for (const auto& index_info_pb : l->pb.indexes()) {
    if (index_info_pb.table_id() == index_id) {
      return qlexpr::IndexInfo(index_info_pb);
    }
  }
  return qlexpr::IndexInfo();
}

TableIds TableInfo::GetIndexIds() const {
  TableIds result;
  auto lock = LockForRead();

  DCHECK(!IsIndex(lock->pb) || lock->pb.indexes().empty())
      << "Indexes should be empty for index table";

  result.reserve(lock->pb.indexes().size());
  for (const auto& index_info_pb : lock->pb.indexes()) {
    result.emplace_back(index_info_pb.table_id());
  }
  return result;
}

TableIds TableInfo::GetVectorIndexIds() const {
  TableIds result;
  auto lock = LockForRead();
  result.reserve(lock->pb.indexes().size());
  for (const auto& index_info_pb : lock->pb.indexes()) {
    if (index_info_pb.has_vector_idx_options()) {
      result.emplace_back(index_info_pb.table_id());
    }
  }
  return result;
}

bool TableInfo::TableTypeUsesTablespacesForPlacement() const {
  auto l = LockForRead();
  // Global transaction table is excluded due to not having a tablespace id set.
  bool is_transaction_table_using_tablespaces =
      l->pb.table_type() == TRANSACTION_STATUS_TABLE_TYPE &&
      l->pb.has_transaction_table_tablespace_id();
  bool is_regular_ysql_table =
      l->pb.table_type() == PGSQL_TABLE_TYPE &&
      l->namespace_id() != kPgSequencesDataNamespaceId &&
      !IsSecondaryTable() &&
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

bool TableInfo::IsSecondaryTable() const {
  return colocated() && !IsColocationParentTable();
}

bool TableInfo::IsSequencesSystemTable() const {
  return IsSequencesSystemTable(LockForRead());
}

bool TableInfo::IsSequencesSystemTable(const ReadLock& lock) const {
  if (lock->pb.table_type() != PGSQL_TABLE_TYPE || IsColocationParentTable()) {
    return false;
  }
  // This case commonly occurs during unit testing. Avoid unnecessary assert within Get().
  if (!IsPgsqlId(lock->namespace_id()) || !IsPgsqlId(id())) {
    LOG(WARNING) << "Not PGSQL IDs " << namespace_id() << ", " << id();
    return false;
  }
  Result<uint32_t> database_oid = GetPgsqlDatabaseOid(lock->namespace_id());
  if (!database_oid.ok()) {
    LOG(WARNING) << "Invalid Namespace ID " << lock->namespace_id();
    return false;
  }
  if (*database_oid != kPgSequencesDataDatabaseOid) {
    return false;
  }
  Result<uint32_t> table_oid = GetPgsqlTableOid(id());
  if (!table_oid.ok()) {
    LOG(WARNING) << "Invalid Table ID " << id();
    return false;
  }
  return *table_oid == kPgSequencesDataTableOid;
}

bool TableInfo::IsXClusterDDLReplicationDDLQueueTable() const {
  return LockForRead()->IsXClusterDDLReplicationDDLQueueTable();
}

bool TableInfo::IsXClusterDDLReplicationReplicatedDDLsTable() const {
  return LockForRead()->IsXClusterDDLReplicationReplicatedDDLsTable();
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

void TableInfo::AddDdlTxnWaitingForSchemaVersion(int schema_version, const TransactionId& txn) {
  std::lock_guard l(lock_);
  auto res = ddl_txns_waiting_for_schema_version_.emplace(schema_version, txn);
  // There should never have existed an entry for this schema version already as only one
  // DDL transaction is allowed on an entity at a given time.
  LOG_IF(DFATAL, !res.second) << "Found existing entry for schema version " << schema_version
                             << " for table " << table_id_ << " with txn " << txn
                             << " previous transaction " << res.first->second;
}

std::vector<TransactionId> TableInfo::EraseDdlTxnsWaitingForSchemaVersion(int schema_version) {
  std::lock_guard l(lock_);
  std::vector<TransactionId> txns;
  auto upper_bound_iter = ddl_txns_waiting_for_schema_version_.upper_bound(schema_version);
  // Ideally we will perform this erase operation at the end of every alter table operation, and
  // thus we should be able to return only one schema version. However, alter table is an async
  // operation. It is possible in the case of YSQL DDL Transaction verification that while an
  // alter operation for a rollback operation is about to start (i.e. table in ALTERING state), a
  // new alter operation can start on the same table (this is by design and poses no correctness
  // issues). It may be possible that the TServers respond back with the latest schema version.
  // To handle this case, we return the transactions waiting on all schema versions less than the
  // requested schema version as well.
  for (auto it = ddl_txns_waiting_for_schema_version_.begin(); it != upper_bound_iter; ++it) {
    txns.push_back(it->second);
  }
  ddl_txns_waiting_for_schema_version_.erase(
      ddl_txns_waiting_for_schema_version_.begin(), upper_bound_iter);
  return txns;
}

void TableInfo::AddDdlTxnForRollbackToSubTxnWaitingForSchemaVersion(
    int schema_version, const TransactionId& txn) {
  std::lock_guard l(lock_);
  auto res = ddl_txns_for_subtxn_rollback_waiting_for_schema_version_.emplace(schema_version, txn);
  // There should never have existed an entry for this schema version already as only one
  // DDL transaction is allowed on an entity at a given time.
  LOG_IF(DFATAL, !res.second) << "Found existing entry for schema version " << schema_version
                              << " for table " << table_id_ << " with txn " << txn
                              << " previous transaction " << res.first->second;
}

TransactionId TableInfo::EraseDdlTxnForRollbackToSubTxnWaitingForSchemaVersion(
    int schema_version) {
  std::lock_guard l(lock_);
  TransactionId txn;

  auto itr = ddl_txns_for_subtxn_rollback_waiting_for_schema_version_.find(schema_version);
  if (itr != ddl_txns_for_subtxn_rollback_waiting_for_schema_version_.end()) {
    txn = itr->second;
    ddl_txns_for_subtxn_rollback_waiting_for_schema_version_.erase(itr);
  }
  return txn;
}

bool TableInfo::IsUserCreated() const {
  return IsUserCreated(LockForRead());
}

bool TableInfo::IsUserTable() const {
  return IsUserTable(LockForRead());
}

bool TableInfo::IsUserIndex() const {
  return IsUserIndex(LockForRead());
}

bool TableInfo::HasUserSpecifiedPrimaryKey() const {
  return HasUserSpecifiedPrimaryKey(LockForRead());
}

bool TableInfo::IsUserCreated(const ReadLock& lock) const {
  if (lock->pb.table_type() != PGSQL_TABLE_TYPE && lock->pb.table_type() != YQL_TABLE_TYPE) {
    return false;
  }
  return !is_system() && !IsSequencesSystemTable(lock) &&
         lock->namespace_id() != kSystemNamespaceId &&
         !IsColocationParentTable();
}

bool TableInfo::IsUserTable(const ReadLock& lock) const {
  return IsUserCreated(lock) && lock->indexed_table_id().empty();
}

bool TableInfo::IsUserIndex(const ReadLock& lock) const {
  return IsUserCreated(lock) && !lock->indexed_table_id().empty();
}

bool TableInfo::HasUserSpecifiedPrimaryKey(const ReadLock& lock) const {
  auto schema_result = lock->GetSchema();
  if (!schema_result.ok()) {
    LOG_WITH_FUNC(DFATAL) << "Error while getting schema for table " << lock->name() << ": "
                          << schema_result.status();
    return false;
  }
  const auto& schema = *schema_result;
  for (const auto& col : schema.columns()) {
    if (col.order() == static_cast<int32_t>(PgSystemAttrNum::kYBRowId)) {
      // ybrowid column is added for tables that don't have user-specified primary key.
      return false;
    }
  }
  return true;
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

bool PersistentTableInfo::is_vector_index() const {
  return pb.has_index_info() && pb.index_info().has_vector_idx_options();
}

const std::string& PersistentTableInfo::indexed_table_id() const {
  static const std::string kEmptyString;
  return pb.has_index_info()
             ? pb.index_info().indexed_table_id()
             : pb.has_indexed_table_id() ? pb.indexed_table_id() : kEmptyString;
}

Result<bool> PersistentTableInfo::is_being_modified_by_ddl_transaction(
  const TransactionId& txn) const {

  return txn == VERIFY_RESULT(GetCurrentDdlTransactionId());
}

Result<TransactionId> PersistentTableInfo::GetCurrentDdlTransactionId() const {
  TransactionId txn = TransactionId::Nil();
  if (has_ysql_ddl_txn_verifier_state()) {
    auto& pb_txn_id = pb_transaction_id();
    RSTATUS_DCHECK(!pb_txn_id.empty(), InternalError,
        "Table $0 has ysql_ddl_txn_verifier_state but no transaction", name());
    txn = VERIFY_RESULT(FullyDecodeTransactionId(pb_txn_id));
  }
  return txn;
}

bool PersistentTableInfo::IsXClusterDDLReplicationDDLQueueTable() const {
  return pb.table_type() == PGSQL_TABLE_TYPE &&
         schema().deprecated_pgschema_name() == xcluster::kDDLQueuePgSchemaName &&
         name() == xcluster::kDDLQueueTableName;
}

bool PersistentTableInfo::IsXClusterDDLReplicationReplicatedDDLsTable() const {
  return pb.table_type() == PGSQL_TABLE_TYPE &&
         schema().deprecated_pgschema_name() == xcluster::kDDLQueuePgSchemaName &&
         name() == xcluster::kDDLReplicatedTableName;
}

Result<uint32_t> PersistentTableInfo::GetPgTableOid(const std::string& id) const {
  const auto& pg_table_id = pb.pg_table_id();
  return pg_table_id.empty() ? GetPgsqlTableOid(id) :
                               GetPgsqlTableOid(pg_table_id);
}

bool PersistentTableInfo::has_pg_type_oid() const {
  for (const auto& col : schema().columns()) {
    if (!col.has_pg_type_oid()) {
      return false;
    }
  }
  return true;
}

Result<Schema> PersistentTableInfo::GetSchema() const {
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(pb.schema(), &schema));
  return schema;
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
// NamespaceInfo
// ================================================================================================

NamespaceInfo::NamespaceInfo(NamespaceId ns_id, scoped_refptr<TasksTracker> tasks_tracker)
    : CatalogEntityWithTasks(std::move(tasks_tracker)), namespace_id_(std::move(ns_id)) {}

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

::yb::master::SysNamespaceEntryPB_YsqlNextMajorVersionState
    NamespaceInfo::ysql_next_major_version_state() const {
  return LockForRead()->pb.ysql_next_major_version_state();
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

// ================================================================================================
// ObjectLockInfo
// ================================================================================================

Result<std::variant<ObjectLockInfo::WriteLock, SysObjectLockEntryPB::LeaseInfoPB>>
ObjectLockInfo::RefreshYsqlOperationLease(
    const RefreshYsqlLeaseRequestPB& req, MonoDelta lease_ttl) {
  auto l = LockForWrite();
  auto& current_lease_info = l->pb.lease_info();
  if (req.instance().instance_seqno() < current_lease_info.instance_seqno()) {
    return STATUS_FORMAT(
        IllegalState,
        "Cannot grant lease, instance seqno of requestor $0 is lower than instance seqno of a "
        "previously granted lease $1",
        req.instance().instance_seqno(), current_lease_info.instance_seqno());
  }
  if (current_lease_info.lease_relinquished() &&
      req.instance().instance_seqno() <= current_lease_info.instance_seqno()) {
    return STATUS_FORMAT(
        IllegalState,
        "Cannot grant lease, lease has been relinquished by instance_seqno $0 already",
        current_lease_info.instance_seqno());
  }
  {
    std::lock_guard l(mutex_);
    // When doing this mutation we cannot be sure the tserver receives the response.
    // So we cannot safely reduce the lease deadline, only extend it.
    ysql_lease_deadline_ = std::max(ysql_lease_deadline_, MonoTime::Now() + lease_ttl);
  }
  if (l->pb.lease_info().live_lease() &&
      l->pb.lease_info().instance_seqno() == req.instance().instance_seqno() &&
      // Only extend the current lease if the tserver thinks it still has a live lease.
      req.current_lease_epoch() == current_lease_info.lease_epoch()) {
    return l->pb.lease_info();
  }
  auto& lease_info = *l.mutable_data()->pb.mutable_lease_info();
  lease_info.set_live_lease(true);
  lease_info.set_lease_epoch(lease_info.lease_epoch() + 1);
  lease_info.set_instance_seqno(req.instance().instance_seqno());
  lease_info.set_lease_relinquished(false);
  return std::move(l);
}

void ObjectLockInfo::Load(const SysObjectLockEntryPB& metadata) {
  MetadataCowWrapper<PersistentObjectLockInfo>::Load(metadata);
  {
    std::lock_guard l(mutex_);
    ysql_lease_deadline_ =
        MonoTime::Now() +
        MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_master_ysql_operation_lease_ttl_ms));
  }
}

MonoTime ObjectLockInfo::ysql_lease_deadline() const {
  std::lock_guard l(mutex_);
  return ysql_lease_deadline_;
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

bool CDCStreamInfo::IsXClusterStream() const { return !IsCDCSDKStream(); }

bool CDCStreamInfo::IsCDCSDKStream() const {
  auto l = LockForRead();
  return l->pb.has_namespace_id() && !l->pb.namespace_id().empty();
}

HybridTime CDCStreamInfo::GetConsistentSnapshotHybridTime() const {
  auto l = LockForRead();
  return HybridTime(l->pb.cdcsdk_stream_metadata().snapshot_time());
}

const ReplicationSlotName CDCStreamInfo::GetCdcsdkYsqlReplicationSlotName() const {
  auto l = LockForRead();
  return ReplicationSlotName(l->pb.cdcsdk_ysql_replication_slot_name());
}

bool CDCStreamInfo::IsConsistentSnapshotStream() const {
  auto l = LockForRead();
  return l->pb.has_cdcsdk_stream_metadata() &&
         l->pb.cdcsdk_stream_metadata().has_consistent_snapshot_option();
}

const google::protobuf::Map<::std::string, ::yb::PgReplicaIdentity>
CDCStreamInfo::GetReplicaIdentityMap() const {
  auto l = LockForRead();
  return l->pb.replica_identity_map();
}

bool CDCStreamInfo::IsDynamicTableAdditionDisabled() const {
  if (!FLAGS_cdcsdk_enable_dynamic_tables_disable_option) {
    return false;
  }

  auto l = LockForRead();
  return l->pb.has_cdcsdk_disable_dynamic_table_addition() &&
         l->pb.cdcsdk_disable_dynamic_table_addition();
}

bool CDCStreamInfo::IsTablesWithoutPrimaryKeyAllowed() const {
  auto l = LockForRead();
  return l->pb.has_allow_tables_without_primary_key() && l->pb.allow_tables_without_primary_key();
}

std::string CDCStreamInfo::ToString() const {
  auto l = LockForRead();
  if (l->pb.has_namespace_id()) {
    return Format(
        "$0 [namespace=$1] {metadata=$2} ", id(), l->pb.namespace_id(), l->pb.ShortDebugString());
  }
  if (l->pb.table_id().empty()) {
    return Format("$0 {metadata=$2} ", id(), l->pb.ShortDebugString());
  }
  return Format("$0 [table=$1] {metadata=$2} ", id(), l->pb.table_id(0), l->pb.ShortDebugString());
}

// ================================================================================================
// UniverseReplicationInfoBase
// ================================================================================================
Result<std::shared_ptr<XClusterRpcTasks>> UniverseReplicationInfoBase::GetOrCreateXClusterRpcTasks(
    google::protobuf::RepeatedPtrField<HostPortPB> producer_masters) {
  std::vector<HostPort> hp;
  HostPortsFromPBs(producer_masters, &hp);
  std::string master_addrs = HostPort::ToCommaSeparatedString(hp);

  std::lock_guard l(lock_);
  if (xcluster_rpc_tasks_ != nullptr) {
    // Master Addresses changed, update YBClient with new retry logic.
    if (master_addrs_ != master_addrs) {
      RETURN_NOT_OK(xcluster_rpc_tasks_->UpdateMasters(master_addrs));
      master_addrs_ = master_addrs;
    }
    return xcluster_rpc_tasks_;
  }

  auto rpc_task =
      VERIFY_RESULT(XClusterRpcTasks::CreateWithMasterAddrs(replication_group_id_, master_addrs));
  xcluster_rpc_tasks_ = rpc_task;
  master_addrs_ = master_addrs;
  return rpc_task;
}

// ================================================================================================
// PersistentUniverseReplicationInfo
// ================================================================================================

bool PersistentUniverseReplicationInfo::IsDbScoped() const { return yb::master::IsDbScoped(pb); }

bool PersistentUniverseReplicationInfo::IsAutomaticDdlMode() const {
  return yb::master::IsAutomaticDdlMode(pb);
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
  if (setup_universe_replication_error_.ok()) {
    setup_universe_replication_error_ = status;
  }
}

Status UniverseReplicationInfo::GetSetupUniverseReplicationErrorStatus() const {
  SharedLock<decltype(lock_)> l(lock_);
  return setup_universe_replication_error_;
}

bool UniverseReplicationInfo::IsDbScoped() const { return LockForRead()->IsDbScoped(); }

bool UniverseReplicationInfo::IsAutomaticDdlMode() const {
  return LockForRead()->IsAutomaticDdlMode();
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

TabletInfoPtr MakeUnlockedTabletInfo(
    const TableInfoPtr& table,
    const TabletId& tablet_id) {
  auto tablet = std::make_shared<TabletInfo>(
      table, tablet_id.empty() ? GenerateObjectId() : tablet_id);

  VLOG_WITH_FUNC(2)
      << "Table: " << table->ToString() << ", tablet: " << tablet->ToString();

  return tablet;
}

TabletInfoPtr MakeTabletInfo(
    const TableInfoPtr& table,
    const TabletId& tablet_id) {
  auto tablet = MakeUnlockedTabletInfo(table, tablet_id);
  tablet->mutable_metadata()->StartMutation();
  return tablet;
}

void SetupTabletInfo(
    TabletInfo& tablet,
    const TableInfo& table,
    const PartitionPB& partition,
    SysTabletsEntryPB::State state) {
  auto& metadata = tablet.mutable_metadata()->mutable_dirty()->pb;
  metadata.set_state(state);
  metadata.mutable_partition()->CopyFrom(partition);
  metadata.set_table_id(table.id());
  if (FLAGS_use_parent_table_id_field && !table.is_system()) {
    tablet.SetTableIds({table.id()});
    metadata.set_hosted_tables_mapped_by_parent_id(true);
  } else {
    // This is important: we are setting the first table id in the table_ids list
    // to be the id of the original table that creates the tablet.
    metadata.add_table_ids(table.id());
  }

  auto& cstate = *metadata.mutable_committed_consensus_state();
  cstate.set_current_term(consensus::kMinimumTerm);
  cstate.mutable_config()->set_opid_index(consensus::kInvalidOpIdIndex);
}

TabletInfoPtr CreateTabletInfo(
    const TableInfoPtr& table, const PartitionPB& partition, SysTabletsEntryPB::State state,
    const TabletId& tablet_id) {
  auto tablet = MakeTabletInfo(table, tablet_id);
  SetupTabletInfo(*tablet, *table, partition, state);
  return tablet;
}

}  // namespace yb::master
