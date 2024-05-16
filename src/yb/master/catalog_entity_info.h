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

#pragma once

#include <mutex>
#include <vector>

#include <boost/bimap.hpp>

#include "yb/cdc/cdc_types.h"
#include "yb/cdc/xcluster_types.h"
#include "yb/master/catalog_entity_base.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.pb.h"
#include "yb/qlexpr/index.h"
#include "yb/dockv/partition.h"
#include "yb/common/snapshot.h"
#include "yb/common/transaction.h"

#include "yb/consensus/consensus_types.pb.h"

#include "yb/master/master_client.fwd.h"
#include "yb/master/master_fwd.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/tasks_tracker.h"

#include "yb/server/monitored_task.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/util/cow_object.h"
#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"
#include "yb/util/shared_lock.h"

DECLARE_bool(use_parent_table_id_field);

namespace yb {
namespace master {

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
  typedef std::pair<std::string, std::string> PartitionKeys;
  typedef std::map<PartitionKeys, TabletId> PartitionToIdMap;
  std::vector<std::pair<TabletId, PartitionPB>> old_tablets;
  PartitionToIdMap new_tablets_map;
  // Mapping: Old tablet ID -> New tablet ID.
  std::optional<ImportSnapshotMetaResponsePB::TableMetaPB> table_meta = std::nullopt;
};
typedef std::unordered_map<TableId, ExternalTableSnapshotData> ExternalTableSnapshotDataMap;

struct ExternalNamespaceSnapshotData {
  ExternalNamespaceSnapshotData() : db_type(YQL_DATABASE_UNKNOWN), just_created(false) {}

  NamespaceId new_namespace_id;
  YQLDatabase db_type;
  bool just_created;
};
// Map: old_namespace_id (key) -> new_namespace_id + db_type + created-flag.
typedef std::unordered_map<NamespaceId, ExternalNamespaceSnapshotData> NamespaceMap;

struct ExternalUDTypeSnapshotData {
  ExternalUDTypeSnapshotData() : just_created(false) {}

  UDTypeId new_type_id;
  SysUDTypeEntryPB type_entry_pb;
  bool just_created;
};
// Map: old_type_id (key) -> new_type_id + type_entry_pb + created-flag.
typedef std::unordered_map<UDTypeId, ExternalUDTypeSnapshotData> UDTypeMap;

struct TableDescription {
  scoped_refptr<NamespaceInfo> namespace_info;
  scoped_refptr<TableInfo> table_info;
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
};

// Drive usage information on a current replica of a tablet.
// This allows us to look at individual resource usage per replica of a tablet.
struct TabletReplicaDriveInfo {
  uint64 sst_files_size = 0;
  uint64 wal_files_size = 0;
  uint64 uncompressed_sst_file_size = 0;
  bool may_have_orphaned_post_split_data = true;
};

struct FullCompactionStatus {
  tablet::FullCompactionState full_compaction_state = tablet::FULL_COMPACTION_STATE_UNKNOWN;

  // Not valid if full_compaction_state == UNKNOWN.
  HybridTime last_full_compaction_time;
};

// Information on a current replica of a tablet.
// This is copyable so that no locking is needed.
struct TabletReplica {
  TSDescriptor* ts_desc;
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
struct PersistentTabletInfo : public Persistent<SysTabletsEntryPB, SysRowEntryType::TABLET> {
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

