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

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional_fwd.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/opid.h"
#include "yb/common/opid.pb.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/schema_packing.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/metadata.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/mutex.h"

namespace yb {
namespace tablet {

using TableInfoMap = std::unordered_map<TableId, TableInfoPtr>;

extern const int64 kNoDurableMemStore;
extern const std::string kIntentsSubdir;
extern const std::string kIntentsDBSuffix;
extern const std::string kSnapshotsDirSuffix;

const uint64_t kNoLastFullCompactionTime = HybridTime::kMin.ToUint64();

YB_STRONGLY_TYPED_BOOL(Primary);
YB_STRONGLY_TYPED_BOOL(OnlyIfDirty);
YB_STRONGLY_TYPED_BOOL(LazySuperblockFlushEnabled);
YB_STRONGLY_TYPED_BOOL(SkipTableTombstoneCheck);

struct TableInfo {
 private:
  class PrivateTag {};
 public:
  // Table id, name and type.
  std::string table_id;
  std::string namespace_name;
  // namespace_id is currently used on the xcluster path to determine safe time on the apply
  // transaction path.
  NamespaceId namespace_id;
  std::string table_name;
  TableType table_type;
  Uuid cotable_id; // table_id as Uuid

  std::string log_prefix;
  // The table schema, secondary index map, index info (for index table only) and schema version.
  const std::shared_ptr<docdb::DocReadContext> doc_read_context;
  const std::shared_ptr<qlexpr::IndexMap> index_map;
  std::unique_ptr<qlexpr::IndexInfo> index_info;
  std::shared_ptr<dockv::ReaderProjection> unique_index_key_projection;
  SchemaVersion schema_version = 0;

  // Partition schema of the table.
  dockv::PartitionSchema partition_schema;

  // In case the table was rewritten, explicitly store the TableId containing the PG table OID
  // (as the table's TableId no longer matches).
  TableId pg_table_id;

  // Whether we can skip table tombstone check for this table
  // (only applies to colocated tables).
  SkipTableTombstoneCheck skip_table_tombstone_check;

  // A vector of column IDs that have been deleted, so that the compaction filter can free the
  // associated memory. As of 01/2019, deleted column IDs are persisted forever, even if all the
  // associated data has been discarded. In the future, we can garbage collect such column IDs to
  // make sure this vector doesn't grow too large.
  std::vector<DeletedColumn> deleted_cols;

  // We use the retention time from the primary table.
  uint32_t wal_retention_secs = 0;

  // Public ctor with private argument to allow std::make_shared, but prevent public usage.
  TableInfo(const std::string& log_prefix,
            TableType table_type,
            SkipTableTombstoneCheck skip_table_tombstone_check,
            PrivateTag);
  TableInfo(const std::string& tablet_log_prefix,
            Primary primary,
            std::string table_id,
            std::string namespace_name,
            std::string table_name,
            TableType table_type,
            const Schema& schema,
            const qlexpr::IndexMap& index_map,
            const boost::optional<qlexpr::IndexInfo>& index_info,
            SchemaVersion schema_version,
            dockv::PartitionSchema partition_schema,
            TableId pg_table_id,
            SkipTableTombstoneCheck skip_table_tombstone_check);
  TableInfo(const TableInfo& other,
            const Schema& schema,
            const qlexpr::IndexMap& index_map,
            const std::vector<DeletedColumn>& deleted_cols,
            SchemaVersion schema_version);
  TableInfo(const TableInfo& other,
            const Schema& schema);
  TableInfo(const TableInfo& other, SchemaVersion min_schema_version);
  ~TableInfo();

  static Result<TableInfoPtr> LoadFromPB(
      const std::string& tablet_log_prefix, const TableId& primary_table_id, const TableInfoPB& pb);
  void ToPB(TableInfoPB* pb) const;

  Status MergeSchemaPackings(const TableInfoPB& pb, dockv::OverwriteSchemaPacking overwrite);

  std::string ToString() const {
    TableInfoPB pb;
    ToPB(&pb);
    return pb.ShortDebugString();
  }

  // If schema version is kLatestSchemaVersion, then latest possible schema packing is returned.
  static Result<docdb::CompactionSchemaInfo> Packing(
      const TableInfoPtr& self, uint32_t schema_version, HybridTime history_cutoff);

  const Schema& schema() const;
  SchemaPtr SharedSchema() const;

