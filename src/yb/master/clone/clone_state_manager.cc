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

#include "yb/master/clone/clone_state_manager.h"

#include <mutex>

#include "yb/common/snapshot.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/clone/clone_state_entity.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/sys_catalog.h"

#include "yb/util/oid_generator.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

namespace yb {
namespace master {

using std::string;
using namespace std::literals;
using namespace std::placeholders;

std::unique_ptr<CloneStateManager> CloneStateManager::Create(
    CatalogManagerIf* catalog_manager, Master* master, SysCatalogTable* sys_catalog) {
  ExternalFunctions external_functions = {
    .Restore = [catalog_manager, &snapshot_coordinator = catalog_manager->snapshot_coordinator()]
        (const TxnSnapshotId& snapshot_id, HybridTime restore_at) {
      return snapshot_coordinator.Restore(snapshot_id, restore_at,
          catalog_manager->leader_ready_term());
    },
    .ListRestorations = [&snapshot_coordinator = catalog_manager->snapshot_coordinator()]
        (const TxnSnapshotId& snapshot_id, ListSnapshotRestorationsResponsePB* resp) {
      return snapshot_coordinator.ListRestorations(
          TxnSnapshotRestorationId::Nil(), snapshot_id, resp);
    },

    .GetTabletInfo = [catalog_manager](const TabletId& tablet_id) {
      return catalog_manager->GetTabletInfo(tablet_id);
    },
    .ScheduleCloneTabletCall = [catalog_manager, master]
        (const TabletInfoPtr& source_tablet, LeaderEpoch epoch, tablet::CloneTabletRequestPB req) {
      auto call = std::make_shared<AsyncCloneTablet>(
        master, catalog_manager->AsyncTaskPool(), source_tablet, epoch, std::move(req));
      return catalog_manager->ScheduleTask(call);
    },

    .Upsert = [catalog_manager, sys_catalog](const CloneStateInfoPtr& clone_state){
      return sys_catalog->Upsert(catalog_manager->leader_ready_term(), clone_state);
    },
    .Load = [sys_catalog](
        const std::string& type,
        std::function<Status(const std::string&, const SysCloneStatePB&)> inserter) {
      return sys_catalog->Load<CloneStateLoader, SysCloneStatePB>(type, inserter);
    }
  };

  return std::unique_ptr<CloneStateManager>(new CloneStateManager(std::move(external_functions)));
}

CloneStateManager::CloneStateManager(ExternalFunctions external_functions):
    external_funcs_(std::move(external_functions)) {}

Status CloneStateManager::ClearAndRunLoaders() {
  {
    std::lock_guard l(mutex_);
    source_clone_state_map_.clear();
  }
  RETURN_NOT_OK(external_funcs_.Load(
      "Clone states",
      std::function<Status(const std::string&, const SysCloneStatePB&)>(
          std::bind(&CloneStateManager::LoadCloneState, this, _1, _2))));

  return Status::OK();
}

Status CloneStateManager::LoadCloneState(const std::string& id, const SysCloneStatePB& metadata) {
  auto clone_state = CloneStateInfoPtr(new CloneStateInfo(id));
  clone_state->Load(metadata);
  std::lock_guard lock(mutex_);
  auto read_lock = clone_state->LockForRead();
  auto& source_namespace_id = read_lock->pb.source_namespace_id();
  auto seq_no = read_lock->pb.clone_request_seq_no();

  auto it = source_clone_state_map_.find(source_namespace_id);
  if (it != source_clone_state_map_.end()) {
    auto existing_seq_no = it->second->LockForRead()->pb.clone_request_seq_no();
    LOG(INFO) << Format(
        "Found existing clone state for source namespace $0 with seq_no $1. This clone "
        "state's seq_no is $2", source_namespace_id, existing_seq_no, seq_no);
    if (seq_no < existing_seq_no) {
      // Do not overwrite the higher seq_no clone state.
      return Status::OK();
    }
    // TODO: Delete clone state with lower seq_no from sys catalog.
  }
  source_clone_state_map_[source_namespace_id] = clone_state;
  return Status::OK();
}

Result<CloneStateInfoPtr> CloneStateManager::CreateCloneState(
    uint32_t seq_no,
    const NamespaceId& source_namespace_id,
    const string& target_namespace_name,
    const TxnSnapshotId& source_snapshot_id,
    const TxnSnapshotId& target_snapshot_id,
    const HybridTime& restore_time,
    const ExternalTableSnapshotDataMap& table_snapshot_data) {
  CloneStateInfoPtr clone_state;
  {
    std::lock_guard lock(mutex_);
    auto it = source_clone_state_map_.find(source_namespace_id);
    if (it != source_clone_state_map_.end()) {
      auto state = it->second->LockForRead()->pb.aggregate_state();
      if (state != SysCloneStatePB::RESTORED) {
        return STATUS_FORMAT(
            AlreadyPresent, "Cannot create new clone state because there is already an ongoing "
            "clone for source namespace $0 in state $1", source_namespace_id, state);
      }
      // One day we might want to clean up the replaced clone state object here instead of at load
      // time.
    }
    clone_state = CloneStateInfoPtr(new CloneStateInfo(GenerateObjectId()));
    source_clone_state_map_[source_namespace_id] = clone_state;
  }

  clone_state->mutable_metadata()->StartMutation();
  auto* pb = &clone_state->mutable_metadata()->mutable_dirty()->pb;
  pb->set_clone_request_seq_no(seq_no);
  pb->set_source_snapshot_id(source_snapshot_id.data(), source_snapshot_id.size());
  pb->set_target_snapshot_id(target_snapshot_id.data(), target_snapshot_id.size());
  pb->set_source_namespace_id(source_namespace_id);
  pb->set_restore_time(restore_time.ToUint64());

  // Add data for each tablet in this table.
  std::unordered_map<TabletId, int> target_tablet_to_index;
  for (const auto& [_, table_snapshot_data] : table_snapshot_data) {
    for (auto& tablet : table_snapshot_data.table_meta->tablets_ids()) {
      auto* tablet_data = pb->add_tablet_data();
      tablet_data->set_source_tablet_id(tablet.old_id());
      tablet_data->set_target_tablet_id(tablet.new_id());
      target_tablet_to_index[tablet.new_id()] = pb->tablet_data_size() - 1;
    }
  }
  // If this namespace somehow has 0 tablets, create it as RESTORED.
  pb->set_aggregate_state(
      target_tablet_to_index.empty() ? SysCloneStatePB::RESTORED : SysCloneStatePB::CREATING);

  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  clone_state->mutable_metadata()->CommitMutation();
  return clone_state;
}

Status CloneStateManager::ScheduleCloneOps(
    const CloneStateInfoPtr& clone_state, const LeaderEpoch& epoch) {
  auto lock = clone_state->LockForRead();
  auto& pb = lock->pb;
  for (auto& tablet_data : pb.tablet_data()) {
    auto source_tablet = VERIFY_RESULT(
        external_funcs_.GetTabletInfo(tablet_data.source_tablet_id()));
    auto target_tablet = VERIFY_RESULT(
        external_funcs_.GetTabletInfo(tablet_data.target_tablet_id()));
    auto target_table = target_tablet->table();
    auto target_table_lock = target_table->LockForRead();

    tablet::CloneTabletRequestPB req;
    req.set_tablet_id(tablet_data.source_tablet_id());
    req.set_target_tablet_id(tablet_data.target_tablet_id());
    req.set_source_snapshot_id(pb.source_snapshot_id().data(), pb.source_snapshot_id().size());
    req.set_target_snapshot_id(pb.target_snapshot_id().data(), pb.target_snapshot_id().size());
    req.set_target_table_id(target_table->id());
    req.set_target_namespace_name(target_table_lock->namespace_name());
    req.set_clone_request_seq_no(pb.clone_request_seq_no());
    req.set_target_pg_table_id(target_table_lock->pb.pg_table_id());
    if (target_table_lock->pb.has_index_info()) {
      *req.mutable_target_index_info() = target_table_lock->pb.index_info();
    }
    *req.mutable_target_schema() = target_table_lock->pb.schema();
    *req.mutable_target_partition_schema() = target_table_lock->pb.partition_schema();
    RETURN_NOT_OK(external_funcs_.ScheduleCloneTabletCall(source_tablet, epoch, std::move(req)));
  }
  return Status::OK();
}

Result<CloneStateInfoPtr> CloneStateManager::GetCloneStateFromSourceNamespace(
    const NamespaceId& namespace_id) {
  std::lock_guard lock(mutex_);
  return FIND_OR_RESULT(source_clone_state_map_, namespace_id);
}

Status CloneStateManager::HandleCreatingState(const CloneStateInfoPtr& clone_state) {
  auto lock = clone_state->LockForWrite();

  SCHECK_EQ(lock->pb.aggregate_state(), SysCloneStatePB::CREATING, IllegalState,
      "Expected clone to be in creating state");

  bool all_tablets_running = true;
  auto& pb = lock.mutable_data()->pb;
  for (auto& tablet_data : *pb.mutable_tablet_data()) {
    // Check to see if the tablet is done cloning (i.e. it is RUNNING).
    auto tablet = VERIFY_RESULT(external_funcs_.GetTabletInfo(tablet_data.target_tablet_id()));
    if (!tablet->LockForRead()->is_running()) {
      all_tablets_running = false;
    }
  }

  if (!all_tablets_running) {
    return Status::OK();
  }

  LOG(INFO) << Format("All tablets for cloned namespace $0 with seq_no $1 are running. "
      "Marking clone operation as restoring.",
      pb.source_namespace_id(), pb.clone_request_seq_no());
  auto target_snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(pb.target_snapshot_id()));

