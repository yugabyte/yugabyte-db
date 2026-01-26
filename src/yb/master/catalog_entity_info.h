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

#pragma once

#include <mutex>
#include <vector>

#include <boost/bimap.hpp>

#include "yb/cdc/cdc_types.h"
#include "yb/cdc/xcluster_types.h"
#include "yb/common/snapshot.h"
#include "yb/common/transaction.h"
#include "yb/consensus/consensus_types.pb.h"
#include "yb/dockv/partition.h"

#include "yb/master/catalog_entity_base.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_client.fwd.h"
#include "yb/master/master_fwd.h"
#include "yb/master/sys_catalog_types.h"
#include "yb/master/tasks_tracker.h"

#include "yb/qlexpr/index.h"
#include "yb/tablet/metadata.pb.h"

#include "yb/util/cow_object.h"
#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"
#include "yb/util/shared_lock.h"

DECLARE_bool(use_parent_table_id_field);

namespace yb::master {

class RetryingRpcTask;

YB_STRONGLY_TYPED_BOOL(DeactivateOnly);

// Per table structure for external cluster snapshot importing to this cluster.
// Old IDs mean IDs on external/source cluster, new IDs - IDs on this cluster.
struct ExternalTableSnapshotData {
  bool is_index() const { return !table_entry_pb.indexed_table_id().empty(); }

  NamespaceId old_namespace_id;
  TableId old_table_id;
  TableId new_table_id;
  SysTablesEntryPB table_entry_pb;
  std::string pg_schema_name;
  size_t num_tablets = 0;
  using PartitionKeys = std::pair<std::string, std::string>;
  using PartitionToIdMap = std::map<PartitionKeys, TabletId>;
  std::vector<std::pair<TabletId, PartitionPB>> old_tablets;
  PartitionToIdMap new_tablets_map;
  // Mapping: Old tablet ID -> New tablet ID.
  std::optional<ImportSnapshotMetaResponsePB::TableMetaPB> table_meta = std::nullopt;
  // The correct schema version of the new table used for cloning colocated tables. Colocated
  // tables' schemas and schema versions are added to the target tablet's superblock when applying
  // the clone_op.
  std::optional<int> new_table_schema_version = std::nullopt;
  // When false, skip old DocDB vs new DocDB schema equality checks at import time for this table.
  // This is set to false for YSQL tables that were undergoing DDL verification at backup time
  // (i.e. the SysTablesEntryPB has ysql_ddl_txn_verifier_state).
  bool validate_schema = true;
};
using ExternalTableSnapshotDataMap = std::unordered_map<TableId, ExternalTableSnapshotData>;

struct ExternalNamespaceSnapshotData {
  ExternalNamespaceSnapshotData() : db_type(YQL_DATABASE_UNKNOWN), just_created(false) {}

  NamespaceId new_namespace_id;
  YQLDatabase db_type;
  bool just_created;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(new_namespace_id, db_type, just_created);
  }
};
// Map: old_namespace_id (key) -> new_namespace_id + db_type + created-flag.
using NamespaceMap = std::unordered_map<NamespaceId, ExternalNamespaceSnapshotData>;

struct ExternalUDTypeSnapshotData {
  ExternalUDTypeSnapshotData() : just_created(false) {}

  UDTypeId new_type_id;
  SysUDTypeEntryPB type_entry_pb;
  bool just_created;
};
// Map: old_type_id (key) -> new_type_id + type_entry_pb + created-flag.
typedef std::unordered_map<UDTypeId, ExternalUDTypeSnapshotData> UDTypeMap;

struct TableDescription {
  NamespaceInfoPtr namespace_info;
  TableInfoPtr table_info;
  TabletInfos tablet_infos;
};

struct TabletLeaderLeaseInfo {
  bool initialized = false;
  consensus::LeaderLeaseStatus leader_lease_status =
      consensus::LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE;
  // Expiration time of leader ht lease, invalid when leader_lease_status != HAS_LEASE.
  MicrosTime ht_lease_expiration = 0;
  // Number of heartbeats that current tablet leader doesn't have a valid lease.
  uint64 heartbeats_without_leader_lease = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        initialized, (leader_lease_status, consensus::LeaderLeaseStatus_Name(leader_lease_status)),
        ht_lease_expiration, heartbeats_without_leader_lease);
  }
};

// Drive usage information on a current replica of a tablet.
// This allows us to look at individual resource usage per replica of a tablet.
struct TabletReplicaDriveInfo {
  uint64 sst_files_size = 0;
  uint64 wal_files_size = 0;
  uint64 uncompressed_sst_file_size = 0;
  bool may_have_orphaned_post_split_data = true;
  uint64 total_size = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        sst_files_size, wal_files_size, uncompressed_sst_file_size,
        may_have_orphaned_post_split_data, total_size);
  }
};

struct FullCompactionStatus {
  tablet::FullCompactionState full_compaction_state = tablet::FULL_COMPACTION_STATE_UNKNOWN;

  // Not valid if full_compaction_state == UNKNOWN.
  HybridTime last_full_compaction_time;
};

// Information on a current replica of a tablet.
// This is copyable so that no locking is needed.
struct TabletReplica {
  std::weak_ptr<TSDescriptor> ts_desc;
  tablet::RaftGroupStatePB state;
  PeerRole role;
  consensus::PeerMemberType member_type;
  MonoTime time_updated;

  // Replica is reporting that load balancer moves should be disabled. This could happen in the case
  // where a tablet has just been split and still refers to data from its parent which is no longer
  // relevant, for example.
  bool should_disable_lb_move = false;

  std::string fs_data_dir;

  TabletReplicaDriveInfo drive_info;

  TabletLeaderLeaseInfo leader_lease_info;

  FullCompactionStatus full_compaction_status;

  uint32_t last_attempted_clone_seq_no;

  TabletReplica() : time_updated(MonoTime::Now()) {}

  void UpdateFrom(const TabletReplica& source);

  void UpdateDriveInfo(const TabletReplicaDriveInfo& info);

  void UpdateLeaderLeaseInfo(const TabletLeaderLeaseInfo& info);

  bool IsStale() const;

  bool IsStarting() const;

  std::string ToString() const;
};

// The data related to a tablet which is persisted on disk.
// This portion of TabletInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentTabletInfo : public Persistent<SysTabletsEntryPB> {
  bool is_running() const {
    return pb.state() == SysTabletsEntryPB::RUNNING;
  }

  bool is_deleted() const {
    return pb.state() == SysTabletsEntryPB::REPLACED ||
           pb.state() == SysTabletsEntryPB::DELETED;
  }

  bool is_hidden() const {
    return pb.hide_hybrid_time() != 0;
  }

  bool ListedAsHidden() const {
    // Tablet was hidden, but not yet deleted (to avoid resending delete for it).
    return is_hidden() && !is_deleted();
  }

  bool is_colocated() const {
    return pb.colocated();
  }

  HybridTime hide_hybrid_time() const {
    DCHECK(is_hidden());
    return HybridTime::FromPB(pb.hide_hybrid_time());
  }

  // Helper to set the state of the tablet with a custom message.
  // Requires that the caller has prepared this object for write.
  // The change will only be visible after Commit().
  void set_state(SysTabletsEntryPB::State state, const std::string& msg);
};

template <class T>
struct CowObjectWithWriteLock {
  scoped_refptr<T> info;
  typename T::WriteLock lock;

  CowObjectWithWriteLock() = default;
  explicit CowObjectWithWriteLock(const scoped_refptr<T>& info_) : info(info_) {}

  T* operator->() const {
    return info.get();
  }

  T& operator*() const {
    return *info;
  }