  Result<SchemaVersion> GetSchemaPackingVersion(const Schema& schema) const;

  const std::string& LogPrefix() const {
    return log_prefix;
  }

  bool primary() const {
    return cotable_id.IsNil();
  }

  // Should account for every field in TableInfo.
  static bool TEST_Equals(const TableInfo& lhs, const TableInfo& rhs);

 private:
  Status DoLoadFromPB(Primary primary, const TableInfoPB& pb);

  void CompleteInit();
};

// Describes KV-store. Single KV-store is backed by one or two RocksDB instances, depending on
// whether distributed transactions are enabled for the table. KV-store for sys catalog could
// contain multiple tables.
struct KvStoreInfo {
  explicit KvStoreInfo(const KvStoreId& kv_store_id_) : kv_store_id(kv_store_id_) {}

  KvStoreInfo(const KvStoreId& kv_store_id_, const std::string& rocksdb_dir_,
              const std::vector<SnapshotScheduleId>& snapshot_schedules_)
      : kv_store_id(kv_store_id_),
        rocksdb_dir(rocksdb_dir_),
        snapshot_schedules(snapshot_schedules_.begin(), snapshot_schedules_.end()) {}

  Status LoadFromPB(const std::string& tablet_log_prefix,
                    const KvStoreInfoPB& pb,
                    const TableId& primary_table_id,
                    bool local_superblock);

  Status MergeWithRestored(
      const KvStoreInfoPB& snapshot_kvstoreinfo, const TableId& primary_table_id, bool colocated,
      dockv::OverwriteSchemaPacking overwrite);

  Status RestoreMissingValuesAndMergeTableSchemaPackings(
      const KvStoreInfoPB& snapshot_kvstoreinfo, const TableId& primary_table_id, bool colocated,
      dockv::OverwriteSchemaPacking overwrite);

  // Given the table info from the tablet metadata in a snapshot, return the corresponding live
  // table.  Used to map tables that existed in the snapshot to tables that are live in the current
  // cluster when restoring a colocated tablet in order to merge schema packings.
  // If nullptr is returned, the input table can be ignored.
  Result<TableInfo*> FindMatchingTable(
      const TableInfoPB& snapshot_table, const TableId& primary_table_id);

  Status LoadTablesFromPB(
      const std::string& tablet_log_prefix,
      const google::protobuf::RepeatedPtrField<TableInfoPB>& pbs, const TableId& primary_table_id);

  void ToPB(const TableId& primary_table_id, KvStoreInfoPB* pb) const;

  // Updates colocation map with new table info.
  void UpdateColocationMap(const TableInfoPtr& table_info);

  KvStoreId kv_store_id;

  // The directory where the regular RocksDB data for this KV-store is stored. For KV-stores having
  // tables with distributed transactions enabled an additional RocksDB is created in directory at
  // `rocksdb_dir + kIntentsDBSuffix` path.
  std::string rocksdb_dir;

  // Optional inclusive lower bound and exclusive upper bound for keys served by this KV-store.
  // See docdb::KeyBounds.
  std::string lower_bound_key;
  std::string upper_bound_key;

  // See KvStoreInfoPB field with the same name.
  bool parent_data_compacted = false;

  // See KvStoreInfoPB field with the same name.
  uint64_t last_full_compaction_time = kNoLastFullCompactionTime;

  // See KvStoreInfoPB field with the same name.
  std::optional<uint64_t> post_split_compaction_file_number_upper_bound = 0;

  // Map of tables sharing this KV-store indexed by the table id.
  // If pieces of the same table live in the same Raft group they should be located in different
  // KV-stores.
  TableInfoMap tables;

  // Mapping form colocation id to table info.
  std::unordered_map<ColocationId, TableInfoPtr> colocation_to_table;

  std::unordered_set<SnapshotScheduleId, SnapshotScheduleIdHash> snapshot_schedules;

