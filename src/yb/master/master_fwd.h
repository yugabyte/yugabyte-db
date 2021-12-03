// Copyright (c) YugaByte, Inc.
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

#ifndef YB_MASTER_MASTER_FWD_H
#define YB_MASTER_MASTER_FWD_H

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional.hpp>

#include "yb/common/entity_ids_types.h"

#include "yb/gutil/ref_counted.h"
#include "yb/util/enums.h"
#include "yb/util/monotime.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class HostPort;
struct HostPortHash;

namespace master {

class TSDescriptor;
typedef std::shared_ptr<TSDescriptor> TSDescriptorPtr;
typedef std::vector<TSDescriptorPtr> TSDescriptorVector;

class EncryptionManager;

class AddUniverseKeysRequestPB;
class AddUniverseKeysResponsePB;
class AlterNamespaceRequestPB;
class AlterNamespaceResponsePB;
class AlterTableRequestPB;
class BlacklistPB;
class CatalogManager;
class CatalogManagerIf;
class CatalogManagerBgTasks;
class CDCConsumerSplitDriverIf;
class ChangeEncryptionInfoRequestPB;
class ChangeEncryptionInfoResponsePB;
class ChangeMasterClusterConfigRequestPB;
class ChangeMasterClusterConfigResponsePB;
class ClusterConfigInfo;
class ClusterLoadBalancer;
class CreateCDCStreamRequestPB;
class CreateNamespaceRequestPB;
class CreateNamespaceResponsePB;
class CreateSnapshotScheduleRequestPB;
class CreateTableRequestPB;
class CreateTableResponsePB;
class DdlLogEntryPB;
class DeleteCDCStreamResponsePB;
class EncryptionInfoPB;
class FlushManager;
class FlushTablesRequestPB;
class FlushTablesResponsePB;
class GetCDCStreamResponsePB;
class GetMasterClusterConfigRequestPB;
class GetMasterClusterConfigResponsePB;
class GetNamespaceInfoResponsePB;
class GetPermissionsResponsePB;
class GetTableLocationsRequestPB;
class GetTableLocationsResponsePB;
class GetTableSchemaRequestPB;
class GetTableSchemaResponsePB;
class GetUniverseKeyRegistryRequestPB;
class GetUniverseKeyRegistryResponsePB;
class GetUniverseReplicationResponsePB;
class HasUniverseKeyInMemoryRequestPB;
class HasUniverseKeyInMemoryResponsePB;
class IsCreateTableDoneRequestPB;
class IsCreateTableDoneResponsePB;
class IsDeleteNamespaceDoneRequestPB;
class IsEncryptionEnabledRequestPB;
class IsEncryptionEnabledResponsePB;
class IsFlushTablesDoneRequestPB;
class IsFlushTablesDoneResponsePB;
class IsInitDbDoneRequestPB;
class IsInitDbDoneResponsePB;
class IsLoadBalancedRequestPB;
class IsLoadBalancedResponsePB;
class ListCDCStreamsRequestPB;
class ListCDCStreamsResponsePB;
class ListNamespacesResponsePB;
class ListSnapshotRestorationsResponsePB;
class ListSnapshotSchedulesResponsePB;
class ListSnapshotsResponsePB;
class ListTablegroupsRequestPB;
class ListTablegroupsResponsePB;
class ListTablesRequestPB;
class ListTablesResponsePB;
class Master;
class MasterErrorPB;
class MasterOptions;
class MasterPathHandlers;
class MasterServiceProxy;
class NamespaceIdentifierPB;
class NamespaceInfo;
class PermissionsManager;
class PlacementInfoPB;
class ReplicationInfoPB;
class ReportedTabletPB;
class RetryingTSRpcTask;
class RolePermissionInfoPB;
class SnapshotCoordinatorContext;
class SnapshotScheduleFilterPB;
class SnapshotState;
class SysCDCStreamEntryPB;
class SysCatalogTable;
class SysClusterConfigEntryPB;
class SysConfigInfo;
class SysNamespaceEntryPB;
class SysRowEntries;
class SysSnapshotEntryPB;
class SysTablesEntryPB;
class SysTabletsEntryPB;
class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;
class TSInfoPB;
class TSInformationPB;
class TSManager;
class TSRegistrationPB;
class TSSnapshotSchedulesInfoPB;
class TSSnapshotsInfoPB;
class TableIdentifierPB;
class TablegroupIdentifierPB;
class TabletLocationsPB;
class TabletReportPB;
class TabletReportUpdatesPB;
class TabletSplitCompleteHandlerIf;
class UDTypeInfo;
class YQLVirtualTable;
class YsqlTablespaceManager;
class YsqlTransactionDdl;

struct SplitTabletIds;
struct TableDescription;
struct TabletReplica;
struct TabletReplicaDriveInfo;

class AsyncTabletSnapshotOp;
using AsyncTabletSnapshotOpPtr = std::shared_ptr<AsyncTabletSnapshotOp>;

class TableInfo;
using TableInfoPtr = scoped_refptr<TableInfo>;
using TableInfoMap = std::map<TableId, TableInfoPtr>;

class TabletInfo;
using TabletInfoPtr = scoped_refptr<TabletInfo>;
using TabletInfos = std::vector<TabletInfoPtr>;

struct SnapshotScheduleRestoration;
using SnapshotScheduleRestorationPtr = std::shared_ptr<SnapshotScheduleRestoration>;

YB_STRONGLY_TYPED_BOOL(RegisteredThroughHeartbeat);

YB_STRONGLY_TYPED_BOOL(IncludeInactive);

YB_DEFINE_ENUM(
    CollectFlag, (kAddIndexes)(kIncludeParentColocatedTable)(kSucceedIfCreateInProgress));
using CollectFlags = EnumBitSet<CollectFlag>;

using TableToTablespaceIdMap = std::unordered_map<TableId, boost::optional<TablespaceId>>;
using TablespaceIdToReplicationInfoMap = std::unordered_map<
    TablespaceId, boost::optional<ReplicationInfoPB>>;

using LeaderStepDownFailureTimes = std::unordered_map<TabletServerId, MonoTime>;
using TabletReplicaMap = std::unordered_map<std::string, TabletReplica>;
using TabletToTabletServerMap = std::unordered_map<TabletId, TabletServerId>;
using TabletInfoMap = std::map<TabletId, scoped_refptr<TabletInfo>>;
using BlacklistSet = std::unordered_set<HostPort, HostPortHash>;

namespace enterprise {

class CatalogManager;

} // namespace enterprise

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_FWD_H
