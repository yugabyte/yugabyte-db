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

#include <algorithm>

#include "yb/client/client.h"
#include "yb/client/xcluster_client.h"

#include "yb/common/colocated_util.h"
#include "yb/common/constants.h"
#include "yb/common/xcluster_util.h"

#include "yb/gutil/algorithm.h"
#include "yb/gutil/strings/util.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master.h"
#include "yb/master/xcluster_consumer_registry_service.h"
#include "yb/master/xcluster/add_index_to_bidirectional_xcluster_target_task.h"
#include "yb/master/xcluster/add_table_to_xcluster_target_task.h"
#include "yb/master/xcluster/handle_new_schema_for_automatic_xcluster_target_task.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_bootstrap_helper.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/master/xcluster/xcluster_status.h"
#include "yb/master/xcluster/xcluster_universe_replication_alter_helper.h"
#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/status.h"

using namespace std::placeholders;

DEPRECATE_FLAG(bool, xcluster_wait_on_ddl_alter, "11_2024");

DEFINE_RUNTIME_uint32(add_new_index_to_bidirectional_xcluster_timeout_secs, 10 * 60,
    "Time in seconds within which index must be created on other universe when the indexed table "
    "is part of bidirectional xCluster replication. Applies only when "
    "--ysql_auto_add_new_index_to_bidirectional_xcluster is set.");

DECLARE_bool(ysql_auto_add_new_index_to_bidirectional_xcluster);
DECLARE_uint32(ysql_oid_cache_prefetch_size);
DECLARE_uint64(max_clock_skew_usec);

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
      safe_time_service_->ComputeAndGetXClusterSafeTimeInfo(epoch, *resp),
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

XClusterNamespaceToSafeTimeMap XClusterTargetManager::GetXClusterNamespaceToSafeTimeMap() const {
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

  RETURN_NOT_OK(safe_time_service_->ComputeSafeTime(epoch.leader_term));
  auto safe_time_ht =
      VERIFY_RESULT(GetXClusterSafeTimeForNamespace(req->namespace_id(), req->filter()));
  resp->set_safe_time_ht(safe_time_ht.ToUint64());
  return Status::OK();
}

Result<HybridTime> XClusterTargetManager::GetXClusterSafeTimeForNamespace(
    const NamespaceId& namespace_id, const XClusterSafeTimeFilter& filter) const {
  return safe_time_service_->GetXClusterSafeTimeForNamespace(namespace_id, filter);
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
}

