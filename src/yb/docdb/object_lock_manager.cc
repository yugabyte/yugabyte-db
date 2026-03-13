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

#include "yb/docdb/object_lock_manager.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/multi_index/mem_fun.hpp>

#include "yb/ash/wait_state.h"
#include "yb/docdb/local_waiting_txn_registry.h"
#include "yb/docdb/lock_batch.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_shared_state_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/thread_pool.h"

#include "yb/server/server_base.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug/long_operation_tracker.h"

#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/lw_function.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

DEFINE_test_flag(bool, assert_olm_empty_locks_map, false,
    "When set, asserts that the local locks map is empty at shutdown. Used in tests "
    "to assert sanity, where tserver doesn't loose YSQL lease and all connections release "
    "acquired/tryagain/errored locks.");

DEFINE_test_flag(bool, olm_skip_scheduling_waiter_resumption, false,
    "When set, don't signal potential waiters for resumption");

DEFINE_test_flag(bool, olm_skip_sending_wait_for_probes, false,
    "When set, the lock manager doesn't send wait-for probres to the local waiting txn registry, "
    "essentially giving away deadlock detection.");

DEFINE_test_flag(bool, olm_serve_redundant_lock, false,
    "When set, the lock manager re-acquires redundant locks instead of just incrementing "
    "the ref_count and returning.");

METRIC_DEFINE_counter(server, object_locking_lock_acquires,
                      "Number of object locking lock acquires (both fast and slow path)",
                      yb::MetricUnit::kRequests,
                      "Number of object locking lock acquires (both fast and slow path)");

METRIC_DEFINE_counter(server, object_locking_fastpath_acquires,
                      "Number of object locking fast path lock acquires",
                      yb::MetricUnit::kRequests,
                      "Number of object locking fast path lock acquires");

using namespace std::placeholders;
using namespace std::literals;
namespace yb::docdb {

using dockv::IntentTypeSet;

struct ObjectLockedBatchEntry;

namespace {

// Below are the two different scenarios where we fail requests with ShutdownInProgress in case
// of the lock manager shutting down.
// 1. upon ysql lease changes
// 2. on shutdown of the tserver node
//
// Master leader retries failed lock requests with code ShutdownInProgress until either the retry
// attempts are exhausted or the tserver looses it lease. This is the desired behavior as it
// achieves resiliency for DDL lock requests amidst cluster membership changes.
const Status kShuttingDownError = STATUS(
    ShutdownInProgress, "Object Lock Manager shutting down");

const Status kTryAgain = STATUS(
    TryAgain, "Failed to acquire object locks within deadline");

const Status kTxnExpired = STATUS(
    Expired, "Transaction expired, all acquired object locks have been released");

YB_DEFINE_ENUM(LocksMapType, (kGranted)(kWaiting));
YB_STRONGLY_TYPED_BOOL(IsLockRetry);

using LockStateBlockersMap = std::unordered_map<LockState, std::shared_ptr<ConflictDataManager>>;

using LockBatchEntrySpan = std::span<LockBatchEntry<ObjectLockManager>>;

using LockStateMap = std::unordered_map<ObjectLockPrefix, LockState>;

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

  bool HandleRedundantLock(
      LockBatchEntrySpan key_to_intent_type,
      const ObjectLockOwner& object_lock_owner) EXCLUDES(mutex);

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

  mutable std::mutex mutex;

  LockEntryMap granted_locks GUARDED_BY(mutex);
  LockEntryMap waiting_locks GUARDED_BY(mutex);
  LockStateMap existing_states GUARDED_BY(mutex);

  std::unordered_set<SubTransactionId> released_subtxns GUARDED_BY(mutex);
  bool released_all_locks GUARDED_BY(mutex) = false;
  TabletId status_tablet GUARDED_BY(mutex);
  TxnBlockedTableLockRequests was_a_blocker GUARDED_BY(mutex) = TxnBlockedTableLockRequests::kFalse;
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

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(lock_data, resume_it_offset);
  }

  TrackedTxnLockEntryPtr transaction_entry;
  LockData lock_data;
  size_t resume_it_offset;
  // Below fields are operated under corresponding ObjectLockedBatchEntry::mutex.
  std::unique_ptr<ScopedWaitingTxnRegistration> waiter_registration;
  std::shared_ptr<ConflictDataManager> blockers;
  TxnBlockedTableLockRequests was_a_blocker = TxnBlockedTableLockRequests::kFalse;
};

inline bool operator<(const WaiterEntry& lhs, const WaiterEntry& rhs) {
  return lhs.lock_data.start_time < rhs.lock_data.start_time ||
         (lhs.lock_data.start_time == rhs.lock_data.start_time &&
          lhs.lock_data.object_lock_owner.txn_id < rhs.lock_data.object_lock_owner.txn_id);
}

using WaiterEntryPtr = std::shared_ptr<WaiterEntry>;

struct StartUsTag;
struct DeadlineTag;
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

  std::atomic<LockState> waiters_in_resuming_state{0};

  std::string ToString() const {
    return Format("{ ref_count: $0 lock_state: $1 num_waiters: $2 }",
                  ref_count,
                  LockStateDebugString(num_holding.load(std::memory_order_acquire)),
                  num_waiters.load(std::memory_order_acquire));
  }
};

