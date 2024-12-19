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

#include "yb/docdb/shared_lock_manager.h"

#include <array>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "yb/ash/wait_state.h"

#include "yb/docdb/lock_batch.h"
#include "yb/docdb/lock_manager_traits.h"
#include "yb/docdb/lock_util.h"

#include "yb/dockv/intent.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/trace.h"

using std::string;

namespace yb::docdb {

using dockv::IntentTypeSet;

namespace {

// We have 64 bits in LockState and 4 types of intents. So 16 bits is the max number of bits
// that we could reserve for a block of single intent type.
const size_t kIntentTypeBits = 16;
// kFirstIntentTypeMask represents the LockState which, when &'d with another LockState, would
// result in the LockState tracking only the count for intent type represented by the region of bits
// that is the "first" or "least significant", as in furthest to the right.
const LockState kFirstIntentTypeMask = (static_cast<LockState>(1) << kIntentTypeBits) - 1;

bool IntentTypesConflict(dockv::IntentType lhs, dockv::IntentType rhs) {
  auto lhs_value = to_underlying(lhs);
  auto rhs_value = to_underlying(rhs);
  // The rules are the following:
  // 1) At least one intent should be strong for conflict.
  // 2) Read and write conflict only with opposite type.
  return ((lhs_value & dockv::kStrongIntentFlag) || (rhs_value & dockv::kStrongIntentFlag)) &&
         ((lhs_value & dockv::kWriteIntentFlag) != (rhs_value & dockv::kWriteIntentFlag));
}

LockState IntentTypeMask(
    dockv::IntentType intent_type, LockState single_intent_mask = kFirstIntentTypeMask) {
  return single_intent_mask << (to_underlying(intent_type) * kIntentTypeBits);
}

// Generate conflict mask for all possible subsets of intent type set. The i-th index of the
// returned array represents a conflict mask for the i-th possible IntentTypeSet. To determine if a
// given IntentTypeSet i conflicts with the key's existing LockState l, you can do the following:
// bool is_conflicting = kIntentTypeSetConflicts[i.ToUintPtr()] & l != 0;
std::array<LockState, dockv::kIntentTypeSetMapSize> GenerateConflicts() {
  std::array<LockState, dockv::kIntentTypeSetMapSize> result;
  for (size_t idx = 0; idx < dockv::kIntentTypeSetMapSize; ++idx) {
    result[idx] = 0;
    for (auto intent_type : IntentTypeSet(idx)) {
      for (auto other_intent_type : dockv::IntentTypeList()) {
        if (IntentTypesConflict(intent_type, other_intent_type)) {
          result[idx] |= IntentTypeMask(other_intent_type);
        }
      }
    }
  }
  return result;
}

// Generate array of LockState's with one entry for each possible subset of intent type set, where
// each intent type has the provided count.
std::array<LockState, dockv::kIntentTypeSetMapSize> GenerateLockStatesWithCount(uint64_t count) {
  DCHECK_EQ(count & kFirstIntentTypeMask, count);
  std::array<LockState, dockv::kIntentTypeSetMapSize> result;
  for (size_t idx = 0; idx != dockv::kIntentTypeSetMapSize; ++idx) {
    result[idx] = 0;
    for (auto intent_type : IntentTypeSet(idx)) {
      result[idx] |= IntentTypeMask(intent_type, count);
    }
  }
  return result;
}

} // namespace

// The following three arrays are indexed by the integer representation of the IntentTypeSet which
// the value at that index corresponds to. For example, an IntentTypeSet with the 0th and 2nd
// element present would be represented by the number = (2^0 + 2^2) = 5. The fifth index of an
// IntentTypeSetMap stores some value which corresponds to this IntentTypeSet.
// TODO -- clarify the semantics of IntentTypeSetMap by making it a class.
typedef std::array<LockState, dockv::kIntentTypeSetMapSize> IntentTypeSetMap;

// Maps IntentTypeSet to a LockState mask which determines if another LockState will conflict with
// any of the elements present in the IntentTypeSet.
const IntentTypeSetMap kIntentTypeSetConflicts = GenerateConflicts();

// Maps IntentTypeSet to the LockState representing one count for each intent type in the set. Can
// be used to "add one" occurence of an IntentTypeSet to an existing key's LockState.
const IntentTypeSetMap kIntentTypeSetAdd = GenerateLockStatesWithCount(1);

// Maps IntentTypeSet to the LockState representing max count for each intent type in the set. Can
// be used to extract a LockState corresponding to having only that set's elements counts present.
const IntentTypeSetMap kIntentTypeSetMask = GenerateLockStatesWithCount(kFirstIntentTypeMask);

bool IntentTypeSetsConflict(IntentTypeSet lhs, IntentTypeSet rhs) {
  for (auto intent1 : lhs) {
    for (auto intent2 : rhs) {
      if (IntentTypesConflict(intent1, intent2)) {
        return true;
      }
    }
  }
  return false;
}

namespace {

template <typename LockManager>
LockState IntentTypeSetAdd(const LockBatchEntry<LockManager>& lock_entry) {
  return kIntentTypeSetAdd[lock_entry.intent_types.ToUIntPtr()];
}

template <typename LockManager>
LockState IntentTypeSetConflict(const LockBatchEntry<LockManager>& lock_entry) {
  return kIntentTypeSetConflicts[lock_entry.intent_types.ToUIntPtr()];
}

uint16_t GetCountOfIntents(const LockState& num_waiting, dockv::IntentType intent_type) {
  return (num_waiting >> (to_underlying(intent_type) * kIntentTypeBits))
      & kFirstIntentTypeMask;
}

std::string LockStateDebugString(const LockState& state) {
  return Format(
      "{ num_weak_read: $0 num_weak_write: $1 num_strong_read: $2 num_strong_write: $3 }",
      GetCountOfIntents(state, dockv::IntentType::kWeakRead),
      GetCountOfIntents(state, dockv::IntentType::kWeakWrite),
      GetCountOfIntents(state, dockv::IntentType::kStrongRead),
      GetCountOfIntents(state, dockv::IntentType::kStrongWrite));
}

void OutLockTableHeader(std::ostream& out) {
  out << "<tr>"
        << "<th>Lock Owner</th>"
        << "<th>Object Id</th>"
        << "<th>Num Holders</th>"
      << "</tr>" << std::endl;
}

} // namespace

template<>
struct LockManagerInternalTraits<SharedLockManager> {
  using LockTracker = SharedLockManager::Impl;
};

template<>
struct LockManagerInternalTraits<ObjectLockManager> {
  using LockTracker = ObjectLockManager::Impl;
};

template <typename LockManager>
struct LockedBatchEntry {
  using LockTracker = LockManagerInternalTraits<LockManager>::LockTracker;

