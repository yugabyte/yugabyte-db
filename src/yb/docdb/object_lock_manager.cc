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

#include "yb/docdb/object_lock_manager.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/multi_index/mem_fun.hpp>

#include "yb/ash/wait_state.h"
#include "yb/docdb/local_waiting_txn_registry.h"
#include "yb/docdb/lock_batch.h"
#include "yb/docdb/lock_util.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/thread_pool.h"

#include "yb/server/server_base.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug/long_operation_tracker.h"

#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/lw_function.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

DEFINE_test_flag(bool, assert_olm_empty_locks_map, false,
    "When set, asserts that the local locks map is empty at shutdown. Used in tests "
    "to assert sanity, where tserver doesn't loose YSQL lease and all connections release "
    "acquired/timedout/errored locks.");

DEFINE_test_flag(bool, olm_skip_scheduling_waiter_resumption, false,
    "When set, don't signal potential waiters for resumption");

// TODO(bkolagani): Default flag to false once issues with deadlock detection are resolved.
DEFINE_test_flag(bool, olm_skip_sending_wait_for_probes, true,
    "When set, the lock manager doesn't send wait-for probres to the local waiting txn registry, "
    "essentially giving away deadlock detection.");

using namespace std::placeholders;
using namespace std::literals;
namespace yb::docdb {

using dockv::IntentTypeSet;

struct ObjectLockedBatchEntry;

namespace {

const Status kShuttingDownError = STATUS(
    ShutdownInProgress, "Object Lock Manager shutting down");

const Status kTimedOut = STATUS(
    TimedOut, "Failed to acquire object locks within deadline");

const Status kTxnExpired = STATUS(
    Expired, "Transaction expired, all acquired object locks have been released");

YB_DEFINE_ENUM(LocksMapType, (kGranted)(kWaiting));
YB_STRONGLY_TYPED_BOOL(IsLockRetry);

using LockStateBlockersMap = std::unordered_map<LockState, std::shared_ptr<ConflictDataManager>>;

// TrackedLockEntry is used to keep track of the LockState of the transaction for a given key.
//
// When handling release requests by ObjectLockOwner 'state' value is used to reset the info
// of the corresponding ObjectLockedBatchEntry.
struct TrackedLockEntry {
  explicit TrackedLockEntry(ObjectLockedBatchEntry& locked_batch_entry_)
      : locked_batch_entry(locked_batch_entry_) {}

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(state, ref_count);
  }

  // ObjectLockedBatchEntry object's memory is managed by ObjectLockManagerImpl.
  ObjectLockedBatchEntry& locked_batch_entry;
  LockState state = 0;
  size_t ref_count = 0;
};

// TrackedTransactionLockEntry contains the TrackedLockEntry(s) coresponding to a transaction
struct TrackedTransactionLockEntry {
  using LockEntryMap =
      std::unordered_map<SubTransactionId,
                         std::unordered_map<ObjectLockPrefix, TrackedLockEntry>>;

  TrackedTransactionLockEntry() = default;

  TrackedTransactionLockEntry(TrackedTransactionLockEntry&& other)
      : granted_locks(std::move(other.granted_locks)),
        waiting_locks(std::move(other.waiting_locks)) {}

  void AddAcquiredLockUnlocked(
      const LockBatchEntry<ObjectLockManager>& lock_entry,
      const ObjectLockOwner& object_lock_owner, LocksMapType type) REQUIRES(mutex);

  void AddReleasedLockUnlocked(
      const LockBatchEntry<ObjectLockManager>& lock_entry,
      const ObjectLockOwner& object_lock_owner, LocksMapType type) REQUIRES(mutex);

  // Returns true if the acquire request (either being processed for the first time OR being resumed
  // from the wait queue) with the given subtxn can be skipped i.e either the subtxn rolled back or
  // the transaction timedout and released all its locks.
  //
  // Note that this still doesn't address the case where an acquire comes in after a release is
  // processed. That should be addressed by the upstream code.
  bool ShouldSkipAcquireUnlocked(SubTransactionId subtxn) REQUIRES(mutex);

  LockState GetLockStateForKeyUnlocked(const ObjectLockPrefix& object_id) REQUIRES(mutex);

  // Held for the core process of locking/unlocking.
  //
  // Established Locking orders:
  // 1. ObjectLockManagerImpl::global_mutex_ -> this->mutex
  // 2. this->mutex -> ObjectLockedBatchEntry::mutex
  using ObjectPrefixLockStateMap =
      std::unordered_map<ObjectLockPrefix, LockState>;

  mutable std::mutex mutex;

  LockEntryMap granted_locks GUARDED_BY(mutex);
  LockEntryMap waiting_locks GUARDED_BY(mutex);
  ObjectPrefixLockStateMap existing_states GUARDED_BY(mutex);

  std::unordered_set<SubTransactionId> released_subtxns GUARDED_BY(mutex);
  bool released_all_locks GUARDED_BY(mutex) = false;
  TabletId status_tablet GUARDED_BY(mutex);
};

using TrackedTxnLockEntryPtr = std::shared_ptr<TrackedTransactionLockEntry>;

} // namespace

struct WaiterEntry {
  WaiterEntry(
      TrackedTxnLockEntryPtr&& transaction_entry_,
      LockData&& lock_data_,
      size_t resume_it_offset_)
      : transaction_entry(std::move(transaction_entry_)),
        lock_data(std::move(lock_data_)),
        resume_it_offset(resume_it_offset_) {}

  WaiterEntry(WaiterEntry&& other)
    : transaction_entry(std::move(other.transaction_entry)),
      lock_data(std::move(other.lock_data)),
      resume_it_offset(other.resume_it_offset),
      waiter_registration(std::move(other.waiter_registration)),
      blockers(std::move(other.blockers)) {}

  const TransactionId& txn_id() const {
    return lock_data.object_lock_owner.txn_id;
  }

  LockBatchEntries<ObjectLockManager>::const_iterator resume_it() const {
    return lock_data.key_to_lock.lock_batch.begin() + resume_it_offset;
  }

  const ObjectLockOwner& object_lock_owner() const {
    return lock_data.object_lock_owner;
  }

  const TabletId& status_tablet() const {
    return lock_data.status_tablet;
  }

  void Resume(ObjectLockManagerImpl* lock_manager, Status resume_with_status);

  CoarseTimePoint deadline() const {
    return lock_data.deadline;
  }

