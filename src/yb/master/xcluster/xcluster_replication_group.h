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

#include "yb/gutil/integral_types.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/xcluster_consumer_registry_service.h"
#include "yb/util/status_fwd.h"

namespace yb {

class IsOperationDoneResult;
class SysCatalogTable;

namespace client {
class XClusterRemoteClientHolder;
}  // namespace client

namespace master {

// TODO: #19714 Create XClusterReplicationGroup, a wrapper over UniverseReplicationInfo, that will
// manage the ReplicationGroup and its ProducerEntryPB in ClusterConfigInfo.

// Returns nullopt when source universe does not support AutoFlags compatibility check.
// Returns a pair with a bool which indicates if the configs are compatible and the source universe
// AutoFlags config version.
Result<std::optional<std::pair<bool, uint32>>> ValidateAutoFlagsConfig(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config);

Result<std::optional<std::pair<bool, uint32>>> ValidateAutoFlagsConfig(
    client::XClusterRemoteClientHolder& xcluster_client, const AutoFlagsConfigPB& local_config);

// Reruns the AutoFlags compatiblity validation when source universe AutoFlags config version has
// changed.
// Compatiblity validation is needed only if the requested_auto_flag_version is greater than the
// previous validated target config version. get_local_auto_flags_config_func is run only if the
// compatiblity validation is needed. If replication_info, or cluster_config are updated, they are
// stored in the sys_catalog.
Status RefreshAutoFlagConfigVersion(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    ClusterConfigInfo& cluster_config, uint32 requested_auto_flag_version,
    std::function<AutoFlagsConfigPB()> get_local_auto_flags_config_func, const LeaderEpoch& epoch);

// Reruns the AutoFlags compatiblity validation when target (local) universe AutoFlags config
// version has changed.
// Compatiblity validation is needed only if the validated_local_auto_flags_config_version is less
// than the current local config version. If replication_info, or cluster_config are updated, they
// are stored in the sys_catalog.
Status HandleLocalAutoFlagsConfigChange(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    ClusterConfigInfo& cluster_config, const AutoFlagsConfigPB& local_auto_flags_config,
    const LeaderEpoch& epoch);

// Check if the table should be added to the replication group. Returns false if the table is
// already part of the group.
Result<bool> ShouldAddTableToReplicationGroup(
    UniverseReplicationInfo& universe, const TableInfo& table_info,
    CatalogManager& catalog_manager);

// Check if the table is part of the replication group.
Result<bool> HasTable(
    UniverseReplicationInfo& universe, const TableInfo& table_info,
    CatalogManager& catalog_manager);

Result<NamespaceId> GetProducerNamespaceId(
    UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id);

bool IncludesConsumerNamespace(
    UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id);

Result<std::shared_ptr<client::XClusterRemoteClientHolder>> GetXClusterRemoteClientHolder(
    UniverseReplicationInfo& universe);

Result<TableId> GetConsumerTableIdForStreamId(
    CatalogManager& catalog_manager, const xcluster::ReplicationGroupId& replication_group_id,
    const xrepl::StreamId& stream_id);

struct LockedConfigAndTableKeyRanges {
  std::shared_ptr<ClusterConfigInfo> cluster_config;
  ClusterConfigInfo::WriteLock write_lock;
  std::map<std::string, KeyRange> table_key_ranges;

  LockedConfigAndTableKeyRanges(
      std::shared_ptr<ClusterConfigInfo> cc, ClusterConfigInfo::WriteLock&& wl,
      std::map<std::string, KeyRange>&& ranges)
      : cluster_config(std::move(cc)),
        write_lock(std::move(wl)),
        table_key_ranges(std::move(ranges)) {}
};

Result<LockedConfigAndTableKeyRanges> LockClusterConfigAndGetTableKeyRanges(
    CatalogManager& catalog_manager, const TableId& consumer_table_id);

// Returns (false, Status::OK()) if the universe setup is still in progress.
// Returns (true, status) if the setup completed. status is set to OK if it completed successfully,
// or the error that caused it to fail.
Result<IsOperationDoneResult> IsSetupUniverseReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id, CatalogManager& catalog_manager);

Status RemoveTablesFromReplicationGroup(
    scoped_refptr<UniverseReplicationInfo> universe, const std::vector<TableId>& producer_table_ids,
    CatalogManager& catalog_manager, const LeaderEpoch& epoch);

Status RemoveTablesFromReplicationGroupInternal(
    UniverseReplicationInfo& universe, UniverseReplicationInfo::WriteLock& l,
    const std::vector<TableId>& producer_table_ids, CatalogManager& catalog_manager,
    const LeaderEpoch& epoch, bool cleanup_source_streams);

Status RemoveNamespaceFromReplicationGroup(
    scoped_refptr<UniverseReplicationInfo> universe, const NamespaceId& producer_namespace_id,
    CatalogManager& catalog_manager, const LeaderEpoch& epoch);

// Returns true if the namespace is part of the DB Scoped replication group.
bool HasNamespace(UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id);

Status DeleteUniverseReplication(
    UniverseReplicationInfo& universe, bool ignore_errors, bool skip_producer_stream_deletion,
    DeleteUniverseReplicationResponsePB* resp, CatalogManager& catalog_manager,
    const LeaderEpoch& epoch);

// Checks for existing compatible historical schema packings, otherwise adds a new one.
// Returns the last compatible target schema version.
Result<uint32> AddHistoricalPackedSchemaForColocatedTable(
    UniverseReplicationInfo& universe, const NamespaceId& namespace_id,
    const TableId& parent_table_id, const ColocationId& colocation_id,
    const SchemaVersion& source_schema_version, const SchemaPB& schema,
    CatalogManager& catalog_manager, const LeaderEpoch& epoch);

// Gets compatible historical schema packings for the table and updates the table info.
// Note that this may also bump up the starting schema version of this table (to be +1 of the
// historical schema packings).
Status UpdateColocatedTableWithHistoricalSchemaPackings(
    UniverseReplicationInfo& universe, SysTablesEntryPB& table_pb, const TableId& table_id,
    const NamespaceId& namespace_id, const ColocationId colocation_id);

Status CleanupColocatedTableHistoricalSchemaPackings(
    UniverseReplicationInfo& universe, const NamespaceId& namespace_id,
    const ColocationId& colocation_id, CatalogManager& catalog_manager, const LeaderEpoch& epoch);

}  // namespace master
}  // namespace yb