  // Should account for every field in KvStoreInfo.
  static bool TEST_Equals(const KvStoreInfo& lhs, const KvStoreInfo& rhs);
};

struct RaftGroupMetadataData {
  FsManager* fs_manager;
  TableInfoPtr table_info;
  RaftGroupId raft_group_id;
  dockv::Partition partition;
  TabletDataState tablet_data_state;
  bool colocated = false;
  std::vector<SnapshotScheduleId> snapshot_schedules;
  std::unordered_set<StatefulServiceKind> hosted_services;
};

// At startup, the TSTabletManager will load a RaftGroupMetadata for each
// super block found in the tablets/ directory, and then instantiate
// Raft groups from this data.
class RaftGroupMetadata : public RefCountedThreadSafe<RaftGroupMetadata>,
                          public docdb::SchemaPackingProvider {
 public:
  using TableIdToSchemaVersionMap =
      ::google::protobuf::Map<::std::string, ::google::protobuf::uint32>;

  // Create metadata for a new Raft group. This assumes that the given superblock
  // has not been written before, and writes out the initial superblock with
  // the provided parameters.
  // data_root_dir and wal_root_dir dictates which disk this Raft group will
  // use in the respective directories.
  // If empty string is passed in, it will be randomly chosen.
  static Result<RaftGroupMetadataPtr> CreateNew(
      const RaftGroupMetadataData& data, const std::string& data_root_dir = {},
      const std::string& wal_root_dir = {});

  // Load existing metadata from disk.
  static Result<RaftGroupMetadataPtr> Load(FsManager* fs_manager, const RaftGroupId& raft_group_id);
  static Result<RaftGroupMetadataPtr> LoadFromPath(FsManager* fs_manager, const std::string& path);

  // Try to load an existing Raft group. If it does not exist, create it.
  // If it already existed, verifies that the schema of the Raft group matches the
  // provided 'schema'.
  //
  // This is mostly useful for tests which instantiate Raft groups directly.
  static Result<RaftGroupMetadataPtr> TEST_LoadOrCreate(const RaftGroupMetadataData& data);

  Result<TableInfoPtr> GetTableInfo(const TableId& table_id) const;
  Result<TableInfoPtr> GetTableInfoUnlocked(const TableId& table_id) const REQUIRES(data_mutex_);

  Result<TableInfoPtr> GetTableInfo(ColocationId colocation_id) const;
  Result<TableInfoPtr> GetTableInfoUnlocked(ColocationId colocation_id) const REQUIRES(data_mutex_);

  const RaftGroupId& raft_group_id() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return raft_group_id_;
  }

  // Returns the partition of the Raft group.
  std::shared_ptr<dockv::Partition> partition() const {
    DCHECK_NE(state_, kNotLoadedYet);
    std::lock_guard lock(data_mutex_);
    return partition_;
  }

  // Returns the primary table id. For co-located tables, the primary table is the table this Raft
  // group was first created for. For single-tenant table, it is the primary table.
  TableId table_id() const {
    DCHECK_NE(state_, kNotLoadedYet);
    std::lock_guard lock(data_mutex_);
    return primary_table_id_;
  }

  // Returns the name, type, schema, index map, schema, etc of the table.
  std::string namespace_name(const TableId& table_id = "") const;

  NamespaceId namespace_id() const;

  std::string table_name(const TableId& table_id = "") const;

  [[deprecated]]
  TableType table_type(const TableId& table_id = "") const;

  [[deprecated]]
  SchemaPtr schema(const TableId& table_id = "") const;

  std::shared_ptr<qlexpr::IndexMap> index_map(const TableId& table_id = "") const;

  [[deprecated]]
  SchemaVersion schema_version(const TableId& table_id = "") const;

  Result<SchemaVersion> schema_version(ColocationId colocation_id) const;

  Result<SchemaVersion> schema_version(const Uuid& cotable_id) const;

  const std::string& indexed_table_id(const TableId& table_id = "") const;

  bool is_index(const TableId& table_id = "") const;

  bool is_local_index(const TableId& table_id = "") const;

  bool is_unique_index(const TableId& table_id = "") const;

  std::vector<ColumnId> index_key_column_ids(const TableId& table_id = "") const;

  // Returns the partition schema of the Raft group's tables.
  std::shared_ptr<dockv::PartitionSchema> partition_schema() const {
    DCHECK_NE(state_, kNotLoadedYet);
    const TableInfoPtr table_info = primary_table_info();
    return std::shared_ptr<dockv::PartitionSchema>(table_info, &table_info->partition_schema);
  }

  std::shared_ptr<std::vector<DeletedColumn>> deleted_cols(
      const TableId& table_id = "") const;

  const std::string& rocksdb_dir() const { return kv_store_.rocksdb_dir; }
  std::string intents_rocksdb_dir() const { return kv_store_.rocksdb_dir + kIntentsDBSuffix; }
  std::string snapshots_dir() const { return kv_store_.rocksdb_dir + kSnapshotsDirSuffix; }

