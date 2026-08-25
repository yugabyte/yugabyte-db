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

#include "yb/docdb/object_lock_shared_state.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/object_lock_data.h"

#include "yb/util/enums.h"
#include "yb/util/crash_point.h"
#include "yb/util/lw_function.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/shmem/robust_mutex.h"
#include "yb/util/shmem/shared_mem_allocator.h"

namespace yb::docdb {

namespace {

YB_DEFINE_ENUM(ActiveState, (kDisabled)(kEnabled)(kShutdown));

constexpr size_t kNumGroups = 4096;
constexpr size_t kMaxFastpathRequests = 256;

struct FastpathLockRequestEntry {
  ObjectLockFastpathRequest request;
};

class PendingLockRequests {
 public:
  // `accounting_lock_count` is the number of postgres-side acquired locks to count this request as.
  // One fastpath request can correspond to multiple actual locks, and we use this count for metrics
  // accounting to match ObjectLockManager metrics.
  bool AddLockRequest(const ObjectLockFastpathRequest& request, size_t accounting_lock_count) {
    size_t index = SHARED_MEMORY_LOAD(next_);
    if (index >= requests_.size()) {
      return false;
    }

    auto& r = requests_[index];
    std::memcpy(&r.request, &request, sizeof(ObjectLockFastpathRequest));
    TEST_CRASH_POINT("ObjectLockSharedState::AddLockRequest:unfinalized");

    // If we crash before this line, next_ has not been incremented so consuming requests will not
    // reach the incomplete request object. This state is per-session, so there will not be any
    // future requests added either.
    SHARED_MEMORY_STORE(next_, index + 1);
    TEST_CRASH_POINT("ObjectLockSharedState::AddLockRequest:finalized");

    // This may not get updated in event of crash. This is for metrics only, so some inaccuracy
    // is acceptable, and we don't force update at same instruction as next_ for simplicity.
    SHARED_MEMORY_STORE(pg_requests_, SHARED_MEMORY_LOAD(pg_requests_) + accounting_lock_count);

    return true;
  }

  bool Reset(bool accounted) {
    auto requests_pending = SHARED_MEMORY_LOAD(next_);
    if (requests_pending == 0) {
      return false;
    }
    SHARED_MEMORY_STORE(next_, 0);

    // This may not get updated in event of crash. This is for metrics only, so some inaccuracy
    // is acceptable, and we don't force update at same instruction as next_ for simplicity.
    if (accounted) {
      SHARED_MEMORY_STORE(pg_releases_, SHARED_MEMORY_LOAD(pg_releases_) + 1);
    }

    return true;
  }

  size_t PgLockRequestCount() const {
    return SHARED_MEMORY_LOAD(pg_requests_);
  }

  size_t PgLockReleaseCount() const {
    return SHARED_MEMORY_LOAD(pg_releases_);
  }

  bool ConsumeLockRequests(const FastLockRequestConsumer& consume) PARENT_PROCESS_ONLY {
    size_t end = next_.Get();
    for (size_t i = 0; i < end; ++i) {
      auto& entry = requests_[i];
      consume(entry.request);
    }
    SHARED_MEMORY_STORE(next_, 0);
    return end > 0;
  }

 private:
  std::array<FastpathLockRequestEntry, kMaxFastpathRequests> requests_;
  ChildProcessRW<size_t> next_ = 0;

