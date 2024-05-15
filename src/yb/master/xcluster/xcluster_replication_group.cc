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

#include "yb/master/xcluster/xcluster_replication_group.h"

#include "yb/client/client.h"
#include "yb/client/xcluster_client.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/master/xcluster_rpc_tasks.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/cdc/xcluster_util.h"
#include "yb/master/sys_catalog.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/result.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_string(certs_for_cdc_dir);

namespace yb::master {

namespace {
// Returns nullopt when source universe does not support AutoFlags compatibility check.
// Returns a pair of bool which indicates if the configs are compatible and the source universe
// AutoFlags config version.
Result<std::optional<std::pair<bool, uint32>>> ValidateAutoFlagsConfig(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config) {
  auto master_addresses = replication_info.LockForRead()->pb.producer_master_addresses();
  auto xcluster_rpc = VERIFY_RESULT(replication_info.GetOrCreateXClusterRpcTasks(master_addresses));
  auto result = VERIFY_RESULT(
      xcluster_rpc->client()->ValidateAutoFlagsConfig(local_config, AutoFlagClass::kExternal));

  if (!result) {
    return std::nullopt;
  }

  auto& [is_valid, source_version] = *result;
  VLOG(2) << "ValidateAutoFlagsConfig for replication group: "
          << replication_info.ReplicationGroupId() << ", is_valid: " << is_valid
          << ", source universe version: " << source_version
          << ", target universe version: " << local_config.config_version();

  return result;
}

Result<cdc::ProducerEntryPB*> GetProducerEntry(
    SysClusterConfigEntryPB& cluster_config_pb,
    const xcluster::ReplicationGroupId& replication_group_id) {
  auto producer_entry = FindOrNull(
      *cluster_config_pb.mutable_consumer_registry()->mutable_producer_map(),
      replication_group_id.ToString());
  SCHECK_FORMAT(
      producer_entry, NotFound, "Missing producer entry for replication group $0",
      replication_group_id);

  return producer_entry;
}

// Run AutoFlags compatibility validation.
// If the AutoFlags are compatible, compatible_auto_flag_config_version is set to the source
// universe AutoFlags config version.
// If the AutoFlags are not compatible compatible_auto_flag_config_version is set to
// kInvalidAutoFlagsConfigVersion.
// If validated_local_auto_flags_config_version is less than the new local AutoFLags config version,
// it is updated.
// If cluster_config or replication_info are updated, they are stored in the sys_catalog.
Status ValidateAutoFlagsInternal(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    SysUniverseReplicationEntryPB& replication_info_pb, ClusterConfigInfo& cluster_config,
    SysClusterConfigEntryPB& cluster_config_pb, const AutoFlagsConfigPB& local_auto_flags_config,
    const LeaderEpoch& epoch) {
  const auto local_version = local_auto_flags_config.config_version();
  const auto& replication_group_id = replication_info.ReplicationGroupId();

  bool cluster_config_changed = false;
  bool replication_info_changed = false;

  auto producer_entry = VERIFY_RESULT(GetProducerEntry(cluster_config_pb, replication_group_id));

  auto validate_result =
      VERIFY_RESULT(ValidateAutoFlagsConfig(replication_info, local_auto_flags_config));

  if (validate_result) {
    auto& [is_valid, source_version] = *validate_result;
    VLOG(2) << "ValidateAutoFlagsConfig for replication group: " << replication_group_id
            << ", is_valid: " << is_valid << ", source universe version: " << source_version
            << ", target universe version: " << local_version;
    if (producer_entry->validated_auto_flags_config_version() < source_version) {
      producer_entry->set_validated_auto_flags_config_version(source_version);
      cluster_config_changed = true;
    }
    auto old_compatible_auto_flag_config_version =
        producer_entry->compatible_auto_flag_config_version();
    if (is_valid && old_compatible_auto_flag_config_version < source_version) {
      producer_entry->set_compatible_auto_flag_config_version(source_version);
      cluster_config_changed = true;
    } else if (
        !is_valid && old_compatible_auto_flag_config_version != kInvalidAutoFlagsConfigVersion) {
      // We are not compatible with the source universe anymore.
      producer_entry->set_compatible_auto_flag_config_version(kInvalidAutoFlagsConfigVersion);
      cluster_config_changed = true;
    }
  } else {
    VLOG_WITH_FUNC(2)
        << "Source universe of replication group " << replication_group_id
        << " is running a version that does not support the AutoFlags compatibility check yet";
  }

  if (!replication_info_pb.has_validated_local_auto_flags_config_version() ||
      replication_info_pb.validated_local_auto_flags_config_version() < local_version) {
    replication_info_pb.set_validated_local_auto_flags_config_version(local_version);
    replication_info_changed = true;
  }

  if (cluster_config_changed) {
    // Bump the ClusterConfig version so we'll broadcast new config version to tservers.
    cluster_config_pb.set_version(cluster_config_pb.version() + 1);
  }

  if (cluster_config_changed && replication_info_changed) {
    RETURN_NOT_OK_PREPEND(
        sys_catalog.Upsert(epoch.leader_term, &cluster_config, &replication_info),
        "Updating cluster config and replication info in sys-catalog");
  } else if (cluster_config_changed) {
    RETURN_NOT_OK_PREPEND(
        sys_catalog.Upsert(epoch.leader_term, &cluster_config),
        "Updating cluster config in sys-catalog");
  } else if (replication_info_changed) {
    RETURN_NOT_OK_PREPEND(
        sys_catalog.Upsert(epoch.leader_term, &replication_info),
        "Updating replication info in sys-catalog");
  }

  return Status::OK();
}
}  // namespace

Result<uint32> GetAutoFlagConfigVersionIfCompatible(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config) {
  const auto& replication_group_id = replication_info.ReplicationGroupId();

  VLOG_WITH_FUNC(2) << "Validating AutoFlags config for replication group: " << replication_group_id
                    << " with target config version: " << local_config.config_version();

  auto validate_result = VERIFY_RESULT(ValidateAutoFlagsConfig(replication_info, local_config));

  if (!validate_result) {
    VLOG_WITH_FUNC(2)
        << "Source universe of replication group " << replication_group_id
        << " is running a version that does not support the AutoFlags compatibility check yet";
    return kInvalidAutoFlagsConfigVersion;
  }

  auto& [is_valid, source_version] = *validate_result;

  SCHECK(
      is_valid, IllegalState,
      "AutoFlags between the universes are not compatible. Upgrade the target universe to a "
      "version higher than or equal to the source universe");

  return source_version;
}

Status RefreshAutoFlagConfigVersion(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    ClusterConfigInfo& cluster_config, uint32 requested_auto_flag_version,
    std::function<AutoFlagsConfigPB()> get_local_auto_flags_config_func, const LeaderEpoch& epoch) {
  VLOG_WITH_FUNC(2) << "Revalidating AutoFlags config for replication group: "
                    << replication_info.ReplicationGroupId()
                    << " with requested source config version: " << requested_auto_flag_version;

  // Lock ReplicationInfo before ClusterConfig.
  auto replication_info_write_lock = replication_info.LockForWrite();

  auto cluster_config_lock = cluster_config.LockForWrite();
  auto& cluster_config_pb = cluster_config_lock.mutable_data()->pb;

  auto producer_entry =
      VERIFY_RESULT(GetProducerEntry(cluster_config_pb, replication_info.ReplicationGroupId()));

  if (requested_auto_flag_version <= producer_entry->validated_auto_flags_config_version()) {
    VLOG_WITH_FUNC(2)
        << "Requested source universe AutoFlags config version has already been validated";
    return Status::OK();
  }

  auto& replication_info_pb = replication_info_write_lock.mutable_data()->pb;

  const auto local_auto_flags_config = get_local_auto_flags_config_func();
  RETURN_NOT_OK(ValidateAutoFlagsInternal(
      sys_catalog, replication_info, replication_info_pb, cluster_config, cluster_config_pb,
      std::move(local_auto_flags_config), epoch));

  replication_info_write_lock.Commit();
  cluster_config_lock.Commit();

  return Status::OK();
}

Status HandleLocalAutoFlagsConfigChange(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    ClusterConfigInfo& cluster_config, const AutoFlagsConfigPB& local_auto_flags_config,
    const LeaderEpoch& epoch) {
  const auto local_config_version = local_auto_flags_config.config_version();
  VLOG_WITH_FUNC(2) << "Revalidating AutoFlags config for replication group: "
                    << replication_info.ReplicationGroupId()
                    << " with local config version: " << local_config_version;

  {
    auto replication_info_read_lock = replication_info.LockForRead();
    auto& replication_info_pb = replication_info_read_lock->pb;

    if (replication_info_pb.has_validated_local_auto_flags_config_version() &&
        replication_info_pb.validated_local_auto_flags_config_version() >= local_config_version) {
      return Status::OK();
    }
  }

  // Lock ReplicationInfo before ClusterConfig.
  auto replication_info_write_lock = replication_info.LockForWrite();
  auto cluster_config_lock = cluster_config.LockForWrite();

  auto& cluster_config_pb = cluster_config_lock.mutable_data()->pb;
  auto& replication_info_pb = replication_info_write_lock.mutable_data()->pb;

  RETURN_NOT_OK(ValidateAutoFlagsInternal(
      sys_catalog, replication_info, replication_info_pb, cluster_config, cluster_config_pb,
      local_auto_flags_config, epoch));

  replication_info_write_lock.Commit();
  cluster_config_lock.Commit();

  return Status::OK();
}

std::optional<NamespaceId> GetProducerNamespaceIdInternal(
    const SysUniverseReplicationEntryPB& universe_pb, const NamespaceId& consumer_namespace_id) {
  const auto& namespace_infos = universe_pb.db_scoped_info().namespace_infos();
  auto it = std::find_if(
      namespace_infos.begin(), namespace_infos.end(),
      [&consumer_namespace_id](const auto& namespace_info) {
        return namespace_info.consumer_namespace_id() == consumer_namespace_id;
      });

  if (it == namespace_infos.end()) {
    return std::nullopt;
  }

  return it->producer_namespace_id();
}

Result<bool> ShouldAddTableToReplicationGroup(
    UniverseReplicationInfo& universe, const TableInfo& table_info,
    CatalogManager& catalog_manager) {
  const auto& table_pb = table_info.old_pb();

  if (!IsTableEligibleForXClusterReplication(table_info)) {
    return false;
  }

  auto l = universe.LockForRead();
  const auto& universe_pb = l->pb;
  if (l->is_deleted_or_failed()) {
    VLOG(1) << "Skip adding table " << table_info.ToString()
            << " to xCluster replication as the universe " << universe.ReplicationGroupId()
            << " is in a deleted or failed state";
    return false;
  }

  if (l->IsDbScoped()) {
    if (!IncludesConsumerNamespace(universe, table_info.namespace_id())) {
      return false;
    }
  } else {
    // Handle v1 API, transactional xcluster replication for indexes. If the indexed table is under
    // replication then we need to add the index to replication as well.

    if (!universe_pb.transactional() || !IsIndex(table_pb)) {
      return false;
    }

    const auto& indexed_table_id = GetIndexedTableId(table_pb);
    auto indexed_table_stream_ids =
        catalog_manager.GetXClusterConsumerStreamIdsForTable(indexed_table_id);
    if (indexed_table_stream_ids.empty()) {
      return false;
    }
  }

  // Skip if the table has already been added to this replication group.
  auto cluster_config = catalog_manager.ClusterConfig();
  {
    auto l = cluster_config->LockForRead();
    const auto& consumer_registry = l->pb.consumer_registry();

    auto producer_entry =
        FindOrNull(consumer_registry.producer_map(), universe.ReplicationGroupId().ToString());
    if (producer_entry) {
      SCHECK(
          !producer_entry->disable_stream(), IllegalState,
          "Table belongs to xCluster replication group $0 which is currently disabled",
          universe.ReplicationGroupId());
      for (auto& [stream_id, stream_info] : producer_entry->stream_map()) {
        if (stream_info.consumer_table_id() == table_info.id()) {
          VLOG(1) << "Table " << table_info.ToString()
                  << " is already part of xcluster replication " << stream_id;

          return false;
        }
      }
    }
  }

  SCHECK(
      !table_info.IsColocationParentTable(), IllegalState,
      Format(
          "Colocated parent tables can only be added during the initial xCluster replication "
          "setup: $0",
          table_info.ToString()));

  return true;
}

Result<NamespaceId> GetProducerNamespaceId(
    UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id) {
  SCHECK(
      universe.IsDbScoped(), IllegalState, "Replication group $0 is not db-scoped",
      universe.ToString());

  auto l = universe.LockForRead();
  auto opt_namespace_id = GetProducerNamespaceIdInternal(l->pb, consumer_namespace_id);
  SCHECK_FORMAT(
      opt_namespace_id, NotFound, "Namespace $0 not found in replication group $1",
      consumer_namespace_id, universe.ToString());

  return *opt_namespace_id;
}

bool IncludesConsumerNamespace(
    UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id) {
  if (!universe.IsDbScoped()) {
    return false;
  }
  auto l = universe.LockForRead();
  auto opt_namespace_id = GetProducerNamespaceIdInternal(l->pb, consumer_namespace_id);
  return opt_namespace_id.has_value();
}

Result<std::shared_ptr<client::XClusterRemoteClient>> GetXClusterRemoteClient(
    UniverseReplicationInfo& universe) {
  auto master_addresses = universe.LockForRead()->pb.producer_master_addresses();
  std::vector<HostPort> hp;
  HostPortsFromPBs(master_addresses, &hp);
  auto xcluster_client = std::make_shared<client::XClusterRemoteClient>(
      FLAGS_certs_for_cdc_dir, MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));
  RETURN_NOT_OK(xcluster_client->Init(universe.ReplicationGroupId(), hp));

