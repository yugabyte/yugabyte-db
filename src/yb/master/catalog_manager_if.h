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
//

#pragma once

#include "yb/cdc/xrepl_types.h"

#include "yb/common/common_fwd.h"
#include "yb/common/schema.h"

#include "yb/common/snapshot.h"
#include "yb/consensus/consensus_fwd.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_admin.fwd.h"
#include "yb/master/master_client.fwd.h"
#include "yb/master/master_cluster.fwd.h"
#include "yb/master/master_ddl.fwd.h"
#include "yb/master/master_replication.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/monitored_task.h"
#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace google {
namespace protobuf {
template <class T>
class RepeatedPtrField;
}
}

namespace yb {

class ThreadPool;

namespace master {

YB_DEFINE_ENUM(GetTablesMode, (kAll) // All tables
                              (kRunning) // All running tables
                              (kVisibleToClient) // All tables visible to the client
               );

YB_STRONGLY_TYPED_BOOL(HideOnly);
YB_STRONGLY_TYPED_BOOL(KeepData);
YB_STRONGLY_TYPED_BOOL(PrimaryTablesOnly);

class CatalogManagerIf {
 public:
  virtual void CheckTableDeleted(const TableInfoPtr& table, const LeaderEpoch& epoch) = 0;

  virtual void DeleteTabletReplicas(
      TabletInfo* tablet, const std::string& msg, HideOnly hide_only, KeepData keep_data,
      const LeaderEpoch& epoch) = 0;

  virtual void NotifyPrepareDeleteTransactionTabletFinished(
      const scoped_refptr<TabletInfo>& tablet, const std::string& msg, HideOnly hide_only,
      const LeaderEpoch& epoch) = 0;

  virtual void NotifyTabletDeleteFinished(
      const TabletServerId& tserver_uuid, const TabletId& tablet_id,
      const TableInfoPtr& table, const LeaderEpoch& epoch,
      server::MonitoredTaskState task_state) = 0;

  virtual std::string GenerateId() = 0;

  virtual Result<std::shared_ptr<tablet::AbstractTablet>> GetSystemTablet(const TabletId& id) = 0;

  virtual Status WaitForWorkerPoolTests(
      const MonoDelta& timeout = MonoDelta::FromSeconds(10)) const = 0;

  virtual Result<uint64_t> IncrementYsqlCatalogVersion() = 0;

  virtual Result<std::vector<TableDescription>> CollectTables(
      const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
      bool add_indexes,
      bool include_parent_colocated_table = false) = 0;

  virtual ThreadPool* AsyncTaskPool() = 0;

  virtual Status ScheduleTask(std::shared_ptr<server::RunnableMonitoredTask> task) = 0;

  virtual Status HandleTabletSchemaVersionReport(
      TabletInfo* tablet, uint32_t version, const LeaderEpoch& epoch,
      const scoped_refptr<TableInfo>& table = nullptr) = 0;

  // A primary table is a non-colocated table or the dummy parent table created for a colocated
  // tablet. If you are calling GetTables to iterate over all tablets then PrimaryTablesOnly should
  // be kTrue to avoid unnecessarily iterating over colocated tables.
  virtual std::vector<TableInfoPtr> GetTables(
      GetTablesMode mode, PrimaryTablesOnly = PrimaryTablesOnly::kFalse) = 0;

  virtual void GetAllNamespaces(
      std::vector<scoped_refptr<NamespaceInfo>>* namespaces,
      bool include_only_running_namespaces = false) = 0;

  virtual Result<size_t> GetReplicationFactor() = 0;
  Result<size_t> GetReplicationFactor(NamespaceName namespace_name) {
    // TODO ENG-282 We currently don't support per-namespace replication factor.
    return GetReplicationFactor();
  }

  virtual const NodeInstancePB& NodeInstance() const = 0;

  virtual Status GetYsqlCatalogVersion(
      uint64_t* catalog_version, uint64_t* last_breaking_version) = 0;
  virtual Status GetYsqlAllDBCatalogVersions(
      bool use_cache,
      DbOidToCatalogVersionMap* versions,
      uint64_t* fingerprint) = 0;
  virtual Status GetYsqlDBCatalogVersion(
      uint32_t db_oid, uint64_t* catalog_version, uint64_t* last_breaking_version) = 0;

  virtual Result<SysClusterConfigEntryPB> GetClusterConfig() = 0;

  virtual Status ListTables(
      const ListTablesRequestPB* req, ListTablesResponsePB* resp) = 0;

  virtual Status CheckIsLeaderAndReady() const = 0;

  virtual void AssertLeaderLockAcquiredForReading() const = 0;

  virtual bool IsUserTable(const TableInfo& table) const = 0;