  explicit operator bool() const {
    return info != nullptr;
  }

  void Lock() {
    lock = info->LockForWrite();
  }

  void Commit() {
    lock.Commit();
  }
};

struct IdLess {
  template <class T>
  bool operator()(const T& lhs, const T& rhs) const {
    return lhs.id() < rhs.id();
  }

  template <class T>
  bool operator()(const scoped_refptr<T>& lhs, const scoped_refptr<T>& rhs) const {
    return (*this)(*lhs, *rhs);
  }
};

// The information about a single tablet which exists in the cluster,
// including its state and locations.
//
// This object uses copy-on-write for the portions of data which are persisted
// on disk. This allows the mutated data to be staged and written to disk
// while readers continue to access the previous version. These portions
// of data are in PersistentTabletInfo above, and typically accessed using
// MetadataLock. For example:
//
//   TabletInfo* tablet = ...;
//   MetadataLock l = tablet->LockForRead();
//   if (l.data().is_running()) { ... }
//
// The non-persistent information about the tablet is protected by an internal
// spin-lock.
//
// The object is owned/managed by the CatalogManager, and exposed for testing.
class TabletInfo : public MetadataCowWrapper<PersistentTabletInfo> {
 public:
  TabletInfo(const TableInfoPtr& table, TabletId tablet_id);

  template <class SetupFunctor>
  TabletInfo(const TableInfoPtr& table, TabletId tablet_id, const SetupFunctor& setup)
      : TabletInfo(table, tablet_id) {
    setup(metadata_.DirectStateForInitialSetup());
  }

  virtual const TabletId& id() const override { return tablet_id_; }

  const TabletId& tablet_id() const { return tablet_id_; }
  scoped_refptr<const TableInfo> table() const { return table_; }
  const scoped_refptr<TableInfo>& table() { return table_; }

  // Accessors for the latest known tablet replica locations.
  // These locations include only the members of the latest-reported Raft
  // configuration whose tablet servers have ever heartbeated to this Master.
  // TODO: Make Set/Update private so users are forced to use the catalog manager wrappers which
  // update the tablet locations version.
  void SetReplicaLocations(std::shared_ptr<TabletReplicaMap> replica_locations);
  std::shared_ptr<const TabletReplicaMap> GetReplicaLocations() const;
  Result<TSDescriptorPtr> GetLeader() const;
  Result<TabletReplicaDriveInfo> GetLeaderReplicaDriveInfo() const;
  Result<TabletLeaderLeaseInfo> GetLeaderLeaseInfoIfLeader(const std::string& ts_uuid) const;

  // Replaces a replica in replica_locations_ map if it exists. Otherwise, it adds it to the map.
  void UpdateReplicaLocations(const std::string& ts_uuid, const TabletReplica& replica);

  // Updates a replica in replica_locations_ map if it exists.
  void UpdateReplicaInfo(const std::string& ts_uuid,
                         const TabletReplicaDriveInfo& drive_info,
                         const TabletLeaderLeaseInfo& leader_lease_info);

  // Returns the per-stream replication status bitmasks.
  std::unordered_map<xrepl::StreamId, uint64_t> GetReplicationStatus();

  // Accessors for the last time the replica locations were updated.
  void set_last_update_time(const MonoTime& ts);
  MonoTime last_update_time() const;

  // Update last_time_with_valid_leader_ to the Now().
  void UpdateLastTimeWithValidLeader();
  MonoTime last_time_with_valid_leader() const;
  void TEST_set_last_time_with_valid_leader(const MonoTime& time);

  // Accessors for the last reported schema version.
  bool set_reported_schema_version(const TableId& table_id, uint32_t version);
  uint32_t reported_schema_version(const TableId& table_id);

  // Accessors for the initial leader election protege.
  void SetInitiaLeaderElectionProtege(const std::string& protege_uuid) EXCLUDES(lock_);
  std::string InitiaLeaderElectionProtege() EXCLUDES(lock_);

  bool colocated() const;

  // No synchronization needed.
  std::string ToString() const override;

  // This is called when a leader stepdown request fails. Optionally, takes an amount of time since
  // the stepdown failure, in case it happened in the past (e.g. we talked to a tablet server and
  // it told us that it previously tried to step down in favor of this server and that server lost
  // the election).
  void RegisterLeaderStepDownFailure(const TabletServerId& intended_leader,
                                     MonoDelta time_since_stepdown_failure);

  // Retrieves a map of recent leader step-down failures. At the same time, forgets about step-down
  // failures that happened before a certain point in time.
  void GetLeaderStepDownFailureTimes(MonoTime forget_failures_before,
                                     LeaderStepDownFailureTimes* dest);

  Status CheckRunning() const;

  bool InitiateElection() {
    bool expected = false;
    return initiated_election_.compare_exchange_strong(expected, true);
  }

  void UpdateReplicaFullCompactionStatus(
      const TabletServerId& ts_uuid, const FullCompactionStatus& full_compaction_status);

  // The next four methods are getters and setters for the transient, in memory list of table ids
  // hosted by this tablet. They are only used if the underlying tablet proto's
  // hosted_tables_mapped_by_parent_id field is set.
  void SetTableIds(std::vector<TableId>&& table_ids);
  void AddTableId(const TableId& table_id);
  std::vector<TableId> GetTableIds() const;
  void RemoveTableIds(const std::unordered_set<TableId>& tables_to_remove);

  ~TabletInfo();

 private:
  friend class RefCountedThreadSafe<TabletInfo>;

  class LeaderChangeReporter;
  friend class LeaderChangeReporter;

  Result<TSDescriptorPtr> GetLeaderUnlocked() const REQUIRES_SHARED(lock_);
  Status GetLeaderNotFoundStatus() const REQUIRES_SHARED(lock_);

  const TabletId tablet_id_;
  const scoped_refptr<TableInfo> table_;

  // Lock protecting the below mutable fields.
  // This doesn't protect metadata_ (the on-disk portion).
  mutable simple_spinlock lock_;

  // The last time the replica locations were updated.
  // Also set when the Master first attempts to create the tablet.
  MonoTime last_update_time_ GUARDED_BY(lock_);

  // The last time the tablet has a valid leader, a valid leader is:
  // 1. with peer role LEADER.
  // 2. has not-expired lease.
  MonoTime last_time_with_valid_leader_ GUARDED_BY(lock_);

  // The locations in the latest Raft config where this tablet has been
  // reported. The map is keyed by tablet server UUID.
  std::shared_ptr<TabletReplicaMap> replica_locations_ GUARDED_BY(lock_) =
      std::make_shared<TabletReplicaMap>();

  // Reported schema version (in-memory only).
  std::unordered_map<TableId, uint32_t> reported_schema_version_ GUARDED_BY(lock_);

  // The protege UUID to use for the initial leader election (in-memory only).
  std::string initial_leader_election_protege_ GUARDED_BY(lock_);

  LeaderStepDownFailureTimes leader_stepdown_failure_times_ GUARDED_BY(lock_);

  std::atomic<bool> initiated_election_{false};

  std::unordered_map<xrepl::StreamId, uint64_t> replication_stream_to_status_bitmask_;

  // Transient, in memory list of table ids hosted by this tablet. This is not persisted.
  // Only used when FLAGS_use_parent_table_id_field is set.
  std::vector<TableId> table_ids_ GUARDED_BY(lock_);

  uint32_t last_attempted_clone_seq_no_ GUARDED_BY(lock_) = 0;

  DISALLOW_COPY_AND_ASSIGN(TabletInfo);
};