  // Check for an existing restoration id. This might have happened if a restoration was submitted
  // but we crashed / failed over before we were able to persist the clone state as RESTORING.
  ListSnapshotRestorationsResponsePB resp;
  RETURN_NOT_OK(external_funcs_.ListRestorations(target_snapshot_id, &resp));
  if (resp.restorations().empty()) {
    RETURN_NOT_OK(external_funcs_.Restore(target_snapshot_id, HybridTime(pb.restore_time())));
  }
  pb.set_aggregate_state(SysCloneStatePB::RESTORING);

  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  lock.Commit();
  return Status::OK();
}

Status CloneStateManager::HandleRestoringState(const CloneStateInfoPtr& clone_state) {
  auto lock = clone_state->LockForWrite();
  auto target_snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(lock->pb.target_snapshot_id()));

  ListSnapshotRestorationsResponsePB resp;
  RETURN_NOT_OK(external_funcs_.ListRestorations(target_snapshot_id, &resp));
  SCHECK_EQ(resp.restorations_size(), 1, IllegalState, "Unexpected number of restorations.");

  if (resp.restorations(0).entry().state() != SysSnapshotEntryPB::RESTORED) {
    return Status::OK();
  }

  lock.mutable_data()->pb.set_aggregate_state(SysCloneStatePB::RESTORED);
  RETURN_NOT_OK(external_funcs_.Upsert(clone_state));
  lock.Commit();
  return Status::OK();
}

Status CloneStateManager::Run() {
  std::lock_guard lock(mutex_);
  for (auto& [source_namespace_id, clone_state] : source_clone_state_map_) {
    Status s;
    switch (clone_state->LockForRead()->pb.aggregate_state()) {
      case SysCloneStatePB::CREATING:
        s = HandleCreatingState(clone_state);
        break;
      case SysCloneStatePB::RESTORING:
        s = HandleRestoringState(clone_state);
        break;
      case SysCloneStatePB::RESTORED:
        break;
    }
    WARN_NOT_OK(s,
        Format("Could not handle clone state for source namespace $0", source_namespace_id));
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
