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

#include "yb/docdb/doc_key.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_error.h"
#include "yb/master/sys_catalog_writer.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/operations/write_operation.h"

#include "yb/util/pb_util.h"

using namespace std::literals;
using namespace std::placeholders;

DECLARE_int32(sys_catalog_write_timeout_ms);

namespace yb {
namespace master {

namespace {

class StateWithTablets {
 public:
  StateWithTablets(
      SnapshotCoordinatorContext* context, SysSnapshotEntryPB::State initial_state)
      : context_(*context), initial_state_(initial_state) {
  }

  // Initialize tablet states using tablet ids, i.e. put all tablets in initial state.
  template <class TabletIds>
  void InitTabletIds(const TabletIds& tablet_ids) {
    for (const auto& id : tablet_ids) {
      tablets_.emplace(id, initial_state_);
    }
    num_tablets_in_initial_state_ = tablet_ids.size();
  }

  // Initialize tablet states from serialized data.
  void InitTablets(
      const google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>& tablets) {
    for (const auto& tablet : tablets) {
      tablets_.emplace(tablet.id(), tablet.state());
      if (tablet.state() == initial_state_) {
        ++num_tablets_in_initial_state_;
      }
    }
  }

  StateWithTablets(const StateWithTablets&) = delete;
  void operator=(const StateWithTablets&) = delete;

  // If any of tablets failed returns this failure.
  // Otherwise if any of tablets is in initial state returns initial state.
  // Otherwise all tablets should be in the same state, which is returned.
  Result<SysSnapshotEntryPB::State> AggregatedState() {
    SysSnapshotEntryPB::State result = initial_state_;
    bool has_initial = false;
    for (const auto& p : tablets_) {
      auto& state = p.second;
      if (!state.ok()) {
        return state.status();
      }
      if (*state == initial_state_) {
        has_initial = true;
      } else if (result == initial_state_) {
        result = *state;
      } else if (*state != result) {
        // Should not happen.
        return STATUS_FORMAT(IllegalState, "Tablets in different terminal states: $0 and $1",
                             SysSnapshotEntryPB::State_Name(result),
                             SysSnapshotEntryPB::State_Name(*state));
      }
    }
    return has_initial ? initial_state_ : result;
  }

  SysSnapshotEntryPB::State ReportedState() {
    auto temp = AggregatedState();
    return temp.ok() ? *temp : SysSnapshotEntryPB::FAILED;
  }

  Result<bool> Complete() {
    return VERIFY_RESULT(AggregatedState()) != initial_state_;
  }