  explicit LockedBatchEntry(LockTracker* tracker_) : tracker(tracker_) {}

  // Taken only for short duration, with no blocking wait.
  mutable std::mutex mutex;
  std::condition_variable cond_var;

  // Refcounting for garbage collection. Can only be used while the global mutex is locked.
  // Global mutex resides in lock manager and covers this field for all LockBatchEntries.
  size_t ref_count = 0;

  // Number of holders for each type
  std::atomic<LockState> num_holding{0};

  std::atomic<size_t> num_waiters{0};

  // Pointer pointing back to the caller of Lock/Unlock, LockManagerImpl<LockManager> in this case.
  // The tracker instruments the locking activity based on whether tracking is enabled or not. When
  // tracking is disabled, the call is a no-op.
  LockTracker* tracker;

  MUST_USE_RESULT bool Lock(
      const LockBatchEntry<LockManager>& lock_entry, CoarseTimePoint deadline,
      const ObjectLockOwner* object_lock_owner);

  void Unlock(const LockBatchEntry<LockManager>& lock_entry,
              const ObjectLockOwner* object_lock_owner);

  void DoUnlock(
      LockState sub, const dockv::IntentTypeSet* intent_types = nullptr);

  std::string ToString() const {
    return Format("{ ref_count: $0 lock_state: $1 num_waiters: $2 }",
                  ref_count,
                  LockStateDebugString(num_holding.load(std::memory_order_acquire)),
                  num_waiters.load(std::memory_order_acquire));
  }
};

template <typename LockManager>
bool LockedBatchEntry<LockManager>::Lock(
    const LockBatchEntry<LockManager>& lock_entry, CoarseTimePoint deadline,
    const ObjectLockOwner* object_lock_owner) {
  auto& num_holding = this->num_holding;
  auto old_value = num_holding.load(std::memory_order_acquire);
  auto add = IntentTypeSetAdd(lock_entry);
  auto conflicting_lock_state = IntentTypeSetConflict(lock_entry);
  for (;;) {
    // Note: For a read/write trying to acquire the shared in-memory locks at the source tablet,
    // lock.existing_state is always 0. 'existing_state' is only relevant in the context of
    // table/object locks, i.e when LockManager == ObjectLockManager, where a transaction ignores
    // conflicts with itself when requesting locks on an object.
    if (((old_value ^ lock_entry.existing_state) & conflicting_lock_state) == 0) {
      auto new_value = old_value + add;
      if (num_holding.compare_exchange_weak(old_value, new_value, std::memory_order_acq_rel)) {
        tracker->Acquiredlock(lock_entry, object_lock_owner);
        return true;
      }
      continue;
    }
    tracker->WaitingOnLock(lock_entry, object_lock_owner);
    num_waiters.fetch_add(1, std::memory_order_release);
    auto se = ScopeExit([this, &lock_entry, object_lock_owner] {
      tracker->FinishedWaitingOnLock(lock_entry, object_lock_owner);
      num_waiters.fetch_sub(1, std::memory_order_release);
    });
    std::unique_lock<std::mutex> lock(mutex);
    old_value = num_holding.load(std::memory_order_acquire);
    if (((old_value ^ lock_entry.existing_state) & conflicting_lock_state) != 0) {
      DEBUG_ONLY_TEST_SYNC_POINT("LockedBatchEntry<T>::Lock");
      SCOPED_WAIT_STATUS(LockedBatchEntry_Lock);
      if (deadline != CoarseTimePoint::max()) {
        // Note -- even if we wait here, we don't need to be aware for the purposes of deadlock
        // detection since this eventually succeeds (in which case thread gets to queue) or times
        // out (thereby eliminating any possibly untraced deadlock).
        VLOG(4) << "Waiting to acquire lock for entry: " << lock_entry.ToString()
                << " with num_holding: " << old_value << ", num_waiters: " << num_waiters
                << " with deadline: " << deadline.time_since_epoch() << " .";
        if (cond_var.wait_until(lock, deadline) == std::cv_status::timeout) {
          return false;
        }
      } else {
        VLOG(4) << "Waiting to acquire lock for entry: " << lock_entry.ToString()
                << " with num_holding: " << old_value << ", num_waiters: " << num_waiters
                << " without deadline.";
        // TODO(wait-queues): Hitting this branch with wait queues could cause deadlocks if
        // we never reach the wait queue and register the "waiting for" relationship. We should add
        // a DCHECK that wait queues are not enabled in this branch, or remove the branch.
        cond_var.wait(lock);
      }
    }
  }
}

template <typename LockManager>
void LockedBatchEntry<LockManager>::Unlock(
    const LockBatchEntry<LockManager>& lock_entry, const ObjectLockOwner* object_lock_owner) {
  tracker->ReleasedLock(lock_entry, object_lock_owner);

  DoUnlock(IntentTypeSetAdd(lock_entry), &lock_entry.intent_types);
}

template <typename LockManager>
void LockedBatchEntry<LockManager>::DoUnlock(
    LockState sub, const dockv::IntentTypeSet* intent_types) {
  // Have to emulate fetch_sub here, because GCC 5.5 don't have it for int128
  auto old_state = num_holding.load(std::memory_order_acquire);
  LockState new_state;
  for (;;) {
    new_state = old_state - sub;
    if (num_holding.compare_exchange_weak(old_state, new_state, std::memory_order_acq_rel)) {
      break;
    }
  }

  if (!num_waiters.load(std::memory_order_acquire)) {
    return;
  }

  if (intent_types) {
    bool has_zero = false;
    for (auto intent_type : *intent_types) {
      if (!(new_state & IntentTypeMask(intent_type))) {
        has_zero = true;
        break;
      }
    }

    // At least one of counters should become 0 to unblock waiting locks.
    if (!has_zero) {
      return;
    }
  }

  {
    // Lock/unlock mutex as a barrier for Lock.
    // So we don't unlock and notify between check and wait in Lock.
    std::lock_guard lock(mutex);
  }

  YB_PROFILE(cond_var.notify_all());
}

// TrackedLockEntry is used to keep track of the LockState of the session for a given key. Note
// that a session can acquire multiple lock types repeatedly on a key.
//
// In context of object/table locks, when handling release requests by ObjectLockOwner
// (optionally with object id supplied), the LockState value is used to reset the info of the
// corresponding LockedBatchEntry.
template <typename LockManager>
struct TrackedLockEntry {
  TrackedLockEntry(
      const ObjectLockOwner& object_lock_owner_,
      LockManagerTraits<LockManager>::KeyType object_id_,
      LockState state_,
      LockedBatchEntry<LockManager>* locked_batch_entry_)
      : key(object_lock_owner_, object_id_), state(state_),
        locked_batch_entry(locked_batch_entry_) { }