class ObjectLockManagerImpl {
 public:
  ObjectLockManagerImpl(
      ThreadPool* thread_pool, server::RpcServerBase& server, const MetricEntityPtr& metric_entity,
      ObjectLockSharedStateManager* shared_manager)
    : thread_pool_token_(thread_pool->NewToken(ThreadPool::ExecutionMode::CONCURRENT)),
      server_(server),
      waiters_amidst_resumption_on_messenger_("ObjectLockManagerImpl: " /* log_prefix */),
      shared_manager_(shared_manager) {
    metric_num_acquires_ =
        METRIC_object_locking_lock_acquires.Instantiate(metric_entity);
    metric_num_fastpath_acquires_ =
        METRIC_object_locking_fastpath_acquires.Instantiate(metric_entity);
  }

  void Lock(LockData&& data);

  TxnBlockedTableLockRequests Unlock(const ObjectLockOwner& object_lock_owner);

  void UnlockObjectsForSession(
      const TransactionId& txn, DetermineKeysToLockResult<ObjectLockManager>&& key_to_unlock);

  void Poll() EXCLUDES(global_mutex_);

  void Start(docdb::LocalWaitingTxnRegistry* waiting_txn_registry) {
    waiting_txn_registry_ = waiting_txn_registry;
  }

  void Shutdown();

  void DumpStatusHtml(std::ostream& out) EXCLUDES(global_mutex_);

  void ConsumePendingSharedLockRequests() EXCLUDES(global_mutex_);

  size_t TEST_LocksSize(LocksMapType locks_map);
  size_t TEST_GrantedLocksSize();
  size_t TEST_WaitingLocksSize();
  LockStateMap TEST_GetLockStateMapForTxn(const TransactionId& txn) const;

 private:
  friend struct WaiterEntry;

  void ConsumePendingSharedLockRequestsUnlocked() REQUIRES(global_mutex_);
  void ConsumePendingSharedLockRequestUnlocked(
      ObjectSharedLockRequest& request) REQUIRES(global_mutex_);
  void AcquireExclusiveLockIntents(const LockData& data) EXCLUDES(global_mutex_);
  void ReleaseExclusiveLockIntents(const LockStateMap& lockstates_map);
  void ReleaseExclusiveLockIntents(
      std::span<const LockBatchEntry<ObjectLockManager>> key_to_intent_type);

  TrackedTxnLockEntryPtr GetTransactionEntryUnlocked(const TransactionId& txn_id)
      REQUIRES(global_mutex_);

  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  TrackedTxnLockEntryPtr Reserve(
      LockBatchEntrySpan batch, const ObjectLockOwner& object_lock_owner) EXCLUDES(global_mutex_);

  TrackedTxnLockEntryPtr DoReserve(
      LockBatchEntrySpan key_to_intent_type, const ObjectLockOwner& object_lock_owner)
      REQUIRES(global_mutex_);

  // Update refcounts and maybe collect garbage.
  void DoCleanup(std::span<const LockBatchEntry<ObjectLockManager>> key_to_intent_type)
      EXCLUDES(global_mutex_);

  Status MakePrepareAcquireResult(const LockData& data, Status resume_with_status);

  // Performs necessary cleanup and checks whether the locks request needs to be processed.
  Status PrepareAcquire(
      std::unique_lock<std::mutex>& txn_lock, TrackedTxnLockEntryPtr& transaction_entry,
      const LockData& data, size_t resume_it_offset, Status resume_with_status,
      IsLockRetry is_retry) REQUIRES(transaction_entry->mutex) EXCLUDES(global_mutex_);

  std::vector<LockState> FetchExistingStatesForBgTxn(const LockData& data) EXCLUDES(global_mutex_);

  void DoLock(
      TrackedTxnLockEntryPtr transaction_entry, LockData&& data, IsLockRetry is_retry,
      size_t resume_it_offset = 0, Status resume_with_status = Status::OK())
      EXCLUDES(global_mutex_, transaction_entry->mutex);

  MUST_USE_RESULT bool DoLockSingleEntry(
      TrackedTxnLockEntryPtr& transaction_entry, LockData& data, LockState existing_state,
      LockBatchEntries<ObjectLockManager>::iterator lock_entry_it,
      IsLockRetry is_retry) REQUIRES(transaction_entry->mutex);

  void DoLockSingleEntryWithoutConflictCheck(
      const LockBatchEntry<ObjectLockManager>& lock_entry,
      TrackedTxnLockEntryPtr& transaction_entry, const ObjectLockOwner& owner)
      REQUIRES(transaction_entry->mutex);

  void DoUnlock(
      const ObjectLockOwner& object_lock_owner,
      TrackedTransactionLockEntry::LockEntryMap& locks_map,
      TrackedTxnLockEntryPtr& txn_entry,
      LockStateMap& lockstates_map) REQUIRES(global_mutex_, txn_entry->mutex);

  bool UnlockSingleEntry(const LockBatchEntry<ObjectLockManager>& lock_entry);

  bool DoUnlockSingleEntry(
      const ObjectLockPrefix& object_id, ObjectLockedBatchEntry& entry, LockState sub);