  const std::string& lower_bound_key() const { return kv_store_.lower_bound_key; }
  const std::string& upper_bound_key() const { return kv_store_.upper_bound_key; }

  const std::string& wal_dir() const { return wal_dir_; }

  Status set_namespace_id(const NamespaceId& namespace_id);

  // Set the WAL retention time for the primary table.
  void set_wal_retention_secs(uint32 wal_retention_secs);

  // Returns the wal retention time for the primary table.
  uint32_t wal_retention_secs() const;

  Status set_cdc_min_replicated_index(int64 cdc_min_replicated_index);

  Status set_cdc_sdk_min_checkpoint_op_id(const OpId& cdc_min_checkpoint_op_id);

  Status set_cdc_sdk_safe_time(const HybridTime& cdc_sdk_safe_time = HybridTime::kInvalid);

  int64_t cdc_min_replicated_index() const;

  OpId cdc_sdk_min_checkpoint_op_id() const;

  HybridTime cdc_sdk_safe_time() const;

  bool is_under_cdc_sdk_replication() const;

  Result<bool> SetAllCDCRetentionBarriers(
      int64 cdc_wal_index, OpId cdc_sdk_intents_op_id, HybridTime cdc_sdk_history_cutoff,
      bool require_history_cutoff, bool initial_retention_barrier);

  Status SetIsUnderXClusterReplicationAndFlush(bool is_under_xcluster_replication);

  bool IsUnderXClusterReplication() const;

  bool parent_data_compacted() const {
    std::lock_guard lock(data_mutex_);
    return kv_store_.parent_data_compacted;
  }

  void set_parent_data_compacted(const bool& value) {
    std::lock_guard lock(data_mutex_);
    kv_store_.parent_data_compacted = value;
  }

  std::optional<uint64_t> post_split_compaction_file_number_upper_bound() const {
    std::lock_guard<MutexType> lock(data_mutex_);
    return kv_store_.post_split_compaction_file_number_upper_bound;
  }

  void set_post_split_compaction_file_number_upper_bound(uint64_t value) {
    std::lock_guard<MutexType> lock(data_mutex_);
    kv_store_.post_split_compaction_file_number_upper_bound = value;
  }

  uint64_t last_full_compaction_time() {
    std::lock_guard lock(data_mutex_);
    return kv_store_.last_full_compaction_time;
  }

  void set_last_full_compaction_time(const uint64& value) {
    std::lock_guard lock(data_mutex_);
    kv_store_.last_full_compaction_time = value;
  }

  bool AddSnapshotSchedule(const SnapshotScheduleId& schedule_id) {
    std::lock_guard lock(data_mutex_);
    return kv_store_.snapshot_schedules.insert(schedule_id).second;
  }

  bool RemoveSnapshotSchedule(const SnapshotScheduleId& schedule_id) {
    std::lock_guard lock(data_mutex_);
    return kv_store_.snapshot_schedules.erase(schedule_id) != 0;
  }

  std::vector<SnapshotScheduleId> SnapshotSchedules() const {
    std::lock_guard lock(data_mutex_);
    return std::vector<SnapshotScheduleId>(
        kv_store_.snapshot_schedules.begin(), kv_store_.snapshot_schedules.end());
  }

  // Returns the data root dir for this Raft group, for example:
  // /mnt/d0/yb-data/tserver/data
  // TODO(#79): rework when we have more than one KV-store (and data roots) per Raft group.
  std::string data_root_dir() const;

  // Returns the WAL root dir for this Raft group, for example:
  // /mnt/d0/yb-data/tserver/wals
  std::string wal_root_dir() const;

  // Set table_id for altering the schema of a colocated user table.
  void SetSchema(const Schema& schema,
                 const qlexpr::IndexMap& index_map,
                 const std::vector<DeletedColumn>& deleted_cols,
                 const SchemaVersion version,
                 const OpId& op_id,
                 const TableId& table_id = "");

  void SetSchemaUnlocked(const Schema& schema,
                 const qlexpr::IndexMap& index_map,
                 const std::vector<DeletedColumn>& deleted_cols,
                 const SchemaVersion version,
                 const OpId& op_id,
                 const TableId& table_id = "") REQUIRES(data_mutex_);

