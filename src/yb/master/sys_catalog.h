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

#include <string>
#include <vector>
#include <unordered_map>

#include "yb/common/pg_types.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/gutil/callback.h"

#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/snapshot_coordinator.h"

#include "yb/tserver/tablet_memory_manager.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_fwd.h"
#include "yb/util/unique_lock.h"

namespace yb {

class Schema;
class FsManager;
class ThreadPool;

namespace tserver {
class WriteRequestPB;
class WriteResponsePB;
}

namespace master {
class Master;
class MasterOptions;

// Forward declaration from internal header file.
class VisitorBase;
class SysCatalogWriter;

struct PgTypeInfo {
  char typtype;
  uint32_t typbasetype;
  PgTypeInfo(char typtype_, uint32_t typbasetype_) : typtype(typtype_), typbasetype(typbasetype_) {}
};

struct PgTableReadData {
  TableId table_id;
  tablet::TabletPtr tablet;
  tablet::TableInfoPtr table_info;
  ReadHybridTime read_hybrid_time;

  const Schema& schema() const;
  Result<ColumnId> ColumnByName(const std::string& name) const;

  Result<std::unique_ptr<docdb::DocRowwiseIterator>> NewUninitializedIterator(
      const dockv::ReaderProjection& projection) const;

  Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> NewIterator(
      const dockv::ReaderProjection& projection) const;
};

// SysCatalogTable is a YB table that keeps track of table and
// tablet metadata.
// - SysCatalogTable has only one tablet.
// - SysCatalogTable is managed by the master and not exposed to the user
//   as a "normal table", instead we have Master APIs to query the table.
class SysCatalogTable {
 public:
  typedef Callback<Status()> ElectedLeaderCallback;

  // 'leader_cb_' is invoked whenever this node is elected as a leader
  // of the consensus configuration for this tablet, including for local standalone
  // master consensus configurations. It used to initialize leader state, submit any
  // leader-specific tasks and so forth.
  //
  /// NOTE: Since 'leader_cb_' is invoked synchronously and can block
  // the consensus configuration's progress, any long running tasks (e.g., scanning
  // tablets) should be performed asynchronously (by, e.g., submitting
  // them to a to a separate threadpool).
  SysCatalogTable(Master* master, MetricRegistry* metrics, ElectedLeaderCallback leader_cb);

  ~SysCatalogTable();

  // Allow for orderly shutdown of tablet peer, etc.
  void StartShutdown();
  void CompleteShutdown();

  // Load the Metadata from disk, and initialize the TabletPeer for the sys-table
  Status Load(FsManager *fs_manager);

  // Create the new Metadata and initialize the TabletPeer for the sys-table.
  Status CreateNew(FsManager *fs_manager);

  // ==================================================================
  // Templated CRUD methods for items in sys.catalog.
  // ==================================================================
  template <class... Items>
  Status Upsert(int64_t leader_term, Items&&... items);

  // Required to write TableInfo or TabletInfo. This method checks the `namespace` parameter to
  // prevent writes of stale data read before a PITR to clobber object state overwritten by PITR.
  template <class... Items>
  Status Upsert(const LeaderEpoch& epoch, Items&&... items);

  template <class... Items>
  Status ForceUpsert(int64_t leader_term, Items&&... items);

  template <class... Items>
  Status Delete(int64_t leader_term, Items&&... items);

  // Required to delete TableInfo or TabletInfo. This method checks the `namespace` parameter to
  // prevent deletes of stale data read before a PITR to clobber object state overwritten by PITR.
  template <class... Items>
  Status Delete(const LeaderEpoch& epoch, Items&&... items);

  template <class... Items>
  Status Mutate(QLWriteRequestPB::QLStmtType op_type, int64_t leader_term, Items&&... items);

  // Required to mutate TableInfo or TabletInfo. This method checks the `namespace` parameter to
  // prevent mutations of stale data read before a PITR to clobber object state overwritten by PITR.
  template <class... Items>
  Status Mutate(
      QLWriteRequestPB::QLStmtType op_type, const LeaderEpoch& epoch, Items&&... items);

  template <class... Items>
  Status ForceMutate(
      QLWriteRequestPB::QLStmtType op_type, int64_t leader_term, Items&&... items);

  // ==================================================================
  // Static schema related methods.
  // ==================================================================
  static std::string schema_column_type();
  static std::string schema_column_id();
  static std::string schema_column_metadata();