  ObjectLockOwner object_lock_owner() const {
    return key.object_lock_owner;
  }

  VersionedTransaction versioned_txn() const {
    return key.object_lock_owner.versioned_txn;
  }

  std::pair<VersionedTransaction, typename LockManagerTraits<LockManager>::KeyType>
  txn_and_key() const {
    return {key.object_lock_owner.versioned_txn, key.object_id};
  }

  LockManagerTraits<LockManager>::KeyType object_id() const {
    return key.object_id;
  }

  // Key against which this TrackedLockEntry is tracked/stored.
  TrackedLockEntryKey<LockManager> key;
  LockState state;
  // LockedBatchEntry<LockManager> object's memory is managed by LockManagerImpl<LockManager>.
  LockedBatchEntry<LockManager>* locked_batch_entry;
  size_t ref_count = 1;
};

namespace {

template <typename LockManager>
void OutLockTableRow(std::ostream& out, const TrackedLockEntry<LockManager>& lock) {
  out << "<tr>"
        << "<td>" << AsString(lock.key.object_lock_owner) << "</td>"
        << "<td>" << AsString(lock.key.object_id) << "</td>"
        << "<td>" << LockStateDebugString(lock.state) << "</td>"
      << "</tr>";
}

template <typename LockManager>
class LockManagerImpl {
 public:
  using LockTracker = LockManagerInternalTraits<LockManager>::LockTracker;
  using LockedBatchEntryUniquePtr = std::unique_ptr<LockedBatchEntry<LockManager>>;
  using LockedBatchEntryPtr = LockedBatchEntry<LockManager>*;