// The data related to a table which is persisted on disk.
// This portion of TableInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentTableInfo : public Persistent<SysTablesEntryPB> {
  bool started_deleting() const {
    return pb.state() == SysTablesEntryPB::DELETING ||
           pb.state() == SysTablesEntryPB::DELETED;
  }

  bool is_deleted() const {
    return pb.state() == SysTablesEntryPB::DELETED;
  }

  bool is_deleting() const {
    return pb.state() == SysTablesEntryPB::DELETING;
  }

  bool IsPreparing() const { return pb.state() == SysTablesEntryPB::PREPARING; }

  bool is_running() const {
    // Historically, we have always treated PREPARING (tablets not yet ready) and RUNNING as the
    // same, so preparing is also considered running even though the tablets are not all running.
    // ALTERING tables are running.
    return pb.state() == SysTablesEntryPB::PREPARING || pb.state() == SysTablesEntryPB::RUNNING ||
           pb.state() == SysTablesEntryPB::ALTERING;
  }

  bool visible_to_client() const { return is_running() && !is_hidden(); }

  bool is_hiding() const {
    return pb.hide_state() == SysTablesEntryPB::HIDING;
  }

  bool is_hidden() const {
    return pb.hide_state() == SysTablesEntryPB::HIDDEN;
  }

  bool started_hiding() const {
    return is_hiding() || is_hidden();
  }

  bool started_hiding_or_deleting() const {
    return started_hiding() || started_deleting();
  }

  bool is_hidden_but_not_deleting() const {
    return is_hidden() && !started_deleting();
  }

  // Return the table's name.
  const TableName& name() const {
    return pb.name();
  }

  // Return the table's type.
  TableType table_type() const {
    return pb.table_type();
  }

  HybridTime hide_hybrid_time() const {
    DCHECK(is_hidden());
    return HybridTime::FromPB(pb.hide_hybrid_time());
  }

  // Return the table's namespace id.
  const NamespaceId& namespace_id() const { return pb.namespace_id(); }
  // Return the table's namespace name.
  const NamespaceName& namespace_name() const { return pb.namespace_name(); }

  const SchemaPB& schema() const {
    return pb.schema();
  }

  const TableId& indexed_table_id() const;

  bool is_index() const;
  bool is_vector_index() const;

  SchemaPB* mutable_schema() {
    return pb.mutable_schema();
  }

  const std::string& pb_transaction_id() const {
    static std::string kEmptyString;
    return pb.has_transaction() ? pb.transaction().transaction_id() : kEmptyString;
  }

  bool has_ysql_ddl_txn_verifier_state() const {
    return pb.ysql_ddl_txn_verifier_state_size() > 0;
  }

  google::protobuf::RepeatedPtrField<YsqlDdlTxnVerifierStatePB> ysql_ddl_txn_verifier_state()
      const {
    DCHECK_GE(pb.ysql_ddl_txn_verifier_state_size(), 1);
    return pb.ysql_ddl_txn_verifier_state();
  }

  YsqlDdlTxnVerifierStatePB ysql_ddl_txn_verifier_state_first() const {
    DCHECK_GE(pb.ysql_ddl_txn_verifier_state_size(), 1);
    return pb.ysql_ddl_txn_verifier_state(0);
  }

  YsqlDdlTxnVerifierStatePB ysql_ddl_txn_verifier_state_last() const {
    DCHECK_GE(pb.ysql_ddl_txn_verifier_state_size(), 1);
    return pb.ysql_ddl_txn_verifier_state(pb.ysql_ddl_txn_verifier_state_size() - 1);
  }

  // Find the **index** of ddl_state in ysql_ddl_txn_verifier_state with
  // smallest sub-transaction id >= sub_txn_id.
  // Entries of ysql_ddl_txn_verifier_state are sorted in increasing order of sub-transaction id.
  // See comment in catalog_entity_info.proto for more details.
  // Returns length of ysql_ddl_txn_verifier_state if no such state is found.
  int ysql_first_ddl_state_at_or_after_sub_txn(SubTransactionId sub_txn) const {
    const auto& ddl_states = ysql_ddl_txn_verifier_state();
    auto it = std::lower_bound(
        ddl_states.begin(), ddl_states.end(), sub_txn, [](const auto& ddl_state, uint32_t value) {
          return ddl_state.sub_transaction_id() < value;
        });

    return static_cast<int>(std::distance(ddl_states.begin(), it));
  }

  bool is_being_deleted_by_ysql_ddl_txn() const {
    // If we consider all DDL statements that involve a particular DocDB table (unique DocDB id) in
    // a transaction block, the deletion of the table can only happen in the last statement. So just
    // check the last ysql_ddl_txn_verifier_state for drop table operation.
    return has_ysql_ddl_txn_verifier_state() &&
      ysql_ddl_txn_verifier_state_last().contains_drop_table_op();
  }

  bool is_being_created_by_ysql_ddl_txn() const {
    // If we consider all DDL statements that involve a particular DocDB table (unique DocDB id) in
    // a transaction block, the creation of the table can only happen in the first statement.
    // So just check the first ysql_ddl_txn_verifier_state for create table operation.
    return has_ysql_ddl_txn_verifier_state() &&
      ysql_ddl_txn_verifier_state_first().contains_create_table_op();
  }

  bool is_being_altered_by_ysql_ddl_txn() const {
    // A table can be altered by a transaction if it is being altered in at
    // least one of the sub-transactions.
    for (const auto& state : ysql_ddl_txn_verifier_state()) {
      if (state.contains_alter_table_op()) return true;
    }
    return false;
  }

  std::vector<std::string> cols_marked_for_deletion() const {
    std::vector<std::string> columns;
    for (const auto& col : pb.schema().columns()) {
      if (col.marked_for_deletion()) {
        columns.push_back(col.name());
      }
    }
    return columns;
  }

  Result<bool> is_being_modified_by_ddl_transaction(const TransactionId& txn) const;

  // Returns the transaction-id of the DDL transaction operating on it, Nil if no such DDL is
  // happening.
  Result<TransactionId> GetCurrentDdlTransactionId() const;

  const std::string& state_name() const {
    return SysTablesEntryPB_State_Name(pb.state());
  }

  const std::string& hide_state_name() const {
    return SysTablesEntryPB_HideState_Name(pb.hide_state());
  }

  // Helper to set the state of the tablet with a custom message.
  void set_state(SysTablesEntryPB::State state, const std::string& msg);

  bool IsXClusterDDLReplicationDDLQueueTable() const;
  bool IsXClusterDDLReplicationReplicatedDDLsTable() const;

  bool IsXClusterDDLReplicationTable() const {
    return IsXClusterDDLReplicationDDLQueueTable() || IsXClusterDDLReplicationReplicatedDDLsTable();
  }

  Result<uint32_t> GetPgTableOid(const std::string& id) const;
  bool has_pg_type_oid() const;
  Result<Schema> GetSchema() const;

  TableType GetTableType() const {
    return table_type();
  }
};

YB_DEFINE_ENUM(GetTabletsMode, (kOrderByPartitions)(kOrderByTabletId));

// A tablet, and two partitions that together cover the tablet's partition.
struct TabletWithSplitPartitions {
  TabletInfoPtr tablet;
  dockv::Partition left;
  dockv::Partition right;
};

