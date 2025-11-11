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

#include "yb/cdc/xcluster_types.h"
#include "yb/client/client.h"
#include "yb/client/xcluster_client.h"
#include "yb/common/colocated_util.h"
#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/common/xcluster_util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/xcluster_rpc_tasks.h"

#include "yb/util/async_util.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/result.h"

DEFINE_RUNTIME_bool(xcluster_skip_health_check_on_replication_setup, false,
    "Skip health check on xCluster replication setup");

DEFINE_test_flag(bool, exit_unfinished_deleting, false,
    "Whether to exit part way through the deleting universe process.");

namespace yb::master {

namespace {

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
      LOG(WARNING) << "xCluster replication group " << replication_group_id
                   << " is not compatible with the source universe AutoFlags. Upgrade the universe "
                      "to a version that is equal to or higher than the source universe";
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

Result<bool> IsSafeTimeReady(
    const SysUniverseReplicationEntryPB& universe_pb, const XClusterManagerIf& xcluster_manager) {
  if (!IsDbScoped(universe_pb)) {
    // Only valid in Db scoped replication.
    return true;
  }

  const auto safe_time_map = xcluster_manager.GetXClusterNamespaceToSafeTimeMap();
  for (const auto& namespace_info : universe_pb.db_scoped_info().namespace_infos()) {
    const auto& namespace_id = namespace_info.consumer_namespace_id();
    auto* it = FindOrNull(safe_time_map, namespace_id);
    if (!it || it->is_special()) {
      YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 10, 1)
          << "xCluster safe time for namespace " << namespace_id
          << " is not yet ready: " << (it ? it->ToString() : "NA");
      return false;
    }
  }

  return true;
}

Result<bool> IsReplicationGroupReady(
    const UniverseReplicationInfo& universe, XClusterManagerIf& xcluster_manager) {
  // The replication group must be in a healthy state.
  if (!FLAGS_xcluster_skip_health_check_on_replication_setup &&
      VERIFY_RESULT(xcluster_manager.HasReplicationGroupErrors(universe.ReplicationGroupId()))) {
    YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 10, 1)
        << "xCluster replication group " << universe.ReplicationGroupId() << " is not yet healthy";
    return false;
  }

  auto l = universe.LockForRead();
  return IsSafeTimeReady(l->pb, xcluster_manager);
}

Status ReturnErrorOrAddWarning(
    const Status& s, bool ignore_errors, DeleteUniverseReplicationResponsePB* resp) {
  if (!s.ok()) {
    if (ignore_errors) {
      // Continue executing, save the status as a warning.
      AppStatusPB* warning = resp->add_warnings();
      StatusToPB(s, warning);
      return Status::OK();
    }
    return s.CloneAndAppend("\nUse 'ignore-errors' to ignore this error.");
  }
  return s;
}

Result<std::optional<std::pair<bool, uint32>>> ValidateAutoFlagsConfig(
    client::YBClient& client, const AutoFlagsConfigPB& local_config) {
  auto result =
      VERIFY_RESULT(client.ValidateAutoFlagsConfig(local_config, AutoFlagClass::kExternal));

  if (!result) {
    return std::nullopt;
  }

  auto& [is_valid, source_version] = *result;
  VLOG(2) << "ValidateAutoFlagsConfig is_valid: " << is_valid
          << ", source universe version: " << source_version
          << ", target universe version: " << local_config.config_version();

  return result;
}