  return xcluster_client;
}

Result<bool> IsSafeTimeReady(
    const SysUniverseReplicationEntryPB& universe_pb, const XClusterManagerIf& xcluster_manager) {
  if (!IsDbScoped(universe_pb)) {
    // Only valid in Db scoped replication.
    return true;
  }

  const auto safe_time_map = VERIFY_RESULT(xcluster_manager.GetXClusterNamespaceToSafeTimeMap());
  for (const auto& namespace_info : universe_pb.db_scoped_info().namespace_infos()) {
    const auto& namespace_id = namespace_info.consumer_namespace_id();
    auto* it = FindOrNull(safe_time_map, namespace_id);
    if (!it || it->is_special()) {
      VLOG_WITH_FUNC(1) << "Safe time for namespace " << namespace_id
                        << " is not yet ready: " << (it ? it->ToString() : "NA");
      return false;
    }
  }

  return true;
}

Result<IsOperationDoneResult> IsSetupUniverseReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id, CatalogManager& catalog_manager) {
  // Cases for completion:
  //  - For AlterUniverseReplication, we need to wait until the .ALTER universe gets merged with
  //    the main universe - at which point the .ALTER universe is deleted.
  //  - For regular SetupUniverseReplication, we want to wait for the universe to become ACTIVE.
  //      - If this is a brand new Db scoped replication group then we also have to wait for the
  //        xcluster safe time to be ready.

  const auto is_alter_request = xcluster::IsAlterReplicationGroupId(replication_group_id);
  auto universe = catalog_manager.GetUniverseReplication(replication_group_id);

  auto state = SysUniverseReplicationEntryPB::DELETED;
  if (universe) {
    state = universe->LockForRead()->pb.state();
  }
  if (state == SysUniverseReplicationEntryPB::DELETED) {
    Status status;
    if (!is_alter_request) {
      status = STATUS(NotFound, "Could not find xCluster replication group");
    }
    return IsOperationDoneResult::Done(std::move(status));
  }

  CHECK(universe);

  if (state == SysUniverseReplicationEntryPB::DELETED_ERROR ||
      state == SysUniverseReplicationEntryPB::FAILED) {
    auto setup_error = universe->GetSetupUniverseReplicationErrorStatus();
    if (setup_error.ok()) {
      LOG(WARNING) << "Did not find setup error status for universe " << replication_group_id
                   << " in state " << SysUniverseReplicationEntryPB::State_Name(state);
      setup_error = STATUS(InternalError, "unknown error");
    }

    // Add failed universe to GC now that we've responded to the user.
    catalog_manager.MarkUniverseForCleanup(replication_group_id);

    return IsOperationDoneResult::Done(std::move(setup_error));
  }

  bool is_done = false;
  if (!is_alter_request && state == SysUniverseReplicationEntryPB::ACTIVE) {
    auto l = universe->LockForRead();
    is_done = VERIFY_RESULT(IsSafeTimeReady(l->pb, *catalog_manager.GetXClusterManager()));
  }

  return is_done ? IsOperationDoneResult::Done() : IsOperationDoneResult::NotDone();
}