  // Return the schema of the table.
  // NOTE: This is the "server-side" schema, so it must have the column IDs.
  static Schema BuildTableSchema();

  ThreadPool* raft_pool() const { return raft_pool_.get(); }
  rpc::ThreadPool* raft_notifications_pool() const { return raft_notifications_pool_.get(); }
  ThreadPool* tablet_prepare_pool() const { return tablet_prepare_pool_.get(); }
  ThreadPool* append_pool() const { return append_pool_.get(); }
  ThreadPool* log_sync_pool() const { return log_sync_pool_.get(); }

  std::shared_ptr<tablet::TabletPeer> tablet_peer() const {
    return std::atomic_load(&tablet_peer_);
  }

  Result<tablet::TabletPtr> Tablet() const;

  Result<PgTableReadData> TableReadData(
      const TableId& table_id, const ReadHybridTime& read_ht) const;
  Result<PgTableReadData> TableReadData(
      uint32_t database_oid, uint32_t table_oid, const ReadHybridTime& read_ht) const;

  // Create a new tablet peer with information from the metadata
  void SetupTabletPeer(const scoped_refptr<tablet::RaftGroupMetadata>& metadata);

  // Update the in-memory master addresses. Report missing uuid's in the
  // config when check_missing_uuids is set to true.
  Status ConvertConfigToMasterAddresses(
      const yb::consensus::RaftConfigPB& config,
      bool check_missing_uuids = false);

  // Create consensus metadata object and flush it to disk.
  Status CreateAndFlushConsensusMeta(
      FsManager* fs_manager,
      const yb::consensus::RaftConfigPB& config,
      int64_t current_term);

  Status Visit(VisitorBase* visitor);

  template <template <class> class Loader, typename CatalogEntityWrapper>
  Status Load(const std::string& type, CatalogEntityWrapper& catalog_entity_wrapper) {
    Loader<CatalogEntityWrapper> loader(catalog_entity_wrapper);
    RETURN_NOT_OK_PREPEND(Visit(&loader), "Failed while visiting " + type + " in sys catalog");
    return Status::OK();
  }

  template <typename Loader, typename CatalogEntityPB>
  Status Load(
      const std::string& type, std::function<Status(const std::string&, const CatalogEntityPB&)>
                                   catalog_entity_inserter_func) {
    Loader loader(catalog_entity_inserter_func);
    RETURN_NOT_OK_PREPEND(Visit(&loader), "Failed while visiting " + type + " in sys catalog");
    return Status::OK();
  }

  typedef std::function<Status(const ReadHybridTime&, HybridTime*)> ReadRestartFn;
  Status ReadWithRestarts(
      const ReadRestartFn& fn,
      tablet::RequireLease require_lease = tablet::RequireLease::kTrue) const;

  // Read the global ysql catalog version info from the pg_yb_catalog_version catalog table.
  Status ReadYsqlCatalogVersion(const TableId& ysql_catalog_table_id,
                                uint64_t* catalog_version,
                                uint64_t* last_breaking_version);
  // Read the per-db ysql catalog version info from the pg_yb_catalog_version catalog table.
  Status ReadYsqlDBCatalogVersion(const TableId& ysql_catalog_table_id,
                                  uint32_t db_oid,
                                  uint64_t* catalog_version,
                                  uint64_t* last_breaking_version);
  // Read the ysql catalog version info for all databases from the pg_yb_catalog_version
  // catalog table.
  Status ReadYsqlAllDBCatalogVersions(
      const TableId& ysql_catalog_table_id,
      DbOidToCatalogVersionMap* versions);

  // Read the pg_class catalog table. There is a separate pg_class table in each
  // YSQL database, read the information in the pg_class table for the database
  // 'database_oid' and load this information into 'table_to_tablespace_map'.
  // 'is_colocated' indicates whether this database is colocated or not.
  Status ReadPgClassInfo(const uint32_t database_oid,
                         const bool is_colocated,
                         TableToTablespaceIdMap* table_to_tablespace_map);

  Status ReadTablespaceInfoFromPgYbTablegroup(
    const uint32_t database_oid,
    bool is_colocated_database,
    TableToTablespaceIdMap *table_tablespace_map);

  // Read relnamespace OID from the pg_class catalog table.
  Result<uint32_t> ReadPgClassColumnWithOidValue(const uint32_t database_oid,
                                                 const uint32_t table_oid,
                                                 const std::string& column_name);

