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

#include "yb/master/xcluster_consumer_registry_service.h"
#include "yb/master/xcluster/add_table_to_xcluster_target_task.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_bootstrap_helper.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/master/xcluster/xcluster_status.h"
#include "yb/master/xcluster/xcluster_universe_replication_alter_helper.h"
#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"

DEFINE_RUNTIME_bool(xcluster_wait_on_ddl_alter, true,
    "When xCluster replication sends a DDL change, wait for the user to enter a "
    "compatible/matching entry.  Note: Can also set at runtime to resume after stall.");

namespace yb::master {

XClusterTargetManager::XClusterTargetManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : master_(master), catalog_manager_(catalog_manager), sys_catalog_(sys_catalog) {}

XClusterTargetManager::~XClusterTargetManager() {}

void XClusterTargetManager::StartShutdown() {
  {
    std::lock_guard l(replication_setup_tasks_mutex_);
    for (auto& [_, task] : replication_setup_tasks_) {
      task->TryCancel();
    }
  }
}

void XClusterTargetManager::CompleteShutdown() {
  if (safe_time_service_) {
    safe_time_service_->Shutdown();
  }
}

Status XClusterTargetManager::Init() {
  DCHECK(!safe_time_service_);
  safe_time_service_ = std::make_unique<XClusterSafeTimeService>(
      &master_, &catalog_manager_, master_.metric_registry());
  RETURN_NOT_OK(safe_time_service_->Init());

  return Status::OK();
}

void XClusterTargetManager::Clear() {
  safe_time_info_.Clear();

  removed_deleted_tables_from_replication_ = false;

  auto_flags_revalidation_needed_ = true;

  {
    std::lock_guard l(replication_error_map_mutex_);
    replication_error_map_.clear();
  }

  {
    std::lock_guard l(table_stream_ids_map_mutex_);
    table_stream_ids_map_.clear();
  }
}

Status XClusterTargetManager::RunLoaders() {
  RETURN_NOT_OK(sys_catalog_.Load<XClusterSafeTimeLoader>("XCluster safe time", safe_time_info_));
  return Status::OK();
}

void XClusterTargetManager::SysCatalogLoaded() {
  // Refresh the Consumer registry.
  auto cluster_config = catalog_manager_.ClusterConfig();
  if (cluster_config) {
    auto l = cluster_config->LockForRead();
    if (l->pb.has_consumer_registry()) {
      auto& producer_map = l->pb.consumer_registry().producer_map();
      for (const auto& [replication_group_id, _] : producer_map) {
        SyncReplicationStatusMap(xcluster::ReplicationGroupId(replication_group_id), producer_map);
      }
    }
  }

  safe_time_service_->ScheduleTaskIfNeeded();
}

void XClusterTargetManager::DumpState(std::ostream& out, bool on_disk_dump) const {
  if (!on_disk_dump) {
    return;
  }

  auto l = safe_time_info_.LockForRead();
  if (l->pb.safe_time_map().empty()) {
    return;
  }
  out << "XCluster Safe Time: " << l->pb.ShortDebugString() << "\n";
}

void XClusterTargetManager::CreateXClusterSafeTimeTableAndStartService() {
  WARN_NOT_OK(
      safe_time_service_->CreateXClusterSafeTimeTableIfNotFound(),
      "Creation of XClusterSafeTime table failed");

  safe_time_service_->ScheduleTaskIfNeeded();
}

Status XClusterTargetManager::GetXClusterSafeTime(
    GetXClusterSafeTimeResponsePB* resp, const LeaderEpoch& epoch) {
  RETURN_NOT_OK_SET_CODE(
      safe_time_service_->GetXClusterSafeTimeInfoFromMap(epoch, resp),
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
  auto l = safe_time_info_.LockForRead();
  SCHECK(
      l->pb.safe_time_map().count(namespace_id), NotFound,
      "XCluster safe time not found for namespace $0", namespace_id);

  return HybridTime(l->pb.safe_time_map().at(namespace_id));
}

Result<XClusterNamespaceToSafeTimeMap> XClusterTargetManager::GetXClusterNamespaceToSafeTimeMap()
    const {
  XClusterNamespaceToSafeTimeMap result;
  auto l = safe_time_info_.LockForRead();

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
  return safe_time_service_->GetXClusterSafeTimeForNamespace(
      epoch.leader_term, namespace_id, filter);
}

Status XClusterTargetManager::RefreshXClusterSafeTimeMap(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(safe_time_service_->ComputeSafeTime(epoch.leader_term));
  return Status::OK();
}

Status XClusterTargetManager::SetXClusterNamespaceToSafeTimeMap(
    const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) {
  auto l = safe_time_info_.LockForWrite();
  auto& safe_time_map_pb = *l.mutable_data()->pb.mutable_safe_time_map();
  safe_time_map_pb.clear();
  for (auto& [namespace_id, hybrid_time] : safe_time_map) {
    safe_time_map_pb[namespace_id] = hybrid_time.ToUint64();
  }

  RETURN_NOT_OK_PREPEND(
      sys_catalog_.Upsert(leader_term, &safe_time_info_),
      "Updating XCluster safe time in sys-catalog");

  l.Commit();

  return Status::OK();
}

Status XClusterTargetManager::FillHeartbeatResponse(
    const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const {
  auto l = safe_time_info_.LockForRead();
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
    const xcluster::ReplicationGroupId& replication_group_id, CoarseTimePoint deadline,
    bool skip_health_check) {
  return Wait(
      [&]() -> Result<bool> {
        auto is_operation_done =
            VERIFY_RESULT(IsSetupUniverseReplicationDone(replication_group_id, skip_health_check));

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
  std::map<xcluster::ReplicationGroupId, std::vector<TableId>> replication_group_producer_tables;
  {
    SharedLock table_stream_l(table_stream_ids_map_mutex_);
    if (table_stream_ids_map_.empty()) {
      return Status::OK();
    }

    auto cluster_config = catalog_manager_.ClusterConfig();
    auto l = cluster_config->LockForRead();
    auto& replication_group_map = l->pb.consumer_registry().producer_map();

    for (auto& table_id : table_ids) {
      if (!table_stream_ids_map_.contains(table_id)) {
        continue;
      }

      for (const auto& [replication_group_id, stream_id] : table_stream_ids_map_.at(table_id)) {
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
  }

  return Status::OK();
}

void XClusterTargetManager::RunBgTasks(const LeaderEpoch& epoch) {
  WARN_NOT_OK(
      RemoveDroppedTablesFromReplication(epoch),
      "Failed to remove dropped tables from consumer replication groups");

  WARN_NOT_OK(RefreshLocalAutoFlagConfig(epoch), "Failed refreshing local AutoFlags config");

  WARN_NOT_OK(
      ProcessPendingSchemaChanges(epoch), "Failed processing xCluster Pending Schema Changes");
}

Status XClusterTargetManager::RemoveDroppedTablesFromReplication(const LeaderEpoch& epoch) {
  if (removed_deleted_tables_from_replication_) {
    return Status::OK();
  }

  std::unordered_set<TabletId> tables_to_remove;
  {
    SharedLock table_stream_l(table_stream_ids_map_mutex_);
    for (const auto& [table_id, _] : table_stream_ids_map_) {
      auto table_info = catalog_manager_.GetTableInfo(table_id);
      if (!table_info || !table_info->LockForRead()->visible_to_client()) {
        tables_to_remove.insert(table_id);
        continue;
      }
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
  RETURN_NOT_OK(GetReplicationStatus(&req, &replication_status));

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
  RETURN_NOT_OK(GetReplicationStatus(&req, &replication_status));

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

void XClusterTargetManager::NotifyAutoFlagsConfigChanged() {
  auto_flags_revalidation_needed_ = true;
}

Status XClusterTargetManager::ReportNewAutoFlagConfigVersion(
    const xcluster::ReplicationGroupId& replication_group_id, uint32 new_version,
    const LeaderEpoch& epoch) {
  auto replication_info = catalog_manager_.GetUniverseReplication(replication_group_id);
  SCHECK(
      replication_info, NotFound, "Missing replication group $0", replication_group_id.ToString());

  auto cluster_config = catalog_manager_.ClusterConfig();

  return RefreshAutoFlagConfigVersion(
      sys_catalog_, *replication_info, *cluster_config.get(), new_version,
      [&master = master_]() { return master.GetAutoFlagsConfig(); }, epoch);
}

Status XClusterTargetManager::RefreshLocalAutoFlagConfig(const LeaderEpoch& epoch) {
  if (!auto_flags_revalidation_needed_) {
    return Status::OK();
  }

  auto se = ScopeExit([this] { auto_flags_revalidation_needed_ = true; });
  auto_flags_revalidation_needed_ = false;

  auto replication_groups = catalog_manager_.GetAllUniverseReplications();
  if (replication_groups.empty()) {
    return Status::OK();
  }

  const auto local_auto_flags_config = master_.GetAutoFlagsConfig();
  auto cluster_config = catalog_manager_.ClusterConfig();

  bool update_failed = false;
  for (const auto& replication_info : replication_groups) {
    auto status = HandleLocalAutoFlagsConfigChange(
        sys_catalog_, *replication_info, *cluster_config.get(), local_auto_flags_config, epoch);
    if (!status.ok()) {
      LOG(WARNING) << "Failed to handle local AutoFlags config change for replication group "
                   << replication_info->id() << ": " << status;
      update_failed = true;
    }
  }

  SCHECK(!update_failed, IllegalState, "Failed to handle local AutoFlags config change");

  se.Cancel();

  return Status::OK();
}

void XClusterTargetManager::SyncReplicationStatusMap(
    const xcluster::ReplicationGroupId& replication_group_id,
    const google::protobuf::Map<std::string, cdc::ProducerEntryPB>& producer_map) {
  std::lock_guard lock(replication_error_map_mutex_);

  if (producer_map.count(replication_group_id.ToString()) == 0) {
    // Replication group has been deleted.
    replication_error_map_.erase(replication_group_id);
    return;
  }

  auto& replication_group_errors = replication_error_map_[replication_group_id];
  auto& producer_entry = producer_map.at(replication_group_id.ToString());

  std::unordered_set<TableId> all_consumer_table_ids;
  for (auto& [_, stream_map] : producer_entry.stream_map()) {
    const auto& consumer_table_id = stream_map.consumer_table_id();

    std::unordered_set<TabletId> all_producer_tablet_ids;
    for (auto& [_, producer_tablet_ids] : stream_map.consumer_producer_tablet_map()) {
      all_producer_tablet_ids.insert(
          producer_tablet_ids.tablets().begin(), producer_tablet_ids.tablets().end());
    }

    if (all_producer_tablet_ids.empty()) {
      continue;
    }
    all_consumer_table_ids.insert(consumer_table_id);

    auto& tablet_error_map = replication_group_errors[consumer_table_id];
    // Remove tablets that are no longer part of replication.
    std::erase_if(tablet_error_map, [&all_producer_tablet_ids](const auto& entry) {
      return !all_producer_tablet_ids.contains(entry.first);
    });

    // Add new tablets.
    for (const auto& producer_tablet_id : all_producer_tablet_ids) {
      if (!tablet_error_map.contains(producer_tablet_id)) {
        // Default to UNINITIALIZED error. Once the Pollers send the status, this will be updated.
        tablet_error_map[producer_tablet_id].error =
            ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED;
      }
    }
  }

  // Remove tables that are no longer part of replication.
  std::erase_if(replication_group_errors, [&all_consumer_table_ids](const auto& entry) {
    return !all_consumer_table_ids.contains(entry.first);
  });

  if (VLOG_IS_ON(1)) {
    LOG(INFO) << "Synced replication_error_map_ for replication group " << replication_group_id;
    for (const auto& [consumer_table_id, tablet_error_map] : replication_group_errors) {
      LOG(INFO) << "Table: " << consumer_table_id;
      for (const auto& [tablet_id, error] : tablet_error_map) {
        LOG(INFO) << "Tablet: " << tablet_id << ", Error: " << ReplicationErrorPb_Name(error.error);
      }
    }
  }
}

void XClusterTargetManager::StoreReplicationStatus(
    const XClusterConsumerReplicationStatusPB& consumer_replication_status) {
  const auto& replication_group_id = consumer_replication_status.replication_group_id();

  std::lock_guard lock(replication_error_map_mutex_);
  // Heartbeats can report stale entries. So we skip anything that is not in
  // replication_error_map_.

  auto* replication_error_map =
      FindOrNull(replication_error_map_, xcluster::ReplicationGroupId(replication_group_id));
  if (!replication_error_map) {
    VLOG_WITH_FUNC(2) << "Skipping deleted replication group " << replication_group_id;
    return;
  }

  for (const auto& table_status : consumer_replication_status.table_status()) {
    const auto& consumer_table_id = table_status.consumer_table_id();
    auto* consumer_table_map = FindOrNull(*replication_error_map, consumer_table_id);
    if (!consumer_table_map) {
      VLOG_WITH_FUNC(2) << "Skipping removed table " << consumer_table_id
                        << " in replication group " << replication_group_id;
      continue;
    }

    for (const auto& stream_tablet_status : table_status.stream_tablet_status()) {
      const auto& producer_tablet_id = stream_tablet_status.producer_tablet_id();
      auto* tablet_status_map = FindOrNull(*consumer_table_map, producer_tablet_id);
      if (!tablet_status_map) {
        VLOG_WITH_FUNC(2) << "Skipping removed tablet " << producer_tablet_id
                          << " in replication group " << replication_group_id;
        continue;
      }

      // Get status from highest term only. When consumer leaders move we may get stale status
      // from older leaders.
      if (tablet_status_map->consumer_term <= stream_tablet_status.consumer_term()) {
        DCHECK_NE(
            stream_tablet_status.error(), ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED);
        tablet_status_map->consumer_term = stream_tablet_status.consumer_term();
        tablet_status_map->error = stream_tablet_status.error();
        VLOG_WITH_FUNC(2) << "Storing error for replication group: " << replication_group_id
                          << ", consumer table: " << consumer_table_id
                          << ", tablet: " << producer_tablet_id
                          << ", term: " << stream_tablet_status.consumer_term()
                          << ", error: " << ReplicationErrorPb_Name(stream_tablet_status.error());
      } else {
        VLOG_WITH_FUNC(2) << "Skipping stale error for  replication group: " << replication_group_id
                          << ", consumer table: " << consumer_table_id
                          << ", tablet: " << producer_tablet_id
                          << ", term: " << stream_tablet_status.consumer_term();
      }
    }
  }
}

Result<bool> XClusterTargetManager::HasReplicationGroupErrors(
    const xcluster::ReplicationGroupId& replication_group_id) const {
  SharedLock l(replication_error_map_mutex_);

  auto* replication_error_map = FindOrNull(replication_error_map_, replication_group_id);
  SCHECK(
      replication_error_map, NotFound, "Could not find replication group $0",
      replication_group_id.ToString());

  std::unordered_map<TableId, std::unordered_set<std::string>> table_errors;
  for (const auto& [consumer_table_id, tablet_error_map] : *replication_error_map) {
    for (const auto& [_, error_info] : tablet_error_map) {
      if (error_info.error != ReplicationErrorPb::REPLICATION_OK) {
        table_errors[consumer_table_id].insert(ReplicationErrorPb_Name(error_info.error));
      }
    }
  }

  if (!table_errors.empty()) {
    YB_LOG_EVERY_N_SECS(WARNING, 60)
        << "Replication group " << replication_group_id
        << " has errors for the following tables:" << AsString(table_errors);
  }
  return !table_errors.empty();
}

Status XClusterTargetManager::PopulateReplicationGroupErrors(
    const xcluster::ReplicationGroupId& replication_group_id,
    GetReplicationStatusResponsePB* resp) const {
  auto* replication_error_map = FindOrNull(replication_error_map_, replication_group_id);
  SCHECK(
      replication_error_map, NotFound, "Could not find replication group $0",
      replication_group_id.ToString());

  SharedLock l(table_stream_ids_map_mutex_);

  for (const auto& [consumer_table_id, tablet_error_map] : *replication_error_map) {
    if (!table_stream_ids_map_.contains(consumer_table_id) ||
        !table_stream_ids_map_.at(consumer_table_id).contains(replication_group_id)) {
      // It is possible to hit this if a table is deleted and we are still processing poller errors.
      YB_LOG_EVERY_N_SECS_OR_VLOG(WARNING, 10, 1)
          << "xcluster_consumer_replication_error_map_ contains consumer table "
          << consumer_table_id << " in replication group " << replication_group_id
          << " but xcluster_consumer_table_stream_ids_map_ does not.";
      continue;
    }

    // Map from error to list of producer tablet IDs/Pollers reporting them.
    std::unordered_map<ReplicationErrorPb, std::vector<TabletId>> errors;
    for (const auto& [tablet_id, error_info] : tablet_error_map) {
      errors[error_info.error].push_back(tablet_id);
    }

    if (errors.empty()) {
      continue;
    }

    auto resp_status = resp->add_statuses();
    resp_status->set_table_id(consumer_table_id);
    const auto& stream_id = table_stream_ids_map_.at(consumer_table_id).at(replication_group_id);
    resp_status->set_stream_id(stream_id.ToString());
    for (const auto& [error_pb, tablet_ids] : errors) {
      if (error_pb == ReplicationErrorPb::REPLICATION_OK) {
        // Do not add errors for healthy tablets.
        continue;
      }

      auto* resp_error = resp_status->add_errors();
      resp_error->set_error(error_pb);
      // Only include the first 20 tablet IDs to limit response size. VLOG(4) will log all tablet to
      // the log.
      resp_error->set_error_detail(
          Format("Producer Tablet IDs: $0", JoinStringsLimitCount(tablet_ids, ",", 20)));
      if (VLOG_IS_ON(4)) {
        VLOG(4) << "Replication error " << ReplicationErrorPb_Name(error_pb)
                << " for ReplicationGroup: " << replication_group_id << ", stream id: " << stream_id
                << ", consumer table: " << consumer_table_id << ", producer tablet IDs:";
        for (const auto& tablet_id : tablet_ids) {
          VLOG(4) << tablet_id;
        }
      }
    }
  }

  return Status::OK();
}

Status XClusterTargetManager::GetReplicationStatus(
    const GetReplicationStatusRequestPB* req, GetReplicationStatusResponsePB* resp) const {
  SharedLock l(replication_error_map_mutex_);

  // If the 'replication_group_id' is given, only populate the status for the streams in that
  // ReplicationGroup. Otherwise, populate all the status for all groups.
  if (!req->replication_group_id().empty()) {
    return PopulateReplicationGroupErrors(
        xcluster::ReplicationGroupId(req->replication_group_id()), resp);
  }

  for (const auto& [replication_id, _] : replication_error_map_) {
    RETURN_NOT_OK(PopulateReplicationGroupErrors(replication_id, resp));
  }

  return Status::OK();
}

void XClusterTargetManager::RecordTableStream(
    const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id) {
  std::lock_guard lock(table_stream_ids_map_mutex_);
  table_stream_ids_map_[table_id].emplace(replication_group_id, stream_id);
}

void XClusterTargetManager::RemoveTableStream(
    const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id) {
  RemoveTableStreams(replication_group_id, {table_id});
}

void XClusterTargetManager::RemoveTableStreams(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::set<TableId>& tables_to_clear) {
  std::lock_guard lock(table_stream_ids_map_mutex_);
  for (const auto& table_id : tables_to_clear) {
    if (table_stream_ids_map_[table_id].erase(replication_group_id) < 1) {
      LOG(WARNING) << "Failed to remove consumer table from mapping. " << "table_id: " << table_id
                   << ": replication_group_id: " << replication_group_id;
    }
    if (table_stream_ids_map_[table_id].empty()) {
      table_stream_ids_map_.erase(table_id);
    }
  }
}

std::unordered_map<xcluster::ReplicationGroupId, xrepl::StreamId>
XClusterTargetManager::GetStreamIdsForTable(const TableId& table_id) const {
  SharedLock lock(table_stream_ids_map_mutex_);
  auto* stream_ids = FindOrNull(table_stream_ids_map_, table_id);
  if (!stream_ids) {
    return {};
  }

  return *stream_ids;
}

bool XClusterTargetManager::IsTableReplicated(const TableId& table_id) const {
  return !GetStreamIdsForTable(table_id).empty();
}

Result<TableId> XClusterTargetManager::GetTableIdForStreamId(
    const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id) const {
  SharedLock l(table_stream_ids_map_mutex_);

  auto iter = std::find_if(
      table_stream_ids_map_.begin(), table_stream_ids_map_.end(),
      [&replication_group_id, &stream_id](auto& id_map) {
        return ContainsKeyValuePair(id_map.second, replication_group_id, stream_id);
      });
  SCHECK(
      iter != table_stream_ids_map_.end(), NotFound,
      Format("Unable to find the stream id $0", stream_id));

  return iter->first;
}

Status XClusterTargetManager::HandleTabletSplit(
    const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids,
    const LeaderEpoch& epoch) {
  // Check if this table is consuming a stream.
  auto stream_ids = GetStreamIdsForTable(consumer_table_id);
  if (stream_ids.empty()) {
    return Status::OK();
  }

  auto consumer_tablet_keys = VERIFY_RESULT(catalog_manager_.GetTableKeyRanges(consumer_table_id));
  auto cluster_config = catalog_manager_.ClusterConfig();
  auto l = cluster_config->LockForWrite();
  for (const auto& [replication_group_id, stream_id] : stream_ids) {
    // Fetch the stream entry so we can update the mappings.
    auto replication_group_map =
        l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto producer_entry = FindOrNull(*replication_group_map, replication_group_id.ToString());
    // If we can't find the entries, then the stream has been deleted.
    if (!producer_entry) {
      LOG(WARNING) << "Unable to find the producer entry for universe " << replication_group_id;
      continue;
    }
    auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id.ToString());
    if (!stream_entry) {
      LOG(WARNING) << "Unable to find the producer entry for universe " << replication_group_id
                   << ", stream " << stream_id;
      continue;
    }
    DCHECK(stream_entry->consumer_table_id() == consumer_table_id);

    RETURN_NOT_OK(
        UpdateTabletMappingOnConsumerSplit(consumer_tablet_keys, split_tablet_ids, stream_entry));
  }

  // Also bump the cluster_config_ version so that changes are propagated to tservers.
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

  RETURN_NOT_OK_PREPEND(
      sys_catalog_.Upsert(epoch, cluster_config.get()),
      "Failed updating cluster config in sys-catalog");
  l.Commit();

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status XClusterTargetManager::ValidateNewSchema(
    const TableInfo& table_info, const Schema& consumer_schema) const {
  if (!FLAGS_xcluster_wait_on_ddl_alter) {
    return Status::OK();
  }

  // Check if this table is consuming a stream.
  auto stream_ids = GetStreamIdsForTable(table_info.id());
  if (stream_ids.empty()) {
    return Status::OK();
  }

  auto cluster_config = catalog_manager_.ClusterConfig();
  auto l = cluster_config->LockForRead();
  for (const auto& [replication_group_id, stream_id] : stream_ids) {
    // Fetch the stream entry to get Schema information.
    auto& replication_group_map = l.data().pb.consumer_registry().producer_map();
    auto producer_entry = FindOrNull(replication_group_map, replication_group_id.ToString());
    SCHECK(producer_entry, NotFound, Format("Missing universe $0", replication_group_id));
    auto stream_entry = FindOrNull(producer_entry->stream_map(), stream_id.ToString());
    SCHECK(stream_entry, NotFound, Format("Missing stream $0:$1", replication_group_id, stream_id));

    // If we are halted on a Schema update as a Consumer...
    auto& producer_schema_pb = stream_entry->producer_schema();
    if (producer_schema_pb.has_pending_schema()) {
      // Compare our new schema to the Producer's pending schema.
      Schema producer_schema;
      RETURN_NOT_OK(SchemaFromPB(producer_schema_pb.pending_schema(), &producer_schema));

      // This new schema should allow us to consume data for the Producer's next schema.
      // If we instead diverge, we will be unable to consume any more of the Producer's data.
      bool can_apply = consumer_schema.EquivalentForDataCopy(producer_schema);
      SCHECK(
          can_apply, IllegalState,
          Format(
              "New Schema not compatible with XCluster Producer Schema:\n new={$0}\n producer={$1}",
              consumer_schema.ToString(), producer_schema.ToString()));
    }
  }

  return Status::OK();
}

Status XClusterTargetManager::ResumeStreamsAfterNewSchema(
    const TableInfo& table_info, SchemaVersion consumer_schema_version, const LeaderEpoch& epoch) {
  if (!FLAGS_xcluster_wait_on_ddl_alter) {
    return Status::OK();
  }

  // With Replication Enabled, verify that we've finished applying the New Schema.
  // If we're waiting for a Schema because we saw the a replication source with a change,
  // resume replication now that the alter is complete.

  auto stream_ids = GetStreamIdsForTable(table_info.id());
  if (stream_ids.empty()) {
    return Status::OK();
  }

  bool found_schema = false, resuming_replication = false;

  // Now that we've applied the new schema: find pending replication, clear state, resume.
  auto cluster_config = catalog_manager_.ClusterConfig();
  auto l = cluster_config->LockForWrite();
  for (const auto& [replication_group_id, stream_id] : stream_ids) {
    // Fetch the stream entry to get Schema information.
    auto replication_group_map =
        l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto producer_entry = FindOrNull(*replication_group_map, replication_group_id.ToString());
    if (!producer_entry) {
      continue;
    }
    auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id.ToString());
    if (!stream_entry) {
      continue;
    }

    auto producer_schema_pb = stream_entry->mutable_producer_schema();
    if (producer_schema_pb->has_pending_schema()) {
      found_schema = true;
      Schema consumer_schema, producer_schema;
      RETURN_NOT_OK(table_info.GetSchema(&consumer_schema));
      RETURN_NOT_OK(SchemaFromPB(producer_schema_pb->pending_schema(), &producer_schema));
      if (consumer_schema.EquivalentForDataCopy(producer_schema)) {
        resuming_replication = true;
        auto pending_version = producer_schema_pb->pending_schema_version();
        LOG(INFO) << "Consumer schema @ version " << consumer_schema_version
                  << " is now data copy compatible with Producer: " << stream_id
                  << " @ schema version " << pending_version;
        // Clear meta we use to track progress on receiving all WAL entries with old schema.
        producer_schema_pb->set_validated_schema_version(
            std::max(producer_schema_pb->validated_schema_version(), pending_version));
        producer_schema_pb->set_last_compatible_consumer_schema_version(consumer_schema_version);
        producer_schema_pb->clear_pending_schema();
        // Bump the ClusterConfig version so we'll broadcast new schema version & resume operation.
        l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
      } else {
        LOG(INFO) << "Consumer schema not compatible for data copy of next Producer schema.";
      }
    }
  }

  if (resuming_replication) {
    RETURN_NOT_OK_PREPEND(
        sys_catalog_.Upsert(epoch, cluster_config.get()), "Failed updating cluster config");
    l.Commit();
    LOG(INFO) << "Resuming Replication on " << table_info.id() << " after Consumer ALTER.";
  } else if (!found_schema) {
    LOG(INFO) << "No pending schema change from Producer.";
  }

  return Status::OK();
}

Status XClusterTargetManager::ProcessPendingSchemaChanges(const LeaderEpoch& epoch) {
  if (!GetAtomicFlag(&FLAGS_xcluster_wait_on_ddl_alter)) {
    // See if any Streams are waiting on a pending_schema.
    bool found_pending_schema = false;
    auto cluster_config = catalog_manager_.ClusterConfig();
    auto cl = cluster_config->LockForWrite();
    auto replication_group_map =
        cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    // For each user entry.
    for (auto& replication_group_id_and_entry : *replication_group_map) {
      // For each CDC stream in that Universe.
      for (auto& stream_id_and_entry :
           *replication_group_id_and_entry.second.mutable_stream_map()) {
        auto& stream_entry = stream_id_and_entry.second;
        if (stream_entry.has_producer_schema() &&
            stream_entry.producer_schema().has_pending_schema()) {
          // Force resume this stream.
          auto schema = stream_entry.mutable_producer_schema();
          schema->set_validated_schema_version(
              std::max(schema->validated_schema_version(), schema->pending_schema_version()));
          schema->clear_pending_schema();

          found_pending_schema = true;
          LOG(INFO) << "Force Resume Consumer schema: " << stream_id_and_entry.first
                    << " @ schema version " << schema->pending_schema_version();
        }
      }
    }

    if (found_pending_schema) {
      // Bump the ClusterConfig version so we'll broadcast new schema version & resume operation.
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK_PREPEND(
          sys_catalog_.Upsert(epoch.leader_term, cluster_config.get()),
          "Failed updating cluster config");
      cl.Commit();
    }
  }

  return Status::OK();
}

Status XClusterTargetManager::SetupUniverseReplication(
    const SetupUniverseReplicationRequestPB* req, SetupUniverseReplicationResponsePB* resp,
    const LeaderEpoch& epoch) {
  // We should set the universe uuid even if we fail with AlreadyPresent error.
  {
    auto universe_uuid = catalog_manager_.GetUniverseUuidIfExists();
    if (universe_uuid) {
      resp->set_universe_uuid(universe_uuid->ToString());
    }
  }

  auto setup_replication_task =
      VERIFY_RESULT(CreateSetupUniverseReplicationTask(master_, catalog_manager_, req, epoch));

  {
    std::lock_guard l(replication_setup_tasks_mutex_);
    SCHECK(
        !replication_setup_tasks_.contains(setup_replication_task->Id()), AlreadyPresent,
        "Setup already running for xCluster ReplicationGroup $0", setup_replication_task->Id());
    replication_setup_tasks_[setup_replication_task->Id()] = setup_replication_task;
  }

  setup_replication_task->StartSetup();

  return Status::OK();
}

Result<IsOperationDoneResult> XClusterTargetManager::IsSetupUniverseReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id, bool skip_health_check) {
  std::shared_ptr<XClusterInboundReplicationGroupSetupTaskIf> setup_replication_task;
  {
    SharedLock l(replication_setup_tasks_mutex_);
    setup_replication_task = FindPtrOrNull(replication_setup_tasks_, replication_group_id);
  }

  if (setup_replication_task) {
    auto is_done = setup_replication_task->DoneResult();
    if (!is_done.done()) {
      return is_done;
    }

    {
      std::lock_guard l(replication_setup_tasks_mutex_);
      // Forget about the task now that we've responded to the user.
      replication_setup_tasks_.erase(replication_group_id);
    }

    if (!is_done.status().ok()) {
      // Setup failed.
      return is_done;
    }
  }

  if (skip_health_check) {
    return IsOperationDoneResult::Done();
  }

  // Setup completed successfully. Wait for the ReplicationGroup be become healthy.
  return master::IsSetupUniverseReplicationDone(replication_group_id, catalog_manager_);
}

Status XClusterTargetManager::SetupNamespaceReplicationWithBootstrap(
    const SetupNamespaceReplicationWithBootstrapRequestPB* req,
    SetupNamespaceReplicationWithBootstrapResponsePB* resp, const LeaderEpoch& epoch) {
  return SetupUniverseReplicationWithBootstrapHelper::SetupWithBootstrap(
      master_, catalog_manager_, req, resp, epoch);
}

Result<IsSetupNamespaceReplicationWithBootstrapDoneResponsePB>
XClusterTargetManager::IsSetupNamespaceReplicationWithBootstrapDone(
    const xcluster::ReplicationGroupId& replication_group_id) {
  auto bootstrap_info = catalog_manager_.GetUniverseReplicationBootstrap(replication_group_id);
  SCHECK(
      bootstrap_info != nullptr, NotFound,
      Format("Could not find universe replication bootstrap $0", replication_group_id));

  IsSetupNamespaceReplicationWithBootstrapDoneResponsePB resp;

  // Terminal states are DONE or some failure state.
  auto l = bootstrap_info->LockForRead();
  resp.set_state(l->state());

  if (l->is_done()) {
    resp.set_done(true);
    StatusToPB(Status::OK(), resp.mutable_bootstrap_error());
  } else if (l->is_deleted_or_failed()) {
    resp.set_done(true);

    if (!bootstrap_info->GetReplicationBootstrapErrorStatus().ok()) {
      StatusToPB(
          bootstrap_info->GetReplicationBootstrapErrorStatus(), resp.mutable_bootstrap_error());
    } else {
      LOG(WARNING) << "Did not find setup universe replication bootstrap error status.";
      StatusToPB(STATUS(InternalError, "unknown error"), resp.mutable_bootstrap_error());
    }

    // Add failed bootstrap to GC now that we've responded to the user.
    catalog_manager_.MarkReplicationBootstrapForCleanup(bootstrap_info->ReplicationGroupId());
  } else {
    // Not done yet.
    resp.set_done(false);
  }

  return resp;
}

Status XClusterTargetManager::AlterUniverseReplication(
    const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp,
    const LeaderEpoch& epoch) {
  return AlterUniverseReplicationHelper::Alter(master_, catalog_manager_, req, resp, epoch);
}

Status XClusterTargetManager::DeleteUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id, bool ignore_errors,
    bool skip_producer_stream_deletion, DeleteUniverseReplicationResponsePB* resp,
    const LeaderEpoch& epoch) {
  {
    // If a setup is in progress, then cancel it.
    std::lock_guard l(replication_setup_tasks_mutex_);
    auto setup_helper = FindPtrOrNull(replication_setup_tasks_, replication_group_id);
    if (setup_helper) {
      replication_setup_tasks_.erase(replication_group_id);

      if (setup_helper->TryCancel()) {
        // Either we successfully cancelled the Setup or it already failed. There wont be any
        // UniverseReplication to delete, so we succeeded.
        return Status::OK();
      }

      // Setup already completed successfully, but IsSetupUniverseReplicationDone hasn't been
      // called yet. Proceed with the delete.
    }
  }

  auto ri = catalog_manager_.GetUniverseReplication(replication_group_id);
  SCHECK(ri != nullptr, NotFound, "Universe replication $0 does not exist", replication_group_id);

  RETURN_NOT_OK(master::DeleteUniverseReplication(
      *ri, ignore_errors, skip_producer_stream_deletion, resp, catalog_manager_, epoch));

  // Run the safe time task as it may need to perform cleanups of it own
  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

}  // namespace yb::master