// The information about a table, including its state and tablets.
//
// This object uses copy-on-write techniques similarly to TabletInfo.
// Please see the TabletInfo class doc above for more information.
//
// The non-persistent information about the table is protected by an internal
// spin-lock.
//
// N.B. The catalog manager stores this object in a TableIndex data structure with multiple indices.
// Any change to the value of the fields indexed need to be registered with the TableIndex or the
// indices will break. The proper value for the indexed fields needs to be set before the TableInfo
// is added to the TableIndex.
//
// Currently indexed values:
//     colocated
class TableInfo : public RefCountedThreadSafe<TableInfo>,
                  public MetadataCowWrapper<PersistentTableInfo>,
                  public CatalogEntityWithTasks {
 public:
  explicit TableInfo(
      TableId table_id, bool colocated, scoped_refptr<TasksTracker> tasks_tracker = nullptr);

  const TableName name() const;

  bool is_running() const;
  bool is_deleted() const;
  bool is_hidden() const;
  bool started_hiding() const;
  bool IsPreparing() const;
  bool IsOperationalForClient() const {
    auto l = LockForRead();
    return !l->started_hiding_or_deleting();
  }

  bool IsHiddenButNotDeleting() const {
    auto l = LockForRead();
    return l->is_hidden_but_not_deleting();
  }

  // If the table is already hidden then treat it as a duplicate hide request.
  bool IgnoreHideRequest() {
    auto l = LockForRead();
    if (l->started_hiding()) {
      LOG(INFO) << "Table " << id() << " is already hidden. Duplicate request.";
      return true;
    }
    return false;
  }

  HybridTime hide_hybrid_time() const;

  std::string ToString() const override;
  std::string ToStringWithState() const;

  NamespaceId namespace_id() const;
  NamespaceName namespace_name() const;

  ColocationId GetColocationId() const;

  Result<Schema> GetSchema() const;

  bool has_pgschema_name() const;

  const std::string pgschema_name() const;

  // True if all the column schemas have pg_type_oid set.
  bool has_pg_type_oid() const;

  TableId pg_table_id() const;
  // True if the table is a materialized view.
  bool is_matview() const;

  // Return the table's ID. Does not require synchronization.
  virtual const TableId& id() const override { return table_id_; }

  // Return the indexed table id if the table is an index table. Otherwise, return an empty string.
  TableId indexed_table_id() const;

  bool is_index() const {
    return !indexed_table_id().empty();
  }

  // For index table
  bool is_local_index() const;
  bool is_unique_index() const;
  bool is_vector_index() const;

  void set_is_system() { is_system_ = true; }
  bool is_system() const { return is_system_; }

  // True if the table is colocated (including tablegroups, excluding YSQL system tables). This is
  // cached in memory separately from the underlying proto with the expectation it will never
  // change.
  bool colocated() const { return colocated_; }

  // Helper for returning the relfilenode OID of the table. Relfilenode OID diverges from PG table
  // OID after a table rewrite.
  // Note: For system tables, this simply returns the PG table OID. Table rewrite is not permitted
  // on system tables.
  Result<uint32_t> GetPgRelfilenodeOid() const;

  // Helper for returning the PG OID of the table. In case the table was rewritten,
  // we cannot directly infer the PG OID from the table ID. Instead, we need to use the
  // stored pg_table_id field.
  Result<uint32_t> GetPgTableOid() const;

  // Helper for returning all OIDs for the PG Table.
  Result<PgTableAllOids> GetPgTableAllOids() const;

  // Return the table type of the table.
  TableType GetTableType() const;

  // Checks if the table is the internal redis table.
  bool IsRedisTable() const {
    return GetTableType() == REDIS_TABLE_TYPE;
  }

  bool IsBeingDroppedDueToDdlTxn(const std::string& txn_id_pb, bool txn_success) const;

  bool IsBeingDroppedDueToSubTxnRollback(
      const std::string& txn_id_pb, const SubTransactionId sub_transaction_id) const;

  // Add a tablet to this table.
  Status AddTablet(const TabletInfoPtr& tablet);

  // Finds a tablet whose partition can be shrunk.
  // This is only used for transaction status tables.
  Result<TabletWithSplitPartitions> FindSplittableHashPartitionForStatusTable() const;

  // Add a tablet to this table, by shrinking old_tablet's partition to the passed in partition.
  // new_tablet's partition should be the remainder of old_tablet's original partition.
  // This should only be used for transaction status tables, where the partition ranges
  // are not actually used.
  void AddStatusTabletViaSplitPartition(TabletInfoPtr old_tablet,
                                        const dockv::Partition& partition,
                                        const TabletInfoPtr& new_tablet);

  // Replace existing tablet with a new one.
  Status ReplaceTablet(const TabletInfoPtr& old_tablet, const TabletInfoPtr& new_tablet);

  // Add multiple tablets to this table.
  Status AddTablets(const TabletInfos& tablets);

  // Removes the tablet from 'partitions_' and 'tablets_' structures.
  // Return true if the tablet was removed from 'partitions_'.
  // If deactivate_only is set to true then it only
  // deactivates the tablet (i.e. removes it only from partitions_ and not from tablets_).
  // See the declaration of partitions_ structure to understand what constitutes inactive tablets.
  Result<bool> RemoveTablet(
      const TabletId& tablet_id, DeactivateOnly deactivate_only = DeactivateOnly::kFalse);

  // Remove multiple tablets from this table.
  // Return true if all given tablets were removed from 'partitions_'.
  Result<bool> RemoveTablets(
      const TabletInfos& tablets, DeactivateOnly deactivate_only = DeactivateOnly::kFalse);

  // This only returns tablets which are in RUNNING state.
  Result<TabletInfos> GetTabletsInRange(const GetTableLocationsRequestPB* req) const;
  Result<TabletInfos> GetTabletsInRange(
      const std::string& partition_key_start, const std::string& partition_key_end,
      size_t max_returned_locations = std::numeric_limits<size_t>::max()) const EXCLUDES(lock_);
  // Iterates through tablets_ and not partitions_, so there may be duplicates of key ranges.
  Result<TabletInfos> GetInactiveTabletsInRange(
      const std::string& partition_key_start, const std::string& partition_key_end,
      size_t max_returned_locations = std::numeric_limits<size_t>::max()) const EXCLUDES(lock_);

  std::size_t NumPartitions() const;
  // Return whether given partition start keys match partitions_.
  bool HasPartitions(const std::vector<PartitionKey> other) const;

  // Returns true if all active split children are running, and all non-active tablets (e.g. split
  // parents) have already been deleted / hidden.
  // This function should not be called for colocated tables with wait_for_parent_deletion set to
  // true, since colocated tablets are not deleted / hidden if the table is dropped (the tablet may
  // be part of another table).
  Result<bool> HasOutstandingSplits(bool wait_for_parent_deletion) const;

  // Get all tablets of the table.
  // If include_inactive is true then it also returns inactive tablets along with the active ones.
  // See the declaration of partitions_ structure to understand what constitutes inactive tablets.
  Result<TabletInfos> GetTablets(GetTabletsMode mode = GetTabletsMode::kOrderByPartitions) const;
  Result<TabletInfos> GetTabletsIncludeInactive() const;
  size_t TabletCount(IncludeInactive include_inactive = IncludeInactive::kFalse) const;

  // Get info of the specified index.
  qlexpr::IndexInfo GetIndexInfo(const TableId& index_id) const;

  // Get TableIds of all or a specific type of indexes.
  TableIds GetIndexIds() const;
  TableIds GetVectorIndexIds() const;

  // Returns true if all tablets of the table are deleted.
  Result<bool> AreAllTabletsDeleted() const;

  // Returns true if all tablets of the table are deleted or hidden.
  Result<bool> AreAllTabletsHidden() const;

  // Verify that all tablets in partitions_ are running. Newly created tablets (e.g. because of a
  // tablet split) might not be running.
  Status CheckAllActiveTabletsRunning() const;

  // Clears partitions_ and tablets_.
  // N.B.: The deletion flow removes tablets from the Catalog Manager's tablet map by removing all
  // tablets returned by TableInfo::TakeTablets of a DELETED table. So it is possible to leak
  // tablets in the tablet map by calling this function on a primary table.
  void ClearTabletMaps();

  // Returns the value of the tablets_ map and clears partitions_ and tablets_.
  std::map<TabletId, std::weak_ptr<TabletInfo>> TakeTablets();

  // Returns true if the table creation is in-progress.
  bool IsCreateInProgress() const;

  // Check if all tablets of the table are in RUNNING state.
  // new_running_tablets is the new set of tablets that are being transitioned to RUNNING state
  // (dirty copy is modified) and yet to be persisted.
  Result<bool> AreAllTabletsRunning(const std::set<TabletId>& new_running_tablets = {});

  // Returns true if the table is backfilling an index.
  bool IsBackfilling() const {
    SharedLock l(lock_);
    return is_backfilling_;
  }

  Status SetIsBackfilling();

  void ClearIsBackfilling() {
    std::lock_guard l(lock_);
    is_backfilling_ = false;
  }

  // Returns true if an "Alter" operation is in-progress.
  Result<bool> IsAlterInProgress(uint32_t version) const;

  // Set the Status related to errors on CreateTable.
  void SetCreateTableErrorStatus(const Status& status);

  // Get the Status of the last error from the current CreateTable.
  Status GetCreateTableErrorStatus() const;

  // Returns whether this is a type of table that will use tablespaces
  // for placement.
  bool TableTypeUsesTablespacesForPlacement() const;

  bool IsColocationParentTable() const;
  bool IsColocatedDbParentTable() const;
  bool IsTablegroupParentTable() const;

  // A table is a primary table if it appears in the table_id field of every tablet which hosts it.
  // Examples of primary tables are:
  //   non-colocated, user tables
  //   the parent table of a colocated database
  // Secondary tables are non-primary tables which are not on the master tablet.
  // Examples of secondary tables are:
  //   colocated user tables
  //   vector indices
  bool IsSecondaryTable() const;
  bool IsSequencesSystemTable() const;
  bool IsSequencesSystemTable(const ReadLock& lock) const;
  bool IsXClusterDDLReplicationDDLQueueTable() const;
  bool IsXClusterDDLReplicationReplicatedDDLsTable() const;
  bool IsXClusterDDLReplicationTable() const {
    return LockForRead()->IsXClusterDDLReplicationTable();
  }

  // Provides the ID of the tablespace that will be used to determine
  // where the tablets for this table should be placed when the table
  // is first being created.
  TablespaceId TablespaceIdForTableCreation() const;

  // Set the tablespace to use during table creation. This will determine
  // where the tablets of the newly created table should reside.
  void SetTablespaceIdForTableCreation(const TablespaceId& tablespace_id);

  void SetMatview();

  google::protobuf::RepeatedField<int> GetHostedStatefulServices() const;

  bool AttachedYCQLIndexDeletionInProgress(const TableId& index_table_id) const;

  void AddDdlTxnWaitingForSchemaVersion(int schema_version,
                                        const TransactionId& txn) EXCLUDES(lock_);

  std::vector<TransactionId> EraseDdlTxnsWaitingForSchemaVersion(
      int schema_version) EXCLUDES(lock_);

  void AddDdlTxnForRollbackToSubTxnWaitingForSchemaVersion(
      int schema_version, const TransactionId& txn) EXCLUDES(lock_);

  TransactionId EraseDdlTxnForRollbackToSubTxnWaitingForSchemaVersion(
      int schema_version) EXCLUDES(lock_);

  bool IsUserCreated() const;
  bool IsUserTable() const;
  bool IsUserIndex() const;
  bool HasUserSpecifiedPrimaryKey() const;

  bool IsUserCreated(const ReadLock& lock) const;
  bool IsUserTable(const ReadLock& lock) const;
  bool IsUserIndex(const ReadLock& lock) const;
  bool HasUserSpecifiedPrimaryKey(const ReadLock& lock) const;

 private:
  friend class RefCountedThreadSafe<TableInfo>;
  ~TableInfo();

  Status AddTabletUnlocked(const TabletInfoPtr& tablet) REQUIRES(lock_);
  Result<bool> RemoveTabletUnlocked(
      const TableId& tablet_id,
      DeactivateOnly deactivate_only = DeactivateOnly::kFalse) REQUIRES(lock_);

  std::string LogPrefix() const {
    return ToString() + ": ";
  }

  const TableId table_id_;

  // Sorted index of tablet start partition-keys to TabletInfo.
  // The TabletInfo objects are owned by the CatalogManager.
  // At any point in time it contains only the active tablets (defined in the comment on tablets_).
  std::map<PartitionKey, std::weak_ptr<TabletInfo>> partitions_ GUARDED_BY(lock_);
  // At any point in time it contains both active and inactive tablets.
  // Currently there are two cases for a tablet to be categorized as inactive:
  // 1) Not yet deleted split parent tablets for which we've already
  //    registered child split tablets.
  // 2) After a PITR restore, child tablets of a split which are inactive if PITR was to
  //    a time before the split when only parent existed
  // Tablets of HIDDEN tables that have been marked HIDDEN are considered active
  // with the introduction of DBClone, SELECT AS-OF features.
  // TODO(#24956) If a tablet T1[0,100] splits into T2[0,50] and T3[50,100] and later the table is
  // Hidden, the partitions_ structure may end up with T1 and T3 as start_keys are unique.
  // TODO(#15043): remove tablets from tablets_ once they have been deleted from all TServers.
  std::map<TabletId, std::weak_ptr<TabletInfo>> tablets_ GUARDED_BY(lock_);

  // Protects partitions_ and tablets_.
  mutable rw_spinlock lock_;

  // In memory state set during backfill to prevent multiple backfill jobs.
  bool is_backfilling_ = false;

  std::atomic<bool> is_system_{false};

  const bool colocated_;

  // The last error Status of the currently running CreateTable. Will be OK, if freshly constructed
  // object, or if the CreateTable was successful.
  Status create_table_error_;

  // This field denotes the tablespace id that the user specified while
  // creating the table. This will be used only to place tablets at the time
  // of table creation. At all other times, this information needs to be fetched
  // from PG catalog tables because the user may have used Alter Table to change
  // the table's tablespace.
  TablespaceId tablespace_id_for_table_creation_;

  // This field denotes the table is under xcluster bootstrapping. This is used to prevent create
  // table from completing. Not needed once D23712 lands.
  std::atomic_bool bootstrapping_xcluster_replication_ = false;

  // Store a map of schema version->TransactionId of the DDL transaction that initiated the
  // change. When a schema version n has propagated to all tablets, we use this map to signal all
  // the DDL transactions waiting for schema version n and any k < n. The map is ordered by schema
  // version to easily figure out all the entries for schema version < n.
  std::map<int, TransactionId> ddl_txns_waiting_for_schema_version_ GUARDED_BY(lock_);

  // Stores a map of schema version->TransactionId of the DDL transactions that initiated the
  // change due to a rollback to sub-transaction operation. When a schema version n has propagated
  // to all tablets, we use this map to signal such DDL transactions and mark the rollback to
  // sub-transaction operation successful.
  // Note that this map can be merged with the above ddl_txns_waiting_for_schema_version_ map but
  // then we'd have to track why the DDL txn is waiting for the schema version - txn completion or
  // rollback to sub-txn.
  std::map<int, TransactionId> ddl_txns_for_subtxn_rollback_waiting_for_schema_version_
      GUARDED_BY(lock_);

  DISALLOW_COPY_AND_ASSIGN(TableInfo);
};


