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

#include "yb/master/master_snapshot_coordinator.h"

#include <unordered_map>

#include "yb/common/snapshot.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/tablet/operations/snapshot_operation.h"

namespace yb {
namespace master {

namespace {

class SnapshotState {
 public:
  SnapshotState(
      SnapshotCoordinatorContext* context, const tserver::TabletSnapshotOpRequestPB& request)
      : context_(*context),
        snapshot_hybrid_time_(request.snapshot_hybrid_time()),
        tablet_ids_(request.tablet_id().begin(), request.tablet_id().end()) {
    running_tablet_ids_.reserve(tablet_ids_.size());
    for (const auto& id : tablet_ids_) {
      running_tablet_ids_.push_back(&id);
    }
  }

  SnapshotState(const SnapshotState&) = delete;
  void operator=(const SnapshotState&) = delete;

  HybridTime snapshot_hybrid_time() const {
    return snapshot_hybrid_time_;
  }

  TabletInfos RunningTabletInfos() {
    std::vector<TabletId> running_ids;
    running_ids.reserve(running_tablet_ids_.size());
    for (const auto* id : running_tablet_ids_) {
      running_ids.push_back(*id);
    }
    return context_.GetTabletInfos(running_ids);
  }

 private:
  SnapshotCoordinatorContext& context_;
  HybridTime snapshot_hybrid_time_;
  const std::vector<TabletId> tablet_ids_;
  // We don't modify tablet_ids_, so pointers stored in running_tablet_ids_ are valid while
  // tablet_ids_ is alive.
  std::vector<const TabletId*> running_tablet_ids_;
};

} // namespace

class MasterSnapshotCoordinator::Impl {
 public:
  explicit Impl(SnapshotCoordinatorContext* context) : context_(*context) {}

  CHECKED_STATUS Replicated(int64_t leader_term, const tablet::SnapshotOperationState& state) {
    // TODO(txn_backup) retain logs with this operation while doing snapshot
    auto id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(state.request()->snapshot_id()));
    auto snapshot = std::make_unique<SnapshotState>(&context_, *state.request());

    TabletInfos tablet_infos;
    auto snapshot_hybrid_time = snapshot->snapshot_hybrid_time();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto emplace_result = snapshots_.emplace(id, std::move(snapshot));
      if (!emplace_result.second) {
        return STATUS_FORMAT(IllegalState, "Duplicate snapshot id: $0", id);
      }

      if (leader_term < 0) {
        return Status::OK();
      }

      tablet_infos = emplace_result.first->second->RunningTabletInfos();
    }

    auto snapshot_id_str = id.AsSlice().ToBuffer();
    for (const auto& tablet : tablet_infos) {
      context_.SendCreateTabletSnapshotRequest(
          tablet, snapshot_id_str, snapshot_hybrid_time);
    }

    return Status::OK();
  }

 private:
  SnapshotCoordinatorContext& context_;
  std::mutex mutex_;
  std::unordered_map<TxnSnapshotId, std::unique_ptr<SnapshotState>, TxnSnapshotIdHash> snapshots_
      GUARDED_BY(mutex_);
};

MasterSnapshotCoordinator::MasterSnapshotCoordinator(SnapshotCoordinatorContext* context)
    : impl_(new Impl(context)) {}

MasterSnapshotCoordinator::~MasterSnapshotCoordinator() {}

Status MasterSnapshotCoordinator::Replicated(
    int64_t leader_term, const tablet::SnapshotOperationState& state) {
  return impl_->Replicated(leader_term, state);
}

} // namespace master
} // namespace yb