  bool AllTabletsDone() {
    return num_tablets_in_initial_state_ == 0;
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

  void Done(const TabletId& tablet_id, const Result<SysSnapshotEntryPB::State>& result) {
    auto it = tablets_.find(tablet_id);
    if (it == tablets_.end()) {
      LOG(DFATAL) << "Finished " << InitialStateName() <<  " snapshot at unknown tablet "
                  << tablet_id << ": " << result;
      return;
    }
    auto& state = it->second;
    if (state.ok() && *state == initial_state_) {
      if (result.ok()) {
        state = *result;
      } else {
        auto full_status = result.status().CloneAndPrepend(
            Format("Failed to $0 snapshot at $1", InitialStateName(), tablet_id));
        LOG(WARNING) << full_status;
        state = full_status;
      }
      --num_tablets_in_initial_state_;

      LOG(INFO) << "Finished " << InitialStateName() << " snapshot at " << tablet_id << ": "
                << result;
    } else {
      LOG(DFATAL) << "Finished " << InitialStateName() << " snapshot at tablet " << tablet_id
                  << " in a wrong state " << state << ": " << result;
    }
  }

  SnapshotCoordinatorContext& context() const {
    return context_;
  }

  bool AllInState(SysSnapshotEntryPB::State state) {
    for (const auto& tablet : tablets_) {
      if (!tablet.second.ok() || *tablet.second != state) {
        return false;
      }
    }

    return true;
  }

  bool HasInState(SysSnapshotEntryPB::State state) {
    for (const auto& tablet : tablets_) {
      if (tablet.second.ok() && *tablet.second == state) {
        return true;
      }
    }

    return false;
  }

  void SetInitialTabletsState(SysSnapshotEntryPB::State state) {
    initial_state_ = state;
    for (auto& tablet : tablets_) {
      tablet.second = state;
    }
    num_tablets_in_initial_state_ = tablets_.size();
  }

  SysSnapshotEntryPB::State initial_state() const {
    return initial_state_;
  }

 private:
  const std::string& InitialStateName() const {
    return SysSnapshotEntryPB::State_Name(initial_state_);
  }

  SnapshotCoordinatorContext& context_;
  SysSnapshotEntryPB::State initial_state_;
  std::unordered_map<TabletId, Result<SysSnapshotEntryPB::State>> tablets_;
  size_t num_tablets_in_initial_state_ = 0;
};

SysSnapshotEntryPB::State CurrentStateToInitialState(SysSnapshotEntryPB::State state) {
  switch (state) {
    case SysSnapshotEntryPB::CREATING: FALLTHROUGH_INTENDED;
    case SysSnapshotEntryPB::COMPLETE:
      return SysSnapshotEntryPB::CREATING;
    case SysSnapshotEntryPB::DELETING: FALLTHROUGH_INTENDED;
    case SysSnapshotEntryPB::DELETED:
      return SysSnapshotEntryPB::DELETING;
    default:
      break;
  }

  FATAL_INVALID_ENUM_VALUE(SysSnapshotEntryPB::State, state);
}

class SnapshotState : public StateWithTablets {
 public:
  SnapshotState(
      SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
      const tserver::TabletSnapshotOpRequestPB& request)
      : StateWithTablets(context, SysSnapshotEntryPB::CREATING),
        id_(id), snapshot_hybrid_time_(request.snapshot_hybrid_time()) {
    InitTabletIds(request.tablet_id());
    request.extra_data().UnpackTo(&entries_);
  }

  SnapshotState(
      SnapshotCoordinatorContext* context, const TxnSnapshotId& id,
      const SysSnapshotEntryPB& entry)
      : StateWithTablets(context, CurrentStateToInitialState(entry.state())),
        id_(id), snapshot_hybrid_time_(entry.snapshot_hybrid_time()) {
    InitTablets(entry.tablet_snapshots());
    *entries_.mutable_entries() = entry.entries();
  }

  HybridTime snapshot_hybrid_time() const {
    return snapshot_hybrid_time_;
  }

  void ToPB(SnapshotInfoPB* out) {
    out->set_id(id_.data(), id_.size());
    ToEntryPB(out->mutable_entry());
  }

  void ToEntryPB(SysSnapshotEntryPB* out) {
    out->set_state(ReportedState());
    out->set_snapshot_hybrid_time(snapshot_hybrid_time_.ToUint64());

    TabletsToPB(out->mutable_tablet_snapshots());

    *out->mutable_entries() = entries_.entries();
  }

  CHECKED_STATUS StoreToWriteBatch(docdb::KeyValueWriteBatchPB* out) {
    docdb::DocKey doc_key({ docdb::PrimitiveValue::Int32(SysRowEntry::SNAPSHOT),
                            docdb::PrimitiveValue(id_.AsSlice().ToBuffer()) });
    docdb::SubDocKey sub_doc_key(
        doc_key, docdb::PrimitiveValue(VERIFY_RESULT(context().MetadataColumnId())));
    auto encoded_key = sub_doc_key.Encode();
    auto pair = out->add_write_pairs();
    pair->set_key(encoded_key.data());
    faststring value;
    value.push_back(docdb::ValueTypeAsChar::kString);
    SysSnapshotEntryPB entry;
    ToEntryPB(&entry);
    pb_util::AppendToString(entry, &value);
    pair->set_value(value.data(), value.size());
    return Status::OK();
  }

