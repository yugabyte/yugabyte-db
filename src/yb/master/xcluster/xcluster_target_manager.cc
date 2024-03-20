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
#include "yb/master/xcluster/xcluster_safe_time_service.h"

#include "yb/util/flags.h"
#include "yb/util/status.h"

DEFINE_RUNTIME_bool(
    disable_auto_add_index_to_xcluster, false,
    "Disables the automatic addition of indexes to transactional xCluster replication.");

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

Result<XClusterNamespaceToSafeTimeMap>
XClusterTargetManager::RefreshAndGetXClusterNamespaceToSafeTimeMap(const LeaderEpoch& epoch) {
  return xcluster_safe_time_service_->RefreshAndGetXClusterNamespaceToSafeTimeMap(epoch);
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

bool XClusterTargetManager::ShouldAddTableToXClusterTarget(const TableInfo& table) const {
  if (FLAGS_disable_auto_add_index_to_xcluster) {
    return false;
  }

  const auto& pb = table.metadata().dirty().pb;

  // Only user created YSQL Indexes should be automatically added to xCluster replication.
  // For Colocated tables, this function will return false since it is only called on the parent
  // colocated table, which cannot be an index.
  if (pb.colocated() || pb.table_type() != PGSQL_TABLE_TYPE || !IsIndex(pb) ||
      !catalog_manager_.IsUserCreatedTable(table)) {
    return false;
  }

  auto indexed_table_stream_ids = catalog_manager_.GetXClusterConsumerStreamIdsForTable(table.id());
  if (!indexed_table_stream_ids.empty()) {
    VLOG(1) << "Index " << table.ToString() << " is already part of xcluster replication "
            << yb::ToString(indexed_table_stream_ids);
    return false;
  }

  auto indexed_table = catalog_manager_.GetTableInfo(GetIndexedTableId(pb));
  if (!indexed_table) {
    LOG(WARNING) << "Indexed table for " << table.id() << " not found";
    return false;
  }

  auto stream_ids = catalog_manager_.GetXClusterConsumerStreamIdsForTable(indexed_table->id());
  if (stream_ids.empty()) {
    return false;
  }

  if (stream_ids.size() > 1) {
    LOG(WARNING) << "Skipping adding index " << table.ToString()
                 << " to xCluster replication as the base table" << indexed_table->ToString()
                 << " is part of multiple replication streams " << yb::ToString(stream_ids);
    return false;
  }

  const auto& replication_group_id = stream_ids.begin()->first;
  auto cluster_config = catalog_manager_.ClusterConfig();
  {
    auto l = cluster_config->LockForRead();
    const auto& consumer_registry = l.data().pb.consumer_registry();
    // Only add if we are in a transactional replication with STANDBY mode.
    if (consumer_registry.role() != cdc::XClusterRole::STANDBY ||
        !consumer_registry.transactional()) {
      return false;
    }

    auto producer_entry =
        FindOrNull(consumer_registry.producer_map(), replication_group_id.ToString());
    if (producer_entry) {
      // Check if the table is already part of replication.
      // This is needed despite the check for GetXClusterConsumerStreamIdsForTable as the in-memory
      // list is not atomically updated.
      for (auto& stream_info : producer_entry->stream_map()) {
        if (stream_info.second.consumer_table_id() == table.id()) {
          VLOG(1) << "Index " << table.ToString() << " is already part of xcluster replication "
                  << stream_info.first;
          return false;
        }
      }
    }
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    auto universe = catalog_manager_.GetUniverseReplication(replication_group_id);
    if (universe == nullptr) {
      LOG(WARNING) << "Skip adding index " << table.ToString()
                   << " to xCluster replication as the universe " << replication_group_id
                   << " was not found";
      return false;
    }

    if (universe->LockForRead()->is_deleted_or_failed()) {
      LOG(WARNING) << "Skip adding index " << table.ToString()
                   << " to xCluster replication as the universe " << replication_group_id
                   << " is in a deleted or failed state";
      return false;
    }
  }

  return true;
}

std::vector<std::shared_ptr<PostTabletCreateTaskBase>>
XClusterTargetManager::GetPostTabletCreateTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
  if (!ShouldAddTableToXClusterTarget(*table_info)) {
    return {};
  }

  return {std::make_shared<AddTableToXClusterTargetTask>(
      catalog_manager_, *master_.messenger(), table_info, epoch)};
}

}  // namespace yb::master
