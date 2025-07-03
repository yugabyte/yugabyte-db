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

auto GetEntriesForFastpathLockType(ObjectLockFastpathLockType lock_type) {
  return docdb::GetEntriesForLockType(FastpathLockTypeToTableLockType(lock_type));
}

ObjectLockPrefix MakeLockPrefix(
    const ObjectLockFastpathRequest& request, dockv::KeyEntryType entry_type) {
  return ObjectLockPrefix(
      request.database_oid, request.relation_oid, request.object_oid, request.object_sub_oid,
      entry_type);
}

} // namespace

class ObjectLockOwnerRegistry::Impl {
 public:
  RegistrationGuard Register(const TransactionId& id) {
    const auto tag = next_++;
    CHECK_NE(tag, 0);

    std::lock_guard lock(mutex_);
    ids_[tag] = id;
    return {*this, tag};
  }

  void Unregister(SessionLockOwnerTag tag) {
    std::lock_guard lock(mutex_);
    [[maybe_unused]] auto erased = ids_.erase(tag);
    DCHECK_EQ(erased, 1);
  }

  [[nodiscard]] TransactionId GetTransactionId(SessionLockOwnerTag tag) const {
    std::lock_guard lock(mutex_);
    const auto i = ids_.find(tag);
    if (PREDICT_TRUE(i != ids_.end())) {
      return i->second;
    }
    LOG(DFATAL) << "Attempting to access non registered lock owner tag";
    return TransactionId::Nil();
  }

 private:
  mutable std::mutex mutex_;
  std::atomic<SessionLockOwnerTag> next_ = 1;
  std::unordered_map<SessionLockOwnerTag, TransactionId> ids_ GUARDED_BY(mutex_);
};

ObjectLockOwnerRegistry::RegistrationGuard::~RegistrationGuard() {
  registry_.Unregister(tag_);
}

ObjectLockOwnerRegistry::ObjectLockOwnerRegistry() : impl_(std::make_unique<Impl>()) {}

ObjectLockOwnerRegistry::~ObjectLockOwnerRegistry() = default;

ObjectLockOwnerRegistry::RegistrationGuard ObjectLockOwnerRegistry::Register(
    const TransactionId& id) {
  return impl_->Register(id);
}

TransactionId ObjectLockOwnerRegistry::GetTransactionId(SessionLockOwnerTag tag) const {
  return impl_->GetTransactionId(tag);
}

void ObjectLockSharedStateManager::SetupShared(ObjectLockSharedState& shared) {
  DCHECK(!shared_);
  shared_ = &shared;
}

void ObjectLockSharedStateManager::ConsumePendingSharedLockRequests(
    const LockRequestConsumer& consume) {
  ParentProcessGuard g;
  if (!shared_) {
    return;
  }
  auto consume_fastpath_request = [&](ObjectLockFastpathRequest request) {
    auto txn_id = registry_.GetTransactionId(request.owner);
    if (txn_id.IsNil()) {
      return;
    }
    ObjectLockOwner owner(txn_id, request.subtxn_id);
    auto entries = GetEntriesForFastpathLockType(request.lock_type);
    for (const auto& [entry_type, intent_types] : entries) {
      consume(ObjectSharedLockRequest{
          .owner = owner,
          .entry = {
              .key = MakeLockPrefix(request, entry_type),
              .intent_types = intent_types}});
    }
  };
  shared_->ConsumePendingLockRequests(make_lw_function(consume_fastpath_request));
}

TransactionId ObjectLockSharedStateManager::TEST_last_owner() const {
  ParentProcessGuard g;
  return registry_.GetTransactionId(DCHECK_NOTNULL(shared_)->TEST_last_owner());
}

} // namespace yb::docdb
