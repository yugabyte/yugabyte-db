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
#include "yb/util/status_fwd.h"

namespace yb {

class IsOperationDoneResult;
class SysCatalogTable;

namespace client {
class XClusterRemoteClient;
}  // namespace client

namespace master {

// TODO: #19714 Create XClusterReplicationGroup, a wrapper over UniverseReplicationInfo, that will
// manage the ReplicationGroup and its ProducerEntryPB in ClusterConfigInfo.

// Check if the local AutoFlags config is compatible with the source universe and returns the source
// universe AutoFlags config version if they are compatible.
// If they are not compatible, returns a bad status.
// If the source universe is running an older version which does not support AutoFlags compatiblity
// check, returns an invalid AutoFlags config version.
Result<uint32> GetAutoFlagConfigVersionIfCompatible(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config);

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

Result<NamespaceId> GetProducerNamespaceId(
    UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id);

bool IncludesConsumerNamespace(
    UniverseReplicationInfo& universe, const NamespaceId& consumer_namespace_id);

Result<std::shared_ptr<client::XClusterRemoteClient>> GetXClusterRemoteClient(
    UniverseReplicationInfo& universe);

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

Status ValidateTableListForDbScopedReplication(
    UniverseReplicationInfo& universe, const std::vector<NamespaceId>& namespace_ids,
    const std::set<TableId>& replicated_table_ids, const CatalogManager& catalog_manager);

}  // namespace master
}  // namespace yb