Status HandleExtensionOnDropReplication(
    CatalogManager& catalog_manager, const NamespaceId& namespace_id) {
  bool is_switchover =
      catalog_manager.GetXClusterManager()->IsNamespaceInAutomaticModeSource(namespace_id);
  // Don't drop the extension for switchovers, instead transition to source role.
  if (is_switchover) {
    auto namespace_name = VERIFY_RESULT(catalog_manager.FindNamespaceById(namespace_id))->name();
    LOG(INFO) << "Switchover detected for namespace " << namespace_name << " (" << namespace_id
              << "), switching yb_xcluster_ddl_replication extension role to Source.";
    Synchronizer sync;
    RETURN_NOT_OK(master::SetupDDLReplicationExtension(
        catalog_manager, namespace_id, XClusterDDLReplicationRole::kSource,
        sync.AsStdStatusCallback()));
    return sync.Wait();
  }

  // Need to drop the DDL Replication extension for automatic mode.
  // We don't support N:1 replication or daisy-chaining, so we can safely drop on the target.
  Synchronizer sync;
  RETURN_NOT_OK(master::DropDDLReplicationExtensionIfExists(
      catalog_manager, namespace_id,
      [&sync](const Status& status) { sync.AsStdStatusCallback()(status); }));
  return sync.Wait();
}

}  // namespace

Result<LockedConfigAndTableKeyRanges> LockClusterConfigAndGetTableKeyRanges(
    CatalogManager& catalog_manager, const TableId& consumer_table_id) {
  // To avoid races with other updates to table key ranges (eg tablet splits), we need to get the
  // table key ranges under a consistent cluster config version.
  // But since GetTableKeyRanges grabs mutex_, that needs to be called before
  // cluster_config->LockForWrite (we grab the mutex_ before LockForWrite in the loaders).
  // To avoid holding mutex_ for a long time, we optimistically get the lock and retry if needed.
  while (true) {
    int32_t cluster_version = 0;
    {
      auto cc = catalog_manager.ClusterConfig();
      auto l = cc->LockForRead();
      cluster_version = l->pb.version();
    }

    auto ranges = VERIFY_RESULT(catalog_manager.GetTableKeyRanges(consumer_table_id));

    auto cc = catalog_manager.ClusterConfig();
    auto wl = cc->LockForWrite();
    if (wl->pb.version() != cluster_version) {
      continue;
    }

    return LockedConfigAndTableKeyRanges(std::move(cc), std::move(wl), std::move(ranges));
  }
}

Result<std::optional<std::pair<bool, uint32>>> ValidateAutoFlagsConfig(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config) {
  auto master_addresses = replication_info.LockForRead()->pb.producer_master_addresses();
  auto xcluster_rpc = VERIFY_RESULT(replication_info.GetOrCreateXClusterRpcTasks(master_addresses));
  return ValidateAutoFlagsConfig(*xcluster_rpc->client(), local_config);
}

Result<std::optional<std::pair<bool, uint32>>> ValidateAutoFlagsConfig(
    client::XClusterRemoteClientHolder& xcluster_client, const AutoFlagsConfigPB& local_config) {
  return ValidateAutoFlagsConfig(xcluster_client.GetYbClient(), local_config);
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
  if (!IsTableEligibleForXClusterReplication(table_info, universe.IsAutomaticDdlMode())) {
    return false;
  }

  auto table_lock = table_info.LockForRead();
  const auto& table_pb = table_lock->pb;

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
    if (!catalog_manager.GetXClusterManager()->IsTableReplicationConsumer(indexed_table_id)) {
      return false;
    }
  }

  SCHECK(
      !table_info.IsColocationParentTable(), IllegalState,
      Format(
          "Colocated parent tables can only be added during the initial xCluster replication "
          "setup: $0",
          table_info.ToString()));

  // Skip if the table has already been added to this replication group.
  return !VERIFY_RESULT(HasTable(universe, table_info, catalog_manager));
}

