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

#include "yb/master/xcluster/xcluster_manager.h"

#include <string>

#include "yb/consensus/consensus_util.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/xcluster/xcluster_config.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"

namespace yb::master {

XClusterManager::XClusterManager(
    Master* master, CatalogManager* catalog_manager, SysCatalogTable* sys_catalog)
    : master_(master), catalog_manager_(catalog_manager), sys_catalog_(sys_catalog) {
  xcluster_config_ = std::make_unique<XClusterConfig>(sys_catalog_);
}

XClusterManager::~XClusterManager() {}

void XClusterManager::Shutdown() {
  if (xcluster_safe_time_service_) {
    xcluster_safe_time_service_->Shutdown();
  }
}

void XClusterManager::ClearState() { xcluster_config_->ClearState(); }

Status XClusterManager::Init() {
  DCHECK(!xcluster_safe_time_service_);
  xcluster_safe_time_service_ = std::make_unique<XClusterSafeTimeService>(
      master_, catalog_manager_, master_->metric_registry());
  RETURN_NOT_OK(xcluster_safe_time_service_->Init());

  return Status::OK();
}

void XClusterManager::LoadXClusterConfig(const SysXClusterConfigEntryPB& metadata) {
  xcluster_config_->Load(metadata);
}

void XClusterManager::SysCatalogLoaded(const SysCatalogLoadingState& state) {
  xcluster_safe_time_service_->ScheduleTaskIfNeeded();
}

void XClusterManager::CreateXClusterSafeTimeTableAndStartService() {
  auto status = xcluster_safe_time_service_->CreateXClusterSafeTimeTableIfNotFound();
  if (!status.ok()) {
    LOG(WARNING) << "Creation of XClusterSafeTime table failed :" << status;
  }

  xcluster_safe_time_service_->ScheduleTaskIfNeeded();
}

Status XClusterManager::GetXClusterSafeTime(
    const GetXClusterSafeTimeRequestPB* req, GetXClusterSafeTimeResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  const auto status = xcluster_safe_time_service_->GetXClusterSafeTimeInfoFromMap(epoch, resp);
  if (!status.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, status);
  }

  // Also fill out the namespace_name for each entry.
  if (resp->namespace_safe_times_size()) {
    for (auto& safe_time_info : *resp->mutable_namespace_safe_times()) {
      const auto result = catalog_manager_->FindNamespaceById(safe_time_info.namespace_id());
      if (!result) {
        return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, result.status());
      }
      safe_time_info.set_namespace_name(result.get()->name());
    }
  }

  return Status::OK();
}

Status XClusterManager::PrepareDefaultXClusterConfig(int64_t term, bool recreate) {
  return xcluster_config_->PrepareDefault(term, recreate);
}

Status XClusterManager::GetXClusterConfigEntryPB(SysXClusterConfigEntryPB* config) const {
  *config = VERIFY_RESULT(xcluster_config_->GetXClusterConfigEntryPB());
  return Status::OK();
}

Status XClusterManager::GetMasterXClusterConfig(GetMasterXClusterConfigResponsePB* resp) {
  return GetXClusterConfigEntryPB(resp->mutable_xcluster_config());
}

Result<uint32_t> XClusterManager::GetXClusterConfigVersion() const {
  return xcluster_config_->GetVersion();
}

Status XClusterManager::FillHeartbeatResponse(
    const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const {
  return xcluster_config_->FillHeartbeatResponse(req, resp);
}

Status XClusterManager::RemoveStreamFromXClusterProducerConfig(const LeaderEpoch& epoch,
    const std::vector<CDCStreamInfo*>& streams) {
  return xcluster_config_->RemoveStreams(epoch, streams);
}

Status XClusterManager::PauseResumeXClusterProducerStreams(
    const PauseResumeXClusterProducerStreamsRequestPB* req,
    PauseResumeXClusterProducerStreamsResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing PauseXCluster request from " << RequestorString(rpc) << ".";
  SCHECK(req->has_is_paused(), InvalidArgument, "is_paused must be set in the request");
  bool paused = req->is_paused();
  std::string action = paused ? "Pausing" : "Resuming";
  if (req->stream_ids_size() == 0) {
    LOG(INFO) << action << " replication for all XCluster streams.";
  }

  auto xrepl_stream_ids = catalog_manager_->GetAllXreplStreamIds();
  std::vector<xrepl::StreamId> streams_to_change;

  if (req->stream_ids().empty()) {
    for (const auto& stream_id : xrepl_stream_ids) {
      streams_to_change.push_back(stream_id);
    }
  } else {
    for (const auto& stream_id_str : req->stream_ids()) {
      auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_id_str));
      SCHECK(
          xrepl_stream_ids.contains(stream_id), NotFound, "XCluster Stream: $0 does not exists",
          stream_id_str);
      streams_to_change.push_back(stream_id);
    }
  }

  return xcluster_config_->PauseResumeXClusterProducerStreams(epoch, streams_to_change, paused);
}

}  // namespace yb::master