  TrackedTxnLockEntryPtr transaction_entry;
  LockData lock_data;
  size_t resume_it_offset;
  // Below fields are operated under corresponding ObjectLockedBatchEntry::mutex.
  std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration;
  std::shared_ptr<ConflictDataManager> blockers;
};

inline bool operator<(const WaiterEntry& lhs, const WaiterEntry& rhs) {
  return lhs.lock_data.start_time < rhs.lock_data.start_time ||
         (lhs.lock_data.start_time == rhs.lock_data.start_time &&
          lhs.lock_data.object_lock_owner.txn_id < rhs.lock_data.object_lock_owner.txn_id);
}

using WaiterEntryPtr = std::shared_ptr<WaiterEntry>;

struct StartUsTag;
struct DeadlineTag;
struct OwnerTag;
using Waiters = boost::multi_index_container<
  WaiterEntryPtr,
  boost::multi_index::indexed_by<
    boost::multi_index::ordered_non_unique<
        boost::multi_index::tag<StartUsTag>,
        boost::multi_index::identity<WaiterEntry>
    >,
    boost::multi_index::ordered_non_unique<
        boost::multi_index::tag<DeadlineTag>,
        boost::multi_index::const_mem_fun<WaiterEntry, CoarseTimePoint, &WaiterEntry::deadline>
    >,
    boost::multi_index::hashed_non_unique<
        boost::multi_index::tag<OwnerTag>,
        boost::multi_index::const_mem_fun<
            WaiterEntry, const TransactionId&, &WaiterEntry::txn_id
        >
    >
  >
>;

struct ObjectLockedBatchEntry {
  // Taken only for short duration, with no blocking wait.
  //
  // Established Locking orders:
  // 1. ObjectLockManagerImpl::global_mutex_ -> this->mutex
  // 2. TrackedTransactionLockEntry::mutex -> this->mutex
  mutable std::mutex mutex;

  // Refcounting for garbage collection. Can only be used while the global mutex is locked.
  // Global mutex resides in lock manager and covers this field for all LockBatchEntries.
  size_t ref_count = 0;

  // Number of holders for each type
  std::atomic<LockState> num_holding{0};

  std::atomic<size_t> num_waiters{0};

  Waiters wait_queue GUARDED_BY(mutex);

  std::atomic<LockState> waiting_state{0};

  std::atomic<uint32_t> version{0};

  std::string ToString() const {
    return Format("{ ref_count: $0 lock_state: $1 num_waiters: $2 }",
                  ref_count,
                  LockStateDebugString(num_holding.load(std::memory_order_acquire)),
                  num_waiters.load(std::memory_order_acquire));
  }
};

class ObjectLockManagerImpl {
 public:
  ObjectLockManagerImpl(ThreadPool* thread_pool, server::RpcServerBase& server)
  : thread_pool_token_(thread_pool->NewToken(ThreadPool::ExecutionMode::CONCURRENT)),
    server_(server),
    waiters_amidst_resumption_on_messenger_("ObjectLockManagerImpl: " /* log_prefix */) {}

  void Lock(LockData&& data);

  void Unlock(const ObjectLockOwner& object_lock_owner, Status resume_with_status);

  void Poll() EXCLUDES(global_mutex_);

  void Start(docdb::LocalWaitingTxnRegistry* waiting_txn_registry) {
    waiting_txn_registry_ = waiting_txn_registry;
  }

  void Shutdown();

  void DumpStatusHtml(std::ostream& out) EXCLUDES(global_mutex_);

  size_t TEST_LocksSize(LocksMapType locks_map) const;
  size_t TEST_GrantedLocksSize() const;
  size_t TEST_WaitingLocksSize() const;
  std::unordered_map<ObjectLockPrefix, LockState>
      TEST_GetLockStateMapForTxn(const TransactionId& txn) const;

 private:
  friend struct WaiterEntry;

  TrackedTxnLockEntryPtr GetTransactionEntryUnlocked(const ObjectLockOwner& object_lock_owner)
      REQUIRES(global_mutex_);

  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  TrackedTxnLockEntryPtr Reserve(
      std::span<LockBatchEntry<ObjectLockManager>> batch,
      const ObjectLockOwner& object_lock_owner) EXCLUDES(global_mutex_);

  // Update refcounts and maybe collect garbage.
  void DoCleanup(std::span<const LockBatchEntry<ObjectLockManager>> key_to_intent_type)
      EXCLUDES(global_mutex_);

  Status MakePrepareAcquireResult(const LockData& data, Status resume_with_status);

  // Performs necessary cleanup and checks whether the locks request needs to be processed.
  Status PrepareAcquire(
      std::unique_lock<std::mutex>& txn_lock, TrackedTxnLockEntryPtr& transaction_entry,
      const LockData& data, size_t resume_it_offset, Status resume_with_status)
      REQUIRES(transaction_entry->mutex) EXCLUDES(global_mutex_);

  void DoLock(
      TrackedTxnLockEntryPtr transaction_entry, LockData&& data, IsLockRetry is_retry,
      size_t resume_it_offset = 0, Status resume_with_status = Status::OK())
      EXCLUDES(global_mutex_, transaction_entry->mutex);

  MUST_USE_RESULT bool DoLockSingleEntry(
      TrackedTxnLockEntryPtr& transaction_entry, LockData& data, LockState existing_state,
      LockBatchEntries<ObjectLockManager>::iterator lock_entry_it,
      IsLockRetry is_retry) REQUIRES(transaction_entry->mutex);

  void DoUnlock(
      const ObjectLockOwner& object_lock_owner,
      TrackedTransactionLockEntry::LockEntryMap& locks_map,
      TrackedTxnLockEntryPtr& txn_entry) REQUIRES(global_mutex_, txn_entry->mutex);

  void SignalTerminateFinishedWaiters(
      const ObjectLockOwner& object_lock_owner, TrackedTxnLockEntryPtr& txn_entry,
      Status resume_with_status) REQUIRES(global_mutex_, txn_entry->mutex);

  void DoSignalTerminateFinishedWaiters(
      ObjectLockedBatchEntry* entry, TransactionId txn_id, Status resume_with_status);

  bool UnlockSingleEntry(const LockBatchEntry<ObjectLockManager>& lock_entry);

  bool DoUnlockSingleEntry(ObjectLockedBatchEntry& entry, LockState sub);

  void RegisterWaiters(ObjectLockedBatchEntry* locked_batch_entry) EXCLUDES(global_mutex_);