  // Counters for metrics. These may not be completely accurate in event of crash.
  ChildProcessRW<size_t> pg_requests_ = 0;
  ChildProcessRW<size_t> pg_releases_ = 0;
};

constexpr size_t kSharedWriteLockStateBits = 2 * kIntentTypeBits;

constexpr SharedWriteLockState kSharedWriteStateMask =
    (static_cast<SharedWriteLockState>(1) << kSharedWriteLockStateBits) - 1;

SharedWriteLockState WriteStateTypeMask(dockv::IntentType intent_type) {
  return kSharedWriteStateMask <<
      ((std::to_underlying(intent_type) >> 1) * kSharedWriteLockStateBits);
}

std::array<SharedWriteLockState, dockv::kIntentTypeSetMapSize> GenerateWriteConflicts() {
  std::array<SharedWriteLockState, dockv::kIntentTypeSetMapSize> result;
  for (size_t idx = 0; idx < dockv::kIntentTypeSetMapSize; ++idx) {
    auto intent_types = dockv::IntentTypeSet(idx);
    if (!IntentTypeReadOnly(intent_types)) {
      result[idx] = std::numeric_limits<SharedWriteLockState>::max();
      continue;
    }
    result[idx] = 0;
    for (auto intent_type : intent_types) {
      for (auto other_intent_type : dockv::IntentTypeList()) {
        if (IntentTypesConflict(intent_type, other_intent_type)) {
          result[idx] |= WriteStateTypeMask(other_intent_type);
        }
      }
    }
  }
  return result;
}

const std::array<SharedWriteLockState, dockv::kIntentTypeSetMapSize>
    kWriteIntentTypeSetConflicts = GenerateWriteConflicts();

SharedWriteLockState SharedWriteTypeSetConflict(dockv::IntentTypeSet intent_types) {
  return kWriteIntentTypeSetConflicts[intent_types.ToUIntPtr()];
}

std::string DebugSharedWriteLockStateStr(SharedWriteLockState state) {
  return Format(
      "{ num_strong_write: $0, num_weak_write: $1 }",
      (state >> kSharedWriteLockStateBits) & kSharedWriteStateMask,
      state & kSharedWriteStateMask);
}

struct GroupLockState {
  std::atomic<SharedWriteLockState> exclusive_intents{0};
};

} // namespace

SharedWriteLockState LockStateToSharedWriteLockState(LockState lock_state) {
  static constexpr auto kWeakWriteBitShift =
      std::to_underlying(dockv::IntentType::kWeakWrite) * kIntentTypeBits;
  static constexpr auto kStrongWriteBitShift =
      std::to_underlying(dockv::IntentType::kStrongWrite) * kIntentTypeBits;
  return ((lock_state >> kWeakWriteBitShift) & kFirstIntentTypeMask) +
         (((lock_state >> kStrongWriteBitShift) & kFirstIntentTypeMask) <<
               kSharedWriteLockStateBits);
}

void SharedWriteLockStateRelease(SharedWriteLockState& held, SharedWriteLockState release) {
  auto weak_write_sub = std::min(held & kSharedWriteStateMask, release & kSharedWriteStateMask);
  auto strong_write_sub = std::min(
     (held >> kSharedWriteLockStateBits) & kSharedWriteStateMask,
     (release >> kSharedWriteLockStateBits) & kSharedWriteStateMask);
  auto sub = strong_write_sub << kSharedWriteLockStateBits | weak_write_sub;
  LOG_IF(DFATAL, sub != release)
      << "Attempting to release " << DebugSharedWriteLockStateStr(release) << " but only hold "
      << DebugSharedWriteLockStateStr(held);
  held -= sub;
}

TableLockType FastpathLockTypeToTableLockType(ObjectLockFastpathLockType lock_type) {
  switch (lock_type) {
    case ObjectLockFastpathLockType::kAccessShare:
      return TableLockType::ACCESS_SHARE;
    case ObjectLockFastpathLockType::kRowShare:
      return TableLockType::ROW_SHARE;
    case ObjectLockFastpathLockType::kRowExclusive:
      return TableLockType::ROW_EXCLUSIVE;
  }
  FATAL_INVALID_ENUM_VALUE(ObjectLockFastpathLockType, lock_type);
}

std::optional<ObjectLockFastpathLockType> MakeObjectLockFastpathLockType(TableLockType lock_type) {
  switch (lock_type) {
    case TableLockType::ACCESS_SHARE:
      return ObjectLockFastpathLockType::kAccessShare;
    case TableLockType::ROW_SHARE:
      return ObjectLockFastpathLockType::kRowShare;
    case TableLockType::ROW_EXCLUSIVE:
      return ObjectLockFastpathLockType::kRowExclusive;
    default:
      return std::nullopt;
  }
}

std::span<const LockTypeEntry> GetEntriesForFastpathLockType(
    ObjectLockFastpathLockType lock_type) {
  return GetEntriesForLockType(FastpathLockTypeToTableLockType(lock_type));
}

class ObjectLockSharedState::Impl {
  enum class UnlockResult {
    kFastpathUnusable,
    kNoLocks,
    kDroppedLocks,
  };

