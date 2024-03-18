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
#include "yb/master/master_fwd.h"
#include "yb/master/ts_descriptor.h"

namespace yb {
namespace master {

class CloneStateManager {
  friend class CloneStateManagerTest;

 public:
  static std::unique_ptr<CloneStateManager> Create(
      CatalogManagerIf* catalog_manager, Master* master, SysCatalogTable* sys_catalog);

  Status Run();

  Status IsCloneDone(
      const IsCloneDoneRequestPB* req,
      IsCloneDoneResponsePB* resp);

  Status CloneNamespace(
      const CloneNamespaceRequestPB* req,
      CloneNamespaceResponsePB* resp,
      rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Result<CloneStateInfoPtr> CreateCloneState(
      uint32_t seq_no, const scoped_refptr<NamespaceInfo>& source_namespace,
      const HybridTime& restore_time);

  Result<CloneStateInfoPtr> CreateCloneState(
      uint32_t seq_no, const NamespaceId& source_namespace_id,
      const TxnSnapshotId& source_snapshot_id, const TxnSnapshotId& target_snapshot_id,
      const HybridTime& restore_time, const ExternalTableSnapshotDataMap& table_snapshot_data);

  Status UpdateCloneStateWithSnapshotInfo(
      CloneStateInfoPtr clone_state, const TxnSnapshotId& source_snapshot_id,
      const TxnSnapshotId& target_snapshot_id,
      const ExternalTableSnapshotDataMap& table_snapshot_data);

  Status ClearAndRunLoaders();

 private:
  struct ExternalFunctions {
    // Snapshot coordinator.
    const std::function<Status(ListSnapshotSchedulesResponsePB* resp)> ListSnapshotSchedules;
    const std::function<Result<TxnSnapshotRestorationId>(const TxnSnapshotId&, HybridTime)> Restore;
    const std::function<Status(
        const TxnSnapshotId&, ListSnapshotRestorationsResponsePB*)> ListRestorations;

    // Catalog manager.
    const std::function<Result<TabletInfoPtr>(const TabletId&)> GetTabletInfo;

    const std::function<Result<NamespaceInfoPtr>(const NamespaceIdentifierPB&)> FindNamespace;

    const std::function<Status(const TabletInfoPtr&, LeaderEpoch, tablet::CloneTabletRequestPB)>
        ScheduleCloneTabletCall;

    const std::function<Status(
        const std::string& permanent_uuid, const std::string& source_db_name,
        const std::string& target_db_name, HybridTime restore_ht,
        AsyncClonePgSchema::ClonePgSchemaCallbackType callback, MonoTime deadline)>
        ScheduleClonePGSchemaTask;

    const std::function<Status(
        const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp,
        CoarseTimePoint deadline, const LeaderEpoch& epoch)> DoCreateSnapshot;

    const std::function<Result<SnapshotInfoPB>(
      const SnapshotScheduleId& snapshot_schedule_id, HybridTime export_time,
      CoarseTimePoint deadline)> GenerateSnapshotInfoFromSchedule;

    const std::function<Status(
      const SnapshotInfoPB& snapshot_pb, const LeaderEpoch& epoch,
      const std::optional<std::string>& clone_target_namespace_name, NamespaceMap* namespace_map,
      UDTypeMap* type_map, ExternalTableSnapshotDataMap* tables_data,
      CoarseTimePoint deadline)> DoImportSnapshotMeta;

    const std::function<TSDescriptorPtr()> PickTserver;

    // Sys catalog.
    const std::function<Status(const CloneStateInfoPtr&)> Upsert;
    const std::function<Status(
        const std::string& type,
        std::function<Status(const std::string&, const SysCloneStatePB&)> inserter)> Load;
  };

  explicit CloneStateManager(ExternalFunctions external_functions);

  Result<std::pair<NamespaceId, uint32_t>> CloneNamespace(
    const NamespaceIdentifierPB& source_namespace,
    const HybridTime& read_time,
    const std::string& target_namespace_name,
    CoarseTimePoint deadline,
    const LeaderEpoch& epoch);

  // Create PG schema objects of the clone database.
  Status ClonePgSchemaObjects(
      CloneStateInfoPtr clone_state, const std::string& source_db_name,
      const std::string& target_db_name, const SnapshotScheduleId& snapshot_schedule_id,
      const HybridTime& restore_time, const LeaderEpoch& epoch);

  // Transition clone state according to ClonePGSchema async task response then StartTabletsCloning.
  Status StartTabletsCloningYsql(
      CloneStateInfoPtr clone_state, const SnapshotScheduleId& snapshot_schedule_id,
      const HybridTime& restore_time, const std::string& target_namespace_name,
      CoarseTimePoint deadline, const LeaderEpoch& epoch, Status pg_schema_cloning_status);

  // Starts snapshot related operations for clone (mainly generate snapshotInfoPB as of
  // restore_time and then import it and create a new snapshot for target_namespace). Then it
  // schedules async clone tasks for every tablet. The function is the whole clone process in case
  // of YCQL and the second part of the clone process in case of YSQL.
  Status StartTabletsCloning(
      CloneStateInfoPtr clone_state, const SnapshotScheduleId& snapshot_schedule_id,
      const HybridTime& restore_time, const std::string& target_namespace_name,
      CoarseTimePoint deadline, const LeaderEpoch& epoch);

  Status LoadCloneState(const std::string& id, const SysCloneStatePB& metadata);

  Status ScheduleCloneOps(const CloneStateInfoPtr& clone_state, const LeaderEpoch& epoch);

  Result<CloneStateInfoPtr> GetCloneStateFromSourceNamespace(const NamespaceId& namespace_id);

  AsyncClonePgSchema::ClonePgSchemaCallbackType MakeDoneClonePGSchemaCallback(
      CloneStateInfoPtr clone_state, const SnapshotScheduleId& snapshot_schedule_id,
      const HybridTime& restore_time, const std::string& target_namespace_name,
      CoarseTimePoint deadline, const LeaderEpoch& epoch);

  Status HandleCreatingState(const CloneStateInfoPtr& clone_state);
  Status HandleRestoringState(const CloneStateInfoPtr& clone_state);

  std::mutex mutex_;

  // Map from clone source namespace id to the latest clone state for that namespace.
  using CloneStateMap = std::unordered_map<NamespaceId, CloneStateInfoPtr>;
  CloneStateMap source_clone_state_map_ GUARDED_BY(mutex_);

  const ExternalFunctions external_funcs_;
};

} // namespace master
} // namespace yb