  void DoReleaseTrackedLock(
      const ObjectLockPrefix& object_id, TrackedLockEntry& entry) REQUIRES(global_mutex_);

  void UnblockPotentialWaiters(ObjectLockedBatchEntry* entry);

  void DoSignal(ObjectLockedBatchEntry* entry);

  void DoTerminateFinishedWaiters(
      ObjectLockedBatchEntry* entry, TransactionId txn_id, Status resume_with_status);

  void DoComputeBlockersWithinQueue(
      ObjectLockedBatchEntry* locked_batch_entry, std::optional<ObjectLockPrefix>& key,
      LockStateBlockersMap& lockstate_blocker_map);

  void DoPopulateLockStateBlockersMap(
      std::optional<ObjectLockPrefix>& key,
      LockStateBlockersMap& lockstate_blocker_map) EXCLUDES(global_mutex_);

  void DumpStoredObjectLocksMap(
      std::ostream& out, std::string_view caption, LocksMapType locks_map) REQUIRES(global_mutex_);

  // The global mutex should be taken only for very short duration, with no blocking wait.
  //
  // Established Locking orders:
  // 1. this->global_mutex_ -> TrackedTransactionLockEntry::mutex
  // 2. this->global_mutex_ -> ObjectLockedBatchEntry::mutex
  mutable std::mutex global_mutex_;

  std::unordered_map<ObjectLockPrefix, ObjectLockedBatchEntry*> locks_ GUARDED_BY(global_mutex_);
  // Cache of lock entries, to avoid allocation/deallocation of heavy ObjectLockedBatchEntry.
  std::vector<std::unique_ptr<ObjectLockedBatchEntry>> lock_entries_ GUARDED_BY(global_mutex_);
  std::vector<ObjectLockedBatchEntry*> free_lock_entries_ GUARDED_BY(global_mutex_);

  // Lock activity is tracked only when the requests have ObjectLockOwner set. This maps
  // txn => subtxn => object id => entry.
  std::unordered_map<TransactionId, TrackedTxnLockEntryPtr> txn_locks_ GUARDED_BY(global_mutex_);

  std::unique_ptr<ThreadPoolToken> thread_pool_token_;
  server::RpcServerBase& server_;
  LocalWaitingTxnRegistry* waiting_txn_registry_ = nullptr;
  std::atomic<bool> shutdown_in_progress_{false};
  OperationCounter waiters_amidst_resumption_on_messenger_;
};

void WaiterEntry::Resume(ObjectLockManagerImpl* lock_manager, Status resume_with_status) {
  {
    UniqueLock txn_lock(transaction_entry->mutex);
    resume_it()->locked->num_waiters.fetch_sub(1);
    transaction_entry->AddReleasedLockUnlocked(
        *resume_it(), lock_data.object_lock_owner, LocksMapType::kWaiting);
  }
  lock_manager->DoLock(
      transaction_entry, std::move(lock_data), IsLockRetry::kTrue, resume_it_offset,
      resume_with_status);
}

void TrackedTransactionLockEntry::AddAcquiredLockUnlocked(
    const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner& object_lock_owner,
    LocksMapType locks_map) {
  TRACE_FUNC();
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", object_lock_owner: " << AsString(object_lock_owner);
  auto delta = IntentTypeSetAdd(lock_entry.intent_types);

  auto& locks = locks_map == LocksMapType::kGranted ? granted_locks : waiting_locks;
  auto& subtxn_locks = locks[object_lock_owner.subtxn_id];
  auto it = subtxn_locks.find(lock_entry.key);
  if (it == subtxn_locks.end()) {
    it = subtxn_locks.emplace(lock_entry.key, TrackedLockEntry(*lock_entry.locked)).first;
  }
  it->second.state += delta;
  ++it->second.ref_count;
  if (locks_map == LocksMapType::kGranted) {
    existing_states[lock_entry.key] += delta;
  }
}

void TrackedTransactionLockEntry::AddReleasedLockUnlocked(
    const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner& object_lock_owner,
    LocksMapType locks_map) {
  TRACE_FUNC();
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", object_lock_owner: " << AsString(object_lock_owner);
  auto delta = IntentTypeSetAdd(lock_entry.intent_types);

  auto& locks = locks_map == LocksMapType::kGranted ? granted_locks : waiting_locks;
  auto subtxn_itr = locks.find(object_lock_owner.subtxn_id);
  if (subtxn_itr == locks.end()) {
    LOG_WITH_FUNC(DFATAL) << "No locks found for " << AsString(object_lock_owner)
                          << ", cannot release lock on " << AsString(lock_entry.key);
    return;
  }
  auto& subtxn_locks = subtxn_itr->second;
  auto it = subtxn_locks.find(lock_entry.key);
  if (it == subtxn_locks.end()) {
    LOG_WITH_FUNC(DFATAL) << "No lock found for " << AsString(object_lock_owner) << " on "
                          << AsString(lock_entry.key) << ", cannot release";
  }
  auto& entry = it->second;
  entry.state -= delta;
  --entry.ref_count;
  if (entry.state == 0) {
    DCHECK_EQ(entry.ref_count, 0)
        << "TrackedLockEntry::ref_count for key " << AsString(lock_entry.key) << " expected to "
        << "have been 0 here. This could lead to faulty tracking of acquired/waiting object locks "
        << "and also issues with garbage collection of free lock entries in ObjectLockManager.";
    subtxn_locks.erase(it);
  }
  if (locks_map == LocksMapType::kGranted) {
    existing_states[lock_entry.key] -= delta;
  }
}

bool TrackedTransactionLockEntry::ShouldSkipAcquireUnlocked(SubTransactionId subtxn_id) {
  return released_subtxns.contains(subtxn_id) || released_all_locks;
}

LockState TrackedTransactionLockEntry::GetLockStateForKeyUnlocked(
    const ObjectLockPrefix& object_id) {
  return existing_states[object_id];
}

TrackedTxnLockEntryPtr ObjectLockManagerImpl::GetTransactionEntryUnlocked(
    const ObjectLockOwner& object_lock_owner) {
  // TODO: Should we switch similar logic of allocation and reuse as with lock entries?
  const auto& [it, _] = txn_locks_.emplace(
      object_lock_owner.txn_id, std::make_shared<TrackedTransactionLockEntry>());
  return it->second;
}