  void RegisterWaiters(ObjectLockedBatchEntry* locked_batch_entry) EXCLUDES(global_mutex_);

  void DoReleaseTrackedLock(
      const ObjectLockPrefix& object_id, TrackedLockEntry& entry, LockStateMap& lockstates_map)
      REQUIRES(global_mutex_);

  void UnblockPotentialWaiters(ObjectLockedBatchEntry* entry);

  void DoSignal(ObjectLockedBatchEntry* entry);

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

  ObjectLockSharedStateManager* const shared_manager_;

  scoped_refptr<Counter> metric_num_acquires_;
  scoped_refptr<Counter> metric_num_fastpath_acquires_;
};

void WaiterEntry::Resume(ObjectLockManagerImpl* lock_manager, Status resume_with_status) {
  {
    std::lock_guard txn_lock(transaction_entry->mutex);
    transaction_entry->was_a_blocker =
        TxnBlockedTableLockRequests(transaction_entry->was_a_blocker || was_a_blocker);
    resume_it()->locked->num_waiters.fetch_sub(1);
    transaction_entry->AddReleasedLockUnlocked(
        *resume_it(), lock_data.object_lock_owner, LocksMapType::kWaiting);
  }
  DEBUG_ONLY_TEST_SYNC_POINT_CALLBACK("WaiterEntry::Resume", &lock_data.object_lock_owner.txn_id);
  lock_manager->DoLock(
      transaction_entry, std::move(lock_data), IsLockRetry::kTrue, resume_it_offset,
      resume_with_status);
}