  MUST_USE_RESULT bool Lock(
      LockBatchEntries<LockManager>& key_to_intent_type, CoarseTimePoint deadline,
      const ObjectLockOwner* object_lock_owner = nullptr);

  void DumpStatusHtml(std::ostream& out) EXCLUDES(global_mutex_);

 protected:
  ~LockManagerImpl() = default;

  using KeyType = LockManagerTraits<LockManager>::KeyType;

  using LockEntryMap = std::unordered_map<KeyType, LockedBatchEntryPtr, boost::hash<KeyType>>;

  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  void Reserve(LockBatchEntries<LockManager>& batch,
               const ObjectLockOwner* object_lock_owner) EXCLUDES(global_mutex_);

  // Update refcounts and maybe collect garbage.
  void Cleanup(const LockBatchEntries<LockManager>& key_to_intent_type) EXCLUDES(global_mutex_);

  // The global mutex should be taken only for very short duration, with no blocking wait.
  mutable std::mutex global_mutex_;

  LockEntryMap locks_ GUARDED_BY(global_mutex_);
  // Cache of lock entries, to avoid allocation/deallocation of heavy LockedBatchEntry.
  std::vector<LockedBatchEntryUniquePtr> lock_entries_ GUARDED_BY(global_mutex_);
  std::vector<LockedBatchEntryPtr> free_lock_entries_ GUARDED_BY(global_mutex_);
};

template <typename LockManager>
bool LockManagerImpl<LockManager>::Lock(
    LockBatchEntries<LockManager>& key_to_intent_type, CoarseTimePoint deadline,
    const ObjectLockOwner* object_lock_owner) {
  TRACE("Locking a batch of $0 keys", key_to_intent_type.size());
  Reserve(key_to_intent_type, object_lock_owner);
  for (auto it = key_to_intent_type.begin(); it != key_to_intent_type.end(); ++it) {
    const auto& intent_types = it->intent_types;
    VLOG(4) << "Locking " << AsString(intent_types) << ": "
            << AsString(it->key);
    if (!it->locked->Lock(*it, deadline, object_lock_owner)) {
      while (it != key_to_intent_type.begin()) {
        --it;
        it->locked->Unlock(*it, object_lock_owner);
      }
      Cleanup(key_to_intent_type);
      return false;
    }
  }
  TRACE("Acquired a lock batch of $0 keys", key_to_intent_type.size());

  return true;
}

template <typename LockManager>
void LockManagerImpl<LockManager>::DumpStatusHtml(std::ostream& out) {
  out << "<table class='table table-striped'>\n";
  out << "<tr><th>Prefix</th><th>LockBatchEntry</th></tr>" << std::endl;
  std::lock_guard l(global_mutex_);
  for (const auto& [prefix, entry] : locks_) {
    auto key_str = AsString(prefix);
    out << "<tr>"
          << "<td>" << (!key_str.empty() ? key_str : "[empty]") << "</td>"
          << "<td>" << entry->ToString() << "</td>"
        << "</tr>";
  }
  out << "</table>\n";

  static_cast<LockTracker*>(this)->DumpStoredObjectLocksUnlocked(out);
}

template <typename LockManager>
void LockManagerImpl<LockManager>::Reserve(
    LockBatchEntries<LockManager>& key_to_intent_type,
    const ObjectLockOwner* object_lock_owner) {
  std::lock_guard lock(global_mutex_);
  for (auto& key_and_intent_type : key_to_intent_type) {
    auto& value = locks_[key_and_intent_type.key];
    if (!value) {
      if (!free_lock_entries_.empty()) {
        value = free_lock_entries_.back();
        free_lock_entries_.pop_back();
      } else {
        lock_entries_.emplace_back(std::make_unique<LockedBatchEntry<LockManager>>(
            static_cast<LockTracker*>(this)));
        value = lock_entries_.back().get();
      }
    }
    value->ref_count++;
    key_and_intent_type.locked = std::to_address(value);
    // In case of object locking, set the 'existing_state' field of the LockBatchEntry so as to
    // ignore conflicts with self.
    if (object_lock_owner) {
      key_and_intent_type.existing_state =
          static_cast<LockTracker*>(this)->GetLockStateForKeyUnlocked(
              *object_lock_owner, key_and_intent_type.key);
    }
  }
}

template <typename LockManager>
void LockManagerImpl<LockManager>::Cleanup(
    const LockBatchEntries<LockManager>& key_to_intent_type) {
  std::lock_guard lock(global_mutex_);
  for (const auto& item : key_to_intent_type) {
    if (--item.locked->ref_count == 0) {
      locks_.erase(item.key);
      free_lock_entries_.push_back(item.locked);
    }
  }
}

} // namespace

class SharedLockManager::Impl : public LockManagerImpl<SharedLockManager> {
 public:
  ~Impl() {
    std::lock_guard lock(global_mutex_);
    LOG_IF(DFATAL, !locks_.empty()) << "Locks not empty in dtor: " << AsString(locks_);
  }

