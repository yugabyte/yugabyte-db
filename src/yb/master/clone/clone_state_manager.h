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

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/clone/clone_state_entity.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_fwd.h"

namespace yb {
namespace master {

class CloneStateManager {
  friend class CloneStateManagerTest;

 public:
  static std::unique_ptr<CloneStateManager> Create(
      CatalogManagerIf* catalog_manager, Master* master, SysCatalogTable* sys_catalog);

  Status Run();

  Result<CloneStateInfoPtr> CreateCloneState(
      uint32_t seq_no,
      const NamespaceId& source_namespace_id,
      const std::string& target_namespace_name,
      const TxnSnapshotId& source_snapshot_id,
      const TxnSnapshotId& target_snapshot_id,
      const HybridTime& restore_time,
      const ExternalTableSnapshotDataMap& table_snapshot_data);

  Status ClearAndRunLoaders();

 private:
  struct ExternalFunctions {
    // Snapshot coordinator.
    const std::function<Result<TxnSnapshotRestorationId>(const TxnSnapshotId&, HybridTime)> Restore;
    const std::function<Status(
        const TxnSnapshotId&, ListSnapshotRestorationsResponsePB*)> ListRestorations;

    // Catalog manager.
    const std::function<Result<TabletInfoPtr>(const TabletId&)> GetTabletInfo;
    const std::function<Status(const TabletInfoPtr&, LeaderEpoch, tablet::CloneTabletRequestPB)>
        ScheduleCloneTabletCall;

    // Sys catalog.
    const std::function<Status(const CloneStateInfoPtr&)> Upsert;
    const std::function<Status(
        const std::string& type,
        std::function<Status(const std::string&, const SysCloneStatePB&)> inserter)> Load;
  };

  explicit CloneStateManager(ExternalFunctions external_functions);

  Status LoadCloneState(const std::string& id, const SysCloneStatePB& metadata);

  Status ScheduleCloneOps(
      const CloneStateInfoPtr& clone_state, const LeaderEpoch& epoch);

  Result<CloneStateInfoPtr> GetCloneStateFromSourceNamespace(const NamespaceId& namespace_id);

  Status HandleCreatingState(const CloneStateInfoPtr& clone_state);
  Status HandleRestoringState(const CloneStateInfoPtr& clone_state);

  std::mutex mutex_;

  // Map from clone source namespace id to the latest clone state for that namespace.
  std::unordered_map<NamespaceId, CloneStateInfoPtr> source_clone_state_map_ GUARDED_BY(mutex_);

  const ExternalFunctions external_funcs_;
};

} // namespace master
} // namespace yb
