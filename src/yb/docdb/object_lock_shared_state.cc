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

#include "yb/util/crash_point.h"
#include "yb/util/lw_function.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/shmem/robust_mutex.h"
#include "yb/util/shmem/shared_mem_allocator.h"

namespace yb::docdb {

namespace {

constexpr size_t kNumGroups = 4096;
constexpr size_t kMaxFastpathRequests = 4096;

struct FastpathLockRequestEntry {
  ObjectLockFastpathRequest request;
  ChildProcessRW<bool> finalized = false;
};

class PendingLockRequests {
 public:
  bool AddLockRequest(const ObjectLockFastpathRequest& request) {
    if (!FreeLockRequestsAvailable()) {
      return false;
    }

    size_t index = SHARED_MEMORY_LOAD(next_);
    SHARED_MEMORY_STORE(next_, index + 1);

    auto& r = requests_[index];

    std::memcpy(&r.request, &request, sizeof(ObjectLockFastpathRequest));
    TEST_CRASH_POINT("ObjectLockSharedState::AddLockRequest:unfinalized");

    // Mark the lock request has having been completely filled out. If we crash before this line,
    // next_ has been incremented, so future requests will not touch the request we were filling
    // out, but finalized has not been set, so we can just skip it when processing.
    SHARED_MEMORY_STORE(r.finalized, true);
    TEST_CRASH_POINT("ObjectLockSharedState::AddLockRequest:finalized");

    return true;
  }

  size_t ConsumeLockRequests(const FastLockRequestConsumer& consume) PARENT_PROCESS_ONLY {
    size_t end = next_.Get();
    for (size_t i = 0; i < end; ++i) {
      auto& entry = requests_[i];
      if (!entry.finalized.Get()) {
        continue;
      }
      consume(entry.request);
      SHARED_MEMORY_STORE(entry.finalized, false);
    }
    UpdateLastOwner();
    SHARED_MEMORY_STORE(next_, 0);
    return end;
  }

  SessionLockOwnerTag TEST_last_owner() PARENT_PROCESS_ONLY {
    UpdateLastOwner();
    return TEST_last_owner_.Get();
  }

 private:
  bool FreeLockRequestsAvailable() const {
    return SHARED_MEMORY_LOAD(next_) < requests_.size();
  }

  void UpdateLastOwner() PARENT_PROCESS_ONLY {
    auto next = next_.Get();
    if (next > 0) {
      SHARED_MEMORY_STORE(TEST_last_owner_, requests_[next - 1].request.owner);
    }
  }

  std::array<FastpathLockRequestEntry, kMaxFastpathRequests> requests_;
  ChildProcessRW<size_t> next_ = 0;

  ChildProcessForbidden<SessionLockOwnerTag> TEST_last_owner_;
};

// State tracking active kStrongWrite, kWeakWrite intent types at the tserver's Object lock Manager.
// - first 32 bits store the num_active kStrongWrite
// - last 32 bits store the num_active kWeakWrite
//
// Since fastpath object locking is enabled for kAccessShare, kRowShare & kRowExclusive alone, all
// of which request intent_type(s) kWeakRead/kStrongRead, it is sufficient to just track active
// write intent types for detecting fast path locking conflicts. Hence not reusing LockState here.
//
// Additionally, since write lock state for multiple objects (with same hash) is stored in the same
// entry, it is better to not use LockState here as it could potentially lead to overflow.
using SharedWriteLockState = uint64_t;

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

SharedWriteLockState IncSharedWriteLockState(LockState lock_state) {
  static constexpr auto kWeakWriteBitShift =
      std::to_underlying(dockv::IntentType::kWeakWrite) * kIntentTypeBits;
  static constexpr auto kStrongWriteBitShift =
      std::to_underlying(dockv::IntentType::kStrongWrite) * kIntentTypeBits;
  return ((lock_state >> kWeakWriteBitShift) & kFirstIntentTypeMask) +
         (((lock_state >> kStrongWriteBitShift) & kFirstIntentTypeMask) <<
               kSharedWriteLockStateBits);
}

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
 public:
  [[nodiscard]] bool Lock(const ObjectLockFastpathRequest& request) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);

    if (waiting_for_manager_) {
      VLOG_WITH_FUNC(1)
          << AsString(request) << ": waiting for ObjectLockSharedStateManager, cannot use fastpath";
      return false;
    }

    if (!request.owner) {
      VLOG_WITH_FUNC(1) << AsString(request) << ": No owner tag, cannot use fastpath";
      return false;
    }

    const auto& lock_states = SHARED_MEMORY_LOAD(lock_states_);
    for (const auto& [entry_type, intent_type] : GetEntriesForFastpathLockType(request.lock_type)) {
      ObjectLockPrefix object_id(
          request.database_oid, request.relation_oid, request.object_oid, request.object_sub_oid,
          entry_type);
      const auto& group_entry = lock_states[GroupFor(object_id)];
      if ((group_entry.exclusive_intents & SharedWriteTypeSetConflict(intent_type)) > 0) {
        VLOG_WITH_FUNC(1)
            << AsString(request) << ": exclusive intents exist, fastpath unusable. "
            << "exclusive_intents: " << DebugSharedWriteLockStateStr(group_entry.exclusive_intents)
            << ", requested intent_type: " << AsString(intent_type);
        return false;
      }
    }