  void Unlock(const LockBatchEntries<SharedLockManager>& key_to_intent_type);

  void Acquiredlock(const LockBatchEntry<SharedLockManager>& lock_entry,
                    const ObjectLockOwner* object_lock_owner) {}

  void ReleasedLock(const LockBatchEntry<SharedLockManager>& lock_entry,
                    const ObjectLockOwner* object_lock_owner) {}

  void WaitingOnLock(const LockBatchEntry<SharedLockManager>& lock_entry,
                     const ObjectLockOwner* object_lock_owner) {}

  void FinishedWaitingOnLock(const LockBatchEntry<SharedLockManager>&,
                             const ObjectLockOwner* object_lock_owner) {}

  LockState GetLockStateForKeyUnlocked(
      const ObjectLockOwner& object_lock_owner, const RefCntPrefix& key) {
    return 0;
  }

  void DumpStoredObjectLocksUnlocked(std::ostream& out) {}
};

void SharedLockManager::Impl::Unlock(
    const LockBatchEntries<SharedLockManager>& key_to_intent_type) {
  TRACE("Unlocking a batch of $0 keys", key_to_intent_type.size());

  for (const auto& key_and_intent_type : boost::adaptors::reverse(key_to_intent_type)) {
    key_and_intent_type.locked->Unlock(key_and_intent_type, nullptr);
  }
  Cleanup(key_to_intent_type);
}

class ObjectLockManager::Impl : public LockManagerImpl<ObjectLockManager> {
 public:
  void Unlock(const std::vector<TrackedLockEntryKey<ObjectLockManager>>& lock_entry_keys);

