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
#include "yb/master/master_fwd.h"

#include "yb/master/xcluster/master_xcluster_types.h"
#include "yb/master/xcluster/xcluster_catalog_entity.h"
#include "yb/util/status_fwd.h"

namespace yb::master {

class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;

class PostTabletCreateTaskBase;
class UniverseReplicationInfo;
class XClusterSafeTimeService;
struct XClusterInboundReplicationGroupStatus;

class XClusterTargetManager {
 public:
  // XCluster Safe Time.
  void CreateXClusterSafeTimeTableAndStartService();

  Result<XClusterNamespaceToSafeTimeMap> GetXClusterNamespaceToSafeTimeMap() const;

  Status SetXClusterNamespaceToSafeTimeMap(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map);

  Status GetXClusterSafeTime(GetXClusterSafeTimeResponsePB* resp, const LeaderEpoch& epoch);

  Result<HybridTime> GetXClusterSafeTime(const NamespaceId& namespace_id) const;

  Status GetXClusterSafeTimeForNamespace(
      const GetXClusterSafeTimeForNamespaceRequestPB* req,
      GetXClusterSafeTimeForNamespaceResponsePB* resp, const LeaderEpoch& epoch);

  Result<HybridTime> GetXClusterSafeTimeForNamespace(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id,
      const XClusterSafeTimeFilter& filter);

  Status RefreshXClusterSafeTimeMap(const LeaderEpoch& epoch);

  XClusterSafeTimeService* TEST_xcluster_safe_time_service() {
    return xcluster_safe_time_service_.get();
  }

  Status RemoveDroppedTablesOnConsumer(
      const std::unordered_set<TabletId>& table_ids, const LeaderEpoch& epoch);

  Status WaitForSetupUniverseReplicationToFinish(
      const xcluster::ReplicationGroupId& replication_group_id, CoarseTimePoint deadline);

 protected:
  explicit XClusterTargetManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~XClusterTargetManager();

  void Shutdown();

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
  Status RemoveDroppedTablesFromReplication(const LeaderEpoch& epoch);

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

 private:
  // Gets the replication group status for the given replication group id. Does not populate the
  // table statuses.
  Result<XClusterInboundReplicationGroupStatus> GetUniverseReplicationInfo(
      const SysUniverseReplicationEntryPB& replication_info_pb,
      const SysClusterConfigEntryPB& cluster_config_pb) const;

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  std::unique_ptr<XClusterSafeTimeService> xcluster_safe_time_service_;

  bool removed_deleted_tables_from_replication_ = false;

  // The Catalog Entity is stored outside of XClusterSafeTimeService, since we may want to move the
  // service out of master at a later time.
  XClusterSafeTimeInfo xcluster_safe_time_info_;

  DISALLOW_COPY_AND_ASSIGN(XClusterTargetManager);
};

}  // namespace yb::master
