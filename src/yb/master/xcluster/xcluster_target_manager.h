// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/leader_epoch.h"
#include "yb/master/master_ddl.fwd.h"
#include "yb/master/master_fwd.h"
#include "yb/master/xcluster/master_xcluster_types.h"
#include "yb/master/xcluster/xcluster_catalog_entity.h"

#include "yb/util/is_operation_done_result.h"
#include "yb/util/status_fwd.h"

namespace yb::master {

class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;

class PostTabletCreateTaskBase;
class UniverseReplicationInfo;
class XClusterConsumerReplicationStatusPB;
class XClusterInboundReplicationGroupSetupTaskIf;
class XClusterSafeTimeService;
struct XClusterInboundReplicationGroupStatus;
struct XClusterSetupUniverseReplicationData;

class XClusterTargetManager {
 public:
  // XCluster Safe Time.
  void CreateXClusterSafeTimeTableAndStartService();

  XClusterNamespaceToSafeTimeMap GetXClusterNamespaceToSafeTimeMap() const;

  Status SetXClusterNamespaceToSafeTimeMap(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map);

  Status GetXClusterSafeTime(GetXClusterSafeTimeResponsePB* resp, const LeaderEpoch& epoch);

  Result<HybridTime> GetXClusterSafeTime(const NamespaceId& namespace_id) const;

  Status GetXClusterSafeTimeForNamespace(
      const GetXClusterSafeTimeForNamespaceRequestPB* req,
      GetXClusterSafeTimeForNamespaceResponsePB* resp, const LeaderEpoch& epoch);

  Result<HybridTime> GetXClusterSafeTimeForNamespace(
      const NamespaceId& namespace_id, const XClusterSafeTimeFilter& filter) const;

  Status RefreshXClusterSafeTimeMap(const LeaderEpoch& epoch);

  XClusterSafeTimeService* TEST_xcluster_safe_time_service() { return safe_time_service_.get(); }

  Status RemoveDroppedTablesOnConsumer(
      const std::unordered_set<TabletId>& table_ids, const LeaderEpoch& epoch)
      EXCLUDES(table_stream_ids_map_mutex_);

  Status WaitForSetupUniverseReplicationToFinish(
      const xcluster::ReplicationGroupId& replication_group_id, CoarseTimePoint deadline,
      bool skip_health_check);

  Status SetReplicationGroupEnabled(
      const xcluster::ReplicationGroupId& replication_group_id, bool is_enabled,
      const LeaderEpoch& epoch, CoarseTimePoint deadline);

  bool IsNamespaceInAutomaticDDLMode(const NamespaceId& namespace_id) const;

 protected:
  explicit XClusterTargetManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~XClusterTargetManager();

  void StartShutdown() EXCLUDES(replication_setup_tasks_mutex_);
  void CompleteShutdown() EXCLUDES(replication_setup_tasks_mutex_);

  Status Init();

  void Clear();

  Status RunLoaders();

  void SysCatalogLoaded();

  void RunBgTasks(const LeaderEpoch& epoch);

  void DumpState(std::ostream& out, bool on_disk_dump) const;

