// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/object_lock_shared_state_manager.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

#include "yb/docdb/object_lock_shared_state.h"

#include "yb/util/metrics.h"
#include "yb/util/unique_lock.h"

METRIC_DEFINE_gauge_uint64(server, object_locking_fastpath_pg_acquires,
    "Number of postgres-side object locking lock acquires performed via shared memory",
    yb::MetricUnit::kRequests,
    "Number of postgres-side object locking lock acquires performed via shared memory",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, object_locking_fastpath_pg_releases,
    "Number of transactions that used postgres-side object locking lock release via shared memory",
    yb::MetricUnit::kTransactions,
    "Number of transactions that used postgres-side object locking lock release via shared memory",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, object_locking_fastpath_tserver_acquires,
    "Number of TServer-side object locking lock acquires performed via shared memory",
    yb::MetricUnit::kRequests,
    "Number of TServer-side object locking lock acquires performed via shared memory",
    yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, object_locking_fastpath_tserver_releases,
    "Number of transactions that used TServer-side object locking lock release via shared memory",
    yb::MetricUnit::kTransactions,
    "Number of transactions that used TServer-side object locking lock release via shared memory",
    yb::EXPOSE_AS_COUNTER);

namespace yb::docdb {

namespace {

ObjectLockPrefix MakeLockPrefix(
    const ObjectLockFastpathRequest& request, dockv::KeyEntryType entry_type) {
  return ObjectLockPrefix(
      request.database_oid, request.relation_oid, request.object_oid, request.object_sub_oid,
      entry_type);
}

} // namespace

class ObjectLockOwnerRegistry::Impl {
 public:
  RegistrationGuard Register(
      ObjectLockSharedState& shared, TransactionId id, const TabletId& tablet_id) {
    ParentProcessGuard g;
    std::lock_guard lock(mutex_);
    owners_.insert(std::make_shared<OwnerInfo>(shared, id, tablet_id));
    shared.Enable();
    return {*this, id};
  }

  void Unregister(TransactionId id) {
    ParentProcessGuard g;
    std::lock_guard lock(mutex_);
    const auto i = owners_.find(id);
    DCHECK(i != owners_.end());
    (*i)->shared->Disable();
    owners_.erase(i);
  }

  [[nodiscard]] std::shared_ptr<OwnerInfo> GetOwnerInfo(TransactionId id) const {
    std::lock_guard lock(mutex_);
    const auto i = owners_.find(id);
    if (PREDICT_TRUE(i != owners_.end())) {
      return *i;
    }
    return {};
  }

  [[nodiscard]] std::shared_ptr<OwnerInfo> GetOwnerInfo(ObjectLockSharedState& state) const {
    std::lock_guard lock(mutex_);
    const auto& index = owners_.get<SharedTag>();
    const auto i = index.find(&state);
    if (PREDICT_TRUE(i != index.end())) {
      return *i;
    }
    return {};
  }

 private:
  mutable std::mutex mutex_;
  struct SharedTag;
  boost::multi_index_container<std::shared_ptr<OwnerInfo>,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::member<OwnerInfo, TransactionId, &OwnerInfo::txn_id>
          >,
          boost::multi_index::hashed_unique<
              boost::multi_index::tag<SharedTag>,
              boost::multi_index::member<OwnerInfo, ObjectLockSharedState*, &OwnerInfo::shared>
          >
      >
  > owners_ GUARDED_BY(mutex_);
};

ObjectLockOwnerRegistry::RegistrationGuard::~RegistrationGuard() {
  if (registry_) {
    registry_->Unregister(id_);
  }
}

ObjectLockOwnerRegistry::ObjectLockOwnerRegistry() : impl_(std::make_unique<Impl>()) {}

ObjectLockOwnerRegistry::~ObjectLockOwnerRegistry() = default;

ObjectLockOwnerRegistry::RegistrationGuard ObjectLockOwnerRegistry::Register(
    ObjectLockSharedState& shared, TransactionId id, const TabletId& status_tablet) {
  return impl_->Register(shared, id, status_tablet);
}

std::shared_ptr<ObjectLockOwnerRegistry::OwnerInfo>
ObjectLockOwnerRegistry::GetOwnerInfo(TransactionId id) const {
  return impl_->GetOwnerInfo(id);
}

std::shared_ptr<ObjectLockOwnerRegistry::OwnerInfo>
ObjectLockOwnerRegistry::GetOwnerInfo(ObjectLockSharedState& state) const {
  return impl_->GetOwnerInfo(state);
}

ObjectLockSharedStateHolder::~ObjectLockSharedStateHolder() {
  if (manager_) {
    manager_->ReleaseShared(*state_);
  }
}

struct ObjectLockSharedStateManager::MetricInfo {
  using AccumulatorFieldPtr = uint64_t ObjectLockSharedStateManager::*;
  using SharedStateValueFunction = uint64_t (ObjectLockSharedState::*)() const;