TrackedTxnLockEntryPtr ObjectLockManagerImpl::Reserve(
    std::span<LockBatchEntry<ObjectLockManager>> key_to_intent_type,
    const ObjectLockOwner& object_lock_owner) {
  std::lock_guard lock(global_mutex_);
  auto transaction_entry = GetTransactionEntryUnlocked(object_lock_owner);
  for (auto& key_and_intent_type : key_to_intent_type) {
    auto& value = locks_[key_and_intent_type.key];
    if (!value) {
      if (!free_lock_entries_.empty()) {
        value = free_lock_entries_.back();
        free_lock_entries_.pop_back();
      } else {
        lock_entries_.emplace_back(std::make_unique<ObjectLockedBatchEntry>());
        value = lock_entries_.back().get();
      }
    }
    value->ref_count++;
    key_and_intent_type.locked = std::to_address(value);
  }
  return transaction_entry;
}

void ObjectLockManagerImpl::DoCleanup(
    std::span<const LockBatchEntry<ObjectLockManager>> key_to_intent_type) {
  std::lock_guard lock(global_mutex_);
  for (const auto& item : key_to_intent_type) {
    LOG_IF(DFATAL, item.locked->ref_count == 0) << "Local Object lock manager state corrupted";
    if (--item.locked->ref_count == 0) {
      LOG_IF(DFATAL, item.locked->num_waiters)
          << "Local Object lock manager state corrupted, non zero waiter but object being GC'ed";
      locks_.erase(item.key);
      item.locked->version++;
      free_lock_entries_.push_back(item.locked);
    }
  }
}

void ObjectLockManagerImpl::Lock(LockData&& data) {
  TRACE("Locking a batch of $0 keys", data.key_to_lock.lock_batch.size());
  auto transaction_entry = Reserve(data.key_to_lock.lock_batch, data.object_lock_owner);
  DoLock(transaction_entry, std::move(data), IsLockRetry::kFalse);
}

Status ObjectLockManagerImpl::MakePrepareAcquireResult(
    const LockData& data, Status resume_with_status) {
  RETURN_NOT_OK(resume_with_status);
  if (data.deadline < CoarseMonoClock::Now()) {
    return kTimedOut;
  }
  if (shutdown_in_progress_) {
    return kShuttingDownError;
  }
  return Status::OK();
}

Status ObjectLockManagerImpl::PrepareAcquire(
    std::unique_lock<std::mutex>& txn_lock, TrackedTxnLockEntryPtr& transaction_entry,
    const LockData& data, size_t resume_it_offset, Status resume_with_status) {
  auto it = data.key_to_lock.lock_batch.begin() + resume_it_offset;
  const auto& key_to_lock = data.key_to_lock;
  const auto& object_lock_owner = data.object_lock_owner;
  if (transaction_entry->ShouldSkipAcquireUnlocked(object_lock_owner.subtxn_id)) {
    // Release corresponding to this acquire has already been processed.
    DoSignal(it->locked);
    txn_lock.unlock();
    DoCleanup(std::span<const LockBatchEntry<ObjectLockManager>>(it, key_to_lock.lock_batch.end()));
    TRACE("Acquire voided by corresponding release.");
    return kTxnExpired;
  }
  auto status = MakePrepareAcquireResult(data, resume_with_status);
  if (status.ok()) {
    return Status::OK();
  }
  while (it != key_to_lock.lock_batch.begin()) {
    --it;
    if (UnlockSingleEntry(*it)) {
      DoSignal(it->locked);
    }
    transaction_entry->AddReleasedLockUnlocked(*it, object_lock_owner, LocksMapType::kGranted);
  }
  txn_lock.unlock();
  DoCleanup(key_to_lock.lock_batch);
  TRACE("Acquire timed out or lock manager being shut down.");
  return status;
}

void ObjectLockManagerImpl::DoLock(
    TrackedTxnLockEntryPtr transaction_entry, LockData&& data, IsLockRetry is_retry,
    size_t resume_it_offset, Status resume_with_status) {
  {
    UniqueLock txn_lock(transaction_entry->mutex);
    auto it = data.key_to_lock.lock_batch.begin() + resume_it_offset;
    auto prepare_status = PrepareAcquire(
        GetLockForCondition(txn_lock), transaction_entry, data, resume_it_offset,
        resume_with_status);
    if (!prepare_status.ok()) {
      data.callback(prepare_status);
      return;
    }

    if (!is_retry) {
      transaction_entry->status_tablet = data.status_tablet;
    }
    while (it != data.key_to_lock.lock_batch.end()) {
      // Ignore conflicts with self.
      auto existing_state = transaction_entry->GetLockStateForKeyUnlocked(it->key);
      VLOG(4) << "Locking key : " << AsString(it->key)
              << " with intent types : " << AsString(it->intent_types)
              << " and owner : " << AsString(data.object_lock_owner)
              << " with existing state: " << LockStateDebugString(existing_state);
      auto* locked_batch_entry = it->locked;
      if (!DoLockSingleEntry(transaction_entry, data, existing_state, it, is_retry)) {
        WARN_NOT_OK(
            thread_pool_token_->SubmitFunc(
                std::bind(&ObjectLockManagerImpl::RegisterWaiters, this, locked_batch_entry)),
            "Failure in submitting task to register waiters with deadlock detector");
        return;
      }
      ++it;
    }
  }
  TRACE("Acquired a lock batch of $0 keys", data.key_to_lock.lock_batch.size());
  data.callback(Status::OK());
}

