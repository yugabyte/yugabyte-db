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

#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/master/xcluster/master_xcluster_types.h"
#include "yb/util/status_callback.h"

namespace yb {

class IsOperationDoneResult;

namespace cdc {
class CDCStateTable;
}  // namespace cdc

namespace master {
class CatalogManager;
class Master;
class PostTabletCreateTaskBase;
class SysCatalogTable;
class XClusterOutboundReplicationGroup;
class XClusterOutboundReplicationGroupInfo;
class XClusterOutboundReplicationGroupTaskFactory;

class XClusterSourceManager {
 public:
  std::optional<uint32> GetDefaultWalRetentionSec(const NamespaceId& namespace_id) const;

 protected:
  XClusterSourceManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~XClusterSourceManager();

  Status Init();

  void Clear();

  Status RunLoaders();

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

 private:
  friend class XClusterOutboundReplicationGroup;

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

  Result<std::unique_ptr<XClusterCreateStreamsContext>> CreateNewXClusterStreams(
      const std::vector<TableId>& table_ids, const LeaderEpoch& epoch);

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

  DISALLOW_COPY_AND_ASSIGN(XClusterSourceManager);
};

}  // namespace master
}  // namespace yb