  // Read nspname string from the pg_namespace catalog table.
  Result<std::string> ReadPgNamespaceNspname(const uint32_t database_oid,
                                             const uint32_t relnamespace_oid);

  // Read attname and atttypid from pg_attribute catalog table.
  Result<std::unordered_map<std::string, uint32_t>> ReadPgAttNameTypidMap(
      uint32_t database_oid, uint32_t table_oid);

  // Read enumtypid and enumlabel from pg_enum catalog table.
  Result<std::unordered_map<uint32_t, std::string>> ReadPgEnum(
      uint32_t database_oid, uint32_t type_oid = kPgInvalidOid);

  // Read oid, typtype and typbasetype from pg_type catalog table.
  Result<std::unordered_map<uint32_t, PgTypeInfo>> ReadPgTypeInfo(
      uint32_t database_oid, std::vector<uint32_t>* type_oids);

  // Read the pg_tablespace catalog table and return a map with all the tablespaces and their
  // respective placement information.
  Result<std::shared_ptr<TablespaceIdToReplicationInfoMap>> ReadPgTablespaceInfo();

  Result<RelIdToAttributesMap> ReadPgAttributeInfo(
      uint32_t database_oid, std::vector<uint32_t> table_oids);

  Result<RelTypeOIDMap> ReadCompositeTypeFromPgClass(
      uint32_t database_oid, uint32_t type_oid = kPgInvalidOid);

  // Read the pg_yb_tablegroup catalog and return the OID of the tablegroup named <grpname>.
  Result<uint32_t> ReadPgYbTablegroupOid(const uint32_t database_oid,
                                         const std::string& grpname);

  // Copy the content of co-located tables in sys catalog as a batch.
  Status CopyPgsqlTables(const std::vector<TableId>& source_table_ids,
                         const std::vector<TableId>& target_table_ids,
                         int64_t leader_term);

  Status DeleteAllYsqlCatalogTableRows(const std::vector<TableId>& table_ids, int64_t leader_term);

  // Drop YSQL table by removing the table metadata in sys-catalog.
  Status DeleteYsqlSystemTable(const std::string& table_id, int64_t term);

  const Schema& schema();

  const docdb::DocReadContext& doc_read_context();

  const scoped_refptr<MetricEntity>& GetMetricEntity() const { return metric_entity_; }

  Status FetchDdlLog(google::protobuf::RepeatedPtrField<DdlLogEntryPB>* entries);

  Status GetTableSchema(
      const TableId& table_id,
      const ReadHybridTime read_hybrid_time,
      Schema* schema,
      uint32_t* schema_version);

  PitrCount pitr_count() {
    return pitr_count_.load();
  }

  void IncrementPitrCount() EXCLUDES(pitr_count_lock_) {
    // We acquire pitr_count_lock_ to synchronize with sys catalog writes to tables or tablets.
    // We want to be sure such writes complete before PITR updates any sys catalog state.
    UniqueLock<std::shared_mutex> l(pitr_count_lock_);
    pitr_count_.fetch_add(1);
  }

  tablet::TabletPeerPtr TEST_GetTabletPeer() {
    return tablet_peer_;
  }

 private:
  friend class CatalogManager;
  friend class ScopedLeaderSharedLock;

  inline std::unique_ptr<SysCatalogWriter> NewWriter(int64_t leader_term);

  const char *table_name() const { return kSysCatalogTableName; }

  // Returns 'Status::OK()' if the WriteTranasction completed
  Status SyncWrite(SysCatalogWriter* writer);

  void SysCatalogStateChanged(const std::string& tablet_id,
                              std::shared_ptr<consensus::StateChangeContext> context);

  Status SetupTablet(const scoped_refptr<tablet::RaftGroupMetadata>& metadata);

  Status OpenTablet(const scoped_refptr<tablet::RaftGroupMetadata>& metadata);

  // Use the master options to generate a new consensus configuration.
  // In addition, resolve all UUIDs of this consensus configuration.
  Status SetupConfig(const MasterOptions& options,
                     consensus::RaftConfigPB* committed_config);

  std::string tablet_id() const;

  // Conventional "T xxx P xxxx..." prefix for logging.
  std::string LogPrefix() const;