Status RemoveNamespaceFromReplicationGroup(
    scoped_refptr<UniverseReplicationInfo> universe, const NamespaceId& producer_namespace_id,
    CatalogManager& catalog_manager, const LeaderEpoch& epoch) {
  SCHECK(
      universe->IsDbScoped(), IllegalState, "Replication group $0 is not DB Scoped",
      universe->id());

  auto l = universe->LockForWrite();
  auto& universe_pb = l.mutable_data()->pb;

  NamespaceId consumer_namespace_id;
  auto* namespace_infos = universe_pb.mutable_db_scoped_info()->mutable_namespace_infos();
  for (auto it = namespace_infos->begin(); it != namespace_infos->end(); ++it) {
    if (it->producer_namespace_id() == producer_namespace_id) {
      consumer_namespace_id = it->consumer_namespace_id();
      namespace_infos->erase(it);
      break;
    }
  }
  if (consumer_namespace_id.empty()) {
    LOG(INFO) << "Producer Namespace " << producer_namespace_id
              << " not found in xCluster replication group " << universe->ReplicationGroupId();
    return Status::OK();
  }

  auto consumer_tables =
      VERIFY_RESULT(catalog_manager.GetTableInfosForNamespace(consumer_namespace_id));
  std::unordered_set<TableId> consumer_table_ids;
  for (const auto& table_info : consumer_tables) {
    consumer_table_ids.insert(table_info->id());
  }

  std::vector<TableId> producer_table_ids;
  for (const auto& [producer_table_id, consumer_table_id] : universe_pb.validated_tables()) {
    if (consumer_table_ids.contains(consumer_table_id)) {
      producer_table_ids.push_back(producer_table_id);
    }
  }

  // For DB Scoped replication the streams will be cleaned up when the namespace is removed from
  // the outbound replication group on the source.
  return RemoveTablesFromReplicationGroupInternal(
      *universe, l, producer_table_ids, catalog_manager, epoch, /*cleanup_source_streams=*/false);
}