void TrackedTransactionLockEntry::AddAcquiredLockUnlocked(
    const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner& object_lock_owner,
    LocksMapType locks_map) {
  TRACE_FUNC();
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", object_lock_owner: " << AsString(object_lock_owner)
                    << ", " << AsString(locks_map);
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

bool TrackedTransactionLockEntry::HandleRedundantLock(
    LockBatchEntrySpan key_to_intent_type,
    const ObjectLockOwner& object_lock_owner) {
  TRACE_FUNC();
  if (PREDICT_FALSE(FLAGS_TEST_olm_serve_redundant_lock)) {
    return false;
  }
  std::lock_guard lock(mutex);
  const bool is_lock_redundant =
      std::ranges::all_of(key_to_intent_type, [&](auto key_and_intent) REQUIRES (mutex) {
        return LockStateContains(GetLockStateForKeyUnlocked(key_and_intent.key),
                                 IntentTypeSetAdd(key_and_intent.intent_types));
      });
  if (!is_lock_redundant) {
    return false;
  }
  for (auto& lock_entry : key_to_intent_type) {
    VLOG_WITH_FUNC(1)
        << "Ignoring redundant acquire for lock_entry: " << lock_entry.ToString()
        << ", object_lock_owner: " << AsString(object_lock_owner)
        << ", with state: " << LockStateDebugString(GetLockStateForKeyUnlocked(lock_entry.key));
    auto& subtxn_locks = granted_locks[object_lock_owner.subtxn_id];
    auto [it, _] = subtxn_locks.try_emplace(lock_entry.key, TrackedLockEntry(*lock_entry.locked));
    ++it->second.ref_count;
  }
  return true;
}

void TrackedTransactionLockEntry::AddReleasedLockUnlocked(
    const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner& object_lock_owner,
    LocksMapType locks_map) {
  TRACE_FUNC();
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", object_lock_owner: " << AsString(object_lock_owner)
                    << ", " << AsString(locks_map);
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

void ObjectLockManagerImpl::ConsumePendingSharedLockRequestsUnlocked() {
  if (shared_manager_) {
    size_t consumed = shared_manager_->ConsumePendingSharedLockRequests(
        make_lw_function([this](ObjectSharedLockRequest request) NO_THREAD_SAFETY_ANALYSIS {
          ConsumePendingSharedLockRequestUnlocked(request);
        }));
    IncrementCounterBy(metric_num_acquires_, consumed);
    IncrementCounterBy(metric_num_fastpath_acquires_, consumed);
  }
}

void ObjectLockManagerImpl::ConsumePendingSharedLockRequestUnlocked(
    ObjectSharedLockRequest& request) {
  auto& lock_entry = request.entry;
  auto transaction_entry = DoReserve({&lock_entry, 1}, request.owner);
  if (transaction_entry->HandleRedundantLock({&lock_entry, 1}, request.owner)) {
    return;
  }
  std::lock_guard lock(transaction_entry->mutex);
  transaction_entry->status_tablet = std::move(request.status_tablet);
  DoLockSingleEntryWithoutConflictCheck(lock_entry, transaction_entry, request.owner);
}

void ObjectLockManagerImpl::AcquireExclusiveLockIntents(const LockData& data) {
  if (!shared_manager_) {
    return;
  }
  // Single lock type maps to 1-2 entries.
  boost::container::small_vector<const LockBatchEntry<ObjectLockManager>*, 2> exclusive_locks;
  for (const auto& entry : data.key_to_lock.lock_batch) {
    if (!IntentTypeReadOnly(entry.intent_types)) {
      exclusive_locks.push_back(&entry);
    }
  }
  std::lock_guard lock(global_mutex_);
  size_t consumed = shared_manager_->ConsumeAndAcquireExclusiveLockIntents(
      make_lw_function([this](ObjectSharedLockRequest request) NO_THREAD_SAFETY_ANALYSIS {
        ConsumePendingSharedLockRequestUnlocked(request);
      }),
      exclusive_locks);
  IncrementCounterBy(metric_num_acquires_, consumed);
  IncrementCounterBy(metric_num_fastpath_acquires_, consumed);
}

void ObjectLockManagerImpl::ReleaseExclusiveLockIntents(const LockStateMap& lockstates_map) {
  if (!shared_manager_) {
    return;
  }
  for (const auto& [key, lock_state] : lockstates_map) {
    shared_manager_->ReleaseExclusiveLockIntent(key, lock_state);
  }
}

void ObjectLockManagerImpl::ReleaseExclusiveLockIntents(
    std::span<const LockBatchEntry<ObjectLockManager>> key_to_intent_type) {
  if (!shared_manager_) {
    return;
  }
  for (const auto& item : key_to_intent_type) {
    if (!IntentTypeReadOnly(item.intent_types)) {
      shared_manager_->ReleaseExclusiveLockIntent(item.key, IntentTypeSetAdd(item.intent_types));
    }
  }
}

TrackedTxnLockEntryPtr ObjectLockManagerImpl::GetTransactionEntryUnlocked(
    const TransactionId& txn_id) {
  // TODO: Should we switch similar logic of allocation and reuse as with lock entries?
  const auto& [it, _] = txn_locks_.emplace(txn_id, std::make_shared<TrackedTransactionLockEntry>());
  return it->second;
}

TrackedTxnLockEntryPtr ObjectLockManagerImpl::Reserve(
    LockBatchEntrySpan key_to_intent_type, const ObjectLockOwner& object_lock_owner) {
  std::lock_guard lock(global_mutex_);
  return DoReserve(key_to_intent_type, object_lock_owner);
}

TrackedTxnLockEntryPtr ObjectLockManagerImpl::DoReserve(
    LockBatchEntrySpan key_to_intent_type, const ObjectLockOwner& object_lock_owner) {
  auto transaction_entry = GetTransactionEntryUnlocked(object_lock_owner.txn_id);
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
  ReleaseExclusiveLockIntents(key_to_intent_type);
  std::lock_guard lock(global_mutex_);
  for (const auto& item : key_to_intent_type) {
    LOG_IF(DFATAL, item.locked->ref_count == 0) << "Local Object lock manager state corrupted";
    if (--item.locked->ref_count == 0) {
      LOG_IF(DFATAL, item.locked->num_waiters)
          << "Local Object lock manager state corrupted, non zero waiter but object being GC'ed";
      LOG_IF(DFATAL, item.locked->waiters_in_resuming_state.load())
          << "Local Object lock manager state corrupted, non-zero waiters_in_resuming_state";
      locks_.erase(item.key);
      item.locked->version++;
      free_lock_entries_.push_back(item.locked);
    }
  }
}

void ObjectLockManagerImpl::Lock(LockData&& data) {
  TRACE("Locking a batch of $0 keys", data.key_to_lock.lock_batch.size());
  auto transaction_entry = Reserve(data.key_to_lock.lock_batch, data.object_lock_owner);
  if (transaction_entry->HandleRedundantLock(data.key_to_lock.lock_batch, data.object_lock_owner)) {
    data.callback(Status::OK());
    return;
  }
  if (shared_manager_) {
    AcquireExclusiveLockIntents(data);
  }
  DoLock(transaction_entry, std::move(data), IsLockRetry::kFalse);
}

Status ObjectLockManagerImpl::MakePrepareAcquireResult(
    const LockData& data, Status resume_with_status) {
  RETURN_NOT_OK(resume_with_status);
  if (data.deadline < CoarseMonoClock::Now()) {
    return kTryAgain;
  }
  if (shutdown_in_progress_) {
    return kShuttingDownError;
  }
  return Status::OK();
}

Status ObjectLockManagerImpl::PrepareAcquire(
    std::unique_lock<std::mutex>& txn_lock, TrackedTxnLockEntryPtr& transaction_entry,
    const LockData& data, size_t resume_it_offset, Status resume_with_status,
    IsLockRetry is_retry) {
  auto it = data.key_to_lock.lock_batch.begin() + resume_it_offset;
  const auto& key_to_lock = data.key_to_lock;
  const auto& object_lock_owner = data.object_lock_owner;
  if (transaction_entry->ShouldSkipAcquireUnlocked(object_lock_owner.subtxn_id)) {
    // Release corresponding to this acquire has already been processed.
    if (is_retry) {
      it->locked->waiters_in_resuming_state.fetch_sub(IntentTypeSetAdd(it->intent_types));
    }
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
  if (is_retry) {
    it->locked->waiters_in_resuming_state.fetch_sub(IntentTypeSetAdd(it->intent_types));
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

std::vector<LockState> ObjectLockManagerImpl::FetchExistingStatesForBgTxn(const LockData& data) {
  std::vector<LockState> existing_states(data.key_to_lock.lock_batch.size(), 0);
  if (PREDICT_TRUE(data.background_transaction_id.IsNil())) {
    return existing_states;
  }
  TrackedTxnLockEntryPtr bg_txn_entry;
  {
    std::lock_guard lock(global_mutex_);
    bg_txn_entry = GetTransactionEntryUnlocked(data.background_transaction_id);
  }
  if (!bg_txn_entry) {
    return existing_states;
  }
  std::lock_guard txn_lock(bg_txn_entry->mutex);
  for (size_t i = 0; i < data.key_to_lock.lock_batch.size(); ++i) {
    existing_states[i] +=
        bg_txn_entry->GetLockStateForKeyUnlocked(data.key_to_lock.lock_batch[i].key);
    VLOG(1) << "Owner " << AsString(data.object_lock_owner)
        << " with background txn " << data.background_transaction_id.ToString()
        << " initialized with existing state " << LockStateDebugString(existing_states[i])
        << " for key " << AsString(data.key_to_lock.lock_batch[i].key);
  }
  return existing_states;
}

void ObjectLockManagerImpl::DoLock(
    TrackedTxnLockEntryPtr transaction_entry, LockData&& data, IsLockRetry is_retry,
    size_t resume_it_offset, Status resume_with_status) {
  auto existing_states = FetchExistingStatesForBgTxn(data);
  {
    UniqueLock txn_lock(transaction_entry->mutex);
    auto it = data.key_to_lock.lock_batch.begin() + resume_it_offset;
    auto prepare_status = PrepareAcquire(
        GetLockForCondition(txn_lock), transaction_entry, data, resume_it_offset,
        resume_with_status, is_retry);
    if (!prepare_status.ok()) {
      data.callback(prepare_status);
      return;
    }

    if (!is_retry) {
      transaction_entry->status_tablet = data.status_tablet;
    }
    while (it != data.key_to_lock.lock_batch.end()) {
      // Ignore conflicts with self.
      auto& existing_state = existing_states[it - data.key_to_lock.lock_batch.begin()];
      existing_state += transaction_entry->GetLockStateForKeyUnlocked(it->key);
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
      is_retry = IsLockRetry::kFalse;
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
  auto decrement_waiters_in_resuming_state = [is_retry, &entry](LockState delta) {
    if (is_retry) {
      entry->waiters_in_resuming_state.fetch_sub(delta);
    }
  };
  auto& object_lock_owner = data.object_lock_owner;
  auto old_value = entry->num_holding.load();
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
        IncrementCounter(metric_num_acquires_);
        decrement_waiters_in_resuming_state(add);
        return true;
      }
      continue;
    }
    // Realese thread signals for the object when num_waiters > 0, and the resumption thread checks
    // the wait-queue under a mutex. Increment num_waiters under mutex to avoid the race where
    // num_waiters > 0 but wait-queue is empty.
    std::lock_guard l(entry->mutex);
    entry->num_waiters.fetch_add(1);
    old_value = entry->num_holding.load();
    if (should_check_wq || ((old_value ^ existing_state) & conflicting_lock_state) != 0) {
      DEBUG_ONLY_TEST_SYNC_POINT("ObjectLockManagerImpl::DoLockSingleEntry");
      SCOPED_WAIT_STATUS(LockedBatchEntry_Lock);
      VLOG(1) << AsString(object_lock_owner) << " added to wait-queue on " << AsString(lock_entry);
      transaction_entry->AddAcquiredLockUnlocked(
          lock_entry, object_lock_owner, LocksMapType::kWaiting);
      decrement_waiters_in_resuming_state(add);
      entry->waiting_state += add;
      // TODO(bkolagani): Reuse WaiterDataPtr as opposed to creating multiple shared_ptrs for single
      // lock request. Refer https://github.com/yugabyte/yugabyte-db/issues/27107 for details.
      entry->wait_queue.insert(std::make_shared<WaiterEntry>(
          std::move(transaction_entry), std::move(data),
          lock_entry_it - data.key_to_lock.lock_batch.begin()));
      return false;
    }
    entry->num_waiters.fetch_sub(1);
  }
}

void ObjectLockManagerImpl::DoLockSingleEntryWithoutConflictCheck(
    const LockBatchEntry<ObjectLockManager>& lock_entry, TrackedTxnLockEntryPtr& transaction_entry,
    const ObjectLockOwner& owner) {
  TRACE_FUNC();
  auto& entry = lock_entry.locked;
  DCHECK((entry->waiting_state & IntentTypeSetConflict(lock_entry.intent_types)) == 0 &&
         (entry->num_holding.load() & IntentTypeSetConflict(lock_entry.intent_types)) == 0);
  entry->num_holding.fetch_add(
      IntentTypeSetAdd(lock_entry.intent_types), std::memory_order_acq_rel);
  VLOG(4) << AsString(owner) << " acquired lock " << AsString(lock_entry);
  transaction_entry->AddAcquiredLockUnlocked(lock_entry, owner, LocksMapType::kGranted);
}

void ObjectLockManagerImpl::UnlockObjectsForSession(
    const TransactionId& txn, DetermineKeysToLockResult<ObjectLockManager>&& key_to_unlock) {
  LockStateMap lockstates_map;
  {
    std::lock_guard lock(global_mutex_);
    ConsumePendingSharedLockRequestsUnlocked();
    auto txn_itr = txn_locks_.find(txn);
    if (txn_itr == txn_locks_.end()) {
      return;
    }
    TrackedTxnLockEntryPtr txn_entry = txn_itr->second;
    std::lock_guard txn_lock(txn_entry->mutex);
    for (auto& key_and_intent : key_to_unlock.lock_batch) {
      for (auto& [subtxn, locks_map] : txn_entry->granted_locks) {
        auto it = locks_map.find(key_and_intent.key);
        if (it == locks_map.end()) {
          continue;
        }
        txn_entry->existing_states[key_and_intent.key] -= it->second.state;
        DoReleaseTrackedLock(it->first, it->second, lockstates_map);
        locks_map.erase(it);
      }
    }
  }
  ReleaseExclusiveLockIntents(lockstates_map);
}

TxnBlockedTableLockRequests ObjectLockManagerImpl::Unlock(
    const ObjectLockOwner& object_lock_owner) {
  TRACE("Unlocking all keys for owner $0", AsString(object_lock_owner));

  TrackedTxnLockEntryPtr txn_entry;
  {
    std::lock_guard lock(global_mutex_);
    ConsumePendingSharedLockRequestsUnlocked();
    auto txn_itr = txn_locks_.find(object_lock_owner.txn_id);
    if (txn_itr == txn_locks_.end()) {
      return TxnBlockedTableLockRequests::kFalse;
    }
    txn_entry = txn_itr->second;
    if (!object_lock_owner.subtxn_id) {
      txn_locks_.erase(txn_itr);
    }
  }
  TxnBlockedTableLockRequests was_a_blocker(false);
  {
    std::lock_guard txn_lock(txn_entry->mutex);
    if (object_lock_owner.subtxn_id) {
      txn_entry->released_subtxns.emplace(object_lock_owner.subtxn_id);
    } else {
      txn_entry->released_all_locks = true;
    }
    was_a_blocker = txn_entry->was_a_blocker;
  }

  LockStateMap lockstates_map;
  {
    std::lock_guard lock(global_mutex_);
    std::lock_guard txn_lock(txn_entry->mutex);
    DoUnlock(object_lock_owner, txn_entry->granted_locks, txn_entry, lockstates_map);
  }
  ReleaseExclusiveLockIntents(lockstates_map);
  // We let the obsolete waiting lock request for this txn/subtxn, if any, to timeout and be resumed
  // as part of ObjectLockManagerImpl::Poll. This should be okay since:
  // 1. Obsolete waiting request could exist when txn times out due to conflict and pg backend
  //    issues a finish txn request before the lock manager times out the obsolete request. Since
  //    the obsolete waiting request would anyways be past the deadline, it would be resumed soon.
  // 2. On abort due to txn deadlock, we anyways don't send an early release all request.
  //    PgClientSession waits for the previous lock req deadline (FLAGS_refresh_waiter_timeout_ms)
  //    and then drops the retry since the txn failed.
  //
  // If there's any requirement to early terminate obsolete waiters based on txn id, then we should
  // signal appropriately here.
  return was_a_blocker;
}

void ObjectLockManagerImpl::DoUnlock(
    const ObjectLockOwner& object_lock_owner, TrackedTransactionLockEntry::LockEntryMap& locks_map,
    TrackedTxnLockEntryPtr& txn_entry, LockStateMap& lockstates_map) {
  auto& existing_states = txn_entry->existing_states;
  if (object_lock_owner.subtxn_id) {
    auto subtxn_itr = locks_map.find(object_lock_owner.subtxn_id);
    if (subtxn_itr == locks_map.end()) {
      return;
    }
    for (auto itr = subtxn_itr->second.begin(); itr != subtxn_itr->second.end(); itr++) {
      existing_states[itr->first] -= itr->second.state;
      DoReleaseTrackedLock(itr->first, itr->second, lockstates_map);
    }
    locks_map.erase(subtxn_itr);
    return;
  }
  for (auto locks_itr = locks_map.begin(); locks_itr != locks_map.end(); locks_itr++) {
    for (auto itr = locks_itr->second.begin(); itr != locks_itr->second.end(); itr++) {
      existing_states[itr->first] -= itr->second.state;
      DoReleaseTrackedLock(itr->first, itr->second, lockstates_map);
    }
  }
}

bool ObjectLockManagerImpl::UnlockSingleEntry(const LockBatchEntry<ObjectLockManager>& lock_entry) {
  TRACE_FUNC();
  return DoUnlockSingleEntry(
      lock_entry.key, *lock_entry.locked, IntentTypeSetAdd(lock_entry.intent_types));
}

bool ObjectLockManagerImpl::DoUnlockSingleEntry(
     const ObjectLockPrefix& object_id, ObjectLockedBatchEntry& entry, LockState sub) {
  entry.num_holding.fetch_sub(sub, std::memory_order_acq_rel);
  return entry.num_waiters.load();
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
        const auto delta = IntentTypeSetAdd(waiter_entry->resume_it()->intent_types);
        entry->waiters_in_resuming_state.fetch_add(delta);
        entry->waiting_state.fetch_sub(delta);
        timed_out_waiters.push_back(std::move(waiter_entry));
      }
    }
  }
  for (auto& waiter : timed_out_waiters) {
    waiter->Resume(this, kTryAgain);
  }
}

void ObjectLockManagerImpl::Shutdown() {
  shutdown_in_progress_ = true;
  // Shutdown of thread pool token => no more tasks pending/scheduled for resumption.
  thread_pool_token_->Shutdown();
  // Wait for resumptions scheduled on the messenger as they might access thread_pool_token_.
  waiters_amidst_resumption_on_messenger_.Shutdown();
  thread_pool_token_.reset();
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
        const auto delta = IntentTypeSetAdd(waiter_entry->resume_it()->intent_types);
        entry->waiters_in_resuming_state.fetch_add(delta);
        entry->waiting_state.fetch_sub(delta);
        waiters.push_back(std::move(waiter_entry));
      }
    }
  }

  for (auto& waiter : waiters) {
    waiter->Resume(this, kShuttingDownError);
  }
  if (FLAGS_TEST_assert_olm_empty_locks_map) {
    if (shared_manager_) {
      LOG_IF(DFATAL, shared_manager_->TEST_has_exclusive_intents())
          << "Unexpected exclusive intents in Object Lock Manager shared state.";
    }
    std::lock_guard l(global_mutex_);
    LOG_IF(DFATAL, !locks_.empty())
        << "ref_count of some lock structures is non-zero on shutdown, implies either some "
        << "connections are outstanding or indicates a state corruption of the lock manager.\n"
        << AsString(locks_);
  }
}

