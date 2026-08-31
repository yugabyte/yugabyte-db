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

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "yb/common/transaction.h"
#include "yb/common/object_lock_tracker.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_data.h"
#include "yb/docdb/object_lock_shared_fwd.h"
#include "yb/docdb/object_lock_shared_state.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/lw_function.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/std_util.h"
#include "yb/util/tostring.h"

namespace yb::docdb {

struct ObjectSharedLockRequest {
  ObjectLockOwner owner;
  TabletId status_tablet;
  LockBatchEntry<ObjectLockManager> entry;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(owner, entry);
  }
};

using LockRequestConsumer = LWFunction<void(ObjectSharedLockRequest)>;

class ObjectLockOwnerRegistry {
  class Impl;

 public:
  class [[nodiscard]] RegistrationGuard {
   public:
    RegistrationGuard(Impl& registry, TransactionId id) : registry_(&registry), id_(id) {}
    RegistrationGuard(RegistrationGuard&& other)
        : registry_(std::exchange(other.registry_, nullptr)), id_(other.id_) {}
    ~RegistrationGuard();

    [[nodiscard]] TransactionId txn_id() const { return id_; }

   private:
    DISALLOW_COPY_AND_ASSIGN(RegistrationGuard);

    Impl* registry_;
    const TransactionId id_;
  };

  struct OwnerInfo {
    OwnerInfo(ObjectLockSharedState& shared_, TransactionId txn_id_, const TabletId& status_tablet_)
        : shared(&shared_), txn_id(txn_id_), status_tablet(status_tablet_) {}

    ObjectLockSharedState* shared;
    TransactionId txn_id;
    TabletId status_tablet;
  };
  ObjectLockOwnerRegistry();
  ~ObjectLockOwnerRegistry();

  RegistrationGuard Register(
      ObjectLockSharedState& shared, TransactionId id, const TabletId& tablet_id);

  [[nodiscard]] std::shared_ptr<OwnerInfo> GetOwnerInfo(TransactionId id) const;

  [[nodiscard]] std::shared_ptr<OwnerInfo> GetOwnerInfo(ObjectLockSharedState& state) const;

 private:
  std::unique_ptr<Impl> impl_;
};

class [[nodiscard]] ObjectLockSharedStateHolder {
 public:
  ObjectLockSharedStateHolder(ObjectLockSharedStateManager& manager, ObjectLockSharedState& state)
      : manager_{&manager}, state_{&state} {}

  ObjectLockSharedStateHolder(ObjectLockSharedStateHolder&& other)
      : manager_{std::exchange(other.manager_, nullptr)},
        state_{std::exchange(other.state_, nullptr)} {}

  ~ObjectLockSharedStateHolder();

  [[nodiscard]] constexpr ObjectLockSharedState* get() const { return state_; }
  constexpr ObjectLockSharedState* operator->() const { return state_; }
  constexpr ObjectLockSharedState& operator*() const { return *state_; }

 private:
  ObjectLockSharedStateManager* manager_;
  ObjectLockSharedState* state_;
};

class ObjectLockSharedStateManager {
 public:
  ObjectLockSharedStateManager(
      std::shared_ptr<ObjectLockTracker> object_lock_tracker,
      const MetricEntityPtr& metric_entity);

  void SetupShared(SharedMemoryBackingAllocator& allocator);

  Result<ObjectLockSharedStateHolder> AllocateShared();

  // Start allowing creation of new shared states.
  void Start();

  // Shutdown existing shared states and stop creation of new ones.
  // All requests are cleared and new requests are permanently blocked, but we do not free the
  // states immediately and instead expect the states to be freed normally (via destruction of the
  // guard object returned by AllocateShared).
  void Stop();

  [[nodiscard]] ObjectLockOwnerRegistry& registry() { return registry_; }

  // If txn_id is set, consumes only lock requests for that transaction. Otherwise, consumes all
  // lock requests for all transactions.
  void ConsumePendingSharedLockRequests(
      const LockRequestConsumer& consume, TransactionId txn_id = TransactionId::Nil());

  void ConsumeAndAcquireExclusiveLockIntents(
      const LockRequestConsumer& consume,
      std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries);

  void DropPendingSharedLockRequests(TransactionId txn_id);

  void ReleaseExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state);

  void MarkTServerLoaded(TransactionId txn_id);

  [[nodiscard]] bool TEST_has_exclusive_intents() const;

 private:
  friend class ObjectLockSharedStateHolder;
  void ReleaseShared(ObjectLockSharedState& state);

  struct MetricInfo;
  struct MetricInfos;
  uint64_t CalculateMetric(const MetricInfo& metric) const;

  template<typename ConsumeMethod>
  void CallWithRequestConsumer(
      ObjectLockSharedState& state, ConsumeMethod&& m, const LockRequestConsumer& consume)
      REQUIRES(mutex_);

  SharedMemoryBackingAllocator* allocator_ = nullptr;
  ObjectLockOwnerRegistry registry_;

  const std::shared_ptr<ObjectLockTracker> object_lock_tracker_;

  mutable std::mutex mutex_;
  bool stopped_ GUARDED_BY(mutex_) = true;
  std::condition_variable start_cond_;
  std::unordered_map<ObjectLockPrefix, SharedWriteLockState> write_locks_ GUARDED_BY(mutex_);

  PointerUnorderedSet<SharedMemoryUniquePtr<ObjectLockSharedState>>
      shared_states_ GUARDED_BY(mutex_);

  uint64_t num_pg_acquires_ GUARDED_BY(mutex_) = 0;
  uint64_t num_tserver_acquires_ GUARDED_BY(mutex_) = 0;
  uint64_t num_pg_releases_ GUARDED_BY(mutex_) = 0;
  uint64_t num_tserver_releases_ GUARDED_BY(mutex_) = 0;

  std::shared_ptr<void> metric_detacher_;
};

} // namespace yb::docdb