Status XClusterTargetManager::RemoveDroppedTablesFromReplication(const LeaderEpoch& epoch) {
  if (removed_deleted_tables_from_replication_) {
    return Status::OK();
  }

  std::unordered_set<TabletId> tables_to_remove;
  {
    SharedLock table_stream_l(table_stream_ids_map_mutex_);
    for (const auto& [table_id, _] : table_stream_ids_map_) {
      auto stripped_table_id = xcluster::StripSequencesDataAliasIfPresent(table_id);
      auto table_info = catalog_manager_.GetTableInfo(stripped_table_id);
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

  const auto namespace_safe_time = GetXClusterNamespaceToSafeTimeMap();

  for (const auto& replication_info : replication_infos) {
    auto replication_group_status = VERIFY_RESULT(
        GetUniverseReplicationInfo(replication_info, cluster_config_pb, namespace_safe_time));

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
    const SysClusterConfigEntryPB& cluster_config_pb,
    const XClusterNamespaceToSafeTimeMap& namespace_safe_time) const {
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
    result.automatic_ddl_mode = replication_info_pb.db_scoped_info().automatic_ddl_mode();

    result.db_scoped_info += Format(
        "ddl_mode: $0",
        replication_info_pb.db_scoped_info().automatic_ddl_mode() ? "automatic" : "semi-automatic");
    for (const auto& namespace_info : replication_info_pb.db_scoped_info().namespace_infos()) {
      result.db_scope_namespace_id_map[namespace_info.consumer_namespace_id()] =
          namespace_info.producer_namespace_id();

      result.db_scoped_info += Format(
          "\n  namespace: $0\n    consumer_namespace_id: $1\n    producer_namespace_id: $2",
          catalog_manager_.GetNamespaceName(namespace_info.consumer_namespace_id()),
          namespace_info.consumer_namespace_id(), namespace_info.producer_namespace_id());

      auto safe_time_info = FindOrNull(namespace_safe_time, namespace_info.consumer_namespace_id());
      if (safe_time_info) {
        result.db_scoped_info += Format(
            "\n    safe_time: $0",
            Timestamp(safe_time_info->GetPhysicalValueMicros()).ToHumanReadableTime());
      }
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

          const auto& unstripped_target_table_id = table_status.target_table_id;
          const auto stripped_target_table_id =
              xcluster::StripSequencesDataAliasIfPresent(unstripped_target_table_id);
          auto table_info_res = catalog_manager_.GetTableById(stripped_target_table_id);
          if (table_info_res) {
            const auto& table_info = table_info_res.get();
            namespace_name = table_info->namespace_name();
            table_status.full_table_name = GetFullTableName(*table_info);
          }
          if (xcluster::IsSequencesDataAlias(unstripped_target_table_id)) {
            auto namespace_id = VERIFY_RESULT(
                xcluster::GetReplicationNamespaceBelongsTo(unstripped_target_table_id));
            namespace_name = catalog_manager_.GetNamespaceName(namespace_id);
            RSTATUS_DCHECK(
                !namespace_name.empty(), NotFound,
                "Namespace ID $0 from sequences_data alias not found", namespace_id);
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

  const auto safe_time_map = GetXClusterNamespaceToSafeTimeMap();
  if (!safe_time_map.empty()) {
    jw.String("safe_time_map");
    jw.StartArray();
    for (const auto& [namespace_id, safe_time] : safe_time_map) {
      jw.StartObject();
      jw.String("consumer_namespace_id");
      jw.String(namespace_id);

      jw.String("safe_time");
      jw.String(safe_time.ToString());

      jw.EndObject();
    }
    jw.EndArray();
  }

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

  static const XClusterNamespaceToSafeTimeMap empty_namespace_safe_time;

  const auto cluster_config = VERIFY_RESULT(catalog_manager_.GetClusterConfig());
  // Make pb copy to avoid potential deadlock while calling GetUniverseReplicationInfo.
  auto pb = replication_info->LockForRead()->pb;
  return GetUniverseReplicationInfo(pb, cluster_config, empty_namespace_safe_time);
}

Status XClusterTargetManager::ClearXClusterFieldsAfterYsqlDDL(
    TableInfoPtr table_info, SysTablesEntryPB& table_pb, const LeaderEpoch& epoch) {
  if (!table_pb.has_xcluster_table_info()) {
    return Status::OK();
  }

  // We check for xcluster_table_info to determine if we need to do this cleanup, so clean up other
  // fields first.
  if (table_info->IsSecondaryTable()) {
    bool found = false;
    for (const auto& universe : catalog_manager_.GetAllUniverseReplications()) {
      if (HasNamespace(*universe, table_info->namespace_id())) {
        RETURN_NOT_OK(CleanupColocatedTableHistoricalSchemaPackings(
            *universe, table_info->namespace_id(), table_info->GetColocationId(), catalog_manager_,
            epoch));
        found = true;
        break;  // Should only be one automatic mode universe with the namespace.
      }
    }
    LOG_IF(INFO, !found) << "No XCluster replication info found for colocated table "
                         << table_info->id() << " (colocation id " << table_info->GetColocationId()
                         << ") in namespace " << table_info->namespace_id();
  }

  // Clear xcluster_table_info if present.  Exception: leave just xcluster_backfill_hybrid_time if
  // present: we will clear that when the backfill succeeds.  (We need it to start the backfill,
  // which begins after this DDL finishes.)
  if (table_pb.has_xcluster_table_info()) {
    auto* xcluster_table_info = table_pb.mutable_xcluster_table_info();
    if (table_info->is_index() && xcluster_table_info->has_xcluster_backfill_hybrid_time()) {
      xcluster_table_info->clear_xcluster_source_table_id();
      xcluster_table_info->clear_xcluster_colocated_old_schema_packings();
    } else {
      table_pb.clear_xcluster_table_info();
    }
  }

  return Status::OK();
}

bool XClusterTargetManager::IsNamespaceInAutomaticDDLMode(const NamespaceId& namespace_id) const {
  auto all_universe_replications = catalog_manager_.GetAllUniverseReplications();
  if (all_universe_replications.empty()) {
    return false;
  }

  bool namespace_found = false;
  for (const auto& replication_info : all_universe_replications) {
    if (!HasNamespace(*replication_info, namespace_id)) {
      continue;
    }
    if (!replication_info->IsAutomaticDdlMode()) {
      return false;
    }
    namespace_found = true;
  }

  // Return true only if the namespace was found in repl groups and all in automatic DDL mode.
  return namespace_found;
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
  if (auto_flags_revalidation_needed_) {
    RETURN_NOT_OK(DoRefreshLocalAutoFlagConfig(epoch));
    auto_flags_revalidation_needed_ = false;
  }
  return Status::OK();
}

Status XClusterTargetManager::DoRefreshLocalAutoFlagConfig(const LeaderEpoch& epoch) {
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

Result<bool> XClusterTargetManager::IsReplicationGroupFullyPaused(
    const xcluster::ReplicationGroupId& replication_group_id) const {
  SharedLock l(replication_error_map_mutex_);

  auto* replication_error_map = FindOrNull(replication_error_map_, replication_group_id);
  SCHECK(
      replication_error_map, NotFound, "Could not find replication group $0",
      replication_group_id.ToString());

  for (const auto& [consumer_table_id, tablet_error_map] : *replication_error_map) {
    for (const auto& [_, error_info] : tablet_error_map) {
      if (error_info.error != ReplicationErrorPb::REPLICATION_PAUSED) {
        YB_LOG_EVERY_N_SECS(WARNING, 60) << "Replication group " << replication_group_id
                                         << " waiting for table to pause:" << consumer_table_id;
        return false;
      }
    }
  }

  return true;
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

      if (error_pb == ReplicationErrorPb::REPLICATION_PAUSED) {
        resp_error->set_error_detail("Replication paused");
      } else {
        // Only include the first 20 tablet IDs to limit response size.
        // VLOG(4) will write all tablet to the log.
        resp_error->set_error_detail(
            Format("Producer Tablet IDs: $0", JoinStringsLimitCount(tablet_ids, ",", 20)));
      }

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

  auto iter = std::ranges::find_if(
      table_stream_ids_map_, [&replication_group_id, &stream_id](auto& id_map) {
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

  auto locked_config_and_ranges =
      VERIFY_RESULT(LockClusterConfigAndGetTableKeyRanges(catalog_manager_, consumer_table_id));
  auto& cluster_config = locked_config_and_ranges.cluster_config;
  auto& l = locked_config_and_ranges.write_lock;
  auto& consumer_tablet_keys = locked_config_and_ranges.table_key_ranges;

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

  SCHECK_PB_FIELDS_NOT_EMPTY(
      *req, replication_group_id, producer_master_addresses, producer_table_ids);

  // Construct data struct.
  XClusterSetupUniverseReplicationData data;
  data.replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());
  data.source_masters.CopyFrom(req->producer_master_addresses());
  data.transactional = req->transactional();
  data.automatic_ddl_mode = req->automatic_ddl_mode();

  for (const auto& bootstrap_id : req->producer_bootstrap_ids()) {
    data.stream_ids.push_back(VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_id)));
  }

  for (const auto& source_ns_id : req->producer_namespaces()) {
    SCHECK(!source_ns_id.id().empty(), InvalidArgument, "Invalid Namespace Id");
    SCHECK(!source_ns_id.name().empty(), InvalidArgument, "Invalid Namespace name");
    SCHECK_EQ(
        source_ns_id.database_type(), YQLDatabase::YQL_DATABASE_PGSQL, InvalidArgument,
        "Invalid Namespace database_type");

    data.source_namespace_ids.push_back(source_ns_id.id());

    NamespaceIdentifierPB target_ns_id;
    target_ns_id.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    target_ns_id.set_name(source_ns_id.name());
    auto ns_info = VERIFY_RESULT(catalog_manager_.FindNamespace(target_ns_id));
    data.target_namespace_ids.push_back(ns_info->id());
  }

  data.source_table_ids.assign(req->producer_table_ids().begin(), req->producer_table_ids().end());

  return SetupUniverseReplication(std::move(data), epoch);
}

Status XClusterTargetManager::SetupUniverseReplication(
    XClusterSetupUniverseReplicationData&& data, const LeaderEpoch& epoch) {
  auto setup_replication_task = VERIFY_RESULT(
      CreateSetupUniverseReplicationTask(master_, catalog_manager_, std::move(data), epoch));

  {
    std::lock_guard l(replication_setup_tasks_mutex_);
    if (setup_replication_task->IsAlterReplication()) {
      // For alter replication groups, we can retry so return TryAgain.
      SCHECK_FORMAT(
          !replication_setup_tasks_.contains(setup_replication_task->Id()), TryAgain,
          "Alter replication group already running for xCluster ReplicationGroup $0",
          setup_replication_task->Id());
    } else {
      SCHECK_FORMAT(
          !replication_setup_tasks_.contains(setup_replication_task->Id()), AlreadyPresent,
          "Setup already running for xCluster ReplicationGroup $0", setup_replication_task->Id());
    }
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
    const LeaderEpoch& epoch,
    std::unordered_map<NamespaceId, uint32_t> source_namespace_id_to_oid_to_bump_above) {
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

  {
    auto l = ri->LockForRead();
    auto& pb = l->pb;
    if (pb.has_db_scoped_info()) {
      std::unordered_map<NamespaceId, NamespaceId> source_to_target;
      for (const auto& namespace_infos : pb.db_scoped_info().namespace_infos()) {
        source_to_target[namespace_infos.producer_namespace_id()] =
            namespace_infos.consumer_namespace_id();
      }
      auto* yb_client = master_.client_future().get();
      SCHECK(yb_client, IllegalState, "Client not initialized or shutting down");
      for (const auto& [source_namespace_id, oid_to_bump] :
           source_namespace_id_to_oid_to_bump_above) {
        NamespaceId target_namespace_id = source_to_target[source_namespace_id];
        SCHECK(
            !source_to_target.empty(), InvalidArgument,
            "DeleteUniverseReplication called with a namespace ID $0 that is not present in the "
            "replication group ($1)",
            source_namespace_id, replication_group_id);
        // Ensure next OID allocated from normal space will be above oid_to_bump.
        uint32_t begin_oid;
        uint32_t end_oid;
        RETURN_NOT_OK(yb_client->ReservePgsqlOids(
            target_namespace_id, oid_to_bump, /*count=*/1, /*use_secondary_space=*/false,
            &begin_oid, &end_oid));
      }
      RETURN_NOT_OK(master_.catalog_manager()->InvalidateTserverOidCaches());
    }
  }

  RETURN_NOT_OK(master::DeleteUniverseReplication(
      *ri, ignore_errors, skip_producer_stream_deletion, resp, catalog_manager_, epoch));

  // Run the safe time task as it may need to perform cleanups of it own
  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status XClusterTargetManager::AddTableToReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& source_table_id,
    const xrepl::StreamId& bootstrap_id, const std::optional<TableId>& target_table_id,
    const LeaderEpoch& epoch) {
  AlterUniverseReplicationHelper::AddTablesToReplicationData data;
  data.replication_group_id = replication_group_id;
  data.source_table_ids_to_add.push_back(source_table_id);
  data.source_bootstrap_ids_to_add.push_back(bootstrap_id);
  data.target_table_id = target_table_id;

  return AlterUniverseReplicationHelper::AddTablesToReplication(
      master_, catalog_manager_, data, epoch);
}

Result<std::optional<HybridTime>> XClusterTargetManager::TryGetXClusterSafeTimeForBackfill(
    const std::vector<TableId>& index_table_ids, const TableInfoPtr& indexed_table,
    const LeaderEpoch& epoch) const {
  auto& xcluster_manager = *master_.xcluster_manager();

  SCHECK_FORMAT(
      indexed_table->IsBackfilling(), IllegalState, "$0 is not backfilling", indexed_table->id());

  const bool is_colocated = indexed_table->colocated();
  auto indexed_table_id = indexed_table->id();

  if (master_.xcluster_manager()->ShouldAutoAddIndexesToBiDirectionalXCluster(*indexed_table)) {
    if (is_colocated) {
      indexed_table_id = indexed_table->LockForRead()->pb.parent_table_id();
    }
    auto backfill_ht = VERIFY_RESULT_PREPEND(
        PrepareAndGetBackfillTimeForBiDirectionalIndex(index_table_ids, indexed_table_id, epoch),
        "Failed while preparing index for xCluster");

    LOG(INFO) << "Using " << backfill_ht << " as the backfill read time";
    return backfill_ht;
  }

  if (IsNamespaceInAutomaticDDLMode(indexed_table->namespace_id())) {
    // For automatic DDL replication, use the backfill time provided by the ddl_queue handler.
    //
    // Here we need to use the safe time that the ddl_queue handler is going to update the safe
    // time to. This is because we cannot wait for xCluster safe time to reach now, as the
    // ddl_queue table is waiting for this index to complete.

    // Check that all indexes have the same xCluster backfill hybrid time or none at all.
    HybridTime xcluster_backfill_hybrid_time;
    for (const auto& index_table_id : index_table_ids) {
      auto index_table_info = VERIFY_RESULT(catalog_manager_.GetTableById(index_table_id));
      const auto& xcluster_table_info = index_table_info->LockForRead()->pb.xcluster_table_info();
      RSTATUS_DCHECK(
          xcluster_table_info.has_xcluster_backfill_hybrid_time(), IllegalState,
          "Index missing xcluster_backfill_hybrid_time");
      HybridTime ht;
      RETURN_NOT_OK(ht.FromUint64(xcluster_table_info.xcluster_backfill_hybrid_time()));
      if (!ht.is_special()) {
        SCHECK(
            !xcluster_backfill_hybrid_time || ht != xcluster_backfill_hybrid_time, InvalidArgument,
            "Indexes have different xCluster backfill hybrid times");
        xcluster_backfill_hybrid_time = ht;
      }
    }

      if (xcluster_backfill_hybrid_time) {
        LOG(INFO) << "Using provided xcluster_backfill_hybrid_time "
                  << xcluster_backfill_hybrid_time << " as the backfill read time";
        return xcluster_backfill_hybrid_time;
      }

      // Possible to get here for manually created indexes.  Fallback to DB-scoped flow.
      // (We allow this because we want to be able to create an index manually via backdoors that
      // don't have this time set.)
      LOG(WARNING) << "No xCluster backfill hybrid time set for indexes in automatic mode, "
                   << "falling back to non-automatic mode flow.";
  }

  if (is_colocated) {
    // Colocated indexes in transactional xCluster will use the regular tablet safe time.  Only the
    // parent table is part of the xCluster replication, so new data that is added to the index on
    // the source universe automatically flows to the target universe even before the index is
    // created on it.  We still need to run backfill since the WAL entries for the backfill are NOT
    // replicated via xCluster.  This is because both backfill entries and xCluster replicated
    // entries use the same external HT field.  To ensure transactional correctness we just need to
    // pick a time higher than the time that was picked on the source side.  Since the table is
    // created on the source universe before the target this is always guaranteed to be true.
    return std::nullopt;
  }

  if (xcluster_manager.IsTableReplicationConsumer(indexed_table_id)) {
    auto safe_time_result = GetXClusterSafeTime(indexed_table->namespace_id());
    if (safe_time_result.ok()) {
      SCHECK(
          !safe_time_result->is_special(), InvalidArgument,
          "Invalid xCluster safe time for namespace ", indexed_table->namespace_id());

      LOG(INFO) << "Using xCluster safe time " << *safe_time_result << " as the backfill read time";
      return *safe_time_result;
    }

    if (safe_time_result.status().IsNotFound()) {
      VLOG(1) << "Table " << indexed_table->id()
              << "does not belong to transactional replication, continue with "
                 "GetSafeTimeForTablet";
      return std::nullopt;
    }

    return safe_time_result.status();
  }

  return std::nullopt;
}

Result<HybridTime> XClusterTargetManager::PrepareAndGetBackfillTimeForBiDirectionalIndex(
    const std::vector<TableId>& index_table_ids, const TableId& indexed_table_id,
    const LeaderEpoch& epoch) const {
  // Online index creation in Yugabyte is based on the F1 paper, and has 4 stages: Delete Only,
  // Insert and Delete Only, Backfill, Read, Insert and delete. Consistency is guaranteed as long as
  // no two nodes in the system are more than 2 stages apart. Within a single universe the
  // CatalogVersion is bumped to enforce this.
  //
  // With bi-directional xCluster writes happen on both universes, each with its own CatalogVersion.
  // The regular Create Index flow does not have a way to synchronize the stages across the
  // universes, which before this change meant the user could not write to the indexed table while
  // creating indexes. Its important to note that in bi-directional xCluster the users have the
  // responsibility to insert into different key ranges on each universe. This is important as
  // xCluster which operates at the physical layer does not enforce YSQL layer constraints like
  // Foreign keys.
  //
  // In order to allow Online Create Index we synchronize the two universe but only at the Backfill
  // stage. We allow them to diverse by more than 2 stages, since for any given key range each
  // universe will locally ensures the 2 stage apart policy. Only the backfill stage reads and
  // writes data across the entire key range, so this alone needs to be synchronized across the two
  // universe.
  //
  // The stream for the new index table has already been created as part of its DocDB table
  // creation. (See CreateXClusterStreamForBiDirectionalIndexTask) This function performs the
  // following steps:
  // 1. Wait for the index and its stream to get created on the other universe.
  // 2. Wait for the index on the other universe to reach atleast its backfill stage.
  // 3. Add our index table to the xCluster replication group.
  // 4. Pick the backfill read time.
  // 5. Wait for the indexed table to catch up to the backfill time.
  //
  // For colocated tables, only the parent table is part of replication, so we skip the steps
  // related to stream creation, and adding index to replication.

  const auto deadline =
      CoarseMonoClock::Now() +
      MonoDelta::FromSeconds(FLAGS_add_new_index_to_bidirectional_xcluster_timeout_secs);

  LOG(INFO) << "Preparing indexes " << yb::ToString(index_table_ids) << " on table "
            << indexed_table_id << " for bi-directional xCluster replication";

  auto indexed_table_streams = GetStreamIdsForTable(indexed_table_id);
  // Complex scenarios like replicating between 3 universes with multiple bi-directional replication
  // groups A <=> B <=> C <=> A is not supported.
  SCHECK_EQ(
      indexed_table_streams.size(), 1, IllegalState,
      Format("Expected 1 xCluster stream for table $0", indexed_table_id));

  auto& [replication_id, indexed_table_stream_id] = *indexed_table_streams.begin();

  auto replication_group = catalog_manager_.GetUniverseReplication(replication_id);
  SCHECK_FORMAT(replication_group, NotFound, "Replication group $0 not found", replication_id);

  auto remote_client = VERIFY_RESULT(GetXClusterRemoteClientHolder(*replication_group));

  std::vector<std::shared_ptr<MultiStepMonitoredTask>> tasks;
  for (const auto& index_table_id : index_table_ids) {
    auto index_table_info = VERIFY_RESULT(catalog_manager_.GetTableById(index_table_id));
    tasks.emplace_back(std::make_shared<AddBiDirectionalIndexToXClusterTargetTask>(
        std::move(index_table_info), replication_group, remote_client, master_, epoch, deadline));
  }

  Synchronizer sync;
  RETURN_NOT_OK(MultiStepMonitoredTask::StartTasks(
      tasks, [&sync](const Status& status) { sync.AsStdStatusCallback()(status); }));
  RETURN_NOT_OK(sync.Wait());

  // All indexes have reached the backfill stage on both universes. We can proceed once the indexed
  // table is caught up.
  const auto backfill_ht = master_.clock()->Now();
  const auto backfill_time_micros =
      backfill_ht.GetPhysicalValueMicros() + (3 * FLAGS_max_clock_skew_usec);

  LOG(INFO) << "Waiting for replication of indexed table " << indexed_table_id << " stream "
            << indexed_table_stream_id << " to catch up to backfill time " << backfill_ht;
  RETURN_NOT_OK_PREPEND(
      remote_client->GetXClusterClient().WaitForReplicationDrain(
          indexed_table_stream_id, backfill_time_micros, deadline),
      Format(
          "Error waiting for replication drain of indexed table $0 stream $1", indexed_table_id,
          indexed_table_stream_id));
  LOG(INFO) << "Indexed table " << indexed_table_id << " xCluster stream "
            << indexed_table_stream_id << " caught up to backfill time " << backfill_ht;

  return backfill_ht;
}

Status XClusterTargetManager::InsertPackedSchemaForXClusterTarget(
    const TableId& table_id, const SchemaPB& packed_schema_to_insert,
    uint32_t current_schema_version, const LeaderEpoch& epoch,
    bool error_on_incorrect_schema_version) {
  // Lookup the table and verify if it exists.
  scoped_refptr<TableInfo> table = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));
  {
    auto l = table->LockForWrite();
    auto& table_pb = l.mutable_data()->pb;
    SCHECK(l->is_running(), IllegalState, "Table $0 is not running", table->ToStringWithState());

    // Compare the current schema version with the one in the request to avoid repeated updates.
    if (table_pb.version() != current_schema_version) {
      YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 10, 1)
          << __func__ << ": Table " << table->ToString() << " has schema version "
          << table_pb.version() << " but request has version " << current_schema_version
          << ". Skipping update of packed schema.";
      SCHECK(
          !error_on_incorrect_schema_version, InvalidArgument,
          "Table $0 has schema version $1 but request has version $2.", table->ToString(),
          table_pb.version(), current_schema_version);
      return Status::OK();
    }

    // Just bump the version up by two. We will insert the packed schema in as version + 1, and then
    // revert back to the original version, but now with version + 2.
    table_pb.set_version(table_pb.version() + 2);
    RETURN_NOT_OK(sys_catalog_.Upsert(epoch, table));
    l.Commit();
  }

  for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
    auto call = std::make_shared<AsyncInsertPackedSchemaForXClusterTarget>(
        &master_, catalog_manager_.AsyncTaskPool(), tablet, table, packed_schema_to_insert, epoch);
    table->AddTask(call);
    RETURN_NOT_OK(catalog_manager_.ScheduleTask(call));
  }

  LOG(INFO) << "Successfully initiated InsertPackedSchemaForXClusterTarget "
            << "(pending tablet schema updates) for " << table->ToString();

  return Status::OK();
}

Status XClusterTargetManager::InsertHistoricalColocatedSchemaPacking(
    const InsertHistoricalColocatedSchemaPackingRequestPB* req,
    InsertHistoricalColocatedSchemaPackingResponsePB* resp, const LeaderEpoch& epoch) {
  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());
  auto colocation_id = req->colocation_id();
  TableId parent_table_id = req->target_parent_table_id();
  SCHECK(
      IsColocationParentTableId(parent_table_id), InvalidArgument,
      "Parent table id is not a colocation parent id: ", parent_table_id);
  auto tablegroup_id = GetTablegroupIdFromParentTableId(parent_table_id);
  auto namespace_id = VERIFY_RESULT(catalog_manager_.GetTableNamespaceId(parent_table_id));

  auto add_historical_schema_fn = [&](UniverseReplicationInfo& universe) -> Status {
    auto compatible_schema_version = VERIFY_RESULT(AddHistoricalPackedSchemaForColocatedTable(
        universe, namespace_id, parent_table_id, colocation_id, req->source_schema_version(),
        req->schema(), catalog_manager_, epoch));
    resp->set_last_compatible_consumer_schema_version(compatible_schema_version);
    return Status::OK();
  };

  return catalog_manager_.InsertHistoricalColocatedSchemaPacking(
      replication_group_id, tablegroup_id, colocation_id, add_historical_schema_fn);
}

Status XClusterTargetManager::ProcessCreateTableReq(
    const CreateTableRequestPB& req, SysTablesEntryPB& table_pb, const TableId& table_id,
    const NamespaceId& namespace_id) const {
  // Only need to process if this is the target of Automatic Mode xCluster replication.
  if (req.xcluster_table_info().xcluster_source_table_id().empty()) {
    return Status::OK();
  }

  // xcluster_source_table_id will be used to find the correct source table to replicate from.
  table_pb.mutable_xcluster_table_info()->set_xcluster_source_table_id(
      req.xcluster_table_info().xcluster_source_table_id());

  if (req.xcluster_table_info().xcluster_backfill_hybrid_time()) {
    table_pb.mutable_xcluster_table_info()->set_xcluster_backfill_hybrid_time(
        req.xcluster_table_info().xcluster_backfill_hybrid_time());
  }

  // For colocated tables, we also need to fetch and update any historical packing schemas.
  // This may also bump up the initial schema version.
  const auto colocation_id = table_pb.schema().colocated_table_id().colocation_id();
  if (colocation_id == kColocationIdNotSet) {
    return Status::OK();
  }
  SCHECK(
      !IsColocationParentTableId(req.table_id()), InvalidArgument,
      "Received unexpected parent colocation table id: $0", req.table_id());

  // Find the universe replication info for the parent table.
  for (const auto& universe : catalog_manager_.GetAllUniverseReplications()) {
    if (!HasNamespace(*universe, namespace_id)) {
      continue;
    }
    return UpdateColocatedTableWithHistoricalSchemaPackings(
        *universe, table_pb, table_id, namespace_id, colocation_id);
  }

  return STATUS_FORMAT(
      NotFound,
      "XCluster replication group not found for table $0, colocation_id #1, namespace_id $2",
      table_id, colocation_id, namespace_id);
}

Status XClusterTargetManager::SetReplicationGroupEnabled(
    const xcluster::ReplicationGroupId& replication_group_id, bool is_enabled,
    const LeaderEpoch& epoch, CoarseTimePoint deadline) {
  auto cluster_config = catalog_manager_.ClusterConfig();
  auto l = cluster_config->LockForWrite();

  auto replication_group_map =
      l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
  auto replication_group = FindOrNull(*replication_group_map, replication_group_id.ToString());
  SCHECK(
      replication_group, NotFound,
      Format("Replication group $0 not found in Consumer Registry", replication_group_id));

  if (replication_group->disable_stream() == !is_enabled) {
    // Dont update the config version unnecessarily.
    return Status::OK();
  }

  replication_group->set_disable_stream(!is_enabled);

  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, cluster_config.get()));
  l.Commit();

  if (!is_enabled) {
    return Wait(
        [this, replication_group_id] {
          return IsReplicationGroupFullyPaused(replication_group_id);
        },
        deadline, Format("Wait for replication group $0 to pause", replication_group_id));
  }

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