// The data related to a namespace which is persisted on disk.
// This portion of NamespaceInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentNamespaceInfo : public Persistent<SysNamespaceEntryPB> {
  // Get the namespace name.
  const NamespaceName& name() const {
    return pb.name();
  }

  YQLDatabase database_type() const {
    return pb.database_type();
  }

  bool colocated() const {
    return pb.colocated();
  }
};

using TableInfoWithWriteLock = CowObjectWithWriteLock<TableInfo>;

// The information about a namespace.
//
// This object uses copy-on-write techniques similarly to TabletInfo.
// Please see the TabletInfo class doc above for more information.
class NamespaceInfo : public RefCountedThreadSafe<NamespaceInfo>,
                      public MetadataCowWrapper<PersistentNamespaceInfo>,
                      public CatalogEntityWithTasks {
 public:
  explicit NamespaceInfo(NamespaceId ns_id, scoped_refptr<TasksTracker> tasks_tracker);

  virtual const NamespaceId& id() const override { return namespace_id_; }

  const NamespaceName name() const;

  YQLDatabase database_type() const;

  bool colocated() const;

  ::yb::master::SysNamespaceEntryPB_State state() const;

  ::yb::master::SysNamespaceEntryPB_YsqlNextMajorVersionState ysql_next_major_version_state() const;

  std::string ToString() const override;

 private:
  friend class RefCountedThreadSafe<NamespaceInfo>;
  ~NamespaceInfo() = default;

  // The ID field is used in the sys_catalog table.
  const NamespaceId namespace_id_;

  DISALLOW_COPY_AND_ASSIGN(NamespaceInfo);
};