    if (!shared_requests_.AddLockRequest(request)) {
      LOG(WARNING) << AsString(request) << ": too many active fastpath requests, "
                   << "adjust object_locking_num_fastpath_requests";
      return false;
    }

    VLOG_WITH_FUNC(1) << AsString(request) << ": added request";
    return true;
  }

  ObjectLockSharedState::ActivationGuard Activate(
      const std::unordered_map<ObjectLockPrefix, LockState>& initial_intents)
      EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    DCHECK(waiting_for_manager_);
    LOG_WITH_FUNC(INFO) << "Activating with initial exclusive lock intents: "
                        << CollectionToString(initial_intents);
    for (const auto& [object_id, lock_state] : initial_intents) {
      AcquireExclusiveLockIntent(object_id, lock_state);
    }
    waiting_for_manager_ = false;
    return ObjectLockSharedState::ActivationGuard(this);
  }

  void Deactivate() EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    DCHECK(!waiting_for_manager_);
    LOG_WITH_FUNC(INFO) << "Deactivating object lock shared state";
    waiting_for_manager_ = true;
  }

  size_t ConsumePendingLockRequests(const FastLockRequestConsumer& consume)
      EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    return shared_requests_.ConsumeLockRequests(consume);
  }

  size_t ConsumeAndAcquireExclusiveLockIntents(
      const FastLockRequestConsumer& consume,
      std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    size_t consumed = shared_requests_.ConsumeLockRequests(consume);
    for (auto key_and_intent : lock_entries) {
      AcquireExclusiveLockIntent(
          key_and_intent->key, IntentTypeSetAdd(key_and_intent->intent_types));
    }
    return consumed;
  }

  void ReleaseExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state)
      PARENT_PROCESS_ONLY {
    auto& group_entry = group(object_id);
    VLOG_WITH_FUNC(1) << AsString(object_id) << ": " << LockStateDebugString(lock_state);
    const auto sub = IncSharedWriteLockState(lock_state);
    [[maybe_unused]] auto value = group_entry.exclusive_intents.fetch_sub(sub);
    DCHECK_GE(value, sub);
  }

  [[nodiscard]] SessionLockOwnerTag TEST_last_owner() PARENT_PROCESS_ONLY EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return shared_requests_.TEST_last_owner();
  }

  [[nodiscard]] bool TEST_has_exclusive_intents() PARENT_PROCESS_ONLY {
    return std::ranges::any_of(lock_states_.Get(), [](GroupLockState& lock_state) {
      return lock_state.exclusive_intents > 0;
    });
  }

 private:
  void AcquireExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state)
      PARENT_PROCESS_ONLY {
    auto& group_entry = group(object_id);
    VLOG_WITH_FUNC(1) << AsString(object_id);
    group_entry.exclusive_intents.fetch_add(IncSharedWriteLockState(lock_state));
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

  bool waiting_for_manager_ GUARDED_BY(mutex_) = true;
};

ObjectLockSharedState::ActivationGuard::ActivationGuard(Impl* impl) : impl_{impl} {}

ObjectLockSharedState::ActivationGuard::ActivationGuard(ActivationGuard&& other)
    : impl_{std::exchange(other.impl_, nullptr)} {}

ObjectLockSharedState::ActivationGuard::~ActivationGuard() {
  if (impl_) {
    impl_->Deactivate();
  }
}

ObjectLockSharedState::ActivationGuard&
ObjectLockSharedState::ActivationGuard::operator=(ActivationGuard&& other) {
  if (impl_) {
    impl_->Deactivate();
  }
  impl_ = std::exchange(other.impl_, nullptr);
  return *this;
}

ObjectLockSharedState::ObjectLockSharedState(SharedMemoryBackingAllocator& allocator)
    : impl_{CHECK_RESULT(allocator.MakeUnique<Impl>())} {}

ObjectLockSharedState::~ObjectLockSharedState() = default;

bool ObjectLockSharedState::Lock(const ObjectLockFastpathRequest& request) {
  return impl_->Lock(request);
}

ObjectLockSharedState::ActivationGuard ObjectLockSharedState::Activate(
    const std::unordered_map<ObjectLockPrefix, LockState>& initial_intents) {
  return impl_->Activate(initial_intents);
}

size_t ObjectLockSharedState::ConsumeAndAcquireExclusiveLockIntents(
    const FastLockRequestConsumer& consume,
    std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) {
  return impl_->ConsumeAndAcquireExclusiveLockIntents(consume, lock_entries);
}

void ObjectLockSharedState::ReleaseExclusiveLockIntent(
    const ObjectLockPrefix& object_id, LockState lock_state) {
  impl_->ReleaseExclusiveLockIntent(object_id, lock_state);
}

size_t ObjectLockSharedState::ConsumePendingLockRequests(const FastLockRequestConsumer& consume) {
  return impl_->ConsumePendingLockRequests(consume);
}

SessionLockOwnerTag ObjectLockSharedState::TEST_last_owner() {
  return impl_->TEST_last_owner();
}

bool ObjectLockSharedState::TEST_has_exclusive_intents() {
  return impl_->TEST_has_exclusive_intents();
}

} // namespace yb::docdb