void ObjectLockManagerImpl::DoSignal(ObjectLockedBatchEntry* entry) {
  if (shutdown_in_progress_ || FLAGS_TEST_olm_skip_scheduling_waiter_resumption) {
    return;
  }
  WARN_NOT_OK(
      thread_pool_token_->SubmitFunc(
          std::bind(&ObjectLockManagerImpl::UnblockPotentialWaiters, this, entry)),
      "Failure submitting task ObjectLockManagerImpl::UnblockPotentialWaiters");
}

// TODO(#29684): Can possibly enhance this to take current num_holding lockstate so
// as to resume even more waiters to achieve greater throughout whenever possible.
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
      const auto intent_types = waiter_entry->resume_it()->intent_types;
      const auto delta = IntentTypeSetAdd(intent_types);
      const auto waiters_in_resuming_state = entry->waiters_in_resuming_state.load();
      if ((waiters_in_resuming_state & IntentTypeSetConflict(intent_types)) != 0) {
        VLOG_WITH_FUNC(1)
            << "Skipping resuming additional waiters: " << AsString(entry->wait_queue) << ", as "
            << LockStateDebugString(waiters_in_resuming_state) << " are amidst resumption.";
        break;
      }
      VLOG_IF(1, waiters_in_resuming_state)
          << "Resuming waiter despite active waiters since they don't conflict";
      index.erase(index.begin());
      if (waiter_entry->deadline() > now) {
        unblock_state += delta;
      }
      waiter_entry->waiter_registration.reset();
      entry->waiters_in_resuming_state.fetch_add(delta);
      entry->waiting_state.fetch_sub(delta);
      VLOG(1) << "Resuming " << AsString(waiter_entry->object_lock_owner());
      prev_txn = waiter_entry->txn_id();
      if (PREDICT_TRUE(messenger)) {
        ScopedOperation resuming_waiter_op(&waiters_amidst_resumption_on_messenger_);
        messenger->ThreadPool().EnqueueFunctor(
            [operation = std::move(resuming_waiter_op), waiter = std::move(waiter_entry),
             lock_manager = this]() {
          waiter->Resume(lock_manager, Status::OK());
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
    const ObjectLockPrefix& object_id, TrackedLockEntry& entry, LockStateMap& lockstates_map) {
  VLOG(4) << "Removing granted lock on object : " << AsString(object_id)
          << " with delta { state : " << LockStateDebugString(entry.state)
          << " ref_count : " << entry.ref_count << " }";
  if (DoUnlockSingleEntry(object_id, entry.locked_batch_entry, entry.state)) {
    DoSignal(&entry.locked_batch_entry);
  }
  if (const auto write_intent_count = shared_manager_ ? LockStateWriteIntentCount(entry.state) : 0;
      write_intent_count) {
    lockstates_map[object_id] += entry.state;
  }
  LOG_IF(DFATAL, entry.locked_batch_entry.ref_count < entry.ref_count)
      << "Local Object lock manager state corrupted.";
  entry.locked_batch_entry.ref_count -= entry.ref_count;
  if (entry.locked_batch_entry.ref_count == 0) {
    LOG_IF(DFATAL, entry.locked_batch_entry.num_waiters)
        << "Local Object lock manager state corrupted. Non zero waiters, but object geing GC'ed";
    LOG_IF(DFATAL, entry.locked_batch_entry.waiters_in_resuming_state.load())
        << "Local Object lock manager state corrupted, non-zero waiters_in_resuming_state";
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
          other->was_a_blocker = TxnBlockedTableLockRequests::kTrue;
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
    std::lock_guard txn_lock(txn_entry->mutex);
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
        txn_entry->was_a_blocker = TxnBlockedTableLockRequests::kTrue;
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
        VLOG_WITH_FUNC(2)
              << AsString(item->object_lock_owner())
              << " waiting on " << AsString(*item->blockers)
              << " for key " << AsString(key);
        item->waiter_registration = waiting_txn_registry_->Create();
        WARN_NOT_OK(
            item->waiter_registration->Register(
                item->txn_id(), -1 /* request id */, std::move(item->blockers),
                item->status_tablet(), std::nullopt /* pg_session_req_version */),
            Format(
                "Failed to register blockers of waiter $0", AsString(item->object_lock_owner())));
      }
      item->blockers = nullptr;
    });
  }
}

