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

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/common/entity_ids_types.h"

#include "yb/gutil/ref_counted.h"

#include "yb/master/master_backup.fwd.h"
#include "yb/master/master_replication.fwd.h"
#include "yb/master/tablet_split_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class HostPort;
struct HostPortHash;

namespace master {

class TSDescriptor;
using TSDescriptorPtr = std::shared_ptr<TSDescriptor>;
using TSDescriptorVector = std::vector<TSDescriptorPtr>;

class EncryptionManager;

class AsyncAddServerTask;
class AsyncDeleteReplica;
class AsyncRemoveServerTask;
class AsyncTryStepDown;
class CatalogManager;
class CatalogManagerIf;
class CatalogManagerBgTasks;
class CdcsdkManager;
class CdcsdkManagerIf;
class CloneStateManager;
class XClusterRpcTasks;
class ClusterConfigInfo;
class ClusterLoadBalancer;
class FlushManager;
class Master;
class MasterBackupProxy;
class MasterOptions;
class MasterPathHandlers;
class MasterAdminProxy;
class MasterClientProxy;
class MasterClusterProxy;
class MasterDclProxy;
class MasterDdlProxy;
class MasterEncryptionProxy;
class MasterHeartbeatProxy;
class MasterReplicationProxy;
class MasterSnapshotCoordinator;
class MasterTestProxy;
class NamespaceInfo;
class ObjectLockInfoManager;
class PermissionsManager;
class RetryingTSRpcTask;
class RetryingTSRpcTaskWithTable;
class RetrySpecificTSRpcTask;
class RetrySpecificTSRpcTaskWithTable;
class SnapshotCoordinatorContext;
class SnapshotState;
class SysCatalogTable;
class SysCatalogWriter;
class SysConfigInfo;
class SysRowEntries;
class SystemTablet;
class TablegroupInfo;
class TestAsyncRpcManager;
class TSDescriptor;
class TSManager;
class UDTypeInfo;
class XClusterManager;
class XClusterManagerIf;
class YQLPartitionsVTable;
class YQLVirtualTable;
class YsqlBackendsManager;
class YsqlManager;
class YsqlManagerIf;
class YsqlTablegroupManager;
class YsqlTablespaceManager;
class YsqlTransactionDdl;
class MasterClusterHandler;

struct XClusterConsumerStreamInfo;
struct PgTableReadData;
struct TableDescription;
struct TabletReplica;
struct TabletReplicaDriveInfo;

class AsyncTabletSnapshotOp;
using AsyncTabletSnapshotOpPtr = std::shared_ptr<AsyncTabletSnapshotOp>;

class CloneStateInfo;
using CloneStateInfoPtr = std::shared_ptr<CloneStateInfo>;

class NamespaceInfo;
using NamespaceInfoPtr = scoped_refptr<NamespaceInfo>;

class TableInfo;
using TableInfoPtr = scoped_refptr<TableInfo>;

class TabletInfo;
using TabletInfoPtr = std::shared_ptr<TabletInfo>;
using TabletInfos = std::vector<TabletInfoPtr>;

struct SnapshotScheduleRestoration;
using SnapshotScheduleRestorationPtr = std::shared_ptr<SnapshotScheduleRestoration>;

YB_STRONGLY_TYPED_BOOL(RegisteredThroughHeartbeat);

// Used to indicate whether inactive tablets should be included in Rpcs such as GetTableLocations.
// Inactive tablets are parents of split children, that may no longer be allowed to
// server user read-write requests. Inactive tablets could be hidden.
// Since include_inactive allows both parent and child tablets to be returned, the returned tablet
// list could have overlapping partition key ranges.
YB_STRONGLY_TYPED_BOOL(IncludeInactive);

// Used to indicate whether hidden tables/tablets should be included.
YB_STRONGLY_TYPED_BOOL(IncludeHidden);

YB_STRONGLY_TYPED_BOOL(IncludeDeleted);
YB_STRONGLY_TYPED_BOOL(IsSystemObject);


YB_DEFINE_ENUM(
    CollectFlag,
    (kAddIndexes)(kIncludeParentColocatedTable)(kSucceedIfCreateInProgress)(kAddUDTypes)
    (kIncludeHiddenTables));

using CollectFlags = EnumBitSet<CollectFlag>;

using TableToTablespaceIdMap = std::unordered_map<TableId, std::optional<TablespaceId>>;
using TablespaceIdToReplicationInfoMap =
    std::unordered_map<TablespaceId, std::optional<ReplicationInfoPB>>;

using LeaderStepDownFailureTimes = std::unordered_map<TabletServerId, MonoTime>;
using TabletReplicaMap = std::unordered_map<TabletServerId, TabletReplica>;
using TabletToTabletServerMap = std::unordered_map<TabletId, TabletServerId>;
using TabletInfoMap = std::map<TabletId, TabletInfoPtr, std::less<>>;
struct cloud_hash;
struct cloud_equal_to;
using AffinitizedZonesSet = std::unordered_set<CloudInfoPB, cloud_hash, cloud_equal_to>;
using BlacklistSet = std::unordered_set<HostPort, HostPortHash>;
using RetryingTSRpcTaskWithTablePtr = std::shared_ptr<RetryingTSRpcTaskWithTable>;

// Use ordered map to make computing fingerprint of the map easier.
struct PgCatalogVersion;
using DbOidToCatalogVersionMap = std::map<uint32_t, PgCatalogVersion>;

// This map represents pg_yb_invalidation_messages: (db_oid, current_version) => inval messages.
// If the message value is nullopt, it means a SQL null value. If it is empty string, it means
// there is no invalidation messages associated with this (db_oid, current_version). A PG
// backend needs to do a catalog cache refresh on a SQL null value, but treats an empty string
// as a noop because there is nothing in the catalog cache is invalidated.
using DbOidVersionToMessageListMap = std::map<std::pair<uint32_t, uint64_t>,
                                              std::optional<std::string>>;

using RelIdToAttributesMap = std::unordered_map<uint32_t, std::vector<PgAttributePB>>;
using RelTypeOIDMap = std::unordered_map<uint32_t, uint32_t>;
class CatalogManager;

} // namespace master
} // namespace yb