bool ObjectLockManagerImpl::DoLockSingleEntry(
    TrackedTxnLockEntryPtr& transaction_entry, LockData& data, LockState existing_state,
    LockBatchEntries<ObjectLockManager>::iterator lock_entry_it, IsLockRetry is_retry) {
  TRACE_FUNC();
  auto& lock_entry = *lock_entry_it;
  auto& entry = lock_entry.locked;
  auto& object_lock_owner = data.object_lock_owner;
  auto old_value = entry->num_holding.load(std::memory_order_acquire);
  auto add = IntentTypeSetAdd(lock_entry.intent_types);
  auto conflicting_lock_state = IntentTypeSetConflict(lock_entry.intent_types);
  for (;;) {
    auto should_check_wq = !is_retry && (entry->waiting_state & conflicting_lock_state) != 0 &&
                           !existing_state;
    if (!should_check_wq && ((old_value ^ existing_state) & conflicting_lock_state) == 0) {
      auto new_value = old_value + add;
      if (entry->num_holding.compare_exchange_weak(
              old_value, new_value, std::memory_order_acq_rel)) {
        VLOG(4) << AsString(object_lock_owner) << " acquired lock " << AsString(lock_entry);
        transaction_entry->AddAcquiredLockUnlocked(
            lock_entry, object_lock_owner, LocksMapType::kGranted);
        return true;
      }
      continue;
    }
    // Realese thread signals for the object when num_waiters > 0, and the resumption thread checks
    // the wait-queue under a mutex. Increment num_waiters under mutex to avoid the race where
    // num_waiters > 0 but wait-queue is empty.
    std::lock_guard l(entry->mutex);
    entry->num_waiters.fetch_add(1, std::memory_order_release);
    old_value = entry->num_holding.load(std::memory_order_acquire);
    if (should_check_wq || ((old_value ^ existing_state) & conflicting_lock_state) != 0) {
      DEBUG_ONLY_TEST_SYNC_POINT("ObjectLockManagerImpl::DoLockSingleEntry");
      SCOPED_WAIT_STATUS(LockedBatchEntry_Lock);
      VLOG(1) << AsString(object_lock_owner) << " added to wait-queue on " << AsString(lock_entry);
      transaction_entry->AddAcquiredLockUnlocked(
          lock_entry, object_lock_owner, LocksMapType::kWaiting);
      entry->waiting_state += add;
      // TODO(bkolagani): Reuse WaiterDataPtr as opposed to creating multiple shared_ptrs for single
      // lock request. Refer https://github.com/yugabyte/yugabyte-db/issues/27107 for details.
      entry->wait_queue.insert(std::make_shared<WaiterEntry>(
          std::move(transaction_entry), std::move(data),
          lock_entry_it - data.key_to_lock.lock_batch.begin()));
      return false;
    }
    entry->num_waiters.fetch_sub(1, std::memory_order_release);
  }
}

void ObjectLockManagerImpl::Unlock(const ObjectLockOwner& object_lock_owner,
                                   Status resume_with_status) {
  TRACE("Unlocking all keys for owner $0", AsString(object_lock_owner));

  TrackedTxnLockEntryPtr txn_entry;
  {
    std::lock_guard lock(global_mutex_);
    auto txn_itr = txn_locks_.find(object_lock_owner.txn_id);
    if (txn_itr == txn_locks_.end()) {
      return;
    }
    txn_entry = txn_itr->second;
    if (!object_lock_owner.subtxn_id) {
      txn_locks_.erase(txn_itr);
    }
  }
  {
    UniqueLock txn_lock(txn_entry->mutex);
    if (object_lock_owner.subtxn_id) {
      txn_entry->released_subtxns.emplace(object_lock_owner.subtxn_id);
    } else {
      txn_entry->released_all_locks = true;
    }
  }

  std::lock_guard lock(global_mutex_);
  UniqueLock txn_lock(txn_entry->mutex);
  DoUnlock(object_lock_owner, txn_entry->granted_locks, txn_entry);
  // Terminate any obsolete waiting lock request for this txn/subtxn. This could happen when
  // 1. txn gets aborted due to a deadlock and the pg backend issues a finish txn request
  // 2. txn times out due to conflict and pg backend issues a finish txn request before the
  //    lock manager times out the waiting lock request.
  SignalTerminateFinishedWaiters(object_lock_owner, txn_entry, resume_with_status);
}

void ObjectLockManagerImpl::DoUnlock(
    const ObjectLockOwner& object_lock_owner, TrackedTransactionLockEntry::LockEntryMap& locks_map,
    TrackedTxnLockEntryPtr& txn_entry) {
  auto& existing_states = txn_entry->existing_states;
  if (object_lock_owner.subtxn_id) {
    auto subtxn_itr = locks_map.find(object_lock_owner.subtxn_id);
    if (subtxn_itr == locks_map.end()) {
      return;
    }
    for (auto itr = subtxn_itr->second.begin(); itr != subtxn_itr->second.end(); itr++) {
      existing_states[itr->first] -= itr->second.state;
      DoReleaseTrackedLock(itr->first, itr->second);
    }
    locks_map.erase(subtxn_itr);
    return;
  }
  for (auto locks_itr = locks_map.begin(); locks_itr != locks_map.end(); locks_itr++) {
    for (auto itr = locks_itr->second.begin(); itr != locks_itr->second.end(); itr++) {
      existing_states[itr->first] -= itr->second.state;
      DoReleaseTrackedLock(itr->first, itr->second);
    }
  }
}

void ObjectLockManagerImpl::SignalTerminateFinishedWaiters(
    const ObjectLockOwner& object_lock_owner,
    TrackedTxnLockEntryPtr& txn_entry,
    Status resume_with_status) {
  auto& locks_map = txn_entry->waiting_locks;
  if (object_lock_owner.subtxn_id) {
    auto subtxn_itr = locks_map.find(object_lock_owner.subtxn_id);
    if (subtxn_itr == locks_map.end()) {
      return;
    }
    for (auto itr = subtxn_itr->second.begin(); itr != subtxn_itr->second.end(); itr++) {
      DoSignalTerminateFinishedWaiters(
          &itr->second.locked_batch_entry, object_lock_owner.txn_id, resume_with_status);
    }
    return;
  }
  for (auto locks_itr = locks_map.begin(); locks_itr != locks_map.end(); locks_itr++) {
    for (auto itr = locks_itr->second.begin(); itr != locks_itr->second.end(); itr++) {
      DoSignalTerminateFinishedWaiters(
          &itr->second.locked_batch_entry, object_lock_owner.txn_id, resume_with_status);
    }
  }
}

void ObjectLockManagerImpl::DoSignalTerminateFinishedWaiters(
    ObjectLockedBatchEntry* entry, TransactionId txn_id, Status resume_with_status) {
  WARN_NOT_OK(
      thread_pool_token_->SubmitFunc(
          std::bind(&ObjectLockManagerImpl::DoTerminateFinishedWaiters, this, entry, txn_id,
          resume_with_status)),
      "Failure submitting task ObjectLockManagerImpl::DoTerminateFinishedWaiters");
}