Result<bool> HasTable(
    UniverseReplicationInfo& universe, const TableInfo& table_info,
    CatalogManager& catalog_manager) {
  auto cluster_config = catalog_manager.ClusterConfig();
  auto l = cluster_config->LockForRead();
  const auto& consumer_registry = l->pb.consumer_registry();

  auto producer_entry =
      FindOrNull(consumer_registry.producer_map(), universe.ReplicationGroupId().ToString());
  if (!producer_entry) {
    return false;
  }

  SCHECK(
      !producer_entry->disable_stream(), IllegalState,
      "Table belongs to xCluster replication group $0 which is currently disabled",
      universe.ReplicationGroupId());

  for (auto& [stream_id, stream_info] : producer_entry->stream_map()) {
    if (stream_info.consumer_table_id() == table_info.id()) {
      VLOG(1) << "Table " << table_info.ToString() << " is already part of xcluster replication "
              << stream_id;

      return true;
    }
  }

  return false;
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

Result<std::shared_ptr<client::XClusterRemoteClientHolder>> GetXClusterRemoteClientHolder(
    UniverseReplicationInfo& universe) {
  auto master_addresses = universe.LockForRead()->pb.producer_master_addresses();
  std::vector<HostPort> hp;
  HostPortsFromPBs(master_addresses, &hp);

  return client::XClusterRemoteClientHolder::Create(universe.ReplicationGroupId(), hp);
}

Result<TableId> GetConsumerTableIdForStreamId(
    CatalogManager& catalog_manager, const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id) {
  auto cluster_config = catalog_manager.ClusterConfig();
  auto l = cluster_config->LockForRead();
  const auto& consumer_registry = l->pb.consumer_registry();

  const auto* producer_entry =
      FindOrNull(consumer_registry.producer_map(), replication_group_id.ToString());
  SCHECK(producer_entry, NotFound, Format("Missing replication group $0", replication_group_id));

  const auto* stream_entry = FindOrNull(producer_entry->stream_map(), stream_id.ToString());
  SCHECK(
      stream_entry, NotFound,
      Format("Missing replication group $0, stream $1", replication_group_id, stream_id));

  return stream_entry->consumer_table_id();
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
    is_done =
        VERIFY_RESULT(IsReplicationGroupReady(*universe, *catalog_manager.GetXClusterManager()));
  }

  if (is_done) {
    LOG(INFO) << "xCluster replication group " << replication_group_id << " is ready";
    return IsOperationDoneResult::Done();
  }

  return IsOperationDoneResult::NotDone();
}

Status RemoveNamespaceFromReplicationGroup(
    scoped_refptr<UniverseReplicationInfo> universe, const NamespaceId& producer_namespace_id,
    CatalogManager& catalog_manager, const LeaderEpoch& epoch) {
  SCHECK(
      universe->IsDbScoped(), IllegalState, "Replication group $0 is not DB Scoped",
      universe->id());

  auto l = universe->LockForWrite();
  auto& universe_pb = l.mutable_data()->pb;
  auto is_automatic_ddl_mode = IsAutomaticDdlMode(universe_pb);

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

  auto consumer_designators = VERIFY_RESULT(GetTablesEligibleForXClusterReplication(
      catalog_manager, consumer_namespace_id, is_automatic_ddl_mode));
  std::unordered_set<TableId> consumer_table_ids;
  for (const auto& table_designator : consumer_designators) {
    consumer_table_ids.insert(table_designator.id);
  }

  std::vector<TableId> producer_table_ids;
  for (const auto& [producer_table_id, consumer_table_id] : universe_pb.validated_tables()) {
    if (consumer_table_ids.contains(consumer_table_id)) {
      producer_table_ids.push_back(producer_table_id);
    }
  }

  // For DB Scoped replication the streams will be cleaned up when the namespace is removed from
  // the outbound replication group on the source.
  RETURN_NOT_OK(RemoveTablesFromReplicationGroupInternal(
      *universe, l, producer_table_ids, catalog_manager, epoch, /*cleanup_source_streams=*/false));

  if (is_automatic_ddl_mode) {
    RETURN_NOT_OK(HandleExtensionOnDropReplication(catalog_manager, consumer_namespace_id));
  }
  return Status::OK();
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
  catalog_manager.GetXClusterManager()->SyncConsumerReplicationStatusMap(
      replication_group_id, producer_map);

  catalog_manager.GetXClusterManager()->RemoveTableConsumerStreams(
      replication_group_id, consumer_table_ids_to_remove);

  l.Commit();
  cl.Commit();

  return Status::OK();
}

