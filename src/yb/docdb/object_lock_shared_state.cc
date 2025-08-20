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

struct GroupLockState {
  std::atomic<uint64_t> exclusive_intents{0};
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

    if (!request.owner) {
      VLOG_WITH_FUNC(1) << AsString(request) << ": No owner tag, cannot use fastpath";
      return false;
    }

    const auto& lock_states = SHARED_MEMORY_LOAD(lock_states_);
    for (const auto& [entry_type, _] : GetEntriesForFastpathLockType(request.lock_type)) {
      ObjectLockPrefix object_id(
          request.database_oid, request.relation_oid, request.object_oid, request.object_sub_oid,
          entry_type);
      const auto& group_entry = lock_states[GroupFor(object_id)];
      if (group_entry.exclusive_intents > 0) {
        VLOG_WITH_FUNC(1) << AsString(request) << ": exclusive intents exist, fastpath unusable";
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

  size_t ConsumePendingLockRequests(const FastLockRequestConsumer& consume)
      EXCLUDES(mutex_) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    return shared_requests_.ConsumeLockRequests(consume);
  }

  size_t ConsumeAndAcquireExclusiveLockIntents(
      const FastLockRequestConsumer& consume,
      std::span<const ObjectLockPrefix*> object_ids) PARENT_PROCESS_ONLY {
    std::lock_guard lock(mutex_);
    size_t consumed = shared_requests_.ConsumeLockRequests(consume);
    for (auto object_id : object_ids) {
      AcquireExclusiveLockIntent(*object_id);
    }
    return consumed;
  }

  void ReleaseExclusiveLockIntent(const ObjectLockPrefix& object_id, size_t count)
      PARENT_PROCESS_ONLY {
    auto& group_entry = group(object_id);
    VLOG_WITH_FUNC(1) << AsString(object_id) << ": " << count;
    [[maybe_unused]] auto value = group_entry.exclusive_intents.fetch_sub(count);
    DCHECK_GE(value, count);
  }

  [[nodiscard]] SessionLockOwnerTag TEST_last_owner() PARENT_PROCESS_ONLY EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return shared_requests_.TEST_last_owner();
  }

 private:
  void AcquireExclusiveLockIntent(const ObjectLockPrefix& object_id) PARENT_PROCESS_ONLY {
    auto& group_entry = group(object_id);
    VLOG_WITH_FUNC(1) << AsString(object_id);
    ++group_entry.exclusive_intents;
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
};

ObjectLockSharedState::ObjectLockSharedState(SharedMemoryBackingAllocator& allocator)
    : impl_{CHECK_RESULT(allocator.MakeUnique<Impl>())} {}

ObjectLockSharedState::~ObjectLockSharedState() = default;

bool ObjectLockSharedState::Lock(const ObjectLockFastpathRequest& request) {
  return impl_->Lock(request);
}

size_t ObjectLockSharedState::ConsumeAndAcquireExclusiveLockIntents(
    const FastLockRequestConsumer& consume,
    std::span<const ObjectLockPrefix*> object_ids) {
  return impl_->ConsumeAndAcquireExclusiveLockIntents(consume, object_ids);
}

void ObjectLockSharedState::ReleaseExclusiveLockIntent(
    const ObjectLockPrefix& object_id, size_t count) {
  impl_->ReleaseExclusiveLockIntent(object_id, count);
}

size_t ObjectLockSharedState::ConsumePendingLockRequests(const FastLockRequestConsumer& consume) {
  return impl_->ConsumePendingLockRequests(consume);
}

SessionLockOwnerTag ObjectLockSharedState::TEST_last_owner() {
  return impl_->TEST_last_owner();
}

} // namespace yb::docdb