  void SetPartitionSchema(const dockv::PartitionSchema& partition_schema);

  void SetTableName(
      const std::string& namespace_name, const std::string& table_name,
      const OpId& op_id, const TableId& table_id = "");

  void SetTableNameUnlocked(
      const std::string& namespace_name, const std::string& table_name,
      const OpId& op_id, const TableId& table_id = "") REQUIRES(data_mutex_);

  void SetSchemaAndTableName(
      const Schema& schema, const qlexpr::IndexMap& index_map,
      const std::vector<DeletedColumn>& deleted_cols,
      const SchemaVersion version, const std::string& namespace_name,
      const std::string& table_name, const OpId& op_id, const TableId& table_id = "");

  void AddTable(const std::string& table_id,
                const std::string& namespace_name,
                const std::string& table_name,
                const TableType table_type,
                const Schema& schema,
                const qlexpr::IndexMap& index_map,
                const dockv::PartitionSchema& partition_schema,
                const boost::optional<qlexpr::IndexInfo>& index_info,
                const SchemaVersion schema_version,
                const OpId& op_id,
                const TableId& pg_table_id,
                const SkipTableTombstoneCheck skip_table_tombstone_check) EXCLUDES(data_mutex_);

  void RemoveTable(const TableId& table_id, const OpId& op_id);

  // Returns a list of all tables colocated on this tablet.
  std::vector<TableId> GetAllColocatedTables() const;

  std::vector<TableId> GetAllColocatedTablesUnlocked() const REQUIRES(data_mutex_);

  // Returns the number of tables colocated on this tablet, returns 1 for non-colocated case.
  size_t GetColocatedTablesCount() const EXCLUDES(data_mutex_);

  void GetTableIdToSchemaVersionMap(
      TableIdToSchemaVersionMap* table_to_version) const;

  // Iterates through all the tables colocated on this tablet. In case of non-colocated tables,
  // iterates exactly one time. Use light-weight callback as it's triggered under the locked mutex;
  // callback should return true to continue iteration and false - to break the loop and exit.
  void IterateColocatedTables(
      std::function<void(const TableInfo&)> callback) const EXCLUDES(data_mutex_);

  // Gets a map of colocated tables UUIds with their colocation ids on this tablet.
  std::unordered_map<TableId, ColocationId> GetAllColocatedTablesWithColocationId() const;

  // Set / get the remote bootstrap / tablet data state.
  void set_tablet_data_state(TabletDataState state);
  TabletDataState tablet_data_state() const;

  void SetHidden(bool value);
  bool hidden() const;

  void SetRestorationHybridTime(HybridTime value);
  HybridTime restoration_hybrid_time() const;

  // Flushes the superblock to disk.
  // If only_if_dirty is true, flushes only if there are metadata updates that have been applied but
  // not flushed to disk. This is checked by comparing last_applied_change_metadata_op_id_ and
  // last_flushed_change_metadata_op_id_.
  Status Flush(OnlyIfDirty only_if_dirty = OnlyIfDirty::kFalse);

  Status SaveTo(const std::string& path);

  // Merge this metadata with restored metadata located at specified path.
  Status MergeWithRestored(const std::string& path, dockv::OverwriteSchemaPacking overwrite);

  // Mark the superblock to be in state 'delete_type', sync it to disk, and
  // then delete all of the rowsets in this tablet.
  // The metadata (superblock) is not deleted. For that, call DeleteSuperBlock().
  //
  // 'delete_type' must be one of TABLET_DATA_DELETED or TABLET_DATA_TOMBSTONED.
  // 'last_logged_opid' should be set to the last opid in the log, if any is known.
  // If 'last_logged_opid' is not set, then the current value of
  // last_logged_opid is not modified. This is important for roll-forward of
  // partially-tombstoned tablets during crash recovery.
  //
  // Returns only once all data has been removed.
  // The OUT parameter 'was_deleted' can be used by caller to determine if the tablet data was
  // actually deleted from disk or not. For example, in some cases, the tablet may have been
  // already deleted (and are here on a retry) and this operation essentially ends up being a no-op;
  // in such a case, 'was_deleted' will be set to FALSE.
  Status DeleteTabletData(TabletDataState delete_type, const yb::OpId& last_logged_opid);

  // Return true if this metadata references no regular data DB nor intents DB and is
  // already marked as tombstoned. If this is the case, then calling DeleteTabletData
  // would be a no-op.
  bool IsTombstonedWithNoRocksDBData() const;