// The data related to a User-Defined Type which is persisted on disk.
// This portion of UDTypeInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentUDTypeInfo : public Persistent<SysUDTypeEntryPB> {
  // Return the type's name.
  const UDTypeName& name() const {
    return pb.name();
  }

  // Return the table's namespace id.
  const NamespaceId& namespace_id() const {
    return pb.namespace_id();
  }

  int field_names_size() const {
    return pb.field_names_size();
  }

  const std::string& field_names(int index) const {
    return pb.field_names(index);
  }

  int field_types_size() const {
    return pb.field_types_size();
  }

  const QLTypePB& field_types(int index) const {
    return pb.field_types(index);
  }
};

class UDTypeInfo : public RefCountedThreadSafe<UDTypeInfo>,
                   public MetadataCowWrapper<PersistentUDTypeInfo> {
 public:
  explicit UDTypeInfo(UDTypeId udtype_id);

  // Return the user defined type's ID. Does not require synchronization.
  virtual const std::string& id() const override { return udtype_id_; }

  const UDTypeName name() const;

  const NamespaceId namespace_id() const;

  int field_names_size() const;

  const std::string field_names(int index) const;

  int field_types_size() const;

  const QLTypePB field_types(int index) const;

  std::string ToString() const override;

 private:
  friend class RefCountedThreadSafe<UDTypeInfo>;
  ~UDTypeInfo() = default;

  // The ID field is used in the sys_catalog table.
  const UDTypeId udtype_id_;

  DISALLOW_COPY_AND_ASSIGN(UDTypeInfo);
};

// This wraps around the proto containing information about what locks have been taken.
// It will be used for LockObject persistence.
struct PersistentObjectLockInfo : public Persistent<SysObjectLockEntryPB> {};

class ObjectLockInfo : public MetadataCowWrapper<PersistentObjectLockInfo> {
 public:
  explicit ObjectLockInfo(const std::string& ts_uuid)
      : ts_uuid_(ts_uuid), ysql_lease_deadline_(MonoTime::Min()) {}
  ~ObjectLockInfo() = default;

  // Return the user defined type's ID. Does not require synchronization.
  virtual const std::string& id() const override { return ts_uuid_; }

  Result<std::variant<ObjectLockInfo::WriteLock, SysObjectLockEntryPB::LeaseInfoPB>>
  RefreshYsqlOperationLease(const NodeInstancePB& instance, MonoDelta lease_ttl) EXCLUDES(mutex_);

  virtual void Load(const SysObjectLockEntryPB& metadata) override;

  MonoTime ysql_lease_deadline() const EXCLUDES(mutex_);

 private:
  // The ID field is used in the sys_catalog table.
  const std::string ts_uuid_;

  mutable simple_spinlock mutex_;
  MonoTime ysql_lease_deadline_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(ObjectLockInfo);
};

// This wraps around the proto containing cluster level config information. It will be used for
// CowObject managed access.
struct PersistentClusterConfigInfo : public Persistent<SysClusterConfigEntryPB> {};

// This is the in memory representation of the cluster config information serialized proto data,
// using metadata() for CowObject access.
class ClusterConfigInfo : public SingletonMetadataCowWrapper<PersistentClusterConfigInfo> {};

struct PersistentRedisConfigInfo : public Persistent<SysRedisConfigEntryPB> {};

class RedisConfigInfo : public RefCountedThreadSafe<RedisConfigInfo>,
                        public MetadataCowWrapper<PersistentRedisConfigInfo> {
 public:
  explicit RedisConfigInfo(const std::string key) : config_key_(key) {}

  virtual const std::string& id() const override { return config_key_; }

 private:
  friend class RefCountedThreadSafe<RedisConfigInfo>;
  ~RedisConfigInfo() = default;

  const std::string config_key_;

  DISALLOW_COPY_AND_ASSIGN(RedisConfigInfo);
};

struct PersistentRoleInfo : public Persistent<SysRoleEntryPB> {};

class RoleInfo : public RefCountedThreadSafe<RoleInfo>,
                 public MetadataCowWrapper<PersistentRoleInfo> {
 public:
  explicit RoleInfo(const std::string& role) : role_(role) {}
  const std::string& id() const override { return role_; }

 private:
  friend class RefCountedThreadSafe<RoleInfo>;
  ~RoleInfo() = default;

  const std::string role_;

  DISALLOW_COPY_AND_ASSIGN(RoleInfo);
};

struct PersistentSysConfigInfo : public Persistent<SysConfigEntryPB> {};

class SysConfigInfo : public RefCountedThreadSafe<SysConfigInfo>,
                      public MetadataCowWrapper<PersistentSysConfigInfo> {
 public:
  explicit SysConfigInfo(const std::string& config_type) : config_type_(config_type) {}
  const std::string& id() const override { return config_type_; /* config type is the entry id */ }

 private:
  friend class RefCountedThreadSafe<SysConfigInfo>;
  ~SysConfigInfo() = default;

  const std::string config_type_;

  DISALLOW_COPY_AND_ASSIGN(SysConfigInfo);
};

class DdlLogEntry {
 public:
  // time - when DDL operation was started.
  // table_id - modified table id.
  // table - what table was modified during DDL.
  // action - string description of DDL.
  DdlLogEntry(
      HybridTime time, const TableId& table_id, const SysTablesEntryPB& table,
      const std::string& action);

  static SysRowEntryType type() {
    return SysRowEntryType::DDL_LOG_ENTRY;
  }

  std::string id() const;

  // Used by sys catalog writer. It requires 2 protobuf to check whether entry was actually changed.
  const DdlLogEntryPB& new_pb() const;
  const DdlLogEntryPB& old_pb() const;

 protected:
  DdlLogEntryPB pb_;
};