  void Unlock(const ObjectLockOwner& object_lock_owner);

  void Acquiredlock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                    const ObjectLockOwner* object_lock_owner) {
    Acquiredlock(lock_entry, object_lock_owner, &granted_locks_);
  }

  void ReleasedLock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                    const ObjectLockOwner* object_lock_owner) {
    ReleasedLock(lock_entry, object_lock_owner, &granted_locks_);
  }

  void WaitingOnLock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                     const ObjectLockOwner* object_lock_owner) {
    Acquiredlock(lock_entry, object_lock_owner, &waiting_locks_);
  }

  void FinishedWaitingOnLock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                             const ObjectLockOwner* object_lock_owner) {
    ReleasedLock(lock_entry, object_lock_owner, &waiting_locks_);
  }

  LockState GetLockStateForKeyUnlocked(
      const ObjectLockOwner& object_lock_owner, const ObjectLockPrefix& key)
      REQUIRES(global_mutex_);

  void DumpStoredObjectLocksUnlocked(std::ostream& out) REQUIRES(global_mutex_);

  size_t TEST_GrantedLocksSize() const;
  size_t TEST_WaitingLocksSize() const;

 private:
  struct ObjectOwnerTag;
  struct ObjectOwnerPrefixTag;
  struct ObjectIdTag;
  struct OwnerPrefixAndKeyTag;

  // A container for storing acquired/waiting in memory locks with the following properties
  // - hashed on unique TrackedLockEntry<T>::key
  // - hashed on non unique ObjectLockOwner (txn, txn version, subtxn id) to allow fast access to
  //   all locks of a statement
  // - hashed on non unique VersionedTransaction (txn, txn version) to allow fast access to all
  //   locks of a docdb transaction
  // - hashed on non unique key to allow fast access to all txns holding locks on the given key
  // - hashed on non unique (txn, txn version, key) to get the current LockState of the txn for
  //   a given key. This is useful when computing the LockState value to ignore conflicts with self.
  using ObjectLocksMap = boost::multi_index_container<TrackedLockEntry<ObjectLockManager>,
      boost::multi_index::indexed_by <
          boost::multi_index::hashed_unique <
              boost::multi_index::member<
                  TrackedLockEntry<ObjectLockManager>, TrackedLockEntryKey<ObjectLockManager>,
                  &TrackedLockEntry<ObjectLockManager>::key
              >
          >,
          boost::multi_index::hashed_non_unique <
              boost::multi_index::tag<ObjectOwnerTag>,
              boost::multi_index::const_mem_fun<
                  TrackedLockEntry<ObjectLockManager>, ObjectLockOwner,
                  &TrackedLockEntry<ObjectLockManager>::object_lock_owner>
          >,
          boost::multi_index::hashed_non_unique <
              boost::multi_index::tag<ObjectOwnerPrefixTag>,
              boost::multi_index::const_mem_fun<
                  TrackedLockEntry<ObjectLockManager>, VersionedTransaction,
                  &TrackedLockEntry<ObjectLockManager>::versioned_txn>
          >,
          boost::multi_index::hashed_non_unique <
              boost::multi_index::tag<ObjectIdTag>,
              boost::multi_index::const_mem_fun<
                  TrackedLockEntry<ObjectLockManager>, ObjectLockPrefix,
                  &TrackedLockEntry<ObjectLockManager>::object_id>
          >,
          boost::multi_index::hashed_non_unique <
              boost::multi_index::tag<OwnerPrefixAndKeyTag>,
              boost::multi_index::const_mem_fun<
                  TrackedLockEntry<ObjectLockManager>,
                  std::pair<VersionedTransaction, ObjectLockPrefix>,
                  &TrackedLockEntry<ObjectLockManager>::txn_and_key>
          >
      >
  >;

  void DoReleaseTrackedLock(const TrackedLockEntry<ObjectLockManager>& entry)
      REQUIRES(global_mutex_);

  void Acquiredlock(
      const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner* object_lock_owner,
      ObjectLocksMap* container) EXCLUDES(global_mutex_);

  void ReleasedLock(
      const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner* object_lock_owner,
      ObjectLocksMap* container) EXCLUDES(global_mutex_);

  // Lock activity is tracked only when the requests have ObjectLockOwner set.
  ObjectLocksMap granted_locks_ GUARDED_BY(global_mutex_);
  ObjectLocksMap waiting_locks_ GUARDED_BY(global_mutex_);
};

