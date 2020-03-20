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
#include "yb/master/master_error.h"

#include "yb/tablet/operations/snapshot_operation.h"

using namespace std::placeholders;

namespace yb {
namespace master {

namespace {

class StateWithTablets {
 public:
  template <class TabletIds>
  StateWithTablets(
      SnapshotCoordinatorContext* context, const TabletIds& tablet_ids,
      SysSnapshotEntryPB::State initial_state)
      : context_(*context), initial_state_(initial_state) {
    for (const auto& id : tablet_ids) {
      tablets_.emplace(id, initial_state);
    }
  }

  StateWithTablets(const StateWithTablets&) = delete;
  void operator=(const StateWithTablets&) = delete;

  Result<bool> Complete() {
    bool result = true;
    for (const auto& p : tablets_) {
      auto& state = p.second;
      if (!state.ok()) {
        return state.status();
      }
      if (*state != SysSnapshotEntryPB::COMPLETE) {
        result = false;
      }
    }
    return result;
  }

  std::vector<TabletId> TabletIdsInState(SysSnapshotEntryPB::State state) {
    std::vector<TabletId> result;
    result.reserve(tablets_.size());
    for (const auto& p : tablets_) {
      if (p.second.ok() && *p.second == state) {
        result.push_back(p.first);
      }
    }
    return result;
  }

  TabletInfos TabletInfosInState(SysSnapshotEntryPB::State state) {
    return context_.GetTabletInfos(TabletIdsInState(state));
  }

  void TabletsToPB(google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>* out) {
    out->Reserve(tablets_.size());
    for (const auto& p : tablets_) {
      auto* tablet_state = out->Add();
      tablet_state->set_id(p.first);
      tablet_state->set_state(p.second.ok() ? *p.second : SysSnapshotEntryPB::FAILED);
    }
  }

  void Success(const TabletId& tablet_id) {
    auto it = tablets_.find(tablet_id);
    if (it == tablets_.end()) {
      LOG(DFATAL) << "Finished " << InitialStateName() <<  " snapshot at unknown tablet "
                  << tablet_id;
      return;
    }
    auto& state = it->second;
    if (state.ok() && *state == initial_state_) {
      state = SysSnapshotEntryPB::COMPLETE;

      LOG(INFO) << "Finished " << InitialStateName() << " snapshot at " << tablet_id;
    } else {
      LOG(DFATAL) << "Finished " << InitialStateName() << " snapshot at tablet " << tablet_id
                  << " in a wrong state " << state;
    }
  }

  void Failure(const TabletId& tablet_id, const Status& status) {
    auto it = tablets_.find(tablet_id);
    if (it == tablets_.end()) {
      LOG(DFATAL) << "Failed " << InitialStateName() << " snapshot at unknown tablet "
                  << tablet_id << ": " << status;
    }

    auto full_status = status.CloneAndPrepend(
        Format("Failed to $0 snapshot at $1", InitialStateName(), tablet_id));
    LOG(WARNING) << full_status;

    it->second = full_status;
  }

 private:
  const std::string& InitialStateName() const {
    return SysSnapshotEntryPB::State_Name(initial_state_);
  }

  SnapshotCoordinatorContext& context_;
  const SysSnapshotEntryPB::State initial_state_;
  std::unordered_map<TabletId, Result<SysSnapshotEntryPB::State>> tablets_;
};

class SnapshotState : public StateWithTablets {
 public:
  SnapshotState(
      SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
      const tserver::TabletSnapshotOpRequestPB& request)
      : StateWithTablets(context, request.tablet_id(), SysSnapshotEntryPB::CREATING),
        id_(id), snapshot_hybrid_time_(request.snapshot_hybrid_time()) {
    request.extra_data().UnpackTo(&entries_);
  }

  HybridTime snapshot_hybrid_time() const {
    return snapshot_hybrid_time_;
  }

