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

#include "yb/master/xcluster/xcluster_target_manager.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/master.h"
#include "yb/master/xcluster/add_table_to_xcluster_target_task.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"

#include "yb/util/flags.h"
#include "yb/util/status.h"

namespace yb::master {

XClusterTargetManager::XClusterTargetManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : master_(master), catalog_manager_(catalog_manager), sys_catalog_(sys_catalog) {}

XClusterTargetManager::~XClusterTargetManager() {}

void XClusterTargetManager::Shutdown() {
  if (xcluster_safe_time_service_) {
    xcluster_safe_time_service_->Shutdown();
  }
}

Status XClusterTargetManager::Init() {
  DCHECK(!xcluster_safe_time_service_);
  xcluster_safe_time_service_ = std::make_unique<XClusterSafeTimeService>(
      &master_, &catalog_manager_, master_.metric_registry());
  RETURN_NOT_OK(xcluster_safe_time_service_->Init());

  return Status::OK();
}

void XClusterTargetManager::Clear() { xcluster_safe_time_info_.Clear(); }

Status XClusterTargetManager::RunLoaders() {
  RETURN_NOT_OK(
      sys_catalog_.Load<XClusterSafeTimeLoader>("XCluster safe time", xcluster_safe_time_info_));
  return Status::OK();
}

void XClusterTargetManager::SysCatalogLoaded() {
  xcluster_safe_time_service_->ScheduleTaskIfNeeded();
}

void XClusterTargetManager::DumpState(std::ostream& out, bool on_disk_dump) const {
  if (!on_disk_dump) {
    return;
  }

  auto l = xcluster_safe_time_info_.LockForRead();
  if (l->pb.safe_time_map().empty()) {
    return;
  }
  out << "XCluster Safe Time: " << l->pb.ShortDebugString() << "\n";
}

void XClusterTargetManager::CreateXClusterSafeTimeTableAndStartService() {
  WARN_NOT_OK(
      xcluster_safe_time_service_->CreateXClusterSafeTimeTableIfNotFound(),
      "Creation of XClusterSafeTime table failed");

  xcluster_safe_time_service_->ScheduleTaskIfNeeded();
}

Status XClusterTargetManager::GetXClusterSafeTime(
    GetXClusterSafeTimeResponsePB* resp, const LeaderEpoch& epoch) {
  RETURN_NOT_OK_SET_CODE(
      xcluster_safe_time_service_->GetXClusterSafeTimeInfoFromMap(epoch, resp),
      MasterError(MasterErrorPB::INTERNAL_ERROR));

  // Also fill out the namespace_name for each entry.
  if (resp->namespace_safe_times_size()) {
    for (auto& safe_time_info : *resp->mutable_namespace_safe_times()) {
      const auto namespace_info = VERIFY_RESULT_OR_SET_CODE(
          catalog_manager_.FindNamespaceById(safe_time_info.namespace_id()),
          MasterError(MasterErrorPB::INTERNAL_ERROR));

      safe_time_info.set_namespace_name(namespace_info->name());
    }
  }

  return Status::OK();
}

Result<HybridTime> XClusterTargetManager::GetXClusterSafeTime(
    const NamespaceId& namespace_id) const {
  auto l = xcluster_safe_time_info_.LockForRead();
  SCHECK(
      l->pb.safe_time_map().count(namespace_id), NotFound,
      "XCluster safe time not found for namespace $0", namespace_id);

  return HybridTime(l->pb.safe_time_map().at(namespace_id));
}

Result<XClusterNamespaceToSafeTimeMap> XClusterTargetManager::GetXClusterNamespaceToSafeTimeMap()
    const {
  XClusterNamespaceToSafeTimeMap result;
  auto l = xcluster_safe_time_info_.LockForRead();

  for (auto& [namespace_id, hybrid_time] : l->pb.safe_time_map()) {
    result[namespace_id] = HybridTime(hybrid_time);
  }
  return result;
}

Status XClusterTargetManager::GetXClusterSafeTimeForNamespace(
    const GetXClusterSafeTimeForNamespaceRequestPB* req,
    GetXClusterSafeTimeForNamespaceResponsePB* resp, const LeaderEpoch& epoch) {
  SCHECK(!req->namespace_id().empty(), InvalidArgument, "Namespace id must be provided");
  auto safe_time_ht =
      VERIFY_RESULT(GetXClusterSafeTimeForNamespace(epoch, req->namespace_id(), req->filter()));
  resp->set_safe_time_ht(safe_time_ht.ToUint64());
  return Status::OK();
}

Result<HybridTime> XClusterTargetManager::GetXClusterSafeTimeForNamespace(
    const LeaderEpoch& epoch, const NamespaceId& namespace_id,
    const XClusterSafeTimeFilter& filter) {
  return xcluster_safe_time_service_->GetXClusterSafeTimeForNamespace(
      epoch.leader_term, namespace_id, filter);
}

Status XClusterTargetManager::RefreshXClusterSafeTimeMap(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(xcluster_safe_time_service_->ComputeSafeTime(epoch.leader_term));
  return Status::OK();
}

Status XClusterTargetManager::SetXClusterNamespaceToSafeTimeMap(
    const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) {
  auto l = xcluster_safe_time_info_.LockForWrite();
  auto& safe_time_map_pb = *l.mutable_data()->pb.mutable_safe_time_map();
  safe_time_map_pb.clear();
  for (auto& [namespace_id, hybrid_time] : safe_time_map) {
    safe_time_map_pb[namespace_id] = hybrid_time.ToUint64();
  }

  RETURN_NOT_OK_PREPEND(
      sys_catalog_.Upsert(leader_term, &xcluster_safe_time_info_),
      "Updating XCluster safe time in sys-catalog");

  l.Commit();

  return Status::OK();
}

Status XClusterTargetManager::FillHeartbeatResponse(
    const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const {
  auto l = xcluster_safe_time_info_.LockForRead();
  if (!l->pb.safe_time_map().empty()) {
    *resp->mutable_xcluster_namespace_to_safe_time() = l->pb.safe_time_map();
  }

  return Status::OK();
}

std::vector<std::shared_ptr<PostTabletCreateTaskBase>>
XClusterTargetManager::GetPostTabletCreateTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
  std::vector<std::shared_ptr<PostTabletCreateTaskBase>> tasks;

  for (const auto& universe : catalog_manager_.GetAllUniverseReplications()) {
    auto result = ShouldAddTableToReplicationGroup(*universe, *table_info, catalog_manager_);
    if (!result.ok()) {
      LOG(WARNING) << result.status();
      table_info->SetCreateTableErrorStatus(result.status());
      continue;
    }

    if (*result) {
      tasks.emplace_back(std::make_shared<AddTableToXClusterTargetTask>(
          universe, catalog_manager_, *master_.messenger(), table_info, epoch));
    }
  }

  return tasks;
}

}  // namespace yb::master