  CHECKED_STATUS CheckCanDelete() {
    if (AllInState(SysSnapshotEntryPB::DELETED)) {
      return STATUS(NotFound, "The snapshot was deleted", id_.ToString(),
                    MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
    }
    if (HasInState(SysSnapshotEntryPB::DELETING)) {
      return STATUS(NotFound, "The snapshot is being deleted", id_.ToString(),
                    MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
    }

    return Status::OK();
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
      : StateWithTablets(context, SysSnapshotEntryPB::RESTORING),
        restoration_id_(restoration_id) {
    InitTabletIds(snapshot->TabletIdsInState(SysSnapshotEntryPB::COMPLETE));
  }

  void ToPB(SnapshotInfoPB* out) {
    out->set_id(restoration_id_.data(), restoration_id_.size());
    auto& entry = *out->mutable_entry();

    entry.set_state(ReportedState());

    TabletsToPB(entry.mutable_tablet_snapshots());
  }

 private:
  TxnSnapshotRestorationId restoration_id_;
};

struct NoOp {
  template <class... Args>
  void operator()(Args&&... args) const {}
};

// Utility to create callback that is invoked when operation done.
// Finds appropriate entry in passed collection and invokes Done on it.
template <class Collection, class PostProcess = NoOp>
auto MakeDoneCallback(
    std::mutex* mutex, const Collection& collection, const typename Collection::key_type& key,
    const TabletId& tablet_id, SysSnapshotEntryPB::State desired_state,
    const PostProcess& post_process = PostProcess()) {
  struct DoneFunctor {
    std::mutex& mutex;
    const Collection& collection;
    typename Collection::key_type key;
    TabletId tablet_id;
    SysSnapshotEntryPB::State desired_state;
    PostProcess post_process;

    void operator()(Result<const tserver::TabletSnapshotOpResponsePB&> resp) const {
      std::unique_lock<std::mutex> lock(mutex);
      auto it = collection.find(key);
      if (it == collection.end()) {
        LOG(DFATAL) << "Received reply for unknown " << key;
        return;
      }

      it->second->Done(
          tablet_id, resp.ok() ? Result<SysSnapshotEntryPB::State>(desired_state) : resp.status());
      post_process(it->second.get(), &lock);
    }
  };

  return DoneFunctor {
    .mutex = *mutex,
    .collection = collection,
    .key = key,
    .tablet_id = tablet_id,
    .desired_state = desired_state,
    .post_process = post_process,
  };
}

auto SnapshotUpdater(SnapshotCoordinatorContext* context) {
  struct UpdateFunctor {
    SnapshotCoordinatorContext& context;

    void operator()(SnapshotState* snapshot, std::unique_lock<std::mutex>* lock) const {
      if (!snapshot->AllTabletsDone()) {
        return;
      }
      docdb::KeyValueWriteBatchPB write_batch;
      auto status = snapshot->StoreToWriteBatch(&write_batch);
      if (!status.ok()) {
        LOG(DFATAL) << "Failed to prepare write batch for snapshot: " << status;
        return;
      }
      lock->unlock();

      tserver::WriteRequestPB empty_write_request;
      auto state = std::make_unique<tablet::WriteOperationState>(
          /* tablet= */ nullptr, &empty_write_request);
      auto& request = *state->mutable_request();
      *request.mutable_write_batch() = write_batch;
      auto operation = std::make_unique<tablet::WriteOperation>(
          std::move(state), yb::OpId::kUnknownTerm, ScopedOperation(),
          CoarseMonoClock::now() + FLAGS_sys_catalog_write_timeout_ms * 1ms, /* context */ nullptr);
      context.Submit(std::move(operation));
    }
  };

  return UpdateFunctor {
    .context = *context
  };
}

} // namespace

class MasterSnapshotCoordinator::Impl {
 public:
  explicit Impl(SnapshotCoordinatorContext* context) : context_(*context) {}

  Result<TxnSnapshotId> Create(
      const SysRowEntries& entries, HybridTime snapshot_hybrid_time, CoarseTimePoint deadline) {
    auto synchronizer = std::make_shared<Synchronizer>();
    auto operation_state = std::make_unique<tablet::SnapshotOperationState>(/* tablet= */ nullptr);
    auto request = operation_state->AllocateRequest();

    for (const auto& entry : entries.entries()) {
      if (entry.type() == SysRowEntry::TABLET) {
        request->add_tablet_id(entry.id());
      }
    }

    request->set_snapshot_hybrid_time(snapshot_hybrid_time.ToUint64());
    request->set_operation(tserver::TabletSnapshotOpRequestPB::CREATE_ON_MASTER);
    auto snapshot_id = TxnSnapshotId::GenerateRandom();
    request->set_snapshot_id(snapshot_id.data(), snapshot_id.size());

    request->mutable_extra_data()->PackFrom(entries);

    operation_state->set_completion_callback(std::make_unique<
        tablet::WeakSynchronizerOperationCompletionCallback>(synchronizer));
    auto operation = std::make_unique<tablet::SnapshotOperation>(std::move(operation_state));

    context_.Submit(std::move(operation));
    RETURN_NOT_OK(synchronizer->WaitUntil(ToSteady(deadline)));

    return snapshot_id;
  }

  CHECKED_STATUS CreateReplicated(
      int64_t leader_term, const tablet::SnapshotOperationState& state) {
    // TODO(txn_backup) retain logs with this operation while doing snapshot
    auto id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(state.request()->snapshot_id()));
    auto snapshot = std::make_unique<SnapshotState>(&context_, id, *state.request());

    TabletInfos tablet_infos;
    auto snapshot_hybrid_time = snapshot->snapshot_hybrid_time();
    docdb::KeyValueWriteBatchPB write_batch;
    RETURN_NOT_OK(snapshot->StoreToWriteBatch(&write_batch));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto emplace_result = snapshots_.emplace(id, std::move(snapshot));
      if (!emplace_result.second) {
        return STATUS_FORMAT(IllegalState, "Duplicate snapshot id: $0", id);
      }

      if (leader_term >= 0) {
        tablet_infos = emplace_result.first->second->TabletInfosInState(
            SysSnapshotEntryPB::CREATING);
      }
    }

    RETURN_NOT_OK(state.tablet()->ApplyOperationState(state, /* batch_idx= */ -1, write_batch));

    if (!tablet_infos.empty()) {
      auto snapshot_id_str = id.AsSlice().ToBuffer();
      for (const auto& tablet : tablet_infos) {
        context_.SendCreateTabletSnapshotRequest(
            tablet, snapshot_id_str, snapshot_hybrid_time,
            MakeDoneCallback(&mutex_, snapshots_, id, tablet->tablet_id(),
                             SysSnapshotEntryPB::COMPLETE, SnapshotUpdater(&context_)));
      }
    }

    return Status::OK();
  }