  // Waits for the tablet to reach 'RUNNING' state.
  //
  // Contrary to tablet servers, in master we actually wait for the master tablet
  // to become online synchronously, this allows us to fail fast if something fails
  // and shouldn't induce the all-workers-blocked-waiting-for-tablets problem
  // that we've seen in tablet servers since the master only has to boot a few
  // tablets.
  Status WaitUntilRunning();

  // Shutdown the tablet peer and apply pool which are not needed in shell mode for this master.
  Status GoIntoShellMode();

  // Initializes the RaftPeerPB for the local peer.
  // Crashes due to an invariant check if the rpc server is not running.
  void InitLocalRaftPeerPB();

  // Read from pg_yb_catalog_version catalog table. If 'catalog_version/last_breaking_version'
  // are set, we only read the global catalog version if db_oid is 0 (kInvalidOid), or read the
  // per-db catalog version if db_oid is valid (not kInvalidOid). If 'versions' is set we read
  // all rows in the table and db_oid is ignored.
  // Either 'catalog_version/last_breaking_version' or 'versions' should be set but not both.
  Status ReadYsqlDBCatalogVersionImpl(
      const TableId& ysql_catalog_table_id,
      uint32_t db_oid,
      uint64_t* catalog_version,
      uint64_t* last_breaking_version,
      DbOidToCatalogVersionMap* versions);
  Status ReadYsqlDBCatalogVersionImplWithReadTime(
      const TableId& ysql_catalog_table_id,
      uint32_t db_oid,
      const ReadHybridTime& read_time,
      HybridTime* read_restart_ht,
      uint64_t* catalog_version,
      uint64_t* last_breaking_version,
      DbOidToCatalogVersionMap* versions);

  // During a batch write operation, if the max batch bytes have been exceeded, performs a write and
  // creates a new writer. To avoid running the expensive ByteSizeLong calculation too frequently,
  // this method checks whether the max batch bytes have been exceeded only once every 128 rows.
  //
  // The batch itself is atomically written, but there is no atomicity guarantee between two
  // different batches even if they use the same writer.
  // NOTE: FinishWrite must be called to flush any partial batches at the end of the write.
  Status WriteBatchIfNeeded(size_t max_batch_bytes,
                            size_t rows_so_far,
                            int64_t leader_term,
                            std::unique_ptr<SysCatalogWriter>& writer,
                            size_t& total_bytes,
                            size_t& batch_count);

  // During a batch write operation, does the final write if any batch operations remain queued.
  Status FinishWrite(std::unique_ptr<SysCatalogWriter>& writer,
                     size_t& total_bytes,
                     size_t& batch_count);

  // Table schema, with IDs, used for the YQL write path.
  std::unique_ptr<docdb::DocReadContext> doc_read_context_;

  MetricRegistry* metric_registry_;

  scoped_refptr<MetricEntity> metric_entity_;

  std::unique_ptr<ThreadPool> inform_removed_master_pool_;

  // Thread pool for Raft-related operations
  std::unique_ptr<ThreadPool> raft_pool_;

  // Thread pool for callbacks on Raft replication events.
  std::unique_ptr<rpc::ThreadPool> raft_notifications_pool_;

  // Thread pool for preparing transactions, shared between all tablets.
  std::unique_ptr<ThreadPool> tablet_prepare_pool_;

  // Thread pool for appender tasks
  std::unique_ptr<ThreadPool> append_pool_;

  std::unique_ptr<ThreadPool> log_sync_pool_;

  std::unique_ptr<ThreadPool> allocation_pool_;

  std::shared_ptr<tablet::TabletPeer> tablet_peer_;

  Master* master_;

  ElectedLeaderCallback leader_cb_;

  consensus::RaftPeerPB local_peer_pb_;

  std::atomic<PitrCount> pitr_count_{0};

  // This lock is used to synchronize increments of pitr_count_ and sys catalog writes to ensure any
  // in flight sys catalog writes are complete before PITR updates any state.
  mutable std::shared_mutex pitr_count_lock_;

  scoped_refptr<EventStats> setup_config_dns_stats_;

  scoped_refptr<Counter> peer_write_count;

  std::unordered_map<std::string, scoped_refptr<AtomicGauge<uint64>>> visitor_duration_metrics_;

  std::shared_ptr<tserver::TabletMemoryManager> mem_manager_;

  std::unique_ptr<consensus::MultiRaftManager> multi_raft_manager_;

  DISALLOW_COPY_AND_ASSIGN(SysCatalogTable);
};

} // namespace master
} // namespace yb

#include "yb/master/sys_catalog-internal.h"
