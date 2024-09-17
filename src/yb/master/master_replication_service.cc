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

#include "yb/master/catalog_manager.h"
#include "yb/master/master_replication.service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/xcluster/xcluster_manager.h"

namespace yb {
namespace master {

namespace {

class MasterReplicationServiceImpl : public MasterServiceBase, public MasterReplicationIf {
 public:
  explicit MasterReplicationServiceImpl(Master* master)
      : MasterServiceBase(master), MasterReplicationIf(master->metric_entity()) {}

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    CatalogManager,
    (ValidateReplicationInfo)
    (CreateCDCStream)
    (DeleteCDCStream)
    (GetCDCStream)
    (GetUniverseReplication)
    (GetUDTypeMetadata)
    (UpdateConsumerOnProducerSplit)
    (UpdateConsumerOnProducerMetadata)
    (ListCDCStreams)
    (IsObjectPartOfXRepl)
    (SetUniverseReplicationEnabled)
    (UpdateCDCStream)
    (GetCDCDBStreamInfo)
    (IsBootstrapRequired)
    (WaitForReplicationDrain)
    (GetTableSchemaFromSysCatalog)
    (ChangeXClusterRole)
    (BootstrapProducer)
    (YsqlBackfillReplicationSlotNameToCDCSDKStream)
    (DisableDynamicTableAdditionOnCDCSDKStream)
    (RemoveUserTableFromCDCSDKStream)
    (ValidateAndSyncCDCStateEntriesForCDCSDKStream)
    (RemoveTablesFromCDCSDKStream)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      XClusterManager,
      (GetXClusterSafeTime)
      (GetXClusterSafeTimeForNamespace)
      (PauseResumeXClusterProducerStreams)
      (XClusterCreateOutboundReplicationGroup)
      (XClusterAddNamespaceToOutboundReplicationGroup)
      (XClusterRemoveNamespaceFromOutboundReplicationGroup)
      (XClusterDeleteOutboundReplicationGroup)
      (IsXClusterBootstrapRequired)
      (GetXClusterStreams)
      (CreateXClusterReplication)
      (IsCreateXClusterReplicationDone)
      (AddNamespaceToXClusterReplication)
      (IsAlterXClusterReplicationDone)
      (RepairOutboundXClusterReplicationGroupAddTable)
      (RepairOutboundXClusterReplicationGroupRemoveTable)
      (GetXClusterOutboundReplicationGroups)
      (GetXClusterOutboundReplicationGroupInfo)
      (GetUniverseReplications)
      (GetUniverseReplicationInfo)
      (GetReplicationStatus)
      (XClusterReportNewAutoFlagConfigVersion)
      (SetupUniverseReplication)
      (IsSetupUniverseReplicationDone)
      (SetupNamespaceReplicationWithBootstrap)
      (IsSetupNamespaceReplicationWithBootstrapDone)
      (AlterUniverseReplication)
      (DeleteUniverseReplication)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterReplicationService(Master* master) {
  return std::make_unique<MasterReplicationServiceImpl>(master);
}

} // namespace master
} // namespace yb