void ObjectLockManagerImpl::DumpStatusHtml(std::ostream& out) {
  out << "<table class='table table-striped'>\n";
  out << "<tr><th>Prefix</th><th>LockBatchEntry</th></tr>" << std::endl;
  std::lock_guard l(global_mutex_);
  ConsumePendingSharedLockRequestsUnlocked();
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

void ObjectLockManagerImpl::ConsumePendingSharedLockRequests() {
  std::lock_guard l(global_mutex_);
  ConsumePendingSharedLockRequestsUnlocked();
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
    std::lock_guard txn_lock(txn_entry->mutex);
    const auto& locks =
        locks_map == LocksMapType::kGranted ? txn_entry->granted_locks : txn_entry->waiting_locks;
    for (const auto& [subtxn_id, subtxn_locks] : locks) {
      for (const auto& [object_id, entry] : subtxn_locks) {
        out << "<tr>"
            << "<td>" << Format("{txn: $0 subtxn_id: $1}", txn, subtxn_id) << "</td>"
            << "<td>" << AsString(object_id) << "</td>"
            << "<td>"
                << Format("{ ref_count: $0, lock_state: $1 }",
                          entry.ref_count, LockStateDebugString(entry.state))
            << "</td>"
            << "</tr>\n";
      }
    }
  }
  out << "</table>\n";
}