  void ToPB(SnapshotInfoPB* out) {
    out->set_id(id_.data(), id_.size());
    auto& entry = *out->mutable_entry();
    auto complete = Complete();

    if (complete.ok()) {
      entry.set_state(*complete ? SysSnapshotEntryPB::COMPLETE : SysSnapshotEntryPB::CREATING);
    } else {
      entry.set_state(SysSnapshotEntryPB::FAILED);
    }

    TabletsToPB(entry.mutable_tablet_snapshots());

    *entry.mutable_entries() = entries_.entries();
  }

 private:
  TxnSnapshotId id_;
  HybridTime snapshot_hybrid_time_;
  SysRowEntries entries_;
};

class RestorationState : public StateWithTablets {
 public:
  RestorationState(
      SnapshotCoordinatorContext* context, const TxnSnapshotRestorationId& restoration_id,
      SnapshotState* snapshot)
      : StateWithTablets(context, snapshot->TabletIdsInState(SysSnapshotEntryPB::COMPLETE),
                         SysSnapshotEntryPB::RESTORING),
        restoration_id_(restoration_id) {
  }

  void ToPB(SnapshotInfoPB* out) {
    out->set_id(restoration_id_.data(), restoration_id_.size());
    auto& entry = *out->mutable_entry();
    auto complete = Complete();

    if (complete.ok()) {
      entry.set_state(*complete ? SysSnapshotEntryPB::COMPLETE : SysSnapshotEntryPB::RESTORING);
    } else {
      entry.set_state(SysSnapshotEntryPB::FAILED);
    }

    TabletsToPB(entry.mutable_tablet_snapshots());
  }

 private:
  TxnSnapshotRestorationId restoration_id_;
};

template <class Collection>
auto MakeDoneCallback(
    std::mutex* mutex, const Collection& collection, const typename Collection::key_type& key,
    const TabletId& tablet_id) {
  struct DoneFunctor {
    std::mutex& mutex;
    const Collection& collection;
    typename Collection::key_type key;
    TabletId tablet_id;

    void operator()(Result<const tserver::TabletSnapshotOpResponsePB&> resp) const {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = collection.find(key);
      if (it == collection.end()) {
        LOG(DFATAL) << "Received reply for unknown " << key;
        return;
      }

      if (!resp.ok()) {
        it->second->Failure(tablet_id, resp.status());
      } else {
        it->second->Success(tablet_id);
      }
    }
  };

  return DoneFunctor {
    .mutex = *mutex,
    .collection = collection,
    .key = key,
    .tablet_id = tablet_id,
  };
}

} // namespace

class MasterSnapshotCoordinator::Impl {
 public:
  explicit Impl(SnapshotCoordinatorContext* context) : context_(*context) {}

  CHECKED_STATUS Replicated(int64_t leader_term, const tablet::SnapshotOperationState& state) {
    // TODO(txn_backup) retain logs with this operation while doing snapshot
    auto id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(state.request()->snapshot_id()));
    auto snapshot = std::make_unique<SnapshotState>(&context_, id, *state.request());

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

      tablet_infos = emplace_result.first->second->TabletInfosInState(SysSnapshotEntryPB::CREATING);
    }

    auto snapshot_id_str = id.AsSlice().ToBuffer();
    for (const auto& tablet : tablet_infos) {
      context_.SendCreateTabletSnapshotRequest(
          tablet, snapshot_id_str, snapshot_hybrid_time,
          MakeDoneCallback(&mutex_, snapshots_, id, tablet->tablet_id()));
    }

    return Status::OK();
  }

  CHECKED_STATUS ListSnapshots(const TxnSnapshotId& snapshot_id, ListSnapshotsResponsePB* resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_id.IsNil()) {
      for (const auto& p : snapshots_) {
        p.second->ToPB(resp->add_snapshots());
      }
      return Status::OK();
    }

    SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
    snapshot.ToPB(resp->add_snapshots());
    return Status::OK();
  }

  CHECKED_STATUS ListRestorations(
      const TxnSnapshotRestorationId& restoration_id, ListSnapshotRestorationsResponsePB* resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (restoration_id.IsNil()) {
      for (const auto& p : restorations_) {
        p.second->ToPB(resp->add_restorations());
      }
      return Status::OK();
    }

    RestorationState& restoration = VERIFY_RESULT(FindRestoration(restoration_id));
    restoration.ToPB(resp->add_restorations());
    return Status::OK();
  }

  Result<TxnSnapshotRestorationId> Restore(const TxnSnapshotId& snapshot_id) {
    auto restoration_id = TxnSnapshotRestorationId::GenerateRandom();
    TabletInfos tablet_infos;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
      if (!VERIFY_RESULT(snapshot.Complete())) {
        return STATUS(IllegalState, "The snapshot state is not complete", snapshot_id.ToString(),
                      MasterError(MasterErrorPB::SNAPSHOT_IS_NOT_READY));
      }

      auto restoration = std::make_unique<RestorationState>(&context_, restoration_id, &snapshot);
      tablet_infos = restoration->TabletInfosInState(SysSnapshotEntryPB::RESTORING);
      restorations_.emplace(restoration_id, std::move(restoration));
    }

    auto snapshot_id_str = snapshot_id.AsSlice().ToBuffer();
    for (const auto& tablet : tablet_infos) {
      context_.SendRestoreTabletSnapshotRequest(
          tablet, snapshot_id_str,
          MakeDoneCallback(&mutex_, restorations_, restoration_id, tablet->tablet_id()));
    }

    return restoration_id;
  }

 private:
  Result<SnapshotState&> FindSnapshot(const TxnSnapshotId& snapshot_id) REQUIRES(mutex_) {
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
      return STATUS(NotFound, "Could not find snapshot", snapshot_id.ToString(),
                    MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
    }
    return *it->second;
  }

  Result<RestorationState&> FindRestoration(
      const TxnSnapshotRestorationId& restoration_id) REQUIRES(mutex_) {
    auto it = restorations_.find(restoration_id);
    if (it == restorations_.end()) {
      return STATUS(NotFound, "Could not find restoration", restoration_id.ToString(),
                    MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    return *it->second;
  }

  SnapshotCoordinatorContext& context_;
  std::mutex mutex_;
  std::unordered_map<TxnSnapshotId, std::unique_ptr<SnapshotState>,
                     TxnSnapshotIdHash> snapshots_ GUARDED_BY(mutex_);
  std::unordered_map<TxnSnapshotRestorationId, std::unique_ptr<RestorationState>,
                     TxnSnapshotRestorationIdHash> restorations_ GUARDED_BY(mutex_);
};

MasterSnapshotCoordinator::MasterSnapshotCoordinator(SnapshotCoordinatorContext* context)
    : impl_(new Impl(context)) {}

MasterSnapshotCoordinator::~MasterSnapshotCoordinator() {}

Status MasterSnapshotCoordinator::Replicated(
    int64_t leader_term, const tablet::SnapshotOperationState& state) {
  return impl_->Replicated(leader_term, state);
}

Status MasterSnapshotCoordinator::ListSnapshots(
    const TxnSnapshotId& snapshot_id, ListSnapshotsResponsePB* resp) {
  return impl_->ListSnapshots(snapshot_id, resp);
}

Result<TxnSnapshotRestorationId> MasterSnapshotCoordinator::Restore(
    const TxnSnapshotId& snapshot_id) {
  return impl_->Restore(snapshot_id);
}

Status MasterSnapshotCoordinator::ListRestorations(
    const TxnSnapshotRestorationId& restoration_id, ListSnapshotRestorationsResponsePB* resp) {
  return impl_->ListRestorations(restoration_id, resp);
}

} // namespace master
} // namespace yb