bool HasNamespace(UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id) {
  auto l = universe.LockForRead();
  if (!l->IsDbScoped()) {
    return false;
  }

  const auto& namespace_infos = l->pb.db_scoped_info().namespace_infos();
  return std::any_of(
      namespace_infos.begin(), namespace_infos.end(),
      [&consumer_namespace_id](const auto& namespace_info) {
        return namespace_info.consumer_namespace_id() == consumer_namespace_id;
      });
}

Status DeleteUniverseReplication(
    UniverseReplicationInfo& universe, bool ignore_errors, bool skip_producer_stream_deletion,
    DeleteUniverseReplicationResponsePB* resp, CatalogManager& catalog_manager,
    const LeaderEpoch& epoch) {
  const auto& replication_group_id = universe.ReplicationGroupId();
  auto xcluster_manager = catalog_manager.GetXClusterManager();
  auto sys_catalog = catalog_manager.sys_catalog();

  {
    auto l = universe.LockForWrite();
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETING);
    Status s = sys_catalog->Upsert(epoch, &universe);
    RETURN_NOT_OK(
        CheckLeaderStatus(s, "Updating delete universe replication info into sys-catalog"));
    l.Commit();
  }

  auto l = universe.LockForWrite();
  l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED);

  // We can skip the deletion of individual streams for DB Scoped replication since deletion of the
  // outbound replication group will clean it up.
  if (l->IsDbScoped()) {
    skip_producer_stream_deletion = true;
  }

  // Delete subscribers on the Consumer Registry (removes from TServers).
  LOG(INFO) << "Deleting subscribers for producer " << replication_group_id;
  {
    auto cluster_config = catalog_manager.ClusterConfig();
    auto cl = cluster_config->LockForWrite();
    auto* consumer_registry = cl.mutable_data()->pb.mutable_consumer_registry();
    auto replication_group_map = consumer_registry->mutable_producer_map();
    auto it = replication_group_map->find(replication_group_id.ToString());
    if (it != replication_group_map->end()) {
      replication_group_map->erase(it);
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK(CheckStatus(
          sys_catalog->Upsert(epoch, cluster_config.get()),
          "updating cluster config in sys-catalog"));

      xcluster_manager->SyncConsumerReplicationStatusMap(
          replication_group_id, *replication_group_map);
      cl.Commit();
    }
  }

  // Delete CDC stream config on the Producer.
  if (!l->pb.table_streams().empty() && !skip_producer_stream_deletion) {
    auto result = universe.GetOrCreateXClusterRpcTasks(l->pb.producer_master_addresses());
    if (!result.ok()) {
      LOG(WARNING) << "Unable to create cdc rpc task. CDC streams won't be deleted: " << result;
    } else {
      auto xcluster_rpc = *result;
      std::vector<xrepl::StreamId> streams;
      std::unordered_map<xrepl::StreamId, TableId> stream_to_producer_table_id;
      for (const auto& [table_id, stream_id_str] : l->pb.table_streams()) {
        auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_id_str));
        streams.emplace_back(stream_id);
        stream_to_producer_table_id.emplace(stream_id, table_id);
      }

      DeleteCDCStreamResponsePB delete_cdc_stream_resp;
      // Set force_delete=true since we are deleting active xCluster streams.
      // Since we are deleting universe replication, we should be ok with
      // streams not existing on the other side, so we pass in ignore_errors
      bool ignore_missing_streams = false;
      auto s = xcluster_rpc->client()->DeleteCDCStream(
          streams, true, /* force_delete */
          true /* ignore_errors */, &delete_cdc_stream_resp);

      if (delete_cdc_stream_resp.not_found_stream_ids().size() > 0) {
        std::vector<std::string> missing_streams;
        missing_streams.reserve(delete_cdc_stream_resp.not_found_stream_ids().size());
        for (const auto& stream_id : delete_cdc_stream_resp.not_found_stream_ids()) {
          missing_streams.emplace_back(Format(
              "$0 (table_id: $1)", stream_id,
              stream_to_producer_table_id[VERIFY_RESULT(xrepl::StreamId::FromString(stream_id))]));
        }
        auto message =
            Format("Could not find the following streams: $0.", AsString(missing_streams));

        if (s.ok()) {
          // Returned but did not find some streams, so still need to warn the user about those.
          ignore_missing_streams = true;
          s = STATUS(NotFound, message);
        } else {
          s = s.CloneAndPrepend(message);
        }
      }
      RETURN_NOT_OK(ReturnErrorOrAddWarning(s, ignore_errors | ignore_missing_streams, resp));
    }
  }

  // For each namespace, also cleanup the DDL Replication extension if needed.
  if (l->IsAutomaticDdlMode()) {
    for (const auto& namespace_info : l->pb.db_scoped_info().namespace_infos()) {
      RETURN_NOT_OK(HandleExtensionOnDropReplication(
          catalog_manager, namespace_info.consumer_namespace_id()));
    }
  }

  if (PREDICT_FALSE(FLAGS_TEST_exit_unfinished_deleting)) {
    // Exit for testing services
    return Status::OK();
  }

  // Delete universe in the Universe Config.
  RETURN_NOT_OK(
      ReturnErrorOrAddWarning(sys_catalog->Delete(epoch, &universe), ignore_errors, resp));

  // Also update the mapping of consumer tables.
  for (const auto& table : universe.metadata().state().pb.validated_tables()) {
    xcluster_manager->RemoveTableConsumerStream(table.second, universe.ReplicationGroupId());
  }

  l.Commit();

  catalog_manager.RemoveUniverseReplicationFromMap(universe.ReplicationGroupId());

  LOG(INFO) << "Processed delete universe replication of " << universe.ToString();

  return Status::OK();
}

