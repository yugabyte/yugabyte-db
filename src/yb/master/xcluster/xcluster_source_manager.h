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

#include <shared_mutex>

#include "yb/cdc/xcluster_types.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/master/xcluster/master_xcluster_types.h"
#include "yb/util/status_callback.h"

namespace yb {

class IsOperationDoneResult;
class JsonWriter;

namespace cdc {
class CDCStateTable;
struct CDCStateTableKey;
}  // namespace cdc

namespace master {
class PostTabletCreateTaskBase;
class XClusterOutboundReplicationGroup;
class XClusterOutboundReplicationGroupInfo;
class XClusterOutboundReplicationGroupTaskFactory;
struct TabletDeleteRetainerInfo;
struct XClusterStatus;

class XClusterSourceManager {
 public:
  void RecordOutboundStream(const CDCStreamInfoPtr& stream, const TableId& table_id);

  void CleanupStreamFromMaps(const CDCStreamInfo& stream);

  std::optional<uint32> GetDefaultWalRetentionSec(const NamespaceId& namespace_id) const;

  bool IsTableReplicated(const TableId& table_id) const EXCLUDES(tables_to_stream_map_mutex_);
  bool DoesTableHaveAnyBootstrappingStream(const TableId& table_id) const
      EXCLUDES(tables_to_stream_map_mutex_);

  void PopulateTabletDeleteRetainerInfoForTabletDrop(
      const TabletInfo& tablet_info, TabletDeleteRetainerInfo& delete_retainer) const
      EXCLUDES(tables_to_stream_map_mutex_);

  void PopulateTabletDeleteRetainerInfoForTableDrop(
      const TableInfo& table_info, TabletDeleteRetainerInfo& delete_retainer) const
      EXCLUDES(tables_to_stream_map_mutex_);

  void RecordHiddenTablets(
      const TabletInfos& hidden_tablets, const TabletDeleteRetainerInfo& delete_retainer)
      EXCLUDES(retained_hidden_tablets_mutex_);

  bool ShouldRetainHiddenTablet(const TabletInfo& tablet_info) const
      EXCLUDES(retained_hidden_tablets_mutex_);

  Status DoProcessHiddenTablets() EXCLUDES(retained_hidden_tablets_mutex_);

  Result<xrepl::StreamId> CreateNewXClusterStreamForTable(
      const TableId& table_id, bool transactional,
      const std::optional<SysCDCStreamEntryPB::State>& initial_state,
      const google::protobuf::RepeatedPtrField<::yb::master::CDCStreamOptionsPB>& options,
      const LeaderEpoch& epoch);

 protected:
  XClusterSourceManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~XClusterSourceManager();

  Status Init();

  void Clear();

  Status RunLoaders(const TabletInfos& hidden_tablets);

  void SysCatalogLoaded();

  void DumpState(std::ostream& out, bool on_disk_dump) const;

  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> GetPostTabletCreateTasks(
      const TableInfoPtr& table_info, const LeaderEpoch& epoch);

  Result<std::vector<NamespaceId>> CreateOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<NamespaceName>& namespace_names, const LeaderEpoch& epoch);

  Result<NamespaceId> AddNamespaceToOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceName& namespace_name,
      const LeaderEpoch& epoch);