  GaugePrototype<uint64_t>& prototype;
  AccumulatorFieldPtr accumulator_field;
  SharedStateValueFunction shared_state_value;
};

struct ObjectLockSharedStateManager::MetricInfos {
  constexpr static MetricInfo kMetricsInfos[] = {
      { METRIC_object_locking_fastpath_pg_acquires,
        &ObjectLockSharedStateManager::num_pg_acquires_,
        &ObjectLockSharedState::PgLockRequestCount },
      { METRIC_object_locking_fastpath_pg_releases,
        &ObjectLockSharedStateManager::num_pg_releases_,
        &ObjectLockSharedState::PgLockReleaseCount },
      { METRIC_object_locking_fastpath_tserver_acquires,
        &ObjectLockSharedStateManager::num_tserver_acquires_,
        &ObjectLockSharedState::TServerLockRequestCount },
      { METRIC_object_locking_fastpath_tserver_releases,
        &ObjectLockSharedStateManager::num_tserver_releases_,
        &ObjectLockSharedState::TServerLockReleaseCount },
  };
};

ObjectLockSharedStateManager::ObjectLockSharedStateManager(
    std::shared_ptr<ObjectLockTracker> object_lock_tracker,
    const MetricEntityPtr& metric_entity)
    : object_lock_tracker_(std::move(object_lock_tracker)) {
  for (const auto& info : MetricInfos::kMetricsInfos) {
    info.prototype.InstantiateFunctionGauge(
        metric_entity,
        Bind(&ObjectLockSharedStateManager::CalculateMetric, Unretained(this), info))
      ->AutoDetachToLastValue(&metric_detacher_);
  }
}

void ObjectLockSharedStateManager::SetupShared(SharedMemoryBackingAllocator& allocator) {
  {
    std::lock_guard lock(mutex_);
    DCHECK(!allocator_);
    allocator_ = &allocator;
    stopped_ = false;
    VLOG(1) << "Set up ObjectLockSharedStateManager";
  }
  start_cond_.notify_all();
}

Result<ObjectLockSharedStateHolder> ObjectLockSharedStateManager::AllocateShared() {
  UniqueLock lock(mutex_);
  WaitOnConditionVariable(&start_cond_, &lock, [this] REQUIRES(mutex_) { return !stopped_; });
  auto state = VERIFY_RESULT(DCHECK_NOTNULL(allocator_)->MakeUnique<ObjectLockSharedState>(
      *allocator_, write_locks_));
  auto* ptr = state.get();
  shared_states_.insert(std::move(state));
  return ObjectLockSharedStateHolder{*this, *ptr};
}

void ObjectLockSharedStateManager::ReleaseShared(ObjectLockSharedState& state) {
  UniqueLock lock(mutex_);
  ParentProcessGuard g;

  state.Disable();
  for (const auto& info : MetricInfos::kMetricsInfos) {
    std::invoke(info.accumulator_field, *this) += std::invoke(info.shared_state_value, state);
  }

  // TODO: this could be shared_states_.erase(...) except our compilers currently don't support
  // heterogeneous erasure overloads for associative containers.
  auto iter = shared_states_.find(&state);
  DCHECK(iter != shared_states_.end());
  shared_states_.erase(iter);
}

void ObjectLockSharedStateManager::Start() {
  {
    std::lock_guard lock(mutex_);
    DCHECK(stopped_);
    if (!allocator_) {
      // The initial Start() call happens before shared memory is ready.
      VLOG(1) << "Shared memory not ready yet, not starting";
      return;
    }
    stopped_ = false;
    VLOG(1) << "Started ObjectLockSharedStateManager";
  }
  start_cond_.notify_all();
}

void ObjectLockSharedStateManager::Stop() {
  VLOG(1) << "Stopping ObjectLockSharedStateManager";
  {
    std::lock_guard lock(mutex_);
    for (auto& state : shared_states_) {
      ParentProcessGuard g;
      state->Shutdown();
    }
    stopped_ = true;
    VLOG(1) << "Stopped ObjectLockSharedStateManager";
  }
}

void ObjectLockSharedStateManager::ConsumePendingSharedLockRequests(
    const LockRequestConsumer& consume, TransactionId txn_id) {
  std::lock_guard lock(mutex_);

  auto do_consume = [&](ObjectLockSharedState& state) REQUIRES(mutex_) PARENT_PROCESS_ONLY {
    CallWithRequestConsumer(
        state,
        [&state](auto&& c) PARENT_PROCESS_ONLY { state.ConsumePendingLockRequests(c); },
        consume);
  };

  ParentProcessGuard g;
  if (txn_id) {
    if (auto owner = registry_.GetOwnerInfo(txn_id)) {
      do_consume(*owner->shared);
    }
  } else {
    for (auto& state : shared_states_) {
      do_consume(*state);
    }
  }
}

void ObjectLockSharedStateManager::ConsumeAndAcquireExclusiveLockIntents(
    const LockRequestConsumer& consume,
    std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) {
  std::lock_guard lock(mutex_);
  for (const auto* key_and_intent : lock_entries) {
    write_locks_[key_and_intent->key] +=
        LockStateToSharedWriteLockState(IntentTypeSetAdd(key_and_intent->intent_types));
  }

  ParentProcessGuard g;
  for (auto& state : shared_states_) {
    CallWithRequestConsumer(
        *state,
        [&state, lock_entries](auto&& c) PARENT_PROCESS_ONLY {
          state->ConsumeAndAcquireExclusiveLockIntents(c, lock_entries);
        },
        consume);
  }
}

void ObjectLockSharedStateManager::DropPendingSharedLockRequests(TransactionId txn_id) {
  std::lock_guard lock(mutex_);
  ParentProcessGuard g;
  if (auto owner = registry_.GetOwnerInfo(txn_id)) {
    owner->shared->ForceDropAll();
  }
}

void ObjectLockSharedStateManager::ReleaseExclusiveLockIntent(
    const ObjectLockPrefix& object_id, LockState lock_state) {
  std::lock_guard lock(mutex_);
  auto iter = write_locks_.find(object_id);
  DCHECK(iter != write_locks_.end());
  SharedWriteLockStateRelease(iter->second, LockStateToSharedWriteLockState(lock_state));
  if (iter->second == 0) {
    write_locks_.erase(iter);
  }

  ParentProcessGuard g;
  for (auto& state : shared_states_) {
    state->ReleaseExclusiveLockIntent(object_id, lock_state);
  }
}

void ObjectLockSharedStateManager::MarkTServerLoaded(TransactionId txn_id) {
  if (auto owner_info = registry_.GetOwnerInfo(txn_id)) {
    ParentProcessGuard g;
    owner_info->shared->MarkTServerLoaded();
  }
}

uint64_t ObjectLockSharedStateManager::CalculateMetric(const MetricInfo& metric) const {
  std::lock_guard lock(mutex_);
  uint64_t count = std::invoke(metric.accumulator_field, *this);
  for (const auto& state : shared_states_) {
    count += std::invoke(metric.shared_state_value, *state);
  }
  return count;
}

bool ObjectLockSharedStateManager::TEST_has_exclusive_intents() const {
  std::lock_guard lock(mutex_);
  ParentProcessGuard g;
  return std::ranges::any_of(
      shared_states_, [](const auto& state) PARENT_PROCESS_ONLY {
        return state->TEST_has_exclusive_intents();
      });
}

template<typename ConsumeMethod>
void ObjectLockSharedStateManager::CallWithRequestConsumer(
    ObjectLockSharedState& shared, ConsumeMethod&& method, const LockRequestConsumer& consume) {
  auto owner_info = registry_.GetOwnerInfo(shared);
  auto consume_fastpath_request = [this, &owner_info, &consume](ObjectLockFastpathRequest request) {
    if (!owner_info) {
      return;
    }
    ObjectLockOwner owner(owner_info->txn_id, request.subtxn_id);
    auto entries = GetEntriesForFastpathLockType(request.lock_type);
    for (const auto& [entry_type, intent_types] : entries) {
      consume(ObjectSharedLockRequest{
          .owner = owner,
          .status_tablet = owner_info->status_tablet,
          .entry = {
              .key = MakeLockPrefix(request, entry_type),
              .intent_types = intent_types}});
    }

    // Track fastpath object locks for pg_locks.
    object_lock_tracker_->TrackLock(
        ObjectLockContext{
            owner_info->txn_id, request.subtxn_id, request.database_oid, request.relation_oid,
            request.object_oid, request.object_sub_oid,
            FastpathLockTypeToTableLockType(request.lock_type)},
        ObjectLockState::GRANTED);
  };

  ParentProcessGuard g;
  method(make_lw_function(consume_fastpath_request));
}

} // namespace yb::docdb
