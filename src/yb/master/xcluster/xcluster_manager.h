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

#include "yb/gutil/thread_annotations.h"
#include "yb/master/leader_epoch.h"
#include "yb/util/status_fwd.h"

namespace yb {

namespace rpc {

class RpcContext;

}  // namespace rpc

namespace master {

class CatalogManager;
class Master;
class SysCatalogTable;
class XClusterConfig;
class XClusterSafeTimeService;
class CDCStreamInfo;
class GetXClusterSafeTimeRequestPB;
class GetXClusterSafeTimeResponsePB;
class SysXClusterConfigEntryPB;
class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;
class GetMasterXClusterConfigResponsePB;
class PauseResumeXClusterProducerStreamsRequestPB;
class PauseResumeXClusterProducerStreamsResponsePB;
struct SysCatalogLoadingState;

// The XClusterManager class is responsible for managing all yb-master related control logic of
// XCluster. All XCluster related RPCs and APIs are handled by this class.
// TODO(#19353): Move XCluster related code from CatalogManager to this class.
class XClusterManager {
 public:
  explicit XClusterManager(
      Master* master, CatalogManager* catalog_manager, SysCatalogTable* sys_catalog);

  ~XClusterManager();

  Status Init();

  void Shutdown();

  void ClearState();

  void LoadXClusterConfig(const SysXClusterConfigEntryPB& metadata);

  void SysCatalogLoaded();

  XClusterSafeTimeService* TEST_xcluster_safe_time_service() {
    return xcluster_safe_time_service_.get();
  }

  Status GetXClusterSafeTime(
      const GetXClusterSafeTimeRequestPB* req, GetXClusterSafeTimeResponsePB* resp,
      rpc::RpcContext* rpc, const LeaderEpoch& epoch);

  Status GetXClusterConfigEntryPB(SysXClusterConfigEntryPB* config) const EXCLUDES(mutex_);

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

 private:
  friend class AddTableToXClusterTask;

  Master* const master_;
  CatalogManager* const catalog_manager_;
  SysCatalogTable* const sys_catalog_;

  mutable std::shared_mutex mutex_;

  std::unique_ptr<XClusterConfig> xcluster_config_;

  std::unique_ptr<XClusterSafeTimeService> xcluster_safe_time_service_;
};

}  // namespace master

}  // namespace yb