 public:
  explicit Impl(const std::unordered_map<ObjectLockPrefix, SharedWriteLockState>& initial_intents) {
    for (const auto& [object_id, lock_state] : initial_intents) {
      LoadExclusiveLockIntent(object_id, lock_state);
    }
  }

  [[nodiscard]] bool Lock(const ObjectLockFastpathRequest& request) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return DoLock(request, /*account_to_pg=*/true);
  }

  [[nodiscard]] bool UnlockAll() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return DoUnlockAll(/*accounted_to_pg=*/true) != UnlockResult::kFastpathUnusable;
  }

  [[nodiscard]] bool TServerLock(const ObjectLockFastpathRequest& request)
      PARENT_PROCESS_ONLY EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    if (auto num_locks = DoLock(request, /*account_to_pg=*/false)) {
      tserver_lock_requests_.Get() += num_locks;
      return true;
    }
    return false;
  }

  [[nodiscard]] bool TServerUnlockAll() PARENT_PROCESS_ONLY EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    auto result = DoUnlockAll(/*accounted_to_pg=*/false);
    if (result == UnlockResult::kFastpathUnusable) {
      return false;
    }
    if (result == UnlockResult::kDroppedLocks) {
      ++tserver_lock_releases_.Get();
    }
    return true;
  }

  void ForceDropAll() EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    DoDropAll();
    VLOG_WITH_FUNC(1) << "done";
  }

  void MarkTServerLoaded() EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    SHARED_MEMORY_STORE(tserver_loaded_, true);
    VLOG_WITH_FUNC(1) << "done";
  }

  void Enable() EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    if (SHARED_MEMORY_LOAD(enabled_) == ActiveState::kDisabled) {
      SHARED_MEMORY_STORE(enabled_, ActiveState::kEnabled);
      VLOG_WITH_FUNC(1) << "done";
    }
  }

  void Disable() EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    if (SHARED_MEMORY_LOAD(enabled_) == ActiveState::kEnabled) {
      DoDropAll();
      SHARED_MEMORY_STORE(enabled_, ActiveState::kDisabled);
      VLOG_WITH_FUNC(1) << "done";
    }
  }

  void Shutdown() EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    DoDropAll();
    SHARED_MEMORY_STORE(enabled_, ActiveState::kShutdown);
    VLOG_WITH_FUNC(1) << "done";
  }

  void ConsumePendingLockRequests(const FastLockRequestConsumer& consume)
      EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    ConsumePendingLockRequestsUnlocked(consume);
    VLOG_WITH_FUNC(1) << "done";
  }

  void ConsumeAndAcquireExclusiveLockIntents(
      const FastLockRequestConsumer& consume,
      std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    for (auto key_and_intent : lock_entries) {
      AcquireExclusiveLockIntent(
          key_and_intent->key, IntentTypeSetAdd(key_and_intent->intent_types));
    }
    ConsumePendingLockRequestsUnlocked(consume);
    VLOG_WITH_FUNC(1) << "done";
  }

  void ReleaseExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state)
      PARENT_PROCESS_ONLY {
    auto& group_entry = group(object_id);
    VLOG_WITH_FUNC(1) << AsString(object_id) << ": " << LockStateDebugString(lock_state);
    const auto sub = LockStateToSharedWriteLockState(lock_state);
    [[maybe_unused]] auto value = group_entry.exclusive_intents.fetch_sub(sub);
    DCHECK_GE(value, sub);
  }

  uint64_t PgLockRequestCount() const {
    std::lock_guard lock(mutex_);
    return shared_requests_.PgLockRequestCount();
  }

  uint64_t PgLockReleaseCount() const {
    std::lock_guard lock(mutex_);
    return shared_requests_.PgLockReleaseCount();
  }

  uint64_t TServerLockRequestCount() const {
    std::lock_guard lock(mutex_);
    return SHARED_MEMORY_LOAD(tserver_lock_requests_);
  }

  uint64_t TServerLockReleaseCount() const {
    std::lock_guard lock(mutex_);
    return SHARED_MEMORY_LOAD(tserver_lock_releases_);
  }

  [[nodiscard]] bool TEST_has_exclusive_intents() PARENT_PROCESS_ONLY {
    return std::ranges::any_of(lock_states_.Get(), [](GroupLockState& lock_state) {
      return lock_state.exclusive_intents > 0;
    });
  }

 private:
  [[nodiscard]] size_t DoLock(
      const ObjectLockFastpathRequest& request, bool account_to_pg) REQUIRES(mutex_) {
    if (SHARED_MEMORY_LOAD(enabled_) != ActiveState::kEnabled) {
      VLOG_WITH_FUNC(1) << AsString(request) << ": Shared state disabled, cannot use fastpath";
      return 0;
    }

    const auto& lock_states = SHARED_MEMORY_LOAD(lock_states_);
    auto entries = GetEntriesForFastpathLockType(request.lock_type);
    for (const auto& [entry_type, intent_type] : entries) {
      ObjectLockPrefix object_id(
          request.database_oid, request.relation_oid, request.object_oid, request.object_sub_oid,
          entry_type);
      const auto& group_entry = lock_states[GroupFor(object_id)];
      if ((group_entry.exclusive_intents & SharedWriteTypeSetConflict(intent_type)) > 0) {
        VLOG_WITH_FUNC(1)
            << AsString(request) << ": exclusive intents exist, fastpath unusable. "
            << "exclusive_intents: " << DebugSharedWriteLockStateStr(group_entry.exclusive_intents)
            << ", requested intent_type: " << AsString(intent_type);
        return 0;
      }
    }

    if (!shared_requests_.AddLockRequest(request, account_to_pg ? entries.size() : 0)) {
      LOG(WARNING) << AsString(request) << ": too many active fastpath requests";
      return 0;
    }

    VLOG_WITH_FUNC(1) << AsString(request) << ": added request";
    return entries.size();
  }

  [[nodiscard]] UnlockResult DoUnlockAll(bool accounted_to_pg) REQUIRES(mutex_) {
    if (SHARED_MEMORY_LOAD(enabled_) != ActiveState::kEnabled) {
      VLOG_WITH_FUNC(1) << "Shared state disabled, cannot use fastpath";
      return UnlockResult::kFastpathUnusable;
    }

    if (SHARED_MEMORY_LOAD(tserver_loaded_)) {
      VLOG_WITH_FUNC(1) << "TServer loaded locks, cannot use fastpath";
      return UnlockResult::kFastpathUnusable;
    }

    auto dropped = shared_requests_.Reset(accounted_to_pg);
    VLOG_WITH_FUNC(1) << "Dropped all requests";
    return dropped ? UnlockResult::kDroppedLocks : UnlockResult::kNoLocks;
  }

  void DoDropAll() REQUIRES(mutex_) PARENT_PROCESS_ONLY {
    if (shared_requests_.Reset(/*accounted=*/false)) {
      ++tserver_lock_releases_.Get();
    }
    SHARED_MEMORY_STORE(tserver_loaded_, false);
  }

  void AcquireExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state)
      PARENT_PROCESS_ONLY {
    VLOG_WITH_FUNC(1) << AsString(object_id) << ": " << LockStateDebugString(lock_state);
    LoadExclusiveLockIntent(object_id, LockStateToSharedWriteLockState(lock_state));
  }

  void LoadExclusiveLockIntent(
      const ObjectLockPrefix& object_id, SharedWriteLockState lock_state)
      PARENT_PROCESS_ONLY {
    auto& group_entry = group(object_id);
    group_entry.exclusive_intents.fetch_add(lock_state);
  }

  void ConsumePendingLockRequestsUnlocked(const FastLockRequestConsumer& consume)
      REQUIRES(mutex_) PARENT_PROCESS_ONLY {
    if (shared_requests_.ConsumeLockRequests(consume)) {
      SHARED_MEMORY_STORE(tserver_loaded_, true);
    }
  }

  [[nodiscard]] static size_t GroupFor(const ObjectLockPrefix& object_id) {
    return std::hash<ObjectLockPrefix>{}(object_id) % kNumGroups;
  }

  GroupLockState& group(const ObjectLockPrefix& object_id) PARENT_PROCESS_ONLY {
    return lock_states_.Get()[GroupFor(object_id)];
  }

  mutable RobustMutexNoCleanup mutex_;
  PendingLockRequests shared_requests_ GUARDED_BY(mutex_);
  ChildProcessRO<std::array<GroupLockState, kNumGroups>> lock_states_;
  ChildProcessRO<ActiveState> enabled_ GUARDED_BY(mutex_) = ActiveState::kDisabled;

  // Keep track of whether this session/transaction is registered with ObjectLockManager. If it is,
  // we need to release via ObjectLockManager to release that state, and cannot use fastpath
  // release.
  ChildProcessRO<bool> tserver_loaded_ GUARDED_BY(mutex_) = false;

  ChildProcessRO<size_t> tserver_lock_requests_ GUARDED_BY(mutex_) = 0;
  ChildProcessRO<size_t> tserver_lock_releases_ GUARDED_BY(mutex_) = 0;
};

