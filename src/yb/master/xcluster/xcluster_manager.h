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

#include <memory>
#include <shared_mutex>
#include <vector>

#include "yb/common/entity_ids_types.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/xcluster/xcluster_catalog_entity.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group.h"

namespace yb {

namespace master {
class CDCStreamInfo;
class GetMasterXClusterConfigResponsePB;
class PauseResumeXClusterProducerStreamsRequestPB;
class PauseResumeXClusterProducerStreamsResponsePB;
class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;
class XClusterConfig;
class XClusterSafeTimeService;
struct SysCatalogLoadingState;

// The XClusterManager class is responsible for managing all yb-master related control logic of
// XCluster. All XCluster related RPCs and APIs are handled by this class.
// TODO(#19714): Move XCluster related code from CatalogManager to this class.
class XClusterManager : public XClusterManagerIf {
 public:
  explicit XClusterManager(
      Master* master, CatalogManager* catalog_manager, SysCatalogTable* sys_catalog);

  ~XClusterManager();

  Status Init();

  void Shutdown();

  void Clear();

  Status RunLoaders();

  void SysCatalogLoaded();

  void DumpState(std::ostream* out, bool on_disk_dump = false) const;

  Status GetXClusterConfigEntryPB(SysXClusterConfigEntryPB* config) const EXCLUDES(mutex_) override;

  Status GetMasterXClusterConfig(GetMasterXClusterConfigResponsePB* resp) EXCLUDES(mutex_);

  Result<uint32_t> GetXClusterConfigVersion() const;

  void CreateXClusterSafeTimeTableAndStartService();

  Status PrepareDefaultXClusterConfig(int64_t term, bool recreate) EXCLUDES(mutex_);

  Status FillHeartbeatResponse(const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const;

  // Remove deleted xcluster stream IDs from producer stream Id map.
  Status RemoveStreamFromXClusterProducerConfig(
      const LeaderEpoch& epoch, const std::vector<CDCStreamInfo*>& streams);

  Status PauseResumeXClusterProducerStreams(
      const PauseResumeXClusterProducerStreamsRequestPB* req,
      PauseResumeXClusterProducerStreamsResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  // XCluster Safe Time.
  Result<XClusterNamespaceToSafeTimeMap> GetXClusterNamespaceToSafeTimeMap() const override;
  Status SetXClusterNamespaceToSafeTimeMap(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) override;
  Status GetXClusterSafeTime(
      const GetXClusterSafeTimeRequestPB* req, GetXClusterSafeTimeResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Result<HybridTime> GetXClusterSafeTime(const NamespaceId& namespace_id) const override;

  Result<XClusterNamespaceToSafeTimeMap> RefreshAndGetXClusterNamespaceToSafeTimeMap(
      const LeaderEpoch& epoch) override;

  XClusterSafeTimeService* TEST_xcluster_safe_time_service() {
    return xcluster_safe_time_service_.get();
  }

  // OutboundReplicationGroup RPCs.
  Status XClusterCreateOutboundReplicationGroup(
      const XClusterCreateOutboundReplicationGroupRequestPB* req,
      XClusterCreateOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status XClusterAddNamespaceToOutboundReplicationGroup(
      const XClusterAddNamespaceToOutboundReplicationGroupRequestPB* req,
      XClusterAddNamespaceToOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status XClusterRemoveNamespaceFromOutboundReplicationGroup(
      const XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB* req,
      XClusterRemoveNamespaceFromOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status XClusterDeleteOutboundReplicationGroup(
      const XClusterDeleteOutboundReplicationGroupRequestPB* req,
      XClusterDeleteOutboundReplicationGroupResponsePB* resp, rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);
  Status IsXClusterBootstrapRequired(
      const IsXClusterBootstrapRequiredRequestPB* req, IsXClusterBootstrapRequiredResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);
  Status GetXClusterStreams(
      const GetXClusterStreamsRequestPB* req, GetXClusterStreamsResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

 private:
  template <template <class> class Loader, typename CatalogEntityWrapper>
  Status Load(const std::string& key, CatalogEntityWrapper& catalog_entity_wrapper);

  template <typename Loader, typename CatalogEntityPB>
  Status Load(
      const std::string& key, std::function<Status(const std::string&, const CatalogEntityPB&)>
                                  catalog_entity_inserter_func);

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

  Master* const master_;
  CatalogManager* const catalog_manager_;
  SysCatalogTable* const sys_catalog_;

  mutable std::shared_mutex mutex_;

  std::unique_ptr<XClusterConfig> xcluster_config_;

  std::unique_ptr<XClusterSafeTimeService> xcluster_safe_time_service_;

  // The Catalog Entity is stored outside of XClusterSafeTimeService, since we may want to move the
  // service out of master at a later time.
  XClusterSafeTimeInfo xcluster_safe_time_info_;

  mutable std::shared_mutex outbound_replication_group_map_mutex_;
  std::map<xcluster::ReplicationGroupId, XClusterOutboundReplicationGroup>
      outbound_replication_group_map_ GUARDED_BY(outbound_replication_group_map_mutex_);
};

}  // namespace master

}  // namespace yb