Status RemoveTablesFromReplicationGroup(
    scoped_refptr<UniverseReplicationInfo> universe, const std::vector<TableId>& producer_table_ids,
    CatalogManager& catalog_manager, const LeaderEpoch& epoch) {
  auto l = universe->LockForWrite();
  return RemoveTablesFromReplicationGroupInternal(
      *universe, l, producer_table_ids, catalog_manager, epoch, /*cleanup_source_streams=*/true);
}

Status RemoveTablesFromReplicationGroupInternal(
    UniverseReplicationInfo& universe, UniverseReplicationInfo::WriteLock& l,
    const std::vector<TableId>& producer_table_ids, CatalogManager& catalog_manager,
    const LeaderEpoch& epoch, bool cleanup_source_streams) {
  RSTATUS_DCHECK(!producer_table_ids.empty(), InvalidArgument, "No tables to remove");

  auto& replication_group_id = universe.ReplicationGroupId();

  std::set<TableId> producer_table_ids_to_remove(
      producer_table_ids.begin(), producer_table_ids.end());
  std::set<TableId> consumer_table_ids_to_remove;

  // 1. Get the corresponding stream ids and consumer table ids for the tables to remove.
  auto& universe_pb = l.mutable_data()->pb;

  // Filter out any tables that aren't in the existing replication config.
  std::set<TableId> existing_tables(universe_pb.tables().begin(), universe_pb.tables().end());
  std::set<TableId> filtered_list;
  set_intersection(
      producer_table_ids_to_remove.begin(), producer_table_ids_to_remove.end(),
      existing_tables.begin(), existing_tables.end(),
      std::inserter(filtered_list, filtered_list.begin()));
  filtered_list.swap(producer_table_ids_to_remove);

  VLOG(1) << "Removing tables " << yb::ToString(producer_table_ids_to_remove)
          << " from inbound xCluster replication group " << replication_group_id;
  if (producer_table_ids_to_remove.empty()) {
    return Status::OK();
  }

  std::vector<xrepl::StreamId> streams_to_remove;

  auto cluster_config = catalog_manager.ClusterConfig();
  {
    auto cl = cluster_config->LockForRead();
    auto& cluster_config_pb = cl->pb;
    auto& producer_map = cluster_config_pb.consumer_registry().producer_map();
    auto producer_entry = FindOrNull(producer_map, replication_group_id.ToString());
    if (producer_entry) {
      // Remove the Tables Specified (not part of the key).
      auto& stream_map = producer_entry->stream_map();
      for (auto& [stream_id, stream_entry] : stream_map) {
        if (producer_table_ids_to_remove.contains(stream_entry.producer_table_id())) {
          streams_to_remove.emplace_back(VERIFY_RESULT(xrepl::StreamId::FromString(stream_id)));
          // Also fetch the consumer table ids here so we can clean the in-memory maps after.
          consumer_table_ids_to_remove.insert(stream_entry.consumer_table_id());
        }
      }

      // If this ends with an empty Map, disallow and force user to delete.
      LOG_IF(WARNING, streams_to_remove.size() == stream_map.size())
          << "All tables in xCluster replication group " << replication_group_id
          << " have been removed";
    }
  }

  // 2. Delete xCluster streams on the Producer.
  if (!streams_to_remove.empty() && cleanup_source_streams) {
    auto rpc_task = VERIFY_RESULT(
        universe.GetOrCreateXClusterRpcTasks(universe_pb.producer_master_addresses()));

    // Atomicity cannot be guaranteed for both producer and consumer deletion. So ignore any errors
    // due to missing streams.
    RETURN_NOT_OK_PREPEND(
        rpc_task->client()->DeleteCDCStream(
            streams_to_remove, true /* force_delete */, true /* remove_table_ignore_errors */),
        "Unable to delete xCluster streams on source");
  }

  // 3. Update the Consumer Registry (removes from TServers) and Master Configs.
  auto cl = cluster_config->LockForWrite();
  auto& cluster_config_pb = cl.mutable_data()->pb;
  auto& producer_map = *cluster_config_pb.mutable_consumer_registry()->mutable_producer_map();
  auto producer_entry = FindOrNull(producer_map, replication_group_id.ToString());
  if (producer_entry) {
    // Remove the Tables Specified (not part of the key).
    auto stream_map = producer_entry->mutable_stream_map();
    for (auto& stream_id : streams_to_remove) {
      stream_map->erase(stream_id.ToString());
    }

    cluster_config_pb.set_version(cluster_config_pb.version() + 1);
  }

  for (auto& table_id : producer_table_ids_to_remove) {
    universe_pb.mutable_table_streams()->erase(table_id);
    universe_pb.mutable_validated_tables()->erase(table_id);
    Erase(table_id, universe_pb.mutable_tables());
  }

  RETURN_NOT_OK(catalog_manager.sys_catalog()->Upsert(epoch, &universe, cluster_config.get()));

  // 4. Clear in-mem maps.
  catalog_manager.SyncXClusterConsumerReplicationStatusMap(replication_group_id, producer_map);

  catalog_manager.ClearXClusterConsumerTableStreams(
      replication_group_id, consumer_table_ids_to_remove);

  l.Commit();
  cl.Commit();

  return Status::OK();
}

