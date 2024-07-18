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
#include "yb/master/clone/external_functions.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/rpc/rpc_context.h"

namespace yb {
namespace master {

class CloneStateManager {
  friend class CloneStateManagerTest;

 public:
  static std::unique_ptr<CloneStateManager> Create(
      CatalogManagerIf* catalog_manager, Master* master, SysCatalogTable* sys_catalog);

  Status Run();

  Status ListClones(const ListClonesRequestPB* req, ListClonesResponsePB* resp);

  Status CloneNamespace(
      const CloneNamespaceRequestPB* req,
      CloneNamespaceResponsePB* resp,
      rpc::RpcContext* rpc,
      const LeaderEpoch& epoch);

  Status ClearAndRunLoaders(const LeaderEpoch& epoch);

 private:
  explicit CloneStateManager(
      std::unique_ptr<CloneStateManagerExternalFunctionsBase> external_funcs);

  Result<std::pair<NamespaceId, uint32_t>> CloneNamespace(
    const NamespaceIdentifierPB& source_namespace,
    const HybridTime& read_time,
    const std::string& target_namespace_name,
    const std::string& pg_source_owner,
    const std::string& pg_target_owner,
    CoarseTimePoint deadline,
    const LeaderEpoch& epoch);

  Result<CloneStateInfoPtr> CreateCloneState(
      const LeaderEpoch& epoch,
      uint32_t seq_no,
      const NamespaceId& source_namespace_id,
      YQLDatabase database_type,
      const std::string& target_namespace_name,
      const HybridTime& restore_time);

  Status UpdateCloneStateWithSnapshotInfo(
      const CloneStateInfoPtr& clone_state,
      const TxnSnapshotId& source_snapshot_id,
      const TxnSnapshotId& target_snapshot_id,
      const ExternalTableSnapshotDataMap& table_snapshot_data);

  // Create PG schema objects of the clone database.
  Status ClonePgSchemaObjects(
      CloneStateInfoPtr clone_state,
      const std::string& source_db_name,
      const std::string& target_db_name,
      const std::string& pg_source_owner,
      const std::string& pg_target_owner,
      const SnapshotScheduleId& snapshot_schedule_id);

  // Starts snapshot related operations for clone (mainly generate snapshotInfoPB as of
  // restore_time and then import it and create a new snapshot for target_namespace). Then it
  // schedules async clone tasks for every tablet. The function is the whole clone process in case
  // of YCQL and the second part of the clone process in case of YSQL.
  Status StartTabletsCloning(
      CloneStateInfoPtr clone_state,
      const SnapshotScheduleId& snapshot_schedule_id,
      const std::string& target_namespace_name,
      CoarseTimePoint deadline);

  Status LoadCloneState(
      const LeaderEpoch& epoch, const std::string& id, const SysCloneStatePB& metadata);

  Status ScheduleCloneOps(
      const CloneStateInfoPtr& clone_state,
      const std::unordered_set<TabletId>& not_snapshotted_tablets);

  AsyncClonePgSchema::ClonePgSchemaCallbackType MakeDoneClonePgSchemaCallback(
      CloneStateInfoPtr clone_state,
      const SnapshotScheduleId& snapshot_schedule_id,
      const std::string& target_namespace_name,
      CoarseTimePoint deadline);

  Status EnableDbConnections(const CloneStateInfoPtr& clone_state);

  Status HandleCreatingState(const CloneStateInfoPtr& clone_state);
  Status HandleRestoringState(const CloneStateInfoPtr& clone_state);
  Result<bool> IsDeleteNamespaceDone(const CloneStateInfoPtr& clone_state);

  // Mark state as aborting and set a descriptive message for debugging purposes.
  Status MarkCloneAborted(const CloneStateInfoPtr& clone_state, const std::string& abort_reason);
  // The loading code uses this overload to abort a clone after a master leader change.
  Status MarkCloneAborted(
      const CloneStateInfoPtr& clone_state, const std::string& abort_reason, int64_t leader_term);

  std::mutex mutex_;

  using CloneStateSet = std::set<CloneStateInfoPtr, CloneStateInfoComparator>;
  using CloneStateMap = std::unordered_map<NamespaceId, CloneStateSet>;

  // Map from clone source namespace id to all clone states for that namespace.
  CloneStateMap source_clone_state_map_ GUARDED_BY(mutex_);

  std::unique_ptr<CloneStateManagerExternalFunctionsBase> external_funcs_;
};

} // namespace master
} // namespace yb