// Helper class to commit Info mutations at the end of a scope.
template <class Info>
class ScopedInfoCommitter {
 public:
  typedef std::shared_ptr<Info> InfoPtr;
  typedef std::vector<InfoPtr> Infos;
  explicit ScopedInfoCommitter(const Infos* infos) : infos_(DCHECK_NOTNULL(infos)), done_(false) {}
  ~ScopedInfoCommitter() {
    if (!done_) {
      Commit();
    }
  }
  // This method is not thread safe. Must be called by the same thread
  // that would destroy this instance.
  void Abort() {
    if (PREDICT_TRUE(!done_)) {
      for (const InfoPtr& info : *infos_) {
        info->mutable_metadata()->AbortMutation();
      }
    }
    done_ = true;
  }
  void Commit() {
    if (PREDICT_TRUE(!done_)) {
      for (const InfoPtr& info : *infos_) {
        info->mutable_metadata()->CommitMutation();
      }
    }
    done_ = true;
  }
 private:
  const Infos* infos_;
  bool done_;
};

// Convenience typedefs.
// Table(t)InfoMap ordered for deterministic locking.
typedef std::pair<NamespaceId, TableName> TableNameKey;
typedef std::unordered_map<
    TableNameKey, scoped_refptr<TableInfo>, boost::hash<TableNameKey>> TableInfoByNameMap;

typedef std::unordered_map<UDTypeId, scoped_refptr<UDTypeInfo>> UDTypeInfoMap;
typedef std::pair<NamespaceId, UDTypeName> UDTypeNameKey;
typedef std::unordered_map<
    UDTypeNameKey, scoped_refptr<UDTypeInfo>, boost::hash<UDTypeNameKey>> UDTypeInfoByNameMap;

template <class Info>
void FillInfoEntry(const Info& info, SysRowEntry* entry) {
  entry->set_id(info.id());
  entry->set_type(info.metadata().state().type());
  entry->set_data(info.metadata().state().pb.SerializeAsString());
}

template <class Info>
auto AddInfoEntryToPB(Info* info, google::protobuf::RepeatedPtrField<SysRowEntry>* out) {
  auto lock = info->LockForRead();
  FillInfoEntry(*info, out->Add());
  return lock;
}

struct SplitTabletIds {
  const TabletId& source;
  const std::vector<TabletId>& children;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(source, children);
  }
};

// This wraps around the proto containing CDC stream information. It will be used for
// CowObject managed access.
struct PersistentCDCStreamInfo : public Persistent<SysCDCStreamEntryPB> {
  const google::protobuf::RepeatedPtrField<std::string>& table_id() const {
    return pb.table_id();
  }

  const google::protobuf::RepeatedPtrField<std::string>& unqualified_table_id() const {
    return pb.unqualified_table_id();
  }

  const NamespaceId& namespace_id() const {
    return pb.namespace_id();
  }

  bool started_deleting() const {
    return pb.state() == SysCDCStreamEntryPB::DELETING ||
        pb.state() == SysCDCStreamEntryPB::DELETED;
  }

  bool is_deleting() const {
    return pb.state() == SysCDCStreamEntryPB::DELETING;
  }

  bool is_deleted() const {
    return pb.state() == SysCDCStreamEntryPB::DELETED;
  }

  bool is_deleting_metadata() const {
    return pb.state() == SysCDCStreamEntryPB::DELETING_METADATA;
  }

  const google::protobuf::RepeatedPtrField<CDCStreamOptionsPB> options() const {
    return pb.options();
  }

  cdc::StreamModeTransactional transactional() const {
    return cdc::StreamModeTransactional(pb.transactional());
  }
};

class CDCStreamInfo : public RefCountedThreadSafe<CDCStreamInfo>,
                      public MetadataCowWrapper<PersistentCDCStreamInfo> {
 public:
  explicit CDCStreamInfo(const xrepl::StreamId& stream_id)
      : stream_id_(stream_id), stream_id_str_(stream_id.ToString()) {}

  const std::string& id() const override { return stream_id_str_; }
  const xrepl::StreamId& StreamId() const { return stream_id_; }

  const google::protobuf::RepeatedPtrField<std::string> table_id() const;

  const NamespaceId namespace_id() const;

  const ReplicationSlotName GetCdcsdkYsqlReplicationSlotName() const;

  bool IsConsistentSnapshotStream() const;

  const google::protobuf::Map<::std::string, ::yb::PgReplicaIdentity> GetReplicaIdentityMap() const;

  bool IsDynamicTableAdditionDisabled() const;

  bool IsTablesWithoutPrimaryKeyAllowed() const;

  std::string ToString() const override;

  bool IsXClusterStream() const;
  bool IsCDCSDKStream() const;

  HybridTime GetConsistentSnapshotHybridTime() const;

 private:
  friend class RefCountedThreadSafe<CDCStreamInfo>;
  ~CDCStreamInfo() = default;

  const xrepl::StreamId stream_id_;
  const std::string stream_id_str_;

  DISALLOW_COPY_AND_ASSIGN(CDCStreamInfo);
};

typedef scoped_refptr<CDCStreamInfo> CDCStreamInfoPtr;

class UniverseReplicationInfoBase {
 public:
  Result<std::shared_ptr<XClusterRpcTasks>> GetOrCreateXClusterRpcTasks(
      google::protobuf::RepeatedPtrField<HostPortPB> producer_masters);

 protected:
  explicit UniverseReplicationInfoBase(xcluster::ReplicationGroupId replication_group_id)
      : replication_group_id_(std::move(replication_group_id)) {}

  virtual ~UniverseReplicationInfoBase() = default;

  const xcluster::ReplicationGroupId replication_group_id_;

  std::shared_ptr<XClusterRpcTasks> xcluster_rpc_tasks_;
  std::string master_addrs_;

  // Protects xcluster_rpc_tasks_.
  mutable rw_spinlock lock_;
};

// This wraps around the proto containing universe replication information. It will be used for
// CowObject managed access.
struct PersistentUniverseReplicationInfo
    : public Persistent<SysUniverseReplicationEntryPB> {
  bool is_deleted_or_failed() const {
    return pb.state() == SysUniverseReplicationEntryPB::DELETED
      || pb.state() == SysUniverseReplicationEntryPB::DELETED_ERROR
      || pb.state() == SysUniverseReplicationEntryPB::FAILED;
  }

  bool is_active() const {
    return pb.state() == SysUniverseReplicationEntryPB::ACTIVE;
  }

  bool IsDbScoped() const;
  bool IsAutomaticDdlMode() const;
};

class UniverseReplicationInfo : public UniverseReplicationInfoBase,
                                public RefCountedThreadSafe<UniverseReplicationInfo>,
                                public MetadataCowWrapper<PersistentUniverseReplicationInfo> {
 public:
  explicit UniverseReplicationInfo(xcluster::ReplicationGroupId replication_group_id)
      : UniverseReplicationInfoBase(std::move(replication_group_id)) {}

  const std::string& id() const override { return replication_group_id_.ToString(); }
  const xcluster::ReplicationGroupId& ReplicationGroupId() const { return replication_group_id_; }

  std::string ToString() const override;

  // Set the Status related to errors on SetupUniverseReplication. Only the first error status is
  // preserved.
  void SetSetupUniverseReplicationErrorStatus(const Status& status);

  // Get the Status of the last error from the current SetupUniverseReplication.
  Status GetSetupUniverseReplicationErrorStatus() const;

  bool IsDbScoped() const;

  bool IsAutomaticDdlMode() const;

 private:
  friend class RefCountedThreadSafe<UniverseReplicationInfo>;
  virtual ~UniverseReplicationInfo() = default;

  // The last error Status of the currently running SetupUniverseReplication. Will be OK, if freshly
  // constructed object, or if the SetupUniverseReplication was successful.
  Status setup_universe_replication_error_ = Status::OK();

  DISALLOW_COPY_AND_ASSIGN(UniverseReplicationInfo);
};