void ObjectLockManager::Impl::Unlock(
    const std::vector<TrackedLockEntryKey<ObjectLockManager>>& lock_entry_keys) {
  TRACE("Unlocking a batch of $0 object locks", lock_entry_keys.size());

  std::lock_guard lock(global_mutex_);
  for (const auto& key : lock_entry_keys) {
    auto it = granted_locks_.find(key);
    if (it == granted_locks_.end()) {
      // This is expected in case of object/table locking, since while releasing a lock the
      // previously acquired lock mode is not specified. And since the key is formed based
      // on the lock type being acquired, release attempts freeing locks on all key types for
      // the given object and session.
      continue;
    }
    DoReleaseTrackedLock(*it);
    granted_locks_.erase(it);
  }
}

void ObjectLockManager::Impl::Unlock(const ObjectLockOwner& object_lock_owner) {
  TRACE("Unlocking all keys for owner $0", AsString(object_lock_owner));

  std::lock_guard lock(global_mutex_);
  if (object_lock_owner.subtxn_id) {
    // Release locks corresponding to a particular subtxn. Could be invoked when a subtxn is
    // aborted/rolled back.
    auto& lock_owner_index = granted_locks_.template get<ObjectOwnerTag>();
    auto tracked_locks = boost::make_iterator_range(
        lock_owner_index.equal_range(object_lock_owner));
    for (auto& entry : tracked_locks) {
      DoReleaseTrackedLock(entry);
    }
    lock_owner_index.erase(tracked_locks.begin(), tracked_locks.end());
    return;
  }
  // Release all locks tagged against <txn_id, txn_version>, may be on commit/abort.
  auto& owner_prefix_index = granted_locks_.template get<ObjectOwnerPrefixTag>();
  auto tracked_locks = boost::make_iterator_range(
      owner_prefix_index.equal_range(object_lock_owner.versioned_txn));
  for (auto& entry : tracked_locks) {
    DoReleaseTrackedLock(entry);
  }
  owner_prefix_index.erase(tracked_locks.begin(), tracked_locks.end());
}

LockState ObjectLockManager::Impl::GetLockStateForKeyUnlocked(
    const ObjectLockOwner& object_lock_owner, const ObjectLockPrefix& key) {
  auto& owner_prefix_and_key_index = granted_locks_.template get<OwnerPrefixAndKeyTag>();
  auto tracked_locks = boost::make_iterator_range(owner_prefix_and_key_index.equal_range(
      std::pair<VersionedTransaction, ObjectLockPrefix>(
          object_lock_owner.versioned_txn, key
      )
  ));
  LockState existing_state = 0;
  for (auto& entry : tracked_locks) {
    existing_state += entry.state;
  }
  return existing_state;
}

void ObjectLockManager::Impl::DumpStoredObjectLocksUnlocked(std::ostream& out) {
  out << "<table class='table table-striped'>\n";
  out << "<caption>Granted object locks</caption>";
  OutLockTableHeader(out);
  for (const auto& lock : granted_locks_) {
    OutLockTableRow(out, lock);
  }
  out << "</table>\n";

  out << "<table class='table table-striped'>\n";
  out << "<caption>Waiting object locks</caption>";
  OutLockTableHeader(out);
  for (const auto& lock : waiting_locks_) {
    OutLockTableRow(out, lock);
  }
  out << "</table>\n";
}

size_t ObjectLockManager::Impl::TEST_GrantedLocksSize() const {
  std::lock_guard lock(global_mutex_);
  return granted_locks_.size();
}

size_t ObjectLockManager::Impl::TEST_WaitingLocksSize() const {
  std::lock_guard lock(global_mutex_);
  return waiting_locks_.size();
}