namespace {
Result<ColocationId> ExtractColocationId(const TableId& table_id, const SchemaPB& table_schema) {
  if (!IsColocationParentTableId(table_id)) {
    return kColocationIdNotSet;
  }
  SCHECK(
      table_schema.has_colocated_table_id() &&
          table_schema.colocated_table_id().has_colocation_id(),
      InvalidArgument, "Missing colocation id for given colocated table $0", table_id);
  return table_schema.colocated_table_id().colocation_id();
}

Result<cdc::StreamEntryPB> GetStreamEntry(
    CatalogManager& catalog_manager, const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id) {
  auto cluster_config = catalog_manager.ClusterConfig();
  auto cluster_config_lock = cluster_config->LockForRead();
  auto replication_group_map = cluster_config_lock->pb.consumer_registry().producer_map();
  auto producer_entry = FindOrNull(replication_group_map, replication_group_id.ToString());
  SCHECK(producer_entry, NotFound, Format("Replication group $0 not found", replication_group_id));
  auto stream_entry = FindOrNull(producer_entry->stream_map(), stream_id.ToString());
  SCHECK(
      stream_entry, NotFound,
      Format("Stream $0 not found in replication group $1", stream_id, replication_group_id));
  return *stream_entry;
}

const cdc::SchemaVersionsPB* FindSchemaVersionsMapping(
    const cdc::StreamEntryPB& stream_entry, ColocationId colocation_id) {
  if (colocation_id == kColocationIdNotSet) {
    return &stream_entry.schema_versions();
  }
  const auto& colocated_schema_versions = stream_entry.colocated_schema_versions();
  // Null is ok, just means that this is a new colocated table.
  return FindOrNull(colocated_schema_versions, colocation_id);
}

bool MappingHasProducerSchemaVersion(
    const cdc::SchemaVersionsPB* schema_versions_mapping, uint32_t producer_schema_version) {
  if (!schema_versions_mapping) {
    return false;  // New colocated table.
  }
  if (schema_versions_mapping->current_producer_schema_version() == producer_schema_version) {
    return true;
  }
  const auto& old_schema_versions = schema_versions_mapping->old_producer_schema_versions();
  return ::util::gtl::contains(
      old_schema_versions.begin(), old_schema_versions.end(), producer_schema_version);
}
}  // namespace