size_t ObjectLockManagerImpl::TEST_LocksSize(LocksMapType locks_map) {
  std::lock_guard lock(global_mutex_);
  ConsumePendingSharedLockRequestsUnlocked();
  size_t size = 0;
  for (const auto& [txn, txn_entry] : txn_locks_) {
    std::lock_guard txn_lock(txn_entry->mutex);
    const auto& locks =
        locks_map == LocksMapType::kGranted ? txn_entry->granted_locks : txn_entry->waiting_locks;
    for (const auto& [subtxn_id, subtxn_locks] : locks) {
      size += subtxn_locks.size();
    }
  }
  return size;
}

size_t ObjectLockManagerImpl::TEST_GrantedLocksSize() {
  return TEST_LocksSize(LocksMapType::kGranted);
}

size_t ObjectLockManagerImpl::TEST_WaitingLocksSize() {
  return TEST_LocksSize(LocksMapType::kWaiting);
}

LockStateMap ObjectLockManagerImpl::TEST_GetLockStateMapForTxn(const TransactionId& txn) const {
  TrackedTxnLockEntryPtr txn_entry;
  {
    std::lock_guard lock(global_mutex_);
    auto txn_it = txn_locks_.find(txn);
    if (txn_it == txn_locks_.end()) {
      return {};
    }
    txn_entry = txn_it->second;
  }
  std::lock_guard txn_lock(txn_entry->mutex);
  return txn_entry->existing_states;
}