  Status RemoveNamespaceFromOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      const LeaderEpoch& epoch);

  Status DeleteOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id, const LeaderEpoch& epoch);

  Result<std::optional<bool>> IsBootstrapRequired(
      const xcluster::ReplicationGroupId& replication_group_id,
      const NamespaceId& namespace_id) const;

  Result<std::optional<NamespaceCheckpointInfo>> GetXClusterStreams(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      std::vector<std::pair<TableName, PgSchemaName>> opt_table_names) const;

  Status CreateXClusterReplication(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch);

  Result<IsOperationDoneResult> IsCreateXClusterReplicationDone(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<HostPort>& target_master_addresses, const LeaderEpoch& epoch);

  Status PopulateXClusterStatus(
      XClusterStatus& xcluster_status, const SysXClusterConfigEntryPB& xcluster_config) const;

  Status PopulateXClusterStatusJson(JsonWriter& jw) const;

  Status RemoveStreamsFromSysCatalog(
      const std::vector<CDCStreamInfo*>& streams, const LeaderEpoch& epoch);

 private:
  friend class XClusterOutboundReplicationGroup;

  struct HiddenTabletInfo {
    TableId table_id;
    TabletId parent_tablet_id;
    std::array<TabletId, kNumSplitParts> split_tablets;
  };

  // Get all the outbound replication groups. Shared pointer are guaranteed not to be null.
  std::vector<std::shared_ptr<XClusterOutboundReplicationGroup>> GetAllOutboundGroups() const
      EXCLUDES(outbound_replication_group_map_mutex_);

  Status InsertOutboundReplicationGroup(
      const std::string& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& metadata)
      EXCLUDES(outbound_replication_group_map_mutex_);

  std::shared_ptr<XClusterOutboundReplicationGroup> InitOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& metadata);

  Result<std::shared_ptr<XClusterOutboundReplicationGroup>> GetOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id) const
      EXCLUDES(outbound_replication_group_map_mutex_);

  Result<std::vector<TableInfoPtr>> GetTablesToReplicate(const NamespaceId& namespace_id);

  Result<std::unique_ptr<XClusterCreateStreamsContext>> CreateStreamsForDbScoped(
      const std::vector<TableId>& table_ids, const LeaderEpoch& epoch);

  Result<std::unique_ptr<XClusterCreateStreamsContext>> CreateStreamsInternal(
      const std::vector<TableId>& table_ids, SysCDCStreamEntryPB::State state,
      const google::protobuf::RepeatedPtrField<::yb::master::CDCStreamOptionsPB>& options,
      bool transactional, const LeaderEpoch& epoch);

  // Checkpoint the xCluster stream to the given location. Invokes callback with true if bootstrap
  // is required, and false is bootstrap is not required.
  Status CheckpointXClusterStreams(
      const std::vector<std::pair<TableId, xrepl::StreamId>>& table_streams,
      StreamCheckpointLocation checkpoint_location, const LeaderEpoch& epoch,
      bool check_if_bootstrap_required, std::function<void(Result<bool>)> user_callback);

  Status CheckpointStreamsToOp0(
      const std::vector<std::pair<TableId, xrepl::StreamId>>& table_streams,
      StdStatusCallback user_callback);

  Status SetWALRetentionOnTables(const std::vector<TableId>& table_ids, const LeaderEpoch& epoch);

  Status CheckpointStreamsToEndOfWAL(
      const std::vector<std::pair<TableId, xrepl::StreamId>>& table_streams,
      const LeaderEpoch& epoch, bool check_if_bootstrap_required,
      std::function<void(Result<bool>)> user_callback);

  std::vector<CDCStreamInfoPtr> GetStreamsForTable(
      const TableId& table_id, bool include_dropped = false) const
      EXCLUDES(tables_to_stream_map_mutex_);

  std::unordered_map<TableId, std::vector<CDCStreamInfoPtr>> GetAllStreams() const;

  // Populate entries_to_delete, and return true if cdc state table entries across all streams for
  // the tablet have already been deleted, or recorded in entries_to_delete. Entry is eligible for
  // delete once the child tablets start getting polled.
  Result<bool> ProcessSplitChildStreams(
      const TabletId& tablet_id, const HiddenTabletInfo& hidden_tablet,
      const std::vector<CDCStreamInfoPtr>& outbound_streams,
      std::vector<cdc::CDCStateTableKey>& entries_to_delete);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  mutable std::shared_mutex outbound_replication_group_map_mutex_;
  // Map of XClusterOutboundReplicationGroups.
  // Value will be nullptr if the group is being created and not yet written to the sys_catalog.
  // This is done to ensure multiple create operations on the same Id are not allowed.
  std::map<xcluster::ReplicationGroupId, std::shared_ptr<XClusterOutboundReplicationGroup>>
      outbound_replication_group_map_ GUARDED_BY(outbound_replication_group_map_mutex_);

  std::unique_ptr<XClusterOutboundReplicationGroupTaskFactory> async_task_factory_;

  std::unique_ptr<cdc::CDCStateTable> cdc_state_table_;

  // Map of tables -> set of cdc streams they are producers for.
  mutable std::shared_mutex tables_to_stream_map_mutex_;
  std::unordered_map<TableId, std::vector<CDCStreamInfoPtr>> tables_to_stream_map_
      GUARDED_BY(tables_to_stream_map_mutex_);

  mutable std::shared_mutex retained_hidden_tablets_mutex_;
  // Split parent tablets are retained until their child tablets start getting polled by the target
  // universe.
  std::unordered_map<TabletId, HiddenTabletInfo> retained_hidden_tablets_
      GUARDED_BY(retained_hidden_tablets_mutex_);

  DISALLOW_COPY_AND_ASSIGN(XClusterSourceManager);
};

}  // namespace master
}  // namespace yb