void ObjectLockManagerImpl::DoTerminateFinishedWaiters(
    ObjectLockedBatchEntry* entry, TransactionId txn_id, Status resume_with_status) {
  std::vector<WaiterEntryPtr> waiters_failed_to_schedule;
  {
    std::lock_guard l(entry->mutex);
    auto& index = entry->wait_queue.get<OwnerTag>();
    auto it_range = index.equal_range(txn_id);
    auto it = it_range.first;
    auto* messenger = server_.messenger();
    while (it != it_range.second) {
      auto waiter_entry = *it;
      it = index.erase(it);
      waiter_entry->waiter_registration.reset();
      entry->waiting_state -= IntentTypeSetAdd(waiter_entry->resume_it()->intent_types);
      VLOG(1) << "Resuming " << AsString(waiter_entry->object_lock_owner());
      if (PREDICT_TRUE(messenger)) {
        ScopedOperation resuming_waiter_op(&waiters_amidst_resumption_on_messenger_);
        messenger->ThreadPool().EnqueueFunctor(
            [operation = std::move(resuming_waiter_op), entry = std::move(waiter_entry),
             lock_manager = this, resume_with_status]() {
          entry->Resume(lock_manager, resume_with_status);
        });
      } else {
        // Don't schedule anything here on thread_pool_token_ as a shutdown could destroy tasks.
        LOG_WITH_FUNC(WARNING) << "Messenger not available";
        waiters_failed_to_schedule.push_back(std::move(waiter_entry));
      }
    }
  }
  for (auto& waiter : waiters_failed_to_schedule) {
    waiter->Resume(this, resume_with_status);
  }
}

bool ObjectLockManagerImpl::UnlockSingleEntry(const LockBatchEntry<ObjectLockManager>& lock_entry) {
  TRACE_FUNC();
  return DoUnlockSingleEntry(*lock_entry.locked, IntentTypeSetAdd(lock_entry.intent_types));
}

bool ObjectLockManagerImpl::DoUnlockSingleEntry(ObjectLockedBatchEntry& entry, LockState sub) {
  entry.num_holding.fetch_sub(sub, std::memory_order_acq_rel);
  return entry.num_waiters.load(std::memory_order_acquire);
}

void ObjectLockManagerImpl::Poll() {
  const auto now = CoarseMonoClock::Now();
  std::vector<WaiterEntryPtr> timed_out_waiters;
  {
    std::lock_guard l(global_mutex_);
    for (auto& [_, entry] : locks_) {
      std::lock_guard object_lock(entry->mutex);
      auto& index = entry->wait_queue.get<DeadlineTag>();
      while (!index.empty() && (*index.begin())->deadline() <= now) {
        auto waiter_entry = *index.begin();
        index.erase(index.begin());
        entry->waiting_state -= IntentTypeSetAdd(waiter_entry->resume_it()->intent_types);
        timed_out_waiters.push_back(std::move(waiter_entry));
      }
    }
  }
  for (auto& waiter : timed_out_waiters) {
    waiter->Resume(this, kTimedOut);
  }
}

void ObjectLockManagerImpl::Shutdown() {
  shutdown_in_progress_ = true;
  // Shutdown of thread pool token => no more tasks pending/scheduled for resumption.
  thread_pool_token_->Shutdown();
  thread_pool_token_.reset();
  waiters_amidst_resumption_on_messenger_.Shutdown();
  // Since TSLocalLockManager waits for running requests before processing shutdown, and no new
  // requests are sent post initiating shutdown, the OLM should be empty after resuming waiters.
  std::vector<WaiterEntryPtr> waiters;
  {
    std::lock_guard l(global_mutex_);
    for (auto& [_, entry] : locks_) {
      std::lock_guard obj_lock(entry->mutex);
      auto& index = entry->wait_queue.get<StartUsTag>();
      while (!index.empty()) {
        auto waiter_entry = *index.begin();
        index.erase(index.begin());
        entry->waiting_state -= IntentTypeSetAdd(waiter_entry->resume_it()->intent_types);
        waiters.push_back(std::move(waiter_entry));
      }
    }
  }

  for (auto& waiter : waiters) {
    waiter->Resume(this, kShuttingDownError);
  }
  if (FLAGS_TEST_assert_olm_empty_locks_map) {
    std::lock_guard l(global_mutex_);
    LOG_IF(DFATAL, !locks_.empty())
        << "ref_count of some lock structures is non-zero on shutdown, implies either some "
        << "connections are outstanding or indicates a state corruption of the lock manager.\n"
        << AsString(locks_);
  }
}

void ObjectLockManagerImpl::DoSignal(ObjectLockedBatchEntry* entry) {
  if (FLAGS_TEST_olm_skip_scheduling_waiter_resumption) {
    return;
  }
  WARN_NOT_OK(
      thread_pool_token_->SubmitFunc(
          std::bind(&ObjectLockManagerImpl::UnblockPotentialWaiters, this, entry)),
      "Failure submitting task ObjectLockManagerImpl::UnblockPotentialWaiters");
}

void ObjectLockManagerImpl::UnblockPotentialWaiters(ObjectLockedBatchEntry* entry) {
  const auto now = CoarseMonoClock::Now();
  std::vector<WaiterEntryPtr> waiters_failed_to_schedule;
  {
    std::lock_guard l(entry->mutex);
    if (entry->wait_queue.empty()) {
      return;
    }
    // Everytime we are signaled on a release, resume at least one waiter in the queue. Keep
    // resuming waiters until we encounter a waiter that we know for sure conflcits with the
    // released waiters.
    LockState unblock_state = 0;
    auto& index = entry->wait_queue.get<StartUsTag>();
    bool schedule_next;
    TransactionId prev_txn;
    auto* messenger = server_.messenger();
    do {
      auto waiter_entry = *index.begin();
      index.erase(index.begin());
      auto delta = IntentTypeSetAdd(waiter_entry->resume_it()->intent_types);
      if (waiter_entry->deadline() > now) {
        unblock_state += delta;
      }
      waiter_entry->waiter_registration.reset();
      entry->waiting_state -= IntentTypeSetAdd(waiter_entry->resume_it()->intent_types);
      VLOG(1) << "Resuming " << AsString(waiter_entry->object_lock_owner());
      prev_txn = waiter_entry->txn_id();
      if (PREDICT_TRUE(messenger)) {
        ScopedOperation resuming_waiter_op(&waiters_amidst_resumption_on_messenger_);
        messenger->ThreadPool().EnqueueFunctor(
            [operation = std::move(resuming_waiter_op), entry = std::move(waiter_entry),
             lock_manager = this]() {
          entry->Resume(lock_manager, Status::OK());
        });
      } else {
        // Don't schedule anything here on thread_pool_token_ as a shutdown could destroy tasks.
        LOG_WITH_FUNC(WARNING) << "Messenger not available";
        waiters_failed_to_schedule.push_back(std::move(waiter_entry));
      }

      if (index.empty()) {
        break;
      }
      const auto& next = *index.begin();
      auto conflict_state = IntentTypeSetConflict(next->resume_it()->intent_types);
      schedule_next = ((unblock_state & conflict_state) == 0) || next->txn_id() == prev_txn;
    } while (schedule_next);
  }
  for (auto& waiter : waiters_failed_to_schedule) {
    waiter->Resume(this, Status::OK());
  }
}