  virtual NamespaceName GetNamespaceName(const NamespaceId& id) const = 0;

  virtual bool IsUserIndex(const TableInfo& table) const = 0;

  virtual TableInfoPtr GetTableInfo(const TableId& table_id) = 0;

  virtual Result<ReplicationInfoPB> GetTableReplicationInfo(
      const ReplicationInfoPB& table_replication_info,
      const TablespaceId& tablespace_id) = 0;

  virtual Result<ReplicationInfoPB> GetTableReplicationInfo(const TableInfoPtr& table) = 0;

  virtual Result<size_t> GetTableReplicationFactor(const TableInfoPtr& table) const = 0;

  virtual std::vector<std::shared_ptr<server::MonitoredTask>> GetRecentJobs() = 0;

  virtual bool IsSystemTable(const TableInfo& table) const = 0;

  virtual Result<scoped_refptr<NamespaceInfo>> FindNamespaceById(const NamespaceId& id) const = 0;

  virtual Result<scoped_refptr<NamespaceInfo>> FindNamespace(
      const NamespaceIdentifierPB& ns_identifier) const = 0;

  virtual scoped_refptr<TableInfo> GetTableInfoFromNamespaceNameAndTableName(
      YQLDatabase db_type, const NamespaceName& namespace_name, const TableName& table_name,
      const PgSchemaName pg_schema_name = {}) = 0;

  virtual std::vector<std::shared_ptr<server::MonitoredTask>> GetRecentTasks() = 0;

  virtual Result<boost::optional<TablespaceId>> GetTablespaceForTable(
      const scoped_refptr<TableInfo>& table) const = 0;

  virtual bool IsLoadBalancerEnabled() = 0;

  // API to check if all the live tservers have similar tablet workload.
  virtual Status IsLoadBalanced(
      const IsLoadBalancedRequestPB* req, IsLoadBalancedResponsePB* resp) = 0;

  virtual bool IsUserCreatedTable(const TableInfo& table) const = 0;

  virtual Status GetAllAffinitizedZones(std::vector<AffinitizedZonesSet>* affinitized_zones) = 0;

  virtual Result<BlacklistSet> BlacklistSetFromPB(bool leader_blacklist = false) const = 0;

  virtual void GetAllUDTypes(std::vector<scoped_refptr<UDTypeInfo>>* types) = 0;

  virtual Status GetTabletLocations(
      const TabletId& tablet_id,
      TabletLocationsPB* locs_pb,
      IncludeInactive include_inactive = IncludeInactive::kFalse) = 0;

  virtual Status GetTabletLocations(
      scoped_refptr<TabletInfo> tablet_info,
      TabletLocationsPB* locs_pb,
      IncludeInactive include_inactive = IncludeInactive::kFalse) = 0;

  virtual TSDescriptorVector GetAllLiveNotBlacklistedTServers() const = 0;

  virtual Status DoImportSnapshotMeta(
      const SnapshotInfoPB& snapshot_pb,
      const LeaderEpoch& epoch,
      const std::optional<std::string>& clone_target_namespace_name,
      NamespaceMap* namespace_map,
      UDTypeMap* type_map,
      ExternalTableSnapshotDataMap* tables_data,
      CoarseTimePoint deadline) = 0;

  virtual Status DoCreateSnapshot(
      const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp, CoarseTimePoint deadline,
      const LeaderEpoch& epoch) = 0;

  virtual Status ListSnapshotRestorations(
      const ListSnapshotRestorationsRequestPB* req, ListSnapshotRestorationsResponsePB* resp) = 0;

  virtual Result<std::pair<SnapshotInfoPB, std::unordered_set<TabletId>>>
      GenerateSnapshotInfoFromSchedule(
      const SnapshotScheduleId& snapshot_schedule_id, HybridTime export_time,
      CoarseTimePoint deadline) = 0;

  virtual void HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error) = 0;

  virtual void HandleRestoreTabletSnapshotResponse(TabletInfo *tablet, bool error) = 0;

  virtual void HandleDeleteTabletSnapshotResponse(
      const SnapshotId& snapshot_id, TabletInfo *tablet, bool error) = 0;

  virtual Status GetTableLocations(const GetTableLocationsRequestPB* req,
                                           GetTableLocationsResponsePB* resp) = 0;

  virtual Status IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                           IsCreateTableDoneResponsePB* resp) = 0;

  virtual Status CreateTable(
      const CreateTableRequestPB* req,
      CreateTableResponsePB* resp,
      rpc::RpcContext* rpc,
      const LeaderEpoch& epoch) = 0;

  virtual Status CreateNamespace(
      const CreateNamespaceRequestPB* req, CreateNamespaceResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch) = 0;