  CHECKED_STATUS Load(const TxnSnapshotId& snapshot_id, const SysSnapshotEntryPB& data) {
    auto snapshot = std::make_unique<SnapshotState>(&context_, snapshot_id, data);

    std::lock_guard<std::mutex> lock(mutex_);
    auto emplace_result = snapshots_.emplace(snapshot_id, std::move(snapshot));
    if (!emplace_result.second) {
      // During sys catalog bootstrap we replay WAL, that could contain "create snapshot" operation.
      // In this case we add snapshot to snapshots_ and write it data into RocksDB.
      // Bootstrap sys catalog tries to load all entries from RocksDB, so could find recently
      // added entry and try to Load it.
      // So we could just ignore it in this case.
      return Status::OK();
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

  CHECKED_STATUS Delete(const TxnSnapshotId& snapshot_id, CoarseTimePoint deadline) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
      RETURN_NOT_OK(snapshot.CheckCanDelete());
    }

    auto synchronizer = std::make_shared<Synchronizer>();
    auto operation_state = std::make_unique<tablet::SnapshotOperationState>(nullptr);
    auto request = operation_state->AllocateRequest();

    request->set_operation(tserver::TabletSnapshotOpRequestPB::DELETE_ON_MASTER);
    request->set_snapshot_id(snapshot_id.data(), snapshot_id.size());

    operation_state->set_completion_callback(std::make_unique<
        tablet::WeakSynchronizerOperationCompletionCallback>(synchronizer));
    auto operation = std::make_unique<tablet::SnapshotOperation>(std::move(operation_state));

    context_.Submit(std::move(operation));
    RETURN_NOT_OK(synchronizer->WaitUntil(ToSteady(deadline)));
    return Status::OK();
  }