void ObjectLockManagerImpl::DoReleaseTrackedLock(
    const ObjectLockPrefix& object_id, TrackedLockEntry& entry) {
  VLOG(4) << "Removing granted lock on object : " << AsString(object_id)
          << " with delta { state : " << LockStateDebugString(entry.state)
          << " ref_count : " << entry.ref_count << " }";
  if (DoUnlockSingleEntry(entry.locked_batch_entry, entry.state)) {
    DoSignal(&entry.locked_batch_entry);
  }
  LOG_IF(DFATAL, entry.locked_batch_entry.ref_count < entry.ref_count)
      << "Local Object lock manager state corrupted.";
  entry.locked_batch_entry.ref_count -= entry.ref_count;
  if (entry.locked_batch_entry.ref_count == 0) {
    LOG_IF(DFATAL, entry.locked_batch_entry.num_waiters)
        << "Local Object lock manager state. Non zero waiters, but object geing GC'ed";
    locks_.erase(object_id);
    entry.locked_batch_entry.version++;
    free_lock_entries_.push_back(&entry.locked_batch_entry);
  }
  entry.state = 0;
  entry.ref_count = 0;
}

void ObjectLockManagerImpl::DoComputeBlockersWithinQueue(
    ObjectLockedBatchEntry* locked_batch_entry,
    std::optional<ObjectLockPrefix>& key,
    LockStateBlockersMap& lockstate_blocker_map) {
  std::lock_guard lock(locked_batch_entry->mutex);
  auto& waiters = locked_batch_entry->wait_queue.get<StartUsTag>();
  if (waiters.empty()) {
    return;
  }
  key = (*waiters.begin())->resume_it()->key;
  for (auto it = waiters.begin(); it != waiters.end(); ++it) {
    if ((*it)->waiter_registration) {
      // The waiters has already registered its blockers with the local waiting registry.
      continue;
    }
    lockstate_blocker_map.emplace(IntentTypeSetConflict((*it)->resume_it()->intent_types), nullptr);
    if ((*it)->blockers != nullptr) {
      // Another thread raced with us to compute the blockers for this waiter, but hasn't yet
      // registered them with the local waiting registry.
      continue;
    }
    waiters.modify(it, [&](WaiterEntryPtr& item) {
      item->blockers = std::make_shared<ConflictDataManager>(1);
      for (auto sub_it = waiters.begin(); sub_it != it; sub_it++) {
        auto& other = *sub_it;
        const auto conflict_types = IntentTypeSetConflict(item->resume_it()->intent_types);
        const auto prev_add = IntentTypeSetAdd(other->resume_it()->intent_types);
        if (((conflict_types & prev_add) != 0) &&
            item->object_lock_owner() != other->object_lock_owner()) {
          auto conflict_info = std::make_shared<TransactionConflictInfo>();
          conflict_info->subtransactions.emplace(
              other->object_lock_owner().subtxn_id, SubTransactionConflictInfo());
          item->blockers->AddTransaction(
              other->txn_id(), conflict_info, other->status_tablet());
        }
      }
    });
  }
}

void ObjectLockManagerImpl::DoPopulateLockStateBlockersMap(
    std::optional<ObjectLockPrefix>& key,
    LockStateBlockersMap& lockstate_blocker_map) {
  std::lock_guard lock(global_mutex_);
  for (const auto& [id, txn_entry] : txn_locks_) {
    UniqueLock txn_lock(txn_entry->mutex);
    if (txn_entry->released_all_locks) {
      continue;
    }
    for (auto state_it = lockstate_blocker_map.begin(); state_it != lockstate_blocker_map.end();
          state_it++) {
      auto it = txn_entry->existing_states.find(*key);
      if (it == txn_entry->existing_states.end() || ((it->second & state_it->first) == 0)) {
        continue;
      }
      auto conflict_info = std::make_shared<TransactionConflictInfo>();
      for (const auto& [subtxn, lock_entry] : txn_entry->granted_locks) {
        auto lock_entry_it = lock_entry.find(*key);
        if (lock_entry_it == lock_entry.end()) {
          continue;
        }
        if (state_it->first & lock_entry_it->second.state) {
          conflict_info->subtransactions.emplace(subtxn, SubTransactionConflictInfo());
        }
      }
      if (conflict_info->subtransactions.size()) {
        if (!state_it->second) {
          state_it->second = std::make_shared<ConflictDataManager>(1 /* capacity */);
        }
        state_it->second->AddTransaction(id, conflict_info, txn_entry->status_tablet);
      }
    }
  }
}