Result<uint32> AddHistoricalPackedSchemaForColocatedTable(
    UniverseReplicationInfo& universe, const NamespaceId& namespace_id,
    const TableId& parent_table_id, const ColocationId& colocation_id,
    const SchemaVersion& source_schema_version, const SchemaPB& schema,
    CatalogManager& catalog_manager, const LeaderEpoch& epoch) {
  auto l = universe.LockForWrite();
  auto sys_catalog = catalog_manager.sys_catalog();

  SCHECK(
      l->IsDbScoped() && l->IsAutomaticDdlMode(), IllegalState,
      "Replication group is not in automatic DDL mode");

  // Fetch the map for this colocation_id.
  auto& universe_pb = l.mutable_data()->pb;
  auto& db_scoped_info = *universe_pb.mutable_db_scoped_info();
  auto& target_namespace_infos = *db_scoped_info.mutable_target_namespace_infos();
  auto& historical_schema_packings =
      (*target_namespace_infos[namespace_id]
            .mutable_colocated_historical_schema_packings())[colocation_id];

  // Go through the packed schemas and check if we already have a compatible schema.
  Schema new_schema;
  RETURN_NOT_OK(SchemaFromPB(schema, &new_schema));

  dockv::SchemaPackingStorage old_packings(
      TableType::PGSQL_TABLE_TYPE, std::make_shared<dockv::SchemaPackingRegistry>("XCluster: "));
  RETURN_NOT_OK(old_packings.LoadFromPB(historical_schema_packings.old_schema_packings()));
  const auto existing_version =
      old_packings.GetSchemaPackingVersion(TableType::PGSQL_TABLE_TYPE, new_schema);
  if (existing_version.ok()) {
    LOG(INFO) << "Found compatible schema for colocation_id: " << colocation_id
              << ", parent_table_id: " << parent_table_id
              << ", schema_version: " << existing_version
              << ", source_schema_version: " << source_schema_version
              << ", schema: " << new_schema.ToString();
    return existing_version;
  }

  // Add the new schema to the packed schemas.
  auto new_schema_version =
      static_cast<uint>(historical_schema_packings.old_schema_packings_size());
  dockv::SchemaPacking new_packing(TableType::PGSQL_TABLE_TYPE, new_schema);
  dockv::SchemaPackingPB new_packing_pb;
  new_packing_pb.set_schema_version(new_schema_version);
  new_packing.ToPB(&new_packing_pb);
  historical_schema_packings.add_old_schema_packings()->CopyFrom(new_packing_pb);

  LOG(INFO) << "Added historical packed schema for colocation_id: " << colocation_id
            << ", parent_table_id: " << parent_table_id
            << ", schema_version: " << new_schema_version
            << ", source_schema_version: " << source_schema_version
            << ", schema: " << new_schema.ToString();

  RETURN_NOT_OK_PREPEND(
      sys_catalog->Upsert(epoch.leader_term, &universe),
      "Updating replication info in sys-catalog");
  l.Commit();
  return new_schema_version;
}