Status XClusterTargetManager::HandleNewSchemaForAutomaticXClusterTarget(
    const HandleNewSchemaForAutomaticXClusterTargetRequestPB* req,
    HandleNewSchemaForAutomaticXClusterTargetResponsePB* resp, const LeaderEpoch& epoch) {
  SCHECK_PB_FIELDS_NOT_EMPTY(
      *req, table_id, replication_group_id, stream_id, producer_schema_version, schema);

  auto table_id = TableId(req->table_id());
  const auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());
  const auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));

  const auto stream_entry =
      VERIFY_RESULT(GetStreamEntry(catalog_manager_, replication_group_id, stream_id));
  const auto colocation_id = VERIFY_RESULT(ExtractColocationId(table_id, req->schema()));

  // Check if this producer schema version already exists in the mapping.
  const auto* schema_versions_mapping = FindSchemaVersionsMapping(stream_entry, colocation_id);
  if (MappingHasProducerSchemaVersion(schema_versions_mapping, req->producer_schema_version())) {
    // Pollers will get the updated schema version mapping from a heartbeat response.
    return Status::OK();
  }

  if (IsColocationParentTableId(table_id)) {
    auto tablegroup_id = GetTablegroupIdFromParentTableId(table_id);
    if (!VERIFY_RESULT(catalog_manager_.HasTableWithColocationId(tablegroup_id, colocation_id))) {
      // Handle the case where a colocated table doesn't exist yet.
      return HandleNewTableSchemaForUpcomingColocatedTable(
          table_id, replication_group_id, stream_id, colocation_id, req->producer_schema_version(),
          req->schema(), epoch);
    }
    table_id = VERIFY_RESULT(catalog_manager_.GetColocatedTableId(tablegroup_id, colocation_id));
  }
  const auto table = VERIFY_RESULT(catalog_manager_.FindTableById(table_id));

  // Optimistic check to avoid creating duplicate tasks (task creation/deletion can be noisy).
  if (table->HasTasks(server::MonitoredTaskType::kXClusterHandleNewSchema)) {
    // Pollers will retry if they still don't have the updated schema version mapping.
    return Status::OK();
  }

  auto create_async_insert_packed_schema_tasks_fn = [this, epoch](
                                                        const TableId& table_id,
                                                        const SchemaPB& schema,
                                                        uint32_t current_schema_version) -> Status {
    return InsertPackedSchemaForXClusterTarget(
        table_id, schema, current_schema_version, epoch,
        /*error_on_incorrect_schema_version=*/true);
  };
  // Create and start the multi-step task to handle the new schema.
  // The poller will be blocked until this task adds an entry in the schema version mapping.
  auto task = std::make_shared<HandleNewSchemaForAutomaticXClusterTargetTask>(
      catalog_manager_, *catalog_manager_.AsyncTaskPool(), *master_.messenger(), table, epoch,
      master_, replication_group_id, stream_id, req->producer_schema_version(), req->schema(),
      std::move(create_async_insert_packed_schema_tasks_fn));
  if (!table->AddTaskIfNotPresent(task)) {
    VLOG(1) << "HandleNewSchemaForAutomaticXClusterTargetTask already running for table "
            << table->ToString();
    // Pollers will retry if they still don't have the updated schema version mapping.
    return Status::OK();
  }
  task->Start();

  LOG(INFO) << Format(
      "Scheduled HandleNewSchemaForAutomaticXClusterTargetTask for table $0, "
      "producer_schema_version $1, replication_group_id $2, stream_id $3",
      table->ToString(), req->producer_schema_version(), replication_group_id.ToString(),
      stream_id.ToString());

  return Status::OK();
}

