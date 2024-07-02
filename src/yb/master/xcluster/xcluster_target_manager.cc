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

#include "yb/gutil/strings/util.h"
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
template <typename Element>
std::string PBListAsString(
    const ::google::protobuf::RepeatedPtrField<Element>& pb, const char* delim = ",") {
  std::stringstream ostream;
  for (int i = 0; i < pb.size(); i++) {
    ostream << (i ? delim : "") << pb.Get(i).ShortDebugString();
  }

  return ostream.str();
}

std::string ShortReplicationErrorName(ReplicationErrorPb error) {
  return StringReplace(
      ReplicationErrorPb_Name(error), "REPLICATION_", "",
      /*replace_all=*/false);
}

std::string PBListAsString(
    const ::google::protobuf::RepeatedPtrField<ReplicationStatusErrorPB>& pb,
    const char* delim = ",") {
  std::stringstream ostream;
  for (int i = 0; i < pb.size(); i++) {
    ostream << (i ? delim : "") << ShortReplicationErrorName(pb.Get(i).error());
    if (!pb.Get(i).error_detail().empty()) {
      ostream << ": " << pb.Get(i).error_detail();
    }
  }

  return ostream.str();
}

}  // namespace

Result<std::vector<XClusterInboundReplicationGroupStatus>>
XClusterTargetManager::GetXClusterStatus() const {
  std::vector<XClusterInboundReplicationGroupStatus> result;

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
  }

  const auto cluster_config_pb = VERIFY_RESULT(catalog_manager_.GetClusterConfig());
  const auto replication_infos = catalog_manager_.GetAllXClusterUniverseReplicationInfos();

  for (const auto& replication_info : replication_infos) {
    auto replication_group_status =
        VERIFY_RESULT(GetUniverseReplicationInfo(replication_info, cluster_config_pb));

    for (auto& [_, tables] : replication_group_status.table_statuses_by_namespace) {
      for (auto& table : tables) {
        if (table.stream_id.IsNil()) {
          static const auto repl_error_uninitialized =
              ShortReplicationErrorName(ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED);
          table.status = repl_error_uninitialized;
        } else {
          static const auto repl_ok = ShortReplicationErrorName(ReplicationErrorPb::REPLICATION_OK);
          table.status = FindWithDefault(stream_status, table.stream_id, repl_ok);
        }
      }
    }

    result.push_back(std::move(replication_group_status));
  }

  return result;
}

Result<XClusterInboundReplicationGroupStatus> XClusterTargetManager::GetUniverseReplicationInfo(
    const SysUniverseReplicationEntryPB& replication_info_pb,
    const SysClusterConfigEntryPB& cluster_config_pb) const {
  XClusterInboundReplicationGroupStatus result;
  result.replication_group_id =
      xcluster::ReplicationGroupId(replication_info_pb.replication_group_id());
  result.state = SysUniverseReplicationEntryPB::State_Name(replication_info_pb.state());
  result.replication_type = replication_info_pb.transactional()
                                ? XClusterReplicationType::XCLUSTER_YSQL_TRANSACTIONAL
                                : XClusterReplicationType::XCLUSTER_NON_TRANSACTIONAL;
  result.validated_local_auto_flags_config_version =
      replication_info_pb.validated_local_auto_flags_config_version();

  if (IsDbScoped(replication_info_pb)) {
    result.replication_type = XClusterReplicationType::XCLUSTER_YSQL_DB_SCOPED;
    for (const auto& namespace_info : replication_info_pb.db_scoped_info().namespace_infos()) {
      result.db_scope_namespace_id_map[namespace_info.consumer_namespace_id()] =
          namespace_info.producer_namespace_id();
      result.db_scoped_info += Format(
          "\n  namespace: $0\n    consumer_namespace_id: $1\n    producer_namespace_id: $2",
          catalog_manager_.GetNamespaceName(namespace_info.consumer_namespace_id()),
          namespace_info.consumer_namespace_id(), namespace_info.producer_namespace_id());
    }
  }

  auto* producer_map = FindOrNull(
      cluster_config_pb.consumer_registry().producer_map(),
      replication_info_pb.replication_group_id());
  if (producer_map) {
    result.master_addrs = PBListAsString(producer_map->master_addrs());
    result.disable_stream = producer_map->disable_stream();
    result.compatible_auto_flag_config_version =
        producer_map->compatible_auto_flag_config_version();
    result.validated_remote_auto_flags_config_version =
        producer_map->validated_auto_flags_config_version();
  }

  for (const auto& source_table_id : replication_info_pb.tables()) {
    NamespaceName namespace_name = "<Unknown>";

    InboundXClusterReplicationGroupTableStatus table_status;
    table_status.source_table_id = source_table_id;

    auto* stream_id_it = FindOrNull(replication_info_pb.table_streams(), source_table_id);
    if (stream_id_it) {
      table_status.stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(*stream_id_it));

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
    }

    result.table_statuses_by_namespace[namespace_name].push_back(std::move(table_status));
  }

  return result;
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

std::vector<xcluster::ReplicationGroupId> XClusterTargetManager::GetUniverseReplications(
    const NamespaceId& consumer_namespace_id) const {
  std::vector<xcluster::ReplicationGroupId> result;
  auto universe_replications = catalog_manager_.GetAllUniverseReplications();
  for (const auto& replication_info : universe_replications) {
    if (!consumer_namespace_id.empty() && !HasNamespace(*replication_info, consumer_namespace_id)) {
      continue;
    }
    result.push_back(replication_info->ReplicationGroupId());
  }

  return result;
}

Result<XClusterInboundReplicationGroupStatus> XClusterTargetManager::GetUniverseReplicationInfo(
    const xcluster::ReplicationGroupId& replication_group_id) const {
  auto replication_info = catalog_manager_.GetUniverseReplication(replication_group_id);
  SCHECK_FORMAT(replication_info, NotFound, "Replication group $0 not found", replication_group_id);

  const auto cluster_config = VERIFY_RESULT(catalog_manager_.GetClusterConfig());
  auto l = replication_info->LockForRead();
  return GetUniverseReplicationInfo(l->pb, cluster_config);
}

Status XClusterTargetManager::ClearXClusterSourceTableId(
    TableInfoPtr table_info, const LeaderEpoch& epoch) {
  auto table_l = table_info->LockForWrite();
  table_l.mutable_data()->pb.clear_xcluster_source_table_id();
  RETURN_NOT_OK_PREPEND(
      sys_catalog_.Upsert(epoch, table_info), "clearing xCluster source table id from table");
  table_l.Commit();
  return Status::OK();
}

}  // namespace yb::master