  Status FillHeartbeatResponse(const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const;

  // After a table is marked for drop we remove them from replication. If master leader failed over
  // after persisting the table state but before the xcluster cleanup we will have deletes tables in
  // our replication group that needs to be lazily cleaned up.
  Status RemoveDroppedTablesFromReplication(const LeaderEpoch& epoch)
      EXCLUDES(table_stream_ids_map_mutex_);

  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> GetPostTabletCreateTasks(
      const TableInfoPtr& table_info, const LeaderEpoch& epoch);

  Result<std::vector<XClusterInboundReplicationGroupStatus>> GetXClusterStatus() const;

  Status PopulateXClusterStatusJson(JsonWriter& jw) const;

  std::unordered_set<xcluster::ReplicationGroupId> GetTransactionalReplicationGroups() const;

  // Returns list of all universe replication group ids if consumer_namespace_id is empty. If
  // consumer_namespace_id is not empty then returns DB scoped replication groups that contain the
  // namespace.
  std::vector<xcluster::ReplicationGroupId> GetUniverseReplications(
      const NamespaceId& consumer_namespace_id) const;

  // Gets the replication group status for the given replication group id. Does not populate the
  // table statuses (only contains fields required for GetUniverseReplicationInfoResponsePB).
  Result<XClusterInboundReplicationGroupStatus> GetUniverseReplicationInfo(
      const xcluster::ReplicationGroupId& replication_group_id) const;

  Status ClearXClusterFieldsAfterYsqlDDL(
      TableInfoPtr table_info, SysTablesEntryPB& table_pb, const LeaderEpoch& epoch);

  void NotifyAutoFlagsConfigChanged();

  Status ReportNewAutoFlagConfigVersion(
      const xcluster::ReplicationGroupId& replication_group_id, uint32 new_version,
      const LeaderEpoch& epoch);

  Status GetReplicationStatus(
      const GetReplicationStatusRequestPB* req, GetReplicationStatusResponsePB* resp) const
      EXCLUDES(replication_error_map_mutex_);

  // Sync replication_error_map_ with the streams we have in our producer_map.
  void SyncReplicationStatusMap(
      const xcluster::ReplicationGroupId& replication_group_id,
      const google::protobuf::Map<std::string, cdc::ProducerEntryPB>& producer_map)
      EXCLUDES(replication_error_map_mutex_);

  void StoreReplicationStatus(
      const XClusterConsumerReplicationStatusPB& consumer_replication_status)
      EXCLUDES(replication_error_map_mutex_);

  Result<bool> HasReplicationGroupErrors(const xcluster::ReplicationGroupId& replication_group_id)
      const EXCLUDES(replication_error_map_mutex_);

  Result<bool> IsReplicationGroupFullyPaused(
      const xcluster::ReplicationGroupId& replication_group_id) const
      EXCLUDES(replication_error_map_mutex_);

  void RecordTableStream(
      const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id,
      const xrepl::StreamId& stream_id) EXCLUDES(table_stream_ids_map_mutex_);

  void RemoveTableStream(
      const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id)
      EXCLUDES(table_stream_ids_map_mutex_);

  void RemoveTableStreams(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::set<TableId>& tables_to_clear) EXCLUDES(table_stream_ids_map_mutex_);

  bool IsTableReplicated(const TableId& table_id) const;

  Result<TableId> GetTableIdForStreamId(
      const xcluster::ReplicationGroupId& replication_group_id,
      const xrepl::StreamId& stream_id) const EXCLUDES(table_stream_ids_map_mutex_);

  Status HandleTabletSplit(
      const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids,
      const LeaderEpoch& epoch) EXCLUDES(table_stream_ids_map_mutex_);

  Status SetupUniverseReplication(
      const SetupUniverseReplicationRequestPB* req, SetupUniverseReplicationResponsePB* resp,
      const LeaderEpoch& epoch);

  Status SetupUniverseReplication(
      XClusterSetupUniverseReplicationData&& data, const LeaderEpoch& epoch);

  Result<IsOperationDoneResult> IsSetupUniverseReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id, bool skip_health_check);

  Status SetupNamespaceReplicationWithBootstrap(
      const SetupNamespaceReplicationWithBootstrapRequestPB* req,
      SetupNamespaceReplicationWithBootstrapResponsePB* resp, const LeaderEpoch& epoch);

  Result<IsSetupNamespaceReplicationWithBootstrapDoneResponsePB>
  IsSetupNamespaceReplicationWithBootstrapDone(
      const xcluster::ReplicationGroupId& replication_group_id);

  Status AlterUniverseReplication(
      const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp,
      const LeaderEpoch& epoch);

  Status DeleteUniverseReplication(
      const xcluster::ReplicationGroupId& replication_group_id, bool ignore_errors,
      bool skip_producer_stream_deletion, DeleteUniverseReplicationResponsePB* resp,
      const LeaderEpoch& epoch,
      std::unordered_map<NamespaceId, uint32_t> source_namespace_id_to_oid_to_bump_above);

  Status AddTableToReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id, const TableId& source_table_id,
      const xrepl::StreamId& bootstrap_id, const std::optional<TableId>& target_table_id,
      const LeaderEpoch& epoch);

  Result<std::optional<HybridTime>> TryGetXClusterSafeTimeForBackfill(
      const std::vector<TableId>& index_table_ids, const TableInfoPtr& indexed_table,
      const LeaderEpoch& epoch) const;