  CHECKED_STATUS DeleteReplicated(
      int64_t leader_term, const tablet::SnapshotOperationState& state) {
    auto snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(state.request()->snapshot_id()));
    docdb::KeyValueWriteBatchPB write_batch;
    TabletInfos tablet_infos;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      SnapshotState& snapshot = VERIFY_RESULT(FindSnapshot(snapshot_id));
      snapshot.SetInitialTabletsState(SysSnapshotEntryPB::DELETING);
      RETURN_NOT_OK(snapshot.StoreToWriteBatch(&write_batch));
      if (leader_term >= 0) {
        tablet_infos = snapshot.TabletInfosInState(SysSnapshotEntryPB::DELETING);
      }
    }

    RETURN_NOT_OK(state.tablet()->ApplyOperationState(state, /* batch_idx= */ -1, write_batch));

    if (!tablet_infos.empty()) {
      auto snapshot_id_str = snapshot_id.AsSlice().ToBuffer();
      for (const auto& tablet : tablet_infos) {
        context_.SendDeleteTabletSnapshotRequest(
            tablet, snapshot_id_str,
            MakeDoneCallback(&mutex_, snapshots_, snapshot_id, tablet->tablet_id(),
                             SysSnapshotEntryPB::DELETED, SnapshotUpdater(&context_)));
      }
    }

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
          MakeDoneCallback(&mutex_, restorations_, restoration_id, tablet->tablet_id(),
                           SysSnapshotEntryPB::COMPLETE));
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

Result<TxnSnapshotId> MasterSnapshotCoordinator::Create(
    const SysRowEntries& entries, HybridTime snapshot_hybrid_time, CoarseTimePoint deadline) {
  return impl_->Create(entries, snapshot_hybrid_time, deadline);
}

Status MasterSnapshotCoordinator::CreateReplicated(
    int64_t leader_term, const tablet::SnapshotOperationState& state) {
  return impl_->CreateReplicated(leader_term, state);
}

Status MasterSnapshotCoordinator::DeleteReplicated(
    int64_t leader_term, const tablet::SnapshotOperationState& state) {
  return impl_->DeleteReplicated(leader_term, state);
}

Status MasterSnapshotCoordinator::ListSnapshots(
    const TxnSnapshotId& snapshot_id, ListSnapshotsResponsePB* resp) {
  return impl_->ListSnapshots(snapshot_id, resp);
}

Status MasterSnapshotCoordinator::Delete(
    const TxnSnapshotId& snapshot_id, CoarseTimePoint deadline) {
  return impl_->Delete(snapshot_id, deadline);
}

Result<TxnSnapshotRestorationId> MasterSnapshotCoordinator::Restore(
    const TxnSnapshotId& snapshot_id) {
  return impl_->Restore(snapshot_id);
}

Status MasterSnapshotCoordinator::ListRestorations(
    const TxnSnapshotRestorationId& restoration_id, ListSnapshotRestorationsResponsePB* resp) {
  return impl_->ListRestorations(restoration_id, resp);
}

Status MasterSnapshotCoordinator::Load(const TxnSnapshotId& id, const SysSnapshotEntryPB& data) {
  return impl_->Load(id, data);
}

} // namespace master
} // namespace yb
