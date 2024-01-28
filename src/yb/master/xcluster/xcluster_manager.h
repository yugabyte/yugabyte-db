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
#include "yb/master/xcluster/xcluster_source_manager.h"
#include "yb/master/xcluster/xcluster_target_manager.h"

namespace yb {

namespace master {
class CDCStreamInfo;
class GetMasterXClusterConfigResponsePB;
class PauseResumeXClusterProducerStreamsRequestPB;
class PauseResumeXClusterProducerStreamsResponsePB;
class PostTabletCreateTaskBase;
class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;
class XClusterConfig;
class XClusterSafeTimeService;
struct SysCatalogLoadingState;

// The XClusterManager class is responsible for managing all yb-master related control logic of
// XCluster. All XCluster related RPCs and APIs are handled by this class.
// TODO(#19714): Move XCluster related code from CatalogManager to this class.
class XClusterManager : public XClusterManagerIf,
                        public XClusterSourceManager,
                        public XClusterTargetManager {
 public:
  explicit XClusterManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~XClusterManager();

  Status Init();

  void Shutdown();

  void Clear();

  Status RunLoaders();

  void SysCatalogLoaded();

  void DumpState(std::ostream* out, bool on_disk_dump = false) const;

  Status GetXClusterConfigEntryPB(SysXClusterConfigEntryPB* config) const  override;

  Status GetMasterXClusterConfig(GetMasterXClusterConfigResponsePB* resp);

  Result<uint32_t> GetXClusterConfigVersion() const;

  Status PrepareDefaultXClusterConfig(int64_t term, bool recreate);

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

  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> GetPostTabletCreateTasks(
      const TableInfoPtr& table_info, const LeaderEpoch& epoch);

 private:
  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  std::unique_ptr<XClusterConfig> xcluster_config_;
};

}  // namespace master

}  // namespace yb
