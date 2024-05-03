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
#include "yb/master/master.h"
#include "yb/master/xcluster/add_table_to_xcluster_target_task.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"

#include "yb/master/xcluster/xcluster_status.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/is_operation_done_result.h"
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

void XClusterTargetManager::Clear() {
  xcluster_safe_time_info_.Clear();
  removed_deleted_tables_from_replication_ = false;
}

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

Status XClusterTargetManager::WaitForSetupUniverseReplicationToFinish(
    const xcluster::ReplicationGroupId& replication_group_id, CoarseTimePoint deadline) {
  return Wait(
      [&]() -> Result<bool> {
        auto is_operation_done =
            VERIFY_RESULT(IsSetupUniverseReplicationDone(replication_group_id, catalog_manager_));

        if (is_operation_done.done()) {
          RETURN_NOT_OK(is_operation_done.status());
          return true;
        }

        return false;
      },
      deadline,
      Format("Waiting for SetupUniverseReplication of $0 to finish", replication_group_id),
      MonoDelta::FromMilliseconds(200));
}

Status XClusterTargetManager::RemoveDroppedTablesOnConsumer(
    const std::unordered_set<TabletId>& table_ids, const LeaderEpoch& epoch) {
  auto table_stream_map = catalog_manager_.GetXClusterConsumerTableStreams();
  if (table_stream_map.empty()) {
    return Status::OK();
  }

  std::map<xcluster::ReplicationGroupId, std::vector<TableId>> replication_group_producer_tables;
  {
    auto cluster_config = catalog_manager_.ClusterConfig();
    auto l = cluster_config->LockForRead();
    auto& replication_group_map = l->pb.consumer_registry().producer_map();

    for (auto& table_id : table_ids) {
      auto stream_ids = FindOrNull(table_stream_map, table_id);
      if (!stream_ids) {
        continue;
      }

      for (const auto& [replication_group_id, stream_id] : *stream_ids) {
        // Fetch the stream entry so we can update the mappings.
        auto producer_entry = FindOrNull(replication_group_map, replication_group_id.ToString());
        // If we can't find the entries, then the stream has been deleted.
        if (!producer_entry) {
          LOG(WARNING) << "Unable to find the producer entry for universe " << replication_group_id;
          continue;
        }
        auto stream_entry = FindOrNull(producer_entry->stream_map(), stream_id.ToString());
        if (!stream_entry) {
          LOG(WARNING) << "Unable to find the producer entry for universe " << replication_group_id
                       << ", stream " << stream_id;
          continue;
        }
        replication_group_producer_tables[replication_group_id].push_back(
            stream_entry->producer_table_id());
      }
    }
  }

  for (auto& [replication_group_id, producer_tables] : replication_group_producer_tables) {
    LOG(INFO) << "Removing dropped tables " << yb::ToString(table_ids)
              << " from xcluster replication " << replication_group_id;

    auto replication_group = catalog_manager_.GetUniverseReplication(replication_group_id);
    if (!replication_group) {
      VLOG(1) << "Replication group " << replication_group_id << " not found";
      continue;
    }

    RETURN_NOT_OK(RemoveTablesFromReplicationGroup(
        replication_group, producer_tables, catalog_manager_, epoch));
    RETURN_NOT_OK(WaitForSetupUniverseReplicationToFinish(
        replication_group_id, CoarseMonoClock::TimePoint::max()));
  }

  return Status::OK();
}

void XClusterTargetManager::RunBgTasks(const LeaderEpoch& epoch) {
  WARN_NOT_OK(
      RemoveDroppedTablesFromReplication(epoch),
      "Failed to remove dropped tables from consumer replication groups");
}

Status XClusterTargetManager::RemoveDroppedTablesFromReplication(const LeaderEpoch& epoch) {
  if (removed_deleted_tables_from_replication_) {
    return Status::OK();
  }

  std::unordered_set<TabletId> tables_to_remove;
  auto table_stream_map = catalog_manager_.GetXClusterConsumerTableStreams();
  for (const auto& [table_id, _] : table_stream_map) {
    auto table_info = catalog_manager_.GetTableInfo(table_id);
    if (!table_info || !table_info->LockForRead()->visible_to_client()) {
      tables_to_remove.insert(table_id);
      continue;
    }
  }

  RETURN_NOT_OK(RemoveDroppedTablesOnConsumer(tables_to_remove, epoch));

  removed_deleted_tables_from_replication_ = true;
  return Status::OK();
}

namespace {
template <typename PBList>
std::string PBListAsString(const PBList& pb, const char* delim = ",") {
  std::stringstream ostream;
  for (int i = 0; i < pb.size(); i++) {
    ostream << (i ? delim : "") << pb.Get(i).ShortDebugString();
  }

  return ostream.str();
}
}  // namespace