  virtual Status GetTableSchema(
      const GetTableSchemaRequestPB* req, GetTableSchemaResponsePB* resp) = 0;

  virtual Status TEST_IncrementTablePartitionListVersion(const TableId& table_id) = 0;

  virtual LeaderEpoch GetLeaderEpochInternal() const = 0;

  virtual Result<scoped_refptr<TabletInfo>> GetTabletInfo(const TabletId& tablet_id) = 0;

  virtual bool AreTablesDeletingOrHiding() = 0;

  virtual Status GetCurrentConfig(consensus::ConsensusStatePB *cpb) const = 0;

  virtual Status WaitUntilCaughtUpAsLeader(const MonoDelta& timeout) = 0;

  virtual Status ListCDCStreams(
      const ListCDCStreamsRequestPB* req, ListCDCStreamsResponsePB* resp) = 0;

  virtual Status IsObjectPartOfXRepl(
      const IsObjectPartOfXReplRequestPB* req, IsObjectPartOfXReplResponsePB* resp) = 0;

  virtual Status GetCDCDBStreamInfo(
    const GetCDCDBStreamInfoRequestPB* req, GetCDCDBStreamInfoResponsePB* resp) = 0;

  virtual Result<scoped_refptr<TableInfo>> FindTable(
      const TableIdentifierPB& table_identifier) const = 0;

  virtual Status IsInitDbDone(
      const IsInitDbDoneRequestPB* req, IsInitDbDoneResponsePB* resp) = 0;

  virtual void DumpState(std::ostream* out, bool on_disk_dump = false) const = 0;

  virtual scoped_refptr<TableInfo> NewTableInfo(TableId id, bool colocated) = 0;

  // If is_manual_split is true, we will not call ShouldSplitValidCandidate.
  virtual Status SplitTablet(
      const TabletId& tablet_id, ManualSplit is_manual_split, const LeaderEpoch& epoch) = 0;

  virtual Status TEST_SplitTablet(
      const scoped_refptr<TabletInfo>& source_tablet_info, docdb::DocKeyHash split_hash_code) = 0;

  virtual Status TEST_SplitTablet(
      const TabletId& tablet_id, const std::string& split_encoded_key,
      const std::string& split_partition_key) = 0;

  virtual uint64_t GetTransactionTablesVersion() = 0;

  virtual Result<scoped_refptr<TableInfo>> FindTableById(const TableId& table_id) const = 0;

  virtual SysCatalogTable* sys_catalog() = 0;

  virtual PermissionsManager* permissions_manager() = 0;

  virtual int64_t leader_ready_term() const = 0;

  virtual ClusterLoadBalancer* load_balancer() = 0;

  virtual TabletSplitManager* tablet_split_manager() = 0;

  virtual CloneStateManager* clone_state_manager() = 0;

  virtual XClusterManagerIf* GetXClusterManager() = 0;

  virtual XClusterManager* GetXClusterManagerImpl() = 0;

  virtual std::shared_ptr<tablet::TabletPeer> tablet_peer() const = 0;

  virtual intptr_t tablets_version() const = 0;

  virtual intptr_t tablet_locations_version() const = 0;

  virtual MasterSnapshotCoordinator& snapshot_coordinator() = 0;

  virtual Status UpdateLastFullCompactionRequestTime(
      const TableId& table_id, const LeaderEpoch& epoch) = 0;

  virtual Status GetCompactionStatus(
      const GetCompactionStatusRequestPB* req, GetCompactionStatusResponsePB* resp) = 0;

  virtual Status PromoteTableToRunningState(TableInfoPtr table_info, const LeaderEpoch& epoch) = 0;

  virtual Status PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails(
      const scoped_refptr<TableInfo>& table,
      const yb::TabletId& tablet_id,
      const xrepl::StreamId& cdc_sdk_stream_id,
      const yb::OpIdPB& safe_opid,
      const yb::HybridTime& proposed_snapshot_time,
      const bool require_history_cutoff) = 0;

  virtual Status PopulateCDCStateTableOnNewTableCreation(
      const scoped_refptr<TableInfo>& table,
      const TabletId& tablet_id,
      const OpId& safe_opid) = 0;

  virtual Status WaitForSnapshotSafeOpIdToBePopulated(
      const xrepl::StreamId& stream_id,
      const std::vector<TableId>& table_ids,
      CoarseTimePoint deadline) = 0;

  virtual ~CatalogManagerIf() = default;
};

// Returns whether the namespace is a YCQL namespace.
bool IsYcqlNamespace(const NamespaceInfo& ns);

// Returns whether the table is a YCQL table.
bool IsYcqlTable(const TableInfo& table);

}  // namespace master
}  // namespace yb