ObjectLockSharedState::ObjectLockSharedState(
    SharedMemoryBackingAllocator& allocator,
    const std::unordered_map<ObjectLockPrefix, SharedWriteLockState>& initial_intents)
    : impl_{CHECK_RESULT(allocator.MakeUnique<Impl>(initial_intents))} {}

ObjectLockSharedState::~ObjectLockSharedState() = default;

bool ObjectLockSharedState::Lock(const ObjectLockFastpathRequest& request) {
  return impl_->Lock(request);
}

bool ObjectLockSharedState::UnlockAll() {
  return impl_->UnlockAll();
}

bool ObjectLockSharedState::TServerLock(const ObjectLockFastpathRequest& request) {
  return impl_->TServerLock(request);
}

bool ObjectLockSharedState::TServerUnlockAll() {
  return impl_->TServerUnlockAll();
}

void ObjectLockSharedState::ForceDropAll() {
  impl_->ForceDropAll();
}

void ObjectLockSharedState::MarkTServerLoaded() {
  impl_->MarkTServerLoaded();
}

void ObjectLockSharedState::Enable() {
  impl_->Enable();
}

void ObjectLockSharedState::Disable() {
  impl_->Disable();
}

void ObjectLockSharedState::Shutdown() {
  impl_->Shutdown();
}