  // Permanently deletes the superblock from the disk.
  // DeleteTabletData() must first be called and the tablet data state must be
  // TABLET_DATA_DELETED.
  // Returns Status::InvalidArgument if the list of orphaned blocks is not empty.
  // Returns Status::IllegalState if the tablet data state is not TABLET_DATA_DELETED.
  Status DeleteSuperBlock();

  FsManager *fs_manager() const { return fs_manager_; }

  OpId tombstone_last_logged_opid() const;

  // Loads the currently-flushed superblock from disk into the given protobuf.
  // When path is empty - obtain path from fs manager for this Raft group.
  Status ReadSuperBlockFromDisk(
      RaftGroupReplicaSuperBlockPB* superblock, const std::string& path = std::string()) const;

  static Status ReadSuperBlockFromDisk(
      Env* env, const std::string& path, RaftGroupReplicaSuperBlockPB* superblock);

  // Sets *superblock to the serialized form of the current metadata.
  void ToSuperBlock(RaftGroupReplicaSuperBlockPB* superblock) const;

  // Fully replace a superblock (used for bootstrap).
  Status ReplaceSuperBlock(const RaftGroupReplicaSuperBlockPB &pb);

  // Returns a new WAL dir path to be used for new Raft group `raft_group_id` which will be created
  // as a result of this Raft group splitting.
  // Uses the same root dir as for `this` Raft group.
  std::string GetSubRaftGroupWalDir(const RaftGroupId& raft_group_id) const EXCLUDES(data_mutex_);

  // Returns a new Data dir path to be used for new Raft group `raft_group_id` which will be created
  // as a result of this Raft group splitting.
  // Uses the same root dir as for `this` Raft group.
  std::string GetSubRaftGroupDataDir(const RaftGroupId& raft_group_id) const;

  // Creates a new Raft group metadata for the part of existing tablet contained in this Raft group.
  // Assigns specified Raft group ID, partition and key bounds for a new tablet.
  Result<RaftGroupMetadataPtr> CreateSubtabletMetadata(
      const RaftGroupId& raft_group_id, const dockv::Partition& partition,
      const std::string& lower_bound_key, const std::string& upper_bound_key) const;

  TableInfoPtr primary_table_info() const {
    std::lock_guard lock(data_mutex_);
    return primary_table_info_unlocked();
  }

  bool IsSysCatalog() const;

  bool colocated() const;

  LazySuperblockFlushEnabled IsLazySuperblockFlushEnabled() const;

  Result<std::string> TopSnapshotsDir() const;

  // Return standard "T xxx P yyy" log prefix.
  const std::string& LogPrefix() const;

  std::array<TabletId, kNumSplitParts> split_child_tablet_ids() const;

  OpId split_op_id() const;

  // If this tablet should be deleted, returns op id that should be applied to all replicas,
  // before performing such deletion.
  OpId GetOpIdToDeleteAfterAllApplied() const;

  void SetSplitDone(const OpId& op_id, const TabletId& child1, const TabletId& child2);

  // Methods for handling clone requests that this tablet has applied.
  void MarkClonesAttemptedUpTo(uint32_t clone_request_seq_no);
  bool HasAttemptedClone(uint32_t clone_request_seq_no);
  uint32_t LastAttemptedCloneSeqNo();

  bool has_active_restoration() const;

  void RegisterRestoration(const TxnSnapshotRestorationId& restoration_id);
  void UnregisterRestoration(const TxnSnapshotRestorationId& restoration_id);

  // Find whether some of active restorations complete. Returns max complete hybrid time of such
  // restoration.
  HybridTime CheckCompleteRestorations(const RestorationCompleteTimeMap& restoration_complete_time);

  // Removes all complete or unknown restorations.
  bool CleanupRestorations(const RestorationCompleteTimeMap& restoration_complete_time);

  bool UsePartialRangeKeyIntents() const;

  // versions is a map from table id to min schema version that should be kept for this table.
  Status OldSchemaGC(const std::unordered_map<Uuid, SchemaVersion, UuidHash>& versions);
  void DisableSchemaGC();
  void EnableSchemaGC();

  Result<docdb::CompactionSchemaInfo> CotablePacking(
      const Uuid& cotable_id, uint32_t schema_version, HybridTime history_cutoff) override;