Status XClusterTargetManager::HandleNewTableSchemaForUpcomingColocatedTable(
    const TableId& table_id, const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id, ColocationId colocation_id, uint32_t producer_schema_version,
    const SchemaPB& schema, const LeaderEpoch& epoch) {
  // Store the upcoming schema packings in the replication group.
  InsertHistoricalColocatedSchemaPackingRequestPB insert_req;
  InsertHistoricalColocatedSchemaPackingResponsePB insert_resp;
  insert_req.set_replication_group_id(replication_group_id.ToString());
  insert_req.set_target_parent_table_id(table_id);
  insert_req.set_colocation_id(colocation_id);
  insert_req.set_source_schema_version(producer_schema_version);
  insert_req.mutable_schema()->CopyFrom(schema);

  LOG(INFO) << Format(
      "Inserting historical schema packing for table $0, colocation_id $1, schema $2", table_id,
      colocation_id, schema);
  RETURN_NOT_OK(master_.xcluster_manager()->InsertHistoricalColocatedSchemaPacking(
      &insert_req, &insert_resp, /*rpc_context=*/nullptr, epoch));
  SCHECK(
      insert_resp.has_last_compatible_consumer_schema_version(), InvalidArgument,
      "Missing last compatible consumer schema version in response");

  // Also need to update the schema version mapping (this will then unblock the pollers).
  UpdateConsumerOnProducerMetadataRequestPB update_req;
  UpdateConsumerOnProducerMetadataResponsePB update_resp;
  update_req.set_replication_group_id(replication_group_id.ToString());
  update_req.set_stream_id(stream_id.ToString());
  update_req.set_producer_schema_version(producer_schema_version);
  update_req.set_consumer_schema_version(insert_resp.last_compatible_consumer_schema_version());
  update_req.set_colocation_id(colocation_id);

  return catalog_manager_.UpdateConsumerOnProducerMetadata(
      &update_req, &update_resp, /*rpc_context=*/nullptr);
}

}  // namespace yb::master
