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

#include "yb/master/xcluster/master_xcluster_types.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group.h"

namespace yb {

namespace cdc {
class CDCStateTable;
}  // namespace cdc

namespace master {
class CatalogManager;
class Master;
class PostTabletCreateTaskBase;
class SysCatalogTable;

class XClusterSourceManager {
 public:
 protected:
  explicit XClusterSourceManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~XClusterSourceManager();

  void Clear();

  Status RunLoaders();

  void SysCatalogLoaded();

  void DumpState(std::ostream& out, bool on_disk_dump) const;

  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> GetPostTabletCreateTasks(
      const TableInfoPtr& table_info, const LeaderEpoch& epoch);

  Result<std::vector<NamespaceId>> CreateOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<NamespaceName>& namespace_names, const LeaderEpoch& epoch,
      CoarseTimePoint deadline);

  Result<NamespaceId> AddNamespaceToOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceName& namespace_name,
      const LeaderEpoch& epoch, CoarseTimePoint deadline);

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

 private:
  Status InsertOutboundReplicationGroup(
      const std::string& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& metadata)
      EXCLUDES(outbound_replication_group_map_mutex_);

  XClusterOutboundReplicationGroup InitOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& metadata);

  Result<XClusterOutboundReplicationGroup*> GetOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id)
      REQUIRES(outbound_replication_group_map_mutex_);

  Result<const XClusterOutboundReplicationGroup*> GetOutboundReplicationGroup(
      const xcluster::ReplicationGroupId& replication_group_id) const
      REQUIRES_SHARED(outbound_replication_group_map_mutex_);

  Result<std::vector<TableInfoPtr>> GetTablesToReplicate(const NamespaceId& namespace_id);

  Result<std::vector<xrepl::StreamId>> BootstrapTables(
      const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  mutable std::shared_mutex outbound_replication_group_map_mutex_;
  std::map<xcluster::ReplicationGroupId, XClusterOutboundReplicationGroup>
      outbound_replication_group_map_ GUARDED_BY(outbound_replication_group_map_mutex_);

  DISALLOW_COPY_AND_ASSIGN(XClusterSourceManager);
};

}  // namespace master
}  // namespace yb