  Result<docdb::CompactionSchemaInfo> ColocationPacking(
      ColocationId colocation_id, uint32_t schema_version, HybridTime history_cutoff) override;

  std::unordered_set<StatefulServiceKind> GetHostedServiceList() const;

  Result<std::string> FilePath() const;

  const KvStoreInfo& TEST_kv_store() const {
    return kv_store_;
  }

  OpId LastFlushedChangeMetadataOperationOpId() const;

  OpId TEST_LastAppliedChangeMetadataOperationOpId() const;

  void SetLastAppliedChangeMetadataOperationOpId(const OpId& op_id);

  // Takes OpId of the change metadata operation applied as argument.
  void OnChangeMetadataOperationApplied(const OpId& applied_opid);

  OpId MinUnflushedChangeMetadataOpId() const;

  // Called to update related metadata when index table backfilling is complete.
  // Returns kStatusNotFound if table is not found in kv_store, in other case returns kStatusOk.
  Status OnBackfillDone(const TableId& table_id) EXCLUDES(data_mutex_);
  Status OnBackfillDone(const OpId& op_id, const TableId& table_id) EXCLUDES(data_mutex_);

  // Updates related meta data as a reaction for post split compaction completed. Returns true
  // if any field has been updated and a flush may be required.
  bool OnPostSplitCompactionDone();

 private:
  typedef simple_spinlock MutexType;

  friend class RefCountedThreadSafe<RaftGroupMetadata>;
  friend class MetadataTest;

  // Compile time assert that no one deletes RaftGroupMetadata objects.
  ~RaftGroupMetadata();

  // Constructor for creating a new Raft group.
  explicit RaftGroupMetadata(
      const RaftGroupMetadataData& data, const std::string& data_dir, const std::string& wal_dir);

  // Constructor for loading an existing Raft group.
  RaftGroupMetadata(FsManager* fs_manager, const RaftGroupId& raft_group_id);

  Status LoadFromDisk(const std::string& path = "");

  // Update state of metadata to that of the given superblock PB.
  Status LoadFromSuperBlock(const RaftGroupReplicaSuperBlockPB& superblock,
                            bool local_superblock);

  Status ReadSuperBlock(RaftGroupReplicaSuperBlockPB *pb);

  // Fully replace superblock.
  // Requires 'flush_lock_'.
  // When path is empty - obtain path from fs manager for this Raft group.
  Status SaveToDiskUnlocked(
      const RaftGroupReplicaSuperBlockPB &pb, const std::string& path = std::string());

  // Requires 'data_mutex_'.
  void ToSuperBlockUnlocked(RaftGroupReplicaSuperBlockPB* superblock) const REQUIRES(data_mutex_);

  const TableInfoPtr primary_table_info_unlocked() const REQUIRES(data_mutex_) {
    const auto& tables = kv_store_.tables;
    const auto itr = tables.find(primary_table_id_);
    CHECK(itr != tables.end());
    return itr->second;
  }

  void ResetMinUnflushedChangeMetadataOpIdUnlocked() REQUIRES(data_mutex_);

  void SetLastAppliedChangeMetadataOperationOpIdUnlocked(const OpId& op_id) REQUIRES(data_mutex_);

  void OnChangeMetadataOperationAppliedUnlocked(const OpId& applied_op_id) REQUIRES(data_mutex_);

  Status OnBackfillDoneUnlocked(const TableId& table_id) REQUIRES(data_mutex_);

  Status SetTableInfoUnlocked(const TableInfoMap::iterator& it,
                              const TableInfoPtr& new_table_info) REQUIRES(data_mutex_);

  enum State {
    kNotLoadedYet,
    kNotWrittenYet,
    kInitialized
  };
  State state_;

  // Lock protecting the underlying data.
  // TODO: consider switching to RW mutex.
  mutable MutexType data_mutex_;

  // Lock protecting flushing the data to disk.
  // If taken together with 'data_mutex_', must be acquired first.
  mutable Mutex flush_lock_;

  // No thread safety annotations on raft_group_id_ because it is a constant after the object is
  // fully created. We cannot mark it as const since CreateSubtabletMetadata sets it to its parents
  // id in order to call LoadFromSuperBlock and then updates it to the right value.
  RaftGroupId raft_group_id_;

  std::shared_ptr<dockv::Partition> partition_ GUARDED_BY(data_mutex_);