Status UpdateColocatedTableWithHistoricalSchemaPackings(
    UniverseReplicationInfo& universe, SysTablesEntryPB& table_pb, const TableId& table_id,
    const NamespaceId& namespace_id, const ColocationId colocation_id) {
  auto l = universe.LockForRead();
  auto* colocation_id_map =
      FindOrNull(l->pb.db_scoped_info().target_namespace_infos(), namespace_id);
  SCHECK(
      colocation_id_map, NotFound,
      Format("Colocation ID map not found for namespace $0", namespace_id));
  auto* historical_packed_schemas =
      FindOrNull(colocation_id_map->colocated_historical_schema_packings(), colocation_id);
  SCHECK(
      historical_packed_schemas, NotFound,
      Format("Historical packed schemas not found for colocation ID $0", colocation_id));
  const auto& old_schema_packings = historical_packed_schemas->old_schema_packings();

  // If we only have one schema packing then we can keep the existing schema.
  if (old_schema_packings.size() <= 1) {
    LOG(INFO) << old_schema_packings.size() << " old schema packings found for table " << table_id
              << " (colocation_id " << colocation_id << ") in xCluster replication group "
              << universe.id() << ". No need to update the table schema.";
    return Status::OK();
  }

  auto last_schema_version =
      old_schema_packings.Get(old_schema_packings.size() - 1).schema_version();

  // Update the TableInfo with the old schema packings and update its schema version.
  table_pb.mutable_xcluster_table_info()
      ->mutable_xcluster_colocated_old_schema_packings()
      ->CopyFrom(old_schema_packings);
  table_pb.set_version(last_schema_version + 1);
  LOG(INFO) << Format(
      "Updated table $0 (colocation_id $1) with old schema packings and schema version $2 for "
      "xCluster replication group $3",
      table_id, colocation_id, last_schema_version + 1, universe.id());
  return Status::OK();
}

Status CleanupColocatedTableHistoricalSchemaPackings(
    UniverseReplicationInfo& universe, const NamespaceId& namespace_id,
    const ColocationId& colocation_id, CatalogManager& catalog_manager, const LeaderEpoch& epoch) {
  auto l = universe.LockForWrite();
  auto sys_catalog = catalog_manager.sys_catalog();

  SCHECK(
      l->IsDbScoped() && l->IsAutomaticDdlMode(), IllegalState,
      "Replication group is not in automatic DDL mode");

  auto& universe_pb = l.mutable_data()->pb;
  auto& db_scoped_info = *universe_pb.mutable_db_scoped_info();
  auto& target_namespace_infos = *db_scoped_info.mutable_target_namespace_infos();
  auto& historical_schema_packings =
      *target_namespace_infos[namespace_id].mutable_colocated_historical_schema_packings();

  // TODO(jhe) Need to handle cleanup of this field in cases where the colocated table create fails
  // on the source (#25913).
  bool erased = historical_schema_packings.erase(colocation_id);
  if (!erased) {
    LOG(INFO) << "No historical schema packings found for colocation_id: " << colocation_id;
    return Status::OK();
  }

  RETURN_NOT_OK_PREPEND(
      sys_catalog->Upsert(epoch.leader_term, &universe),
      "Updating replication info in sys-catalog");
  l.Commit();
  return Status::OK();
}

}  // namespace yb::master