void ObjectLockSharedState::ConsumeAndAcquireExclusiveLockIntents(
    const FastLockRequestConsumer& consume,
    std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) {
  impl_->ConsumeAndAcquireExclusiveLockIntents(consume, lock_entries);
}

void ObjectLockSharedState::ReleaseExclusiveLockIntent(
    const ObjectLockPrefix& object_id, LockState lock_state) {
  impl_->ReleaseExclusiveLockIntent(object_id, lock_state);
}

void ObjectLockSharedState::ConsumePendingLockRequests(const FastLockRequestConsumer& consume) {
  impl_->ConsumePendingLockRequests(consume);
}

uint64_t ObjectLockSharedState::PgLockRequestCount() const {
  return impl_->PgLockRequestCount();
}

uint64_t ObjectLockSharedState::PgLockReleaseCount() const {
  return impl_->PgLockReleaseCount();
}

uint64_t ObjectLockSharedState::TServerLockRequestCount() const {
  return impl_->TServerLockRequestCount();
}

uint64_t ObjectLockSharedState::TServerLockReleaseCount() const {
  return impl_->TServerLockReleaseCount();
}

bool ObjectLockSharedState::TEST_has_exclusive_intents() {
  return impl_->TEST_has_exclusive_intents();
}

} // namespace yb::docdb