void ObjectLockManager::Impl::DoReleaseTrackedLock(
    const TrackedLockEntry<ObjectLockManager>& entry) {
  // We don't pass an intents set to unlock so as to trigger notify on every lock release. It is
  // necessary as two (or more) transactions could be holding a read lock and one of the txns
  // could request a conflicting lock mode. And since conflicts with self should be ignored, we
  // need to signal the cond variable on every release, else the lock release call from the other
  // transaction wouldn't unblock the waiter.
  entry.locked_batch_entry->DoUnlock(entry.state);
  entry.locked_batch_entry->ref_count -= entry.ref_count;
  if (entry.locked_batch_entry->ref_count == 0) {
    locks_.erase(entry.key.object_id);
    free_lock_entries_.push_back(entry.locked_batch_entry);
  }
}

void ObjectLockManager::Impl::Acquiredlock(
    const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner* object_lock_owner,
    ObjectLocksMap* container) {
  if (!object_lock_owner) {
    LOG_WITH_FUNC(DFATAL) << "Unexpected null object_lock_owner pointer. "
                          << "Cannot track/store object locks.";
    return;
  }
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", object_lock_owner: " << AsString(*object_lock_owner);
  auto delta = IntentTypeSetAdd(lock_entry);
  TrackedLockEntry record(*object_lock_owner, lock_entry.key, delta, lock_entry.locked);

  std::lock_guard lock(global_mutex_);
  auto [it, did_insert] = container->emplace(record);
  if (did_insert) {
    return;
  }
  container->modify(it, [&record](TrackedLockEntry<ObjectLockManager>& entry) {
    entry.state += record.state;
    ++entry.ref_count;
  });
}

void ObjectLockManager::Impl::ReleasedLock(
    const LockBatchEntry<ObjectLockManager>& lock_entry, const ObjectLockOwner* object_lock_owner,
    ObjectLocksMap* container) {
  if (!object_lock_owner) {
    LOG_WITH_FUNC(DFATAL) << "Unexpected null object_lock_owner pointer. "
                          << "Cannot track/store object locks.";
    return;
  }
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", object_lock_owner: " << AsString(*object_lock_owner);
  auto delta = IntentTypeSetAdd(lock_entry);
  TrackedLockEntryKey<ObjectLockManager> lock_entry_key {*object_lock_owner, lock_entry.key};

  std::lock_guard lock(global_mutex_);
  auto it = container->find(lock_entry_key);
  container->modify(it, [delta](TrackedLockEntry<ObjectLockManager>& entry) {
    entry.state -= delta;
    --entry.ref_count;
  });
  if (it->state == 0) {
    DCHECK_EQ(it->ref_count, 0)
        << "TrackedLockEntry::ref_count for key " << it->key.ToString() << " expected to have "
        << "been 0 here. This could lead to faulty tracking of acquired/waiting object locks "
        << "and also issues with garbage collection of free lock entries in ObjectLockManager.";
    container->erase(it);
  }
}

SharedLockManager::SharedLockManager()
    : impl_(new Impl) {}

SharedLockManager::~SharedLockManager() {}

bool SharedLockManager::Lock(
    LockBatchEntries<SharedLockManager>& key_to_intent_type, CoarseTimePoint deadline) {
  return impl_->Lock(key_to_intent_type, deadline);
}

void SharedLockManager::Unlock(const LockBatchEntries<SharedLockManager>& key_to_intent_type) {
  impl_->Unlock(key_to_intent_type);
}

void SharedLockManager::DumpStatusHtml(std::ostream& out) {
  impl_->DumpStatusHtml(out);
}

ObjectLockManager::ObjectLockManager(): impl_(std::make_unique<Impl>()) { }

ObjectLockManager::~ObjectLockManager() {}

bool ObjectLockManager::Lock(
    const ObjectLockOwner& object_lock_owner,
    LockBatchEntries<ObjectLockManager>& key_to_intent_type, CoarseTimePoint deadline) {
  return impl_->Lock(key_to_intent_type, deadline, &object_lock_owner);
}

void ObjectLockManager::Unlock(
    const std::vector<TrackedLockEntryKey<ObjectLockManager>>& lock_entry_keys) {
  impl_->Unlock(lock_entry_keys);
}

void ObjectLockManager::Unlock(const ObjectLockOwner& object_lock_owner) {
  impl_->Unlock(object_lock_owner);
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

}  // namespace yb::docdb
