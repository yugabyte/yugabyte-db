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

#include <memory>
#include <vector>

#include "yb/gutil/ref_counted.h"
#include "yb/util/enums.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace master {

class TSDescriptor;
typedef std::shared_ptr<TSDescriptor> TSDescriptorPtr;
typedef std::vector<TSDescriptorPtr> TSDescriptorVector;

class EncryptionManager;

class AddUniverseKeysRequestPB;
class AddUniverseKeysResponsePB;
class ChangeEncryptionInfoRequestPB;
class ChangeEncryptionInfoResponsePB;
class CreateSnapshotScheduleRequestPB;
class EncryptionInfoPB;
class GetUniverseKeyRegistryRequestPB;
class GetUniverseKeyRegistryResponsePB;
class HasUniverseKeyInMemoryRequestPB;
class HasUniverseKeyInMemoryResponsePB;
class IsEncryptionEnabledRequestPB;
class IsEncryptionEnabledResponsePB;
class ListSnapshotRestorationsResponsePB;
class ListSnapshotSchedulesResponsePB;
class ListSnapshotsResponsePB;
class PermissionsManager;
class ReportedTabletPB;
class SnapshotCoordinatorContext;
class SnapshotScheduleFilterPB;
class SnapshotState;
class SysRowEntries;
class SysSnapshotEntryPB;
class SysTablesEntryPB;
class SysTabletsEntryPB;
class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;
class TSRegistrationPB;
class TSSnapshotSchedulesInfoPB;
class TabletInfo;
class TabletReportPB;

typedef scoped_refptr<TabletInfo> TabletInfoPtr;
typedef std::vector<TabletInfoPtr> TabletInfos;

struct SnapshotScheduleRestoration;
using SnapshotScheduleRestorationPtr = std::shared_ptr<SnapshotScheduleRestoration>;

YB_STRONGLY_TYPED_BOOL(RegisteredThroughHeartbeat);

YB_DEFINE_ENUM(
    CollectFlag, (kAddIndexes)(kIncludeParentColocatedTable)(kSucceedIfCreateInProgress));
using CollectFlags = EnumBitSet<CollectFlag>;

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_FWD_H
