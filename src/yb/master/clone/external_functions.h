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

#pragma once

#include <memory>
#include <unordered_map>

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/clone/clone_state_entity.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/rpc/rpc_context.h"

namespace yb {
namespace master {

class CloneStateManagerExternalFunctionsBase {
 public:
  virtual ~CloneStateManagerExternalFunctionsBase() {}

  // Snapshot coordinator.
  virtual Status ListSnapshotSchedules(ListSnapshotSchedulesResponsePB* resp) = 0;
  virtual Status DeleteSnapshot(const TxnSnapshotId& snapshot_id) = 0;
  virtual Result<TxnSnapshotRestorationId> Restore(const TxnSnapshotId&, HybridTime) = 0;
  virtual Status ListRestorations(
      const TxnSnapshotRestorationId&, ListSnapshotRestorationsResponsePB*) = 0;

  // Catalog manager.
  virtual Result<TabletInfoPtr> GetTabletInfo(const TabletId&) = 0;

  virtual Result<NamespaceInfoPtr> FindNamespace(const NamespaceIdentifierPB&) = 0;

  virtual Status ScheduleCloneTabletCall(
      const TabletInfoPtr&, LeaderEpoch, tablet::CloneTabletRequestPB) = 0;

  virtual Status ScheduleClonePgSchemaTask(
      const std::string& permanent_uuid,
      const std::string& source_db_name,
      const std::string& target_db_name,
      const std::string& source_owner,
      const std::string& target_owner,
      HybridTime restore_ht,
      AsyncClonePgSchema::ClonePgSchemaCallbackType callback,
      MonoTime deadline) = 0;

  virtual Status ScheduleEnableDbConnectionsTask(
      const std::string& permanent_uuid, const std::string& target_db_name,
      AsyncEnableDbConns::EnableDbConnsCallbackType callback) = 0;

  virtual Status DoCreateSnapshot(
      const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp,
      CoarseTimePoint deadline, const LeaderEpoch& epoch) = 0;

  virtual Result<std::pair<SnapshotInfoPB, std::unordered_set<TabletId>>>
      GenerateSnapshotInfoFromScheduleForClone(
      const SnapshotScheduleId& snapshot_schedule_id, HybridTime export_time,
      CoarseTimePoint deadline) = 0;

  virtual Status DoImportSnapshotMeta(
    const SnapshotInfoPB& snapshot_pb, const LeaderEpoch& epoch,
    const std::optional<std::string>& clone_target_namespace_name, NamespaceMap* namespace_map,
    UDTypeMap* type_map, ExternalTableSnapshotDataMap* tables_data,
    CoarseTimePoint deadline) = 0;

  virtual TSDescriptorPtr PickTserver() = 0;

  // Sys catalog.
  virtual Status Upsert(int64_t leader_term, const CloneStateInfoPtr&) = 0;
  virtual Status Load(
      const std::string& type,
      std::function<Status(const std::string&, const SysCloneStatePB&)> inserter) = 0;
};

} // namespace master
} // namespace yb