Status XClusterTargetManager::PopulateXClusterStatus(XClusterStatus& xcluster_status) const {
  GetReplicationStatusResponsePB replication_status;
  GetReplicationStatusRequestPB req;
  RETURN_NOT_OK(catalog_manager_.GetReplicationStatus(&req, &replication_status, /*rpc=*/nullptr));

  std::unordered_map<xrepl::StreamId, std::string> stream_status;
  for (const auto& table_stream_status : replication_status.statuses()) {
    if (!table_stream_status.errors_size()) {
      continue;
    }

    const auto stream_id =
        VERIFY_RESULT(xrepl::StreamId::FromString(table_stream_status.stream_id()));
    stream_status[stream_id] = PBListAsString(table_stream_status.errors(), ";");
    auto s = table_stream_status.ShortDebugString();
  }

  SysClusterConfigEntryPB cluster_config =
      VERIFY_RESULT(catalog_manager_.GetClusterConfig());
  const auto& consumer_registry = cluster_config.consumer_registry();

  const auto replication_infos = catalog_manager_.GetAllXClusterUniverseReplicationInfos();

  for (const auto& replication_info : replication_infos) {
    XClusterInboundReplicationGroupStatus replication_group_status;
    replication_group_status.replication_group_id =
        xcluster::ReplicationGroupId(replication_info.replication_group_id());
    replication_group_status.state =
        SysUniverseReplicationEntryPB::State_Name(replication_info.state());
    replication_group_status.transactional = replication_info.transactional();
    replication_group_status.validated_local_auto_flags_config_version =
        replication_info.validated_local_auto_flags_config_version();

    for (const auto& namespace_info : replication_info.db_scoped_info().namespace_infos()) {
      replication_group_status.db_scoped_info += Format(
          "\n  namespace: $0\n    consumer_namespace_id: $1\n    producer_namespace_id: $2",
          catalog_manager_.GetNamespaceName(namespace_info.consumer_namespace_id()),
          namespace_info.consumer_namespace_id(), namespace_info.producer_namespace_id());
    }

    auto* producer_map =
        FindOrNull(consumer_registry.producer_map(), replication_info.replication_group_id());
    if (producer_map) {
      replication_group_status.master_addrs = PBListAsString(producer_map->master_addrs());
      replication_group_status.disable_stream = producer_map->disable_stream();
      replication_group_status.compatible_auto_flag_config_version =
          producer_map->compatible_auto_flag_config_version();
      replication_group_status.validated_remote_auto_flags_config_version =
          producer_map->validated_auto_flags_config_version();
    }

    for (const auto& source_table_id : replication_info.tables()) {
      NamespaceName namespace_name = "<Unknown>";

      InboundXClusterReplicationGroupTableStatus table_status;
      table_status.source_table_id = source_table_id;

      auto* stream_id_it = FindOrNull(replication_info.table_streams(), source_table_id);
      if (stream_id_it) {
        table_status.stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(*stream_id_it));
        auto it = FindOrNull(stream_status, table_status.stream_id);
        table_status.status = it ? *it : "OK";

        if (producer_map) {
          auto* stream_info =
              FindOrNull(producer_map->stream_map(), table_status.stream_id.ToString());
          if (stream_info) {
            table_status.target_table_id = stream_info->consumer_table_id();

            auto table_info_res = catalog_manager_.GetTableById(table_status.target_table_id);
            if (table_info_res) {
              const auto& table_info = table_info_res.get();
              namespace_name = table_info->namespace_name();
              table_status.full_table_name = GetFullTableName(*table_info);
            }

            table_status.target_tablet_count = stream_info->consumer_producer_tablet_map_size();
            table_status.local_tserver_optimized = stream_info->local_tserver_optimized();
            table_status.source_schema_version =
                stream_info->schema_versions().current_producer_schema_version();
            table_status.target_schema_version =
                stream_info->schema_versions().current_consumer_schema_version();
            for (const auto& [_, producer_tablets] : stream_info->consumer_producer_tablet_map()) {
              table_status.source_tablet_count += producer_tablets.tablets_size();
            }
          }
        }
      } else {
        table_status.status = "Not Ready";
      }

      replication_group_status.table_statuses_by_namespace[namespace_name].push_back(
          std::move(table_status));
    }

    xcluster_status.inbound_replication_group_statuses.push_back(
        std::move(replication_group_status));
  }

  return Status::OK();
}

Status XClusterTargetManager::PopulateXClusterStatusJson(JsonWriter& jw) const {
  GetReplicationStatusResponsePB replication_status;
  GetReplicationStatusRequestPB req;
  RETURN_NOT_OK(catalog_manager_.GetReplicationStatus(&req, &replication_status, /*rpc=*/nullptr));

  SysClusterConfigEntryPB cluster_config =
      VERIFY_RESULT(catalog_manager_.GetClusterConfig());

  jw.String("replication_status");
  jw.Protobuf(replication_status);

  const auto replication_infos = catalog_manager_.GetAllXClusterUniverseReplicationInfos();
  jw.String("replication_infos");
  jw.StartArray();
  for (auto const& replication_info : replication_infos) {
    jw.Protobuf(replication_info);
  }
  jw.EndArray();

  jw.String("consumer_registry");
  jw.Protobuf(cluster_config.consumer_registry());

  return Status::OK();
}

std::unordered_set<xcluster::ReplicationGroupId>
XClusterTargetManager::GetTransactionalReplicationGroups() const {
  std::unordered_set<xcluster::ReplicationGroupId> result;
  for (const auto& replication_info : catalog_manager_.GetAllXClusterUniverseReplicationInfos()) {
    if (replication_info.transactional()) {
      result.insert(xcluster::ReplicationGroupId(replication_info.replication_group_id()));
    }
  }
  return result;
}

}  // namespace yb::master