void ObjectLockManagerImpl::RegisterWaiters(ObjectLockedBatchEntry* locked_batch_entry) {
  if (!locked_batch_entry->num_waiters || FLAGS_TEST_olm_skip_sending_wait_for_probes) {
    return;
  }
  if (!waiting_txn_registry_) {
    LOG(WARNING) << "waiting_txn_registry_ not set, cannot perform deadlock detection";
    return;
  }
  // We do the following steps in here.
  // 1. ObjectLockedBatchEntry::mutex
  //    - computes blockers within wait-queue and fill keys in lockstate_blocker_map for which
  //      blockers need to be found out.
  // 2. ObjectLockManagerImpl::global_mutex_
  //    - compute blockers for keys in lockstate_blocker_map by scanning granted locks.
  // 3. ObjectLockedBatchEntry::mutex
  //    - register blockers with local registry if blockers is valid
  //
  // Multiple threads can race between Steps 1 & 2, but only one thread registers the blockers
  // with the local registry in Step 3, and it needs the info from lockstate_blocker_map. Hence,
  // form the map iff the waiter hasn't registered its blocker with the waiting registry yet.
  std::optional<ObjectLockPrefix> key;
  LockStateBlockersMap lockstate_blocker_map;
  auto version = locked_batch_entry->version.load();

  DoComputeBlockersWithinQueue(locked_batch_entry, key, lockstate_blocker_map);
  if (!key || version != locked_batch_entry->version.load()) {
    return;
  }

  DoPopulateLockStateBlockersMap(key, lockstate_blocker_map);
  if (version != locked_batch_entry->version.load()) {
    return;
  }

  std::lock_guard lock(locked_batch_entry->mutex);
  auto& waiters = locked_batch_entry->wait_queue.get<StartUsTag>();
  for (auto it = waiters.begin(); it != waiters.end(); ++it) {
    if (!(*it)->blockers) {
      continue;
    }
    waiters.modify(it, [&](WaiterEntryPtr& item) {
      const auto confict_state = IntentTypeSetConflict(item->resume_it()->intent_types);
      auto state_blocker_it = lockstate_blocker_map.find(confict_state);
      if (state_blocker_it != lockstate_blocker_map.end() && state_blocker_it->second) {
        for (const auto& blocker : state_blocker_it->second->RemainingTransactions()) {
          if (item->txn_id() == blocker.id) {
            continue;
          }
          item->blockers->AddTransaction(blocker.id, blocker.conflict_info, blocker.status_tablet);
        }
      }
      if (item->blockers->NumActiveTransactions()) {
        item->waiter_registration = waiting_txn_registry_->Create();
        WARN_NOT_OK(
            item->waiter_registration->Register(
                item->txn_id(), -1 /* request id */, std::move(item->blockers),
                item->status_tablet(), boost::none /* pg_session_req_version */),
            Format("Failed to register blockers of waiter $0",
                   AsString(item->object_lock_owner())));
      }
      item->blockers = nullptr;
    });
  }
}

void ObjectLockManagerImpl::DumpStatusHtml(std::ostream& out) {
  out << "<table class='table table-striped'>\n";
  out << "<tr><th>Prefix</th><th>LockBatchEntry</th></tr>" << std::endl;
  std::lock_guard l(global_mutex_);
  for (const auto& [prefix, entry] : locks_) {
    auto key_str = AsString(prefix);
    out << "<tr>"
          << "<td>" << (!key_str.empty() ? key_str : "[empty]") << "</td>"
          << "<td>" << entry->ToString() << "</td>"
        << "</tr>\n";
  }
  out << "</table>\n";

  DumpStoredObjectLocksMap(out, "Granted object locks", LocksMapType::kGranted);
  DumpStoredObjectLocksMap(out, "Waiting object locks", LocksMapType::kWaiting);
}

void ObjectLockManagerImpl::DumpStoredObjectLocksMap(
    std::ostream& out, std::string_view caption, LocksMapType locks_map) {
  out << R"(
      <table class="table table-striped">
      <caption>)"<< caption << R"(</caption>
      <tr>
        <th>Lock Owner</th>
        <th>Object Id</th>
        <th>Num Holders</th>
      </tr>)";
  for (const auto& [txn, txn_entry] : txn_locks_) {
    UniqueLock txn_lock(txn_entry->mutex);
    const auto& locks =
        locks_map == LocksMapType::kGranted ? txn_entry->granted_locks : txn_entry->waiting_locks;
    for (const auto& [subtxn_id, subtxn_locks] : locks) {
      for (const auto& [object_id, entry] : subtxn_locks) {
        out << "<tr>"
            << "<td>" << Format("{txn: $0 subtxn_id: $1}", txn, subtxn_id) << "</td>"
            << "<td>" << AsString(object_id) << "</td>"
            << "<td>" << LockStateDebugString(entry.state) << "</td>"
            << "</tr>\n";
      }
    }
  }
  out << "</table>\n";
}

size_t ObjectLockManagerImpl::TEST_LocksSize(LocksMapType locks_map) const {
  std::lock_guard lock(global_mutex_);
  size_t size = 0;
  for (const auto& [txn, txn_entry] : txn_locks_) {
    UniqueLock txn_lock(txn_entry->mutex);
    const auto& locks =
        locks_map == LocksMapType::kGranted ? txn_entry->granted_locks : txn_entry->waiting_locks;
    for (const auto& [subtxn_id, subtxn_locks] : locks) {
      size += subtxn_locks.size();
    }
  }
  return size;
}

size_t ObjectLockManagerImpl::TEST_GrantedLocksSize() const {
  return TEST_LocksSize(LocksMapType::kGranted);
}

size_t ObjectLockManagerImpl::TEST_WaitingLocksSize() const {
  return TEST_LocksSize(LocksMapType::kWaiting);
}

std::unordered_map<ObjectLockPrefix, LockState>
    ObjectLockManagerImpl::TEST_GetLockStateMapForTxn(const TransactionId& txn) const {
  TrackedTxnLockEntryPtr txn_entry;
  {
    std::lock_guard lock(global_mutex_);
    auto txn_it = txn_locks_.find(txn);
    if (txn_it == txn_locks_.end()) {
      return {};
    }
    txn_entry = txn_it->second;
  }
  UniqueLock txn_lock(txn_entry->mutex);
  return txn_entry->existing_states;
}

ObjectLockManager::ObjectLockManager(ThreadPool* thread_pool, server::RpcServerBase& server)
  : impl_(std::make_unique<ObjectLockManagerImpl>(thread_pool, server)) {}

ObjectLockManager::~ObjectLockManager() = default;

void ObjectLockManager::Lock(LockData&& data) {
  impl_->Lock(std::move(data));
}

void ObjectLockManager::Unlock(
    const ObjectLockOwner& object_lock_owner, Status resume_with_status) {
  impl_->Unlock(object_lock_owner, resume_with_status);
}

void ObjectLockManager::Poll() {
  impl_->Poll();
}

void ObjectLockManager::Start(
    docdb::LocalWaitingTxnRegistry* waiting_txn_registry) {
  return impl_->Start(waiting_txn_registry);
}

void ObjectLockManager::Shutdown() {
  impl_->Shutdown();
}

void ObjectLockManager::DumpStatusHtml(std::ostream& out) {
  impl_->DumpStatusHtml(out);
}

size_t ObjectLockManager::TEST_GrantedLocksSize() const {
  return impl_->TEST_GrantedLocksSize();
}

size_t ObjectLockManager::TEST_WaitingLocksSize() const {
  return impl_->TEST_WaitingLocksSize();
}

std::unordered_map<ObjectLockPrefix, LockState>
    ObjectLockManager::TEST_GetLockStateMapForTxn(const TransactionId& txn) const {
  return impl_->TEST_GetLockStateMapForTxn(txn);
}

}  // namespace yb::docdb