  // The primary table id. Primary table is the first table this Raft group is created for.
  // Additional tables can be added to this Raft group to co-locate with this table.
  TableId primary_table_id_ GUARDED_BY(data_mutex_);

  // KV-store for this Raft group.
  KvStoreInfo kv_store_;

  FsManager* const fs_manager_;

  // The directory where the write-ahead log for this Raft group is stored.
  std::string wal_dir_ GUARDED_BY(data_mutex_);

  // The current state of remote bootstrap for the tablet.
  TabletDataState tablet_data_state_ GUARDED_BY(data_mutex_) = TABLET_DATA_UNKNOWN;

  // Record of the last opid logged by the tablet before it was last tombstoned. Has no meaning for
  // non-tombstoned tablets.
  OpId tombstone_last_logged_opid_ GUARDED_BY(data_mutex_);

  // True if the raft group is for a colocated tablet.
  bool colocated_ GUARDED_BY(data_mutex_) = false;

  // The minimum index that has been replicated by the cdc service.
  int64_t cdc_min_replicated_index_ GUARDED_BY(data_mutex_) = std::numeric_limits<int64_t>::max();

  // The minimum CDCSDK checkpoint Opid that has been consumed by client.
  OpId cdc_sdk_min_checkpoint_op_id_ GUARDED_BY(data_mutex_);

  // The minimum hybrid time based on which data is retained for before image
  HybridTime cdc_sdk_safe_time_ GUARDED_BY(data_mutex_);

  bool is_under_xcluster_replication_ GUARDED_BY(data_mutex_) = false;

  bool is_under_cdc_sdk_replication_ GUARDED_BY(data_mutex_) = false;

  bool hidden_ GUARDED_BY(data_mutex_) = false;

  HybridTime restoration_hybrid_time_ GUARDED_BY(data_mutex_) = HybridTime::kMin;

  // SPLIT_OP ID designated for this tablet (so child tablets will have this unset until they've
  // been split themselves).
  OpId split_op_id_ GUARDED_BY(data_mutex_);
  std::array<TabletId, kNumSplitParts> split_child_tablet_ids_ GUARDED_BY(data_mutex_);

  std::vector<TxnSnapshotRestorationId> active_restorations_;

  uint32_t last_attempted_clone_seq_no_ GUARDED_BY(data_mutex_) = 0;

  // No thread safety annotations on log_prefix_ because it is a constant after the object is
  // fully created. Check the comment on raft_group_id_ for more info.
  std::string log_prefix_;

  std::unordered_set<StatefulServiceKind> hosted_services_;

  // OpId of the last applied change metadata operation. Used to determine if the in-memory metadata
  // state is dirty and set last_flushed_change_metadata_op_id_ on flush.
  OpId last_applied_change_metadata_op_id_ GUARDED_BY(data_mutex_) = OpId::Invalid();

  // OpId of the last flushed change metadata operation. Used to determine if at the time
  // of local tablet bootstrap we should replay a particular change_metadata op.
  OpId last_flushed_change_metadata_op_id_ GUARDED_BY(data_mutex_) = OpId::Invalid();

  // OpId of the earliest applied change metadata operation that has not been flushed to disk. Used
  // to prevent WAL GC of such operations.
  OpId min_unflushed_change_metadata_op_id_ GUARDED_BY(data_mutex_) = OpId::Max();

  int disable_schema_gc_counter_ GUARDED_BY(data_mutex_) = 0;

  DISALLOW_COPY_AND_ASSIGN(RaftGroupMetadata);
};

class DisableSchemaGC {
 public:
  explicit DisableSchemaGC(RaftGroupMetadata* metadata) : metadata_(metadata) {
    metadata->DisableSchemaGC();
  }

  ~DisableSchemaGC() {
    metadata_->EnableSchemaGC();
  }

 private:
  RaftGroupMetadata* metadata_;
};

Status MigrateSuperblock(RaftGroupReplicaSuperBlockPB* superblock);

// Checks whether tablet data storage is ready for function, i.e. its creation or bootstrap process
// has been completed and tablet is not deleted and not in process of being deleted.
inline bool CanServeTabletData(TabletDataState state) {
  return state == TabletDataState::TABLET_DATA_READY ||
         state == TabletDataState::TABLET_DATA_SPLIT_COMPLETED;
}

Status CheckCanServeTabletData(const RaftGroupMetadata& metadata);

} // namespace tablet
} // namespace yb