Status ValidateTableListForDbScopedReplication(
    UniverseReplicationInfo& universe, const std::vector<NamespaceId>& namespace_ids,
    const std::set<TableId>& replicated_tables, const CatalogManager& catalog_manager) {
  std::set<TableId> validated_tables;

  for (const auto& namespace_id : namespace_ids) {
    auto table_infos =
        VERIFY_RESULT(GetTablesEligibleForXClusterReplication(catalog_manager, namespace_id));

    std::vector<TableId> missing_tables;

    for (const auto& table_info : table_infos) {
      const auto& table_id = table_info->id();
      if (replicated_tables.contains(table_id)) {
        validated_tables.insert(table_id);
      } else {
        missing_tables.push_back(table_id);
      }
    }

    SCHECK_FORMAT(
        missing_tables.empty(), IllegalState,
        "Namespace $0 has additional tables that were not added to xCluster DB Scoped replication "
        "group $1: $2",
        namespace_id, universe.id(), yb::ToString(missing_tables));
  }

  auto diff = STLSetSymmetricDifference(replicated_tables, validated_tables);
  SCHECK_FORMAT(
      diff.empty(), IllegalState,
      "xCluster DB Scoped replication group $0 contains tables $1 that do not belong to replicated "
      "namespaces $2",
      universe.id(), yb::ToString(diff), yb::ToString(namespace_ids));

  return Status::OK();
}

}  // namespace yb::master
