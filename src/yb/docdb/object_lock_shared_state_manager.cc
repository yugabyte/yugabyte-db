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

#include "yb/docdb/object_lock_shared_state.h"

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
  RegistrationGuard Register(const TransactionId& id, const TabletId& tablet_id) {
    const auto tag = next_++;
    CHECK_NE(tag, 0);

    std::lock_guard lock(mutex_);
    owners_[tag] = std::make_shared<OwnerInfo>(id, tablet_id);
    return {*this, tag};
  }

  void Unregister(SessionLockOwnerTag tag) {
    std::lock_guard lock(mutex_);
    [[maybe_unused]] auto erased = owners_.erase(tag);
    DCHECK_EQ(erased, 1);
  }

  [[nodiscard]] std::shared_ptr<OwnerInfo> GetOwnerInfo(SessionLockOwnerTag tag) const {
    std::lock_guard lock(mutex_);
    const auto i = owners_.find(tag);
    if (PREDICT_TRUE(i != owners_.end())) {
      return i->second;
    }
    return {};
  }

 private:
  mutable std::mutex mutex_;
  std::atomic<SessionLockOwnerTag> next_ = 1;
  std::unordered_map<SessionLockOwnerTag, std::shared_ptr<OwnerInfo>> owners_ GUARDED_BY(mutex_);
};

ObjectLockOwnerRegistry::RegistrationGuard::~RegistrationGuard() {
  registry_.Unregister(tag_);
}

ObjectLockOwnerRegistry::ObjectLockOwnerRegistry() : impl_(std::make_unique<Impl>()) {}

ObjectLockOwnerRegistry::~ObjectLockOwnerRegistry() = default;

ObjectLockOwnerRegistry::RegistrationGuard ObjectLockOwnerRegistry::Register(
    const TransactionId& id, const TabletId& status_tablet) {
  return impl_->Register(id, status_tablet);
}

std::shared_ptr<ObjectLockOwnerRegistry::OwnerInfo>
ObjectLockOwnerRegistry::GetOwnerInfo(SessionLockOwnerTag tag) const {
  return impl_->GetOwnerInfo(tag);
}

void ObjectLockSharedStateManager::SetupShared(ObjectLockSharedState& shared) {
  ParentProcessGuard g;
  std::lock_guard lock(setup_mutex_);

  DCHECK(!shared_.load(std::memory_order_relaxed));

  shared_activate_ = shared.Activate(std::move(pre_setup_locks_));
  shared_.store(&shared, std::memory_order_release);
}

size_t ObjectLockSharedStateManager::ConsumePendingSharedLockRequests(
    const LockRequestConsumer& consume) {
  auto* shared = shared_.load(std::memory_order_relaxed);
  return CallWithRequestConsumer(
      shared,
      [shared](auto&& c) PARENT_PROCESS_ONLY { return shared->ConsumePendingLockRequests(c); },
      consume);
}

size_t ObjectLockSharedStateManager::ConsumeAndAcquireExclusiveLockIntents(
    const LockRequestConsumer& consume,
    std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) {
  auto* shared = shared_.load(std::memory_order_relaxed);
  if (PREDICT_FALSE(!shared)) {
    std::lock_guard lock(setup_mutex_);
    shared = shared_.load(std::memory_order_acquire);
    if (!shared) {
      for (const auto* key_and_intent : lock_entries) {
        pre_setup_locks_[key_and_intent->key] += IntentTypeSetAdd(key_and_intent->intent_types);
      }
      return 0uz;
    }
  }

  return CallWithRequestConsumer(
      shared,
      [shared, lock_entries](auto&& c) PARENT_PROCESS_ONLY {
        return shared->ConsumeAndAcquireExclusiveLockIntents(c, lock_entries);
      },
      consume);
}

void ObjectLockSharedStateManager::ReleaseExclusiveLockIntent(
    const ObjectLockPrefix& object_id, LockState lock_state) {
  auto* shared = shared_.load(std::memory_order_relaxed);
  if (PREDICT_FALSE(!shared)) {
    std::lock_guard lock(setup_mutex_);
    shared = shared_.load(std::memory_order_acquire);
    if (!shared) {
      pre_setup_locks_[object_id] -= lock_state;
      return;
    }
  }

  ParentProcessGuard g;
  shared->ReleaseExclusiveLockIntent(object_id, lock_state);
}

TransactionId ObjectLockSharedStateManager::TEST_last_owner() const {
  ParentProcessGuard g;
  return registry_.GetOwnerInfo(
      DCHECK_NOTNULL(shared_.load(std::memory_order_relaxed))->TEST_last_owner())->txn_id;
}

[[nodiscard]] bool ObjectLockSharedStateManager::TEST_has_exclusive_intents() const {
  auto* shared = shared_.load(std::memory_order_relaxed);
  ParentProcessGuard g;
  return shared ? shared->TEST_has_exclusive_intents() : 0;
}

template<typename ConsumeMethod>
size_t ObjectLockSharedStateManager::CallWithRequestConsumer(
    ObjectLockSharedState* shared, ConsumeMethod&& method, const LockRequestConsumer& consume) {
  if (!shared) {
    return 0;
  }

  auto consume_fastpath_request = [this, &consume](ObjectLockFastpathRequest request) {
    auto owner_info = registry_.GetOwnerInfo(request.owner);
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
  return method(make_lw_function(consume_fastpath_request));
}

} // namespace yb::docdb