// This wraps around the proto containing universe replication information. It will be used for
// CowObject managed access.
struct PersistentUniverseReplicationBootstrapInfo
    : public Persistent<SysUniverseReplicationBootstrapEntryPB> {
  bool is_deleted_or_failed() const {
    return pb.state() == SysUniverseReplicationBootstrapEntryPB::DELETED ||
           pb.state() == SysUniverseReplicationBootstrapEntryPB::DELETED_ERROR ||
           pb.state() == SysUniverseReplicationBootstrapEntryPB::FAILED;
  }

  bool is_done() const {
    return pb.state() == SysUniverseReplicationBootstrapEntryPB::DONE;
  }

  TxnSnapshotId old_snapshot_id() const {
    return pb.has_old_snapshot_id() ? TryFullyDecodeTxnSnapshotId(pb.old_snapshot_id())
                                    : TxnSnapshotId::Nil();
  }

  TxnSnapshotId new_snapshot_id() const {
    return pb.has_new_snapshot_id() ? TryFullyDecodeTxnSnapshotId(pb.new_snapshot_id())
                                    : TxnSnapshotId::Nil();
  }

  TxnSnapshotRestorationId restoration_id() const {
    return pb.has_restoration_id() ? TryFullyDecodeTxnSnapshotRestorationId(pb.restoration_id())
                                   : TxnSnapshotRestorationId::Nil();
  }

  LeaderEpoch epoch() const { return LeaderEpoch(pb.leader_term(), pb.pitr_count()); }

  SysUniverseReplicationBootstrapEntryPB::State state() const { return pb.state(); }
  SysUniverseReplicationBootstrapEntryPB::State failed_on() const { return pb.failed_on(); }

  void set_into_namespace_map(NamespaceMap* namespace_map) const;
  void set_into_ud_type_map(UDTypeMap* type_map) const;
  void set_into_tables_data(ExternalTableSnapshotDataMap* tables_data) const;

  void set_old_snapshot_id(const TxnSnapshotId& snapshot_id) {
    pb.set_old_snapshot_id(snapshot_id.data(), snapshot_id.size());
  }

  void set_new_snapshot_id(const TxnSnapshotId& snapshot_id) {
    pb.set_new_snapshot_id(snapshot_id.data(), snapshot_id.size());
  }

  void set_restoration_id(const TxnSnapshotRestorationId& restoration_id) {
    pb.set_restoration_id(restoration_id.data(), restoration_id.size());
  }

  void set_state(const SysUniverseReplicationBootstrapEntryPB::State& state) {
    pb.set_state(state);
  }

  void set_new_snapshot_objects(
      const NamespaceMap& namespace_map, const UDTypeMap& type_map,
      const ExternalTableSnapshotDataMap& tables_data);
};

class UniverseReplicationBootstrapInfo
    : public UniverseReplicationInfoBase,
      public RefCountedThreadSafe<UniverseReplicationBootstrapInfo>,
      public MetadataCowWrapper<PersistentUniverseReplicationBootstrapInfo> {
 public:
  explicit UniverseReplicationBootstrapInfo(xcluster::ReplicationGroupId replication_group_id)
      : UniverseReplicationInfoBase(std::move(replication_group_id)) {}
  UniverseReplicationBootstrapInfo(const UniverseReplicationBootstrapInfo&) = delete;
  UniverseReplicationBootstrapInfo& operator=(const UniverseReplicationBootstrapInfo&) = delete;

  const std::string& id() const override { return replication_group_id_.ToString(); }
  const xcluster::ReplicationGroupId& ReplicationGroupId() const { return replication_group_id_; }

  std::string ToString() const override;

  // Set the Status related to errors on SetupUniverseReplication.
  void SetReplicationBootstrapErrorStatus(const Status& status);

  // Get the Status of the last error from the current SetupUniverseReplication.
  Status GetReplicationBootstrapErrorStatus() const;

  LeaderEpoch epoch() { return LockForRead()->epoch(); }

 private:
  friend class RefCountedThreadSafe<UniverseReplicationBootstrapInfo>;
  ~UniverseReplicationBootstrapInfo() = default;

  // The last error Status of the currently running SetupUniverseReplication. Will be OK, if freshly
  // constructed object, or if the SetupUniverseReplication was successful.
  Status replication_bootstrap_error_ = Status::OK();
};

// The data related to a snapshot which is persisted on disk.
// This portion of SnapshotInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentSnapshotInfo : public Persistent<SysSnapshotEntryPB> {
  SysSnapshotEntryPB::State state() const {
    return pb.state();
  }

  const std::string& state_name() const {
    return SysSnapshotEntryPB::State_Name(state());
  }

  bool is_creating() const {
    return state() == SysSnapshotEntryPB::CREATING;
  }

  bool started_deleting() const {
    return state() == SysSnapshotEntryPB::DELETING ||
           state() == SysSnapshotEntryPB::DELETED;
  }

  bool is_failed() const {
    return state() == SysSnapshotEntryPB::FAILED;
  }

  bool is_cancelled() const {
    return state() == SysSnapshotEntryPB::CANCELLED;
  }

  bool is_complete() const {
    return state() == SysSnapshotEntryPB::COMPLETE;
  }

  bool is_restoring() const {
    return state() == SysSnapshotEntryPB::RESTORING;
  }

  bool is_deleting() const {
    return state() == SysSnapshotEntryPB::DELETING;
  }
};

// The information about a snapshot.
//
// This object uses copy-on-write techniques similarly to TabletInfo.
// Please see the TabletInfo class doc above for more information.
class SnapshotInfo : public RefCountedThreadSafe<SnapshotInfo>,
                     public MetadataCowWrapper<PersistentSnapshotInfo> {
 public:
  explicit SnapshotInfo(SnapshotId id);

  virtual const std::string& id() const override { return snapshot_id_; };

  SysSnapshotEntryPB::State state() const;

  const std::string state_name() const;

  std::string ToString() const override;

  // Returns true if the snapshot creation is in-progress.
  bool IsCreateInProgress() const;

  // Returns true if the snapshot restoring is in-progress.
  bool IsRestoreInProgress() const;

  // Returns true if the snapshot deleting is in-progress.
  bool IsDeleteInProgress() const;

 private:
  friend class RefCountedThreadSafe<SnapshotInfo>;
  ~SnapshotInfo() = default;

  // The ID field is used in the sys_catalog table.
  const SnapshotId snapshot_id_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotInfo);
};

bool IsReplicationInfoSet(const ReplicationInfoPB& replication_info);

// Instantiates a new tablet without any write locks or starting mutation.
// If tablet_id is not specified, generates a new id via GenerateObjectId().
TabletInfoPtr MakeUnlockedTabletInfo(
    const TableInfoPtr& table,
    const TabletId& tablet_id = TabletId());

// Leaves the tablet "write locked" with the new info in the "dirty" state field.
TabletInfoPtr MakeTabletInfo(
    const TableInfoPtr& table,
    const TabletId& tablet_id = TabletId());

// Helper for creating the initial TabletInfo state.
// Leaves the tablet "write locked" with the new info in the "dirty" state field.
TabletInfoPtr CreateTabletInfo(
    const TableInfoPtr& table,
    const PartitionPB& partition,
    SysTabletsEntryPB::State state = SysTabletsEntryPB::PREPARING,
    const TabletId& tablet_id = TabletId());

// Expects tablet to be "write locked".
void SetupTabletInfo(
    TabletInfo& tablet,
    const TableInfo& table,
    const PartitionPB& partition,
    SysTabletsEntryPB::State state);

} // namespace yb::master