  Status InsertPackedSchemaForXClusterTarget(
      const TableId& table_id, const SchemaPB& packed_schema_to_insert,
      uint32_t current_schema_version, const LeaderEpoch& epoch,
      bool error_on_incorrect_schema_version = false);

  Status HandleNewSchemaForAutomaticXClusterTarget(
      const HandleNewSchemaForAutomaticXClusterTargetRequestPB* req,
      HandleNewSchemaForAutomaticXClusterTargetResponsePB* resp, const LeaderEpoch& epoch);

  Status InsertHistoricalColocatedSchemaPacking(
      const InsertHistoricalColocatedSchemaPackingRequestPB* req,
      InsertHistoricalColocatedSchemaPackingResponsePB* resp, const LeaderEpoch& epoch);

  Status ProcessCreateTableReq(
      const CreateTableRequestPB& req, SysTablesEntryPB& table_pb, const TableId& table_id,
      const NamespaceId& namespace_id) const;

 private:
  // Gets the replication group status for the given replication group id. Does not populate the
  // table statuses.
  Result<XClusterInboundReplicationGroupStatus> GetUniverseReplicationInfo(
      const SysUniverseReplicationEntryPB& replication_info_pb,
      const SysClusterConfigEntryPB& cluster_config_pb,
      const XClusterNamespaceToSafeTimeMap& namespace_safe_time) const;

  Status RefreshLocalAutoFlagConfig(const LeaderEpoch& epoch);
  Status DoRefreshLocalAutoFlagConfig(const LeaderEpoch& epoch);

  // Populate the response with the errors for the given replication group.
  Status PopulateReplicationGroupErrors(
      const xcluster::ReplicationGroupId& replication_group_id,
      GetReplicationStatusResponsePB* resp) const REQUIRES_SHARED(replication_error_map_mutex_)
      EXCLUDES(table_stream_ids_map_mutex_);

  std::unordered_map<xcluster::ReplicationGroupId, xrepl::StreamId> GetStreamIdsForTable(
      const TableId& table_id) const EXCLUDES(table_stream_ids_map_mutex_);

  Result<HybridTime> PrepareAndGetBackfillTimeForBiDirectionalIndex(
      const std::vector<TableId>& index_table_ids, const TableId& indexed_table,
      const LeaderEpoch& epoch) const;

  // In automatic mode this is used to handle the case when we receive a new schema for a
  // colocated table that does not exist yet on the target.
  Status HandleNewTableSchemaForUpcomingColocatedTable(
      const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id,
      const xrepl::StreamId& stream_id, ColocationId colocation_id,
      uint32_t producer_schema_version, const SchemaPB& schema, const LeaderEpoch& epoch);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  std::unique_ptr<XClusterSafeTimeService> safe_time_service_;

  bool removed_deleted_tables_from_replication_ = false;

  // The Catalog Entity is stored outside of XClusterSafeTimeService, since we may want to move the
  // service out of master at a later time.
  XClusterSafeTimeInfo safe_time_info_;

  std::atomic<bool> auto_flags_revalidation_needed_{true};

  // TODO: Wrap this and its associated function into its own class, and move it inside
  // InboundReplicationGroup.
  mutable std::shared_mutex replication_error_map_mutex_;
  std::unordered_map<xcluster::ReplicationGroupId, xcluster::ReplicationGroupErrors>
      replication_error_map_ GUARDED_BY(replication_error_map_mutex_);

  mutable std::shared_mutex table_stream_ids_map_mutex_;

  // Map of all consumer tables that are part of xcluster replication, to a map of the stream infos.
  std::unordered_map<TableId, std::unordered_map<xcluster::ReplicationGroupId, xrepl::StreamId>>
      table_stream_ids_map_ GUARDED_BY(table_stream_ids_map_mutex_);

  mutable std::shared_mutex replication_setup_tasks_mutex_;
  std::unordered_map<
      xcluster::ReplicationGroupId, std::shared_ptr<XClusterInboundReplicationGroupSetupTaskIf>>
      replication_setup_tasks_;

  DISALLOW_COPY_AND_ASSIGN(XClusterTargetManager);
};

}  // namespace yb::master