ObjectLockManager::ObjectLockManager(
    ThreadPool* thread_pool, server::RpcServerBase& server, const MetricEntityPtr& metric_entity,
    ObjectLockSharedStateManager* shared_manager)
    : impl_(std::make_unique<ObjectLockManagerImpl>(
          thread_pool, server, metric_entity, shared_manager)) {}

ObjectLockManager::~ObjectLockManager() = default;

void ObjectLockManager::Lock(LockData&& data) {
  impl_->Lock(std::move(data));
}

TxnBlockedTableLockRequests ObjectLockManager::Unlock(const ObjectLockOwner& object_lock_owner) {
  return impl_->Unlock(object_lock_owner);
}

void ObjectLockManager::UnlockObjectsForSession(
    const TransactionId& txn, DetermineKeysToLockResult<ObjectLockManager>&& key_to_unlock) {
  return impl_->UnlockObjectsForSession(txn, std::move(key_to_unlock));
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

void ObjectLockManager::ConsumePendingSharedLockRequests() {
  impl_->ConsumePendingSharedLockRequests();
}

size_t ObjectLockManager::TEST_GrantedLocksSize() {
  return impl_->TEST_GrantedLocksSize();
}

size_t ObjectLockManager::TEST_WaitingLocksSize() {
  return impl_->TEST_WaitingLocksSize();
}

LockStateMap ObjectLockManager::TEST_GetLockStateMapForTxn(const TransactionId& txn) const {
  return impl_->TEST_GetLockStateMapForTxn(txn);
}

}  // namespace yb::docdb