  // Helper to set the state of the tablet with a custom message.
  // Requires that the caller has prepared this object for write.
  // The change will only be visible after Commit().
  void set_state(SysTabletsEntryPB::State state, const std::string& msg);
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
class TabletInfo : public RefCountedThreadSafe<TabletInfo>,
                   public MetadataCowWrapper<PersistentTabletInfo> {
 public:
  TabletInfo(const scoped_refptr<TableInfo>& table, TabletId tablet_id);
  virtual const TabletId& id() const override { return tablet_id_; }

  const TabletId& tablet_id() const { return tablet_id_; }
  scoped_refptr<const TableInfo> table() const { return table_; }
  const scoped_refptr<TableInfo>& table() { return table_; }

  // Accessors for the latest known tablet replica locations.
  // These locations include only the members of the latest-reported Raft
  // configuration whose tablet servers have ever heartbeated to this Master.
  void SetReplicaLocations(std::shared_ptr<TabletReplicaMap> replica_locations);
  std::shared_ptr<const TabletReplicaMap> GetReplicaLocations() const;
  Result<TSDescriptor*> GetLeader() const;
  Result<TabletReplicaDriveInfo> GetLeaderReplicaDriveInfo() const;
  Result<TabletLeaderLeaseInfo> GetLeaderLeaseInfoIfLeader(const std::string& ts_uuid) const;

  // Replaces a replica in replica_locations_ map if it exists. Otherwise, it adds it to the map.
  void UpdateReplicaLocations(const TabletReplica& replica);

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

 private:
  friend class RefCountedThreadSafe<TabletInfo>;

  class LeaderChangeReporter;
  friend class LeaderChangeReporter;

  ~TabletInfo();
  TSDescriptor* GetLeaderUnlocked() const REQUIRES_SHARED(lock_);
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
  std::shared_ptr<TabletReplicaMap> replica_locations_ GUARDED_BY(lock_);

  // Reported schema version (in-memory only).
  std::unordered_map<TableId, uint32_t> reported_schema_version_ GUARDED_BY(lock_) = {};

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
struct PersistentTableInfo : public Persistent<SysTablesEntryPB, SysRowEntryType::TABLE> {
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
    // same. Changing it now will require all callers of this function to be aware of the new state.
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

  // Return the table's namespace id.
  const NamespaceId& namespace_id() const { return pb.namespace_id(); }
  // Return the table's namespace name.
  const NamespaceName& namespace_name() const { return pb.namespace_name(); }

  const SchemaPB& schema() const {
    return pb.schema();
  }

  const std::string& indexed_table_id() const;

  bool is_index() const;

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

  auto ysql_ddl_txn_verifier_state() const {
    // Currently DDL with savepoints is disabled, so this repeated field can have only 1 element.
    DCHECK_EQ(pb.ysql_ddl_txn_verifier_state_size(), 1);
    return pb.ysql_ddl_txn_verifier_state(0);
  }

  bool is_being_deleted_by_ysql_ddl_txn() const {
    return has_ysql_ddl_txn_verifier_state() &&
      ysql_ddl_txn_verifier_state().contains_drop_table_op();
  }

  bool is_being_created_by_ysql_ddl_txn() const {
    return has_ysql_ddl_txn_verifier_state() &&
      ysql_ddl_txn_verifier_state().contains_create_table_op();
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

  const std::string& state_name() const {
    return SysTablesEntryPB_State_Name(pb.state());
  }

  // Helper to set the state of the tablet with a custom message.
  void set_state(SysTablesEntryPB::State state, const std::string& msg);
};

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

  std::string ToString() const override;
  std::string ToStringWithState() const;

  const NamespaceId namespace_id() const;
  const NamespaceName namespace_name() const;

  ColocationId GetColocationId() const;

  const Status GetSchema(Schema* schema) const;

  bool has_pgschema_name() const;

  const std::string pgschema_name() const;

  // True if all the column schemas have pg_type_oid set.
  bool has_pg_type_oid() const;

  TableId pg_table_id() const;
  // True if the table is a materialized view.
  bool is_matview() const;

  // Return the table's ID. Does not require synchronization.
  virtual const std::string& id() const override { return table_id_; }

  // Return the indexed table id if the table is an index table. Otherwise, return an empty string.
  std::string indexed_table_id() const;

  bool is_index() const {
    return !indexed_table_id().empty();
  }

  // For index table
  bool is_local_index() const;
  bool is_unique_index() const;

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

  // Return the table type of the table.
  TableType GetTableType() const;

  // Checks if the table is the internal redis table.
  bool IsRedisTable() const {
    return GetTableType() == REDIS_TABLE_TYPE;
  }

  bool IsBeingDroppedDueToDdlTxn(const std::string& txn_id_pb, bool txn_success) const;

  // Add a tablet to this table.
  void AddTablet(const TabletInfoPtr& tablet);

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
  void ReplaceTablet(const TabletInfoPtr& old_tablet, const TabletInfoPtr& new_tablet);

  // Add multiple tablets to this table.
  void AddTablets(const TabletInfos& tablets);

  // Removes the tablet from 'partitions_' and 'tablets_' structures.
  // Return true if the tablet was removed from 'partitions_'.
  // If deactivate_only is set to true then it only
  // deactivates the tablet (i.e. removes it only from partitions_ and not from tablets_).
  // See the declaration of partitions_ structure to understand what constitutes inactive tablets.
  bool RemoveTablet(const TabletId& tablet_id,
                    DeactivateOnly deactivate_only = DeactivateOnly::kFalse);

  // Remove multiple tablets from this table.
  // Return true if all given tablets were removed from 'partitions_'.
  bool RemoveTablets(const TabletInfos& tablets,
                     DeactivateOnly deactivate_only = DeactivateOnly::kFalse);

  // This only returns tablets which are in RUNNING state.
  TabletInfos GetTabletsInRange(const GetTableLocationsRequestPB* req) const;
  TabletInfos GetTabletsInRange(
      const std::string& partition_key_start, const std::string& partition_key_end,
      int32_t max_returned_locations = std::numeric_limits<int32_t>::max()) const EXCLUDES(lock_);
  // Iterates through tablets_ and not partitions_, so there may be duplicates of key ranges.
  TabletInfos GetInactiveTabletsInRange(
      const std::string& partition_key_start, const std::string& partition_key_end,
      int32_t max_returned_locations = std::numeric_limits<int32_t>::max()) const EXCLUDES(lock_);

  std::size_t NumPartitions() const;
  // Return whether given partition start keys match partitions_.
  bool HasPartitions(const std::vector<PartitionKey> other) const;

  // Returns true if all active split children are running, and all non-active tablets (e.g. split
  // parents) have already been deleted / hidden.
  // This function should not be called for colocated tables with wait_for_parent_deletion set to
  // true, since colocated tablets are not deleted / hidden if the table is dropped (the tablet may
  // be part of another table).
  bool HasOutstandingSplits(bool wait_for_parent_deletion) const;

  // Get all tablets of the table.
  // If include_inactive is true then it also returns inactive tablets along with the active ones.
  // See the declaration of partitions_ structure to understand what constitutes inactive tablets.
  TabletInfos GetTablets(IncludeInactive include_inactive = IncludeInactive::kFalse) const;

  // Get the tablet of the table. The table must satisfy IsColocatedUserTable.
  TabletInfoPtr GetColocatedUserTablet() const;

  // Get info of the specified index.
  qlexpr::IndexInfo GetIndexInfo(const TableId& index_id) const;

  // Returns true if all tablets of the table are deleted.
  bool AreAllTabletsDeleted() const;

  // Returns true if all tablets of the table are deleted or hidden.
  bool AreAllTabletsHidden() const;

  // Verify that all tablets in partitions_ are running. Newly created tablets (e.g. because of a
  // tablet split) might not be running.
  Status CheckAllActiveTabletsRunning() const;

  // Clears partitons_ and tablets_.
  // If deactivate_only is set to true then clear only the partitions_.
  void ClearTabletMaps(DeactivateOnly deactivate_only = DeactivateOnly::kFalse);

  // Returns true if the table creation is in-progress.
  bool IsCreateInProgress() const;

  // Check if all tablets of the table are in RUNNING state.
  // new_running_tablets is the new set of tablets that are being transitioned to RUNNING state
  // (dirty copy is modified) and yet to be persisted.
  bool AreAllTabletsRunning(const std::set<TabletId>& new_running_tablets = {});

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
  bool IsAlterInProgress(uint32_t version) const;

  // Set the Status related to errors on CreateTable.
  void SetCreateTableErrorStatus(const Status& status);

  // Get the Status of the last error from the current CreateTable.
  Status GetCreateTableErrorStatus() const;

  std::size_t NumLBTasks() const;

  // Returns whether this is a type of table that will use tablespaces
  // for placement.
  bool UsesTablespacesForPlacement() const;

  bool IsColocationParentTable() const;
  bool IsColocatedDbParentTable() const;
  bool IsTablegroupParentTable() const;
  bool IsColocatedUserTable() const;

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

 private:
  friend class RefCountedThreadSafe<TableInfo>;
  ~TableInfo();

  void AddTabletUnlocked(const TabletInfoPtr& tablet) REQUIRES(lock_);
  bool RemoveTabletUnlocked(
      const TableId& tablet_id,
      DeactivateOnly deactivate_only = DeactivateOnly::kFalse) REQUIRES(lock_);

  std::string LogPrefix() const {
    return ToString() + ": ";
  }

  const TableId table_id_;

  // Sorted index of tablet start partition-keys to TabletInfo.
  // The TabletInfo objects are owned by the CatalogManager.
  // At any point in time it contains only the active tablets (defined in the comment on tablets_).
  std::map<PartitionKey, TabletInfo*> partitions_ GUARDED_BY(lock_);
  // At any point in time it contains both active and inactive tablets.
  // Currently there are two cases for a tablet to be categorized as inactive:
  // 1) Not yet deleted split parent tablets for which we've already
  //    registered child split tablets.
  // 2) Tablets that are marked as HIDDEN for PITR.
  std::unordered_map<TabletId, TabletInfo*> tablets_ GUARDED_BY(lock_);

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

  DISALLOW_COPY_AND_ASSIGN(TableInfo);
};

class DeletedTableInfo;
typedef std::pair<TabletServerId, TabletId> TabletKey;
typedef std::unordered_map<
    TabletKey, scoped_refptr<DeletedTableInfo>, boost::hash<TabletKey>> DeletedTabletMap;

class DeletedTableInfo : public RefCountedThreadSafe<DeletedTableInfo> {
 public:
  explicit DeletedTableInfo(const TableInfo* table);

  const TableId& id() const { return table_id_; }

  std::size_t NumTablets() const;
  bool HasTablets() const;

  void DeleteTablet(const TabletKey& key);

  void AddTabletsToMap(DeletedTabletMap* tablet_map);

 private:
  const TableId table_id_;

  // Protects tablet_set_.
  mutable simple_spinlock lock_;

  typedef std::unordered_set<TabletKey, boost::hash<TabletKey>> TabletSet;
  TabletSet tablet_set_ GUARDED_BY(lock_);
};

// The data related to a namespace which is persisted on disk.
// This portion of NamespaceInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentNamespaceInfo : public Persistent<
    SysNamespaceEntryPB, SysRowEntryType::NAMESPACE> {
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

  uint32_t FetchAndIncrementCloneSeqNo();

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
struct PersistentUDTypeInfo : public Persistent<SysUDTypeEntryPB, SysRowEntryType::UDTYPE> {
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

// This wraps around the proto containing cluster level config information. It will be used for
// CowObject managed access.
struct PersistentClusterConfigInfo : public Persistent<SysClusterConfigEntryPB,
                                                       SysRowEntryType::CLUSTER_CONFIG> {
};

// This is the in memory representation of the cluster config information serialized proto data,
// using metadata() for CowObject access.
class ClusterConfigInfo : public SingletonMetadataCowWrapper<PersistentClusterConfigInfo> {};

struct PersistentRedisConfigInfo
    : public Persistent<SysRedisConfigEntryPB, SysRowEntryType::REDIS_CONFIG> {};

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

struct PersistentRoleInfo : public Persistent<SysRoleEntryPB, SysRowEntryType::ROLE> {};

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

struct PersistentSysConfigInfo
    : public Persistent<SysConfigEntryPB, SysRowEntryType::SYS_CONFIG> {};

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
  typedef scoped_refptr<Info> InfoPtr;
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
  const std::pair<const TabletId&, const TabletId&> children;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(source, children);
  }
};

// This wraps around the proto containing CDC stream information. It will be used for
// CowObject managed access.
struct PersistentCDCStreamInfo : public Persistent<
    SysCDCStreamEntryPB, SysRowEntryType::CDC_STREAM> {
  const google::protobuf::RepeatedPtrField<std::string>& table_id() const {
    return pb.table_id();
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

  std::string ToString() const override;

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
    : public Persistent<SysUniverseReplicationEntryPB, SysRowEntryType::UNIVERSE_REPLICATION> {
  bool is_deleted_or_failed() const {
    return pb.state() == SysUniverseReplicationEntryPB::DELETED
      || pb.state() == SysUniverseReplicationEntryPB::DELETED_ERROR
      || pb.state() == SysUniverseReplicationEntryPB::FAILED;
  }

  bool is_active() const {
    return pb.state() == SysUniverseReplicationEntryPB::ACTIVE;
  }
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
    : public Persistent<
          SysUniverseReplicationBootstrapEntryPB, SysRowEntryType::UNIVERSE_REPLICATION_BOOTSTRAP> {
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
struct PersistentSnapshotInfo : public Persistent<SysSnapshotEntryPB, SysRowEntryType::SNAPSHOT> {
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

}  // namespace master
}  // namespace yb
