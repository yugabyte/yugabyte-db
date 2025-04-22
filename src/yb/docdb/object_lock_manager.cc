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
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/ash/wait_state.h"

#include "yb/docdb/lock_batch.h"
#include "yb/docdb/lock_util.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug/long_operation_tracker.h"

#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/lw_function.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

using namespace std::literals;
namespace yb::docdb {

using dockv::IntentTypeSet;

struct ObjectLockedBatchEntry;

namespace {

YB_DEFINE_ENUM(LocksMapType, (kGranted)(kWaiting));

using OwnerAsString = LWFunction<std::string(void)>;

// TrackedLockEntry is used to keep track of the LockState of the transaction for a given key. Note
// that a session can acquire multiple lock types repeatedly on a key.
//
// In context of object/table locks, when handling release requests by ObjectLockOwner
// (optionally with object id supplied), the LockState value is used to reset the info of the
// corresponding ObjectLockedBatchEntry.
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

// TrackedTransactionLockEntry contains the TrackedLockEntrys coresponding to a transaction.
struct TrackedTransactionLockEntry {
  using LockEntryMap =
      std::unordered_map<SubTransactionId,
                         std::unordered_map<ObjectLockPrefix, TrackedLockEntry>>;

  TrackedTransactionLockEntry() = default;

  TrackedTransactionLockEntry(TrackedTransactionLockEntry&& other)
      : granted_locks(std::move(other.granted_locks)),
        waiting_locks(std::move(other.waiting_locks)) {}

  mutable std::mutex mutex;
  LockEntryMap granted_locks GUARDED_BY(mutex);
  LockEntryMap waiting_locks GUARDED_BY(mutex);
};

} // namespace

struct ObjectLockedBatchEntry {
  // Taken only for short duration, with no blocking wait.
  mutable std::mutex mutex;
  std::condition_variable cond_var;

  // Refcounting for garbage collection. Can only be used while the global mutex is locked.
  // Global mutex resides in lock manager and covers this field for all LockBatchEntries.
  size_t ref_count = 0;

  // Number of holders for each type
  std::atomic<LockState> num_holding{0};

  std::atomic<size_t> num_waiters{0};

  std::string ToString() const {
    return Format("{ ref_count: $0 lock_state: $1 num_waiters: $2 }",
                  ref_count,
                  LockStateDebugString(num_holding.load(std::memory_order_acquire)),
                  num_waiters.load(std::memory_order_acquire));
  }
};

class ObjectLockManagerImpl {
 public:
  MUST_USE_RESULT bool Lock(
      LockBatchEntries<ObjectLockManager>& key_to_intent_type, CoarseTimePoint deadline,
      const ObjectLockOwner& object_lock_owner);

  void Unlock(const std::vector<TrackedLockEntryKey>& lock_entry_keys);

  void Unlock(const ObjectLockOwner& object_lock_owner);

  void DumpStatusHtml(std::ostream& out) EXCLUDES(global_mutex_);

  void AcquiredLock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                    TrackedTransactionLockEntry& txn,
                    SubTransactionId subtxn_id,
                    const OwnerAsString& owner_as_string) {
    AcquiredLock(lock_entry, txn, subtxn_id, owner_as_string, LocksMapType::kGranted);
  }

  void ReleasedLock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                    TrackedTransactionLockEntry& txn,
                    SubTransactionId subtxn_id,
                    const OwnerAsString& owner_as_string) {
    ReleasedLock(lock_entry, txn, subtxn_id, owner_as_string, LocksMapType::kGranted);
  }

  void WaitingOnLock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                     TrackedTransactionLockEntry& txn,
                     SubTransactionId subtxn_id,
                     const OwnerAsString& owner_as_string) {
    AcquiredLock(lock_entry, txn, subtxn_id, owner_as_string, LocksMapType::kWaiting);
  }

  void FinishedWaitingOnLock(const LockBatchEntry<ObjectLockManager>& lock_entry,
                             TrackedTransactionLockEntry& txn,
                             SubTransactionId subtxn_id,
                             const OwnerAsString& owner_as_string) {
    ReleasedLock(lock_entry, txn, subtxn_id, owner_as_string, LocksMapType::kWaiting);
  }

  size_t TEST_LocksSize(LocksMapType locks_map) const;
  size_t TEST_GrantedLocksSize() const;
  size_t TEST_WaitingLocksSize() const;

 private:
  MUST_USE_RESULT bool LockSingleEntry(
      const LockBatchEntry<ObjectLockManager>& lock_entry, CoarseTimePoint deadline,
      TrackedTransactionLockEntry& transaction_entry, SubTransactionId subtxn_id,
      const OwnerAsString& owner_as_string) EXCLUDES(global_mutex_);

  MUST_USE_RESULT bool DoLockSingleEntry(
      const LockBatchEntry<ObjectLockManager>& lock_entry, CoarseTimePoint deadline,
      TrackedTransactionLockEntry& transaction_entry, SubTransactionId subtxn_id,
      const OwnerAsString& owner_as_string);

  void DoUnlockAll(TrackedTransactionLockEntry& txn_entry) REQUIRES(global_mutex_);

  void UnlockSingleEntry(
      const LockBatchEntry<ObjectLockManager>& lock_entry,
      TrackedTransactionLockEntry& transaction_entry, SubTransactionId subtxn_id,
      const OwnerAsString& owner_as_string) EXCLUDES(global_mutex_);

  void DoUnlockSingleEntry(
      ObjectLockedBatchEntry& entry, const ObjectLockPrefix& object_id, LockState sub);

  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  TrackedTransactionLockEntry& Reserve(
      std::span<LockBatchEntry<ObjectLockManager>> batch,
      const ObjectLockOwner& object_lock_owner) EXCLUDES(global_mutex_);

  void DoReserve(
      std::span<LockBatchEntry<ObjectLockManager>> batch,
      TrackedTransactionLockEntry& transaction_entry) REQUIRES(global_mutex_);

  // Update refcounts and maybe collect garbage.
  void Cleanup(const LockBatchEntries<ObjectLockManager>& key_to_intent_type)
      EXCLUDES(global_mutex_);

  LockState GetLockStateForKey(const TrackedTransactionLockEntry& txn, const ObjectLockPrefix& key);

  void DumpStoredObjectLocksMap(
      std::ostream& out, std::string_view caption, LocksMapType locks_map) REQUIRES(global_mutex_);

  void DoReleaseTrackedLock(const ObjectLockPrefix& object_id, const TrackedLockEntry& entry)
      REQUIRES(global_mutex_);

  void AcquiredLock(
      const LockBatchEntry<ObjectLockManager>& lock_entry, TrackedTransactionLockEntry& txn,
      SubTransactionId subtxn_id, const OwnerAsString& owner_as_string,
      LocksMapType locks_map);

  void ReleasedLock(
      const LockBatchEntry<ObjectLockManager>& lock_entry, TrackedTransactionLockEntry& txn,
      SubTransactionId subtxn_id, const OwnerAsString& owner_as_string,
      LocksMapType locks_map);

  // The global mutex should be taken only for very short duration, with no blocking wait.
  mutable std::mutex global_mutex_;

  std::unordered_map<ObjectLockPrefix, ObjectLockedBatchEntry*> locks_ GUARDED_BY(global_mutex_);
  // Cache of lock entries, to avoid allocation/deallocation of heavy ObjectLockedBatchEntry.
  std::vector<std::unique_ptr<ObjectLockedBatchEntry>> lock_entries_ GUARDED_BY(global_mutex_);
  std::vector<ObjectLockedBatchEntry*> free_lock_entries_ GUARDED_BY(global_mutex_);

  // Lock activity is tracked only when the requests have ObjectLockOwner set. This maps
  // txn => subtxn => object id => entry.
  std::unordered_map<TransactionId, TrackedTransactionLockEntry>
      txn_locks_ GUARDED_BY(global_mutex_);
};

bool ObjectLockManagerImpl::Lock(
    LockBatchEntries<ObjectLockManager>& key_to_intent_type, CoarseTimePoint deadline,
    const ObjectLockOwner& object_lock_owner) {
  TRACE("Locking a batch of $0 keys", key_to_intent_type.size());
  auto& transaction_entry = Reserve(key_to_intent_type, object_lock_owner);
  auto owner_as_string = [&object_lock_owner] {
    return AsString(object_lock_owner);
  };
  for (auto it = key_to_intent_type.begin(); it != key_to_intent_type.end(); ++it) {
    const auto& intent_types = it->intent_types;
    VLOG(4) << "Locking " << AsString(intent_types) << ": "
            << AsString(it->key);
    if (!LockSingleEntry(
        *it, deadline, transaction_entry, object_lock_owner.subtxn_id,
        make_lw_function(owner_as_string))) {
      while (it != key_to_intent_type.begin()) {
        --it;
        UnlockSingleEntry(
            *it, transaction_entry, object_lock_owner.subtxn_id, make_lw_function(owner_as_string));
      }
      Cleanup(key_to_intent_type);
      return false;
    }
  }
  TRACE("Acquired a lock batch of $0 keys", key_to_intent_type.size());
  return true;
}

bool ObjectLockManagerImpl::LockSingleEntry(
    const LockBatchEntry<ObjectLockManager>& lock_entry, CoarseTimePoint deadline,
    TrackedTransactionLockEntry& transaction_entry, SubTransactionId subtxn_id,
    const OwnerAsString& owner_as_string) {
  TRACE_FUNC();
  bool success = DoLockSingleEntry(
      lock_entry, deadline, transaction_entry, subtxn_id, owner_as_string);
  return success;
}

bool ObjectLockManagerImpl::DoLockSingleEntry(
    const LockBatchEntry<ObjectLockManager>& lock_entry, CoarseTimePoint deadline,
    TrackedTransactionLockEntry& transaction_entry, SubTransactionId subtxn_id,
    const OwnerAsString& owner_as_string) {
  TRACE_FUNC();
  auto& entry = *lock_entry.locked;
  auto old_value = entry.num_holding.load(std::memory_order_acquire);
  auto add = IntentTypeSetAdd(lock_entry.intent_types);
  auto conflicting_lock_state = IntentTypeSetConflict(lock_entry.intent_types);
  for (;;) {
    if (((old_value ^ lock_entry.existing_state) & conflicting_lock_state) == 0) {
      auto new_value = old_value + add;
      if (entry.num_holding.compare_exchange_weak(
          old_value, new_value, std::memory_order_acq_rel)) {
        AcquiredLock(lock_entry, transaction_entry, subtxn_id, owner_as_string);
        return true;
      }
      continue;
    }
    WaitingOnLock(lock_entry, transaction_entry, subtxn_id, owner_as_string);
    entry.num_waiters.fetch_add(1, std::memory_order_release);
    auto se = ScopeExit([&] {
      FinishedWaitingOnLock(lock_entry, transaction_entry, subtxn_id, owner_as_string);
      entry.num_waiters.fetch_sub(1, std::memory_order_release);
    });
    LongOperationTracker long_operation_tracker("Waiting for object lock", 1s);
    std::unique_lock lock(entry.mutex);
    old_value = entry.num_holding.load(std::memory_order_acquire);
    if (((old_value ^ lock_entry.existing_state) & conflicting_lock_state) != 0) {
      DEBUG_ONLY_TEST_SYNC_POINT("ObjectLockedBatchEntry::Lock");
      SCOPED_WAIT_STATUS(ConflictResolution_WaitOnConflictingTxns);
      if (deadline != CoarseTimePoint::max()) {
        // Note -- even if we wait here, we don't need to be aware for the purposes of deadlock
        // detection since this eventually succeeds (in which case thread gets to queue) or times
        // out (thereby eliminating any possibly untraced deadlock).
        VLOG(4) << "Waiting to acquire lock for entry: " << lock_entry.ToString()
                << " with num_holding: " << old_value << ", num_waiters: " << entry.num_waiters
                << " with deadline: " << AsString(deadline.time_since_epoch()) << " .";
        if (entry.cond_var.wait_until(lock, deadline) == std::cv_status::timeout) {
          return false;
        }
      } else {
        VLOG(4) << "Waiting to acquire lock for entry: " << lock_entry.ToString()
                << " with num_holding: " << old_value << ", num_waiters: " << entry.num_waiters
                << " without deadline.";
        // TODO(wait-queues): Hitting this branch with wait queues could cause deadlocks if
        // we never reach the wait queue and register the "waiting for" relationship. We should add
        // a DCHECK that wait queues are not enabled in this branch, or remove the branch.
        entry.cond_var.wait(lock);
      }
    }
  }
}

void ObjectLockManagerImpl::Unlock(const std::vector<TrackedLockEntryKey>& lock_entry_keys) {
  TRACE("Unlocking a batch of $0 object locks", lock_entry_keys.size());

  std::lock_guard lock(global_mutex_);
  for (const auto& key : lock_entry_keys) {
    const auto& txn_id = key.object_lock_owner.txn_id;
    auto txn_it = txn_locks_.find(txn_id);
    if (txn_it == txn_locks_.end()) {
      // This is expected in case of object/table locking, since while releasing a lock the
      // previously acquired lock mode is not specified. And since the key is formed based
      // on the lock type being acquired, release attempts freeing locks on all key types for
      // the given object and session.
      continue;
    }
    auto& txn = txn_it->second;
    std::lock_guard txn_lock(txn.mutex);

    auto subtxn_itr = txn.granted_locks.find(key.object_lock_owner.subtxn_id);
    if (subtxn_itr == txn.granted_locks.end()) {
      continue;
    }
    auto& subtxn_locks = subtxn_itr->second;
    auto it = subtxn_locks.find(key.object_id);
    if (it == subtxn_locks.end()) {
      continue;
    }
    DoReleaseTrackedLock(key.object_id, it->second);
    subtxn_locks.erase(it);
  }
}

void ObjectLockManagerImpl::Unlock(const ObjectLockOwner& object_lock_owner) {
  TRACE("Unlocking all keys for owner $0", AsString(object_lock_owner));

  std::lock_guard lock(global_mutex_);
  auto txn_itr = txn_locks_.find(object_lock_owner.txn_id);
  if (txn_itr == txn_locks_.end()) {
    return;
  }
  TrackedTransactionLockEntry& txn_entry = txn_itr->second;

  if (!object_lock_owner.subtxn_id) {
    DoUnlockAll(txn_entry);
    txn_locks_.erase(txn_itr);
    return;
  }

  std::lock_guard txn_lock(txn_entry.mutex);

  // Release locks corresponding to a particular subtxn. Could be invoked when a subtxn is
  // aborted/rolled back.
  auto subtxn_itr = txn_entry.granted_locks.find(object_lock_owner.subtxn_id);
  if (subtxn_itr == txn_entry.granted_locks.end()) {
    return;
  }
  const auto& subtxn_locks = subtxn_itr->second;
  for (const auto& [object_id, entry] : subtxn_locks) {
    DoReleaseTrackedLock(object_id, entry);
  }
  txn_entry.granted_locks.erase(subtxn_itr);
}

void ObjectLockManagerImpl::DoUnlockAll(TrackedTransactionLockEntry& txn_entry) {
  std::lock_guard txn_lock(txn_entry.mutex);
  // Release all locks tied to txn_id may be on commit/abort.
  for (const auto& [subtxn_id, subtxn_locks] : txn_entry.granted_locks) {
    for (const auto& [object_id, entry] : subtxn_locks) {
      DoReleaseTrackedLock(object_id, entry);
    }
  }
}

void ObjectLockManagerImpl::UnlockSingleEntry(
    const LockBatchEntry<ObjectLockManager>& lock_entry,
    TrackedTransactionLockEntry& transaction_entry, SubTransactionId subtxn_id,
    const OwnerAsString& owner_as_string) {
  TRACE_FUNC();
  auto delta = IntentTypeSetAdd(lock_entry.intent_types);
  ReleasedLock(lock_entry, transaction_entry, subtxn_id, owner_as_string);
  DoUnlockSingleEntry(*lock_entry.locked, lock_entry.key, delta);
}

void ObjectLockManagerImpl::DoUnlockSingleEntry(
    ObjectLockedBatchEntry& entry, const ObjectLockPrefix& object_id, LockState sub) {
  entry.num_holding.fetch_sub(sub, std::memory_order_acq_rel);

  if (!entry.num_waiters.load(std::memory_order_acquire)) {
    return;
  }

  {
    // Lock/unlock mutex as a barrier for Lock.
    // So we don't unlock and notify between check and wait in Lock.
    std::lock_guard lock(entry.mutex);
  }

  YB_PROFILE(entry.cond_var.notify_all());
}

TrackedTransactionLockEntry& ObjectLockManagerImpl::Reserve(
    std::span<LockBatchEntry<ObjectLockManager>> key_to_intent_type,
    const ObjectLockOwner& object_lock_owner) {
  std::lock_guard lock(global_mutex_);
  auto& transaction_entry = txn_locks_[object_lock_owner.txn_id];
  DoReserve(key_to_intent_type, transaction_entry);
  return transaction_entry;
}

void ObjectLockManagerImpl::DoReserve(
    std::span<LockBatchEntry<ObjectLockManager>> key_to_intent_type,
    TrackedTransactionLockEntry& transaction_entry) {
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
    // Ignore conflicts with self.
    key_and_intent_type.existing_state =
        GetLockStateForKey(transaction_entry, key_and_intent_type.key);
  }
}

void ObjectLockManagerImpl::Cleanup(const LockBatchEntries<ObjectLockManager>& key_to_intent_type) {
  std::lock_guard lock(global_mutex_);
  for (const auto& item : key_to_intent_type) {
    if (--item.locked->ref_count == 0) {
      locks_.erase(item.key);
      free_lock_entries_.push_back(item.locked);
    }
  }
}

LockState ObjectLockManagerImpl::GetLockStateForKey(
    const TrackedTransactionLockEntry& txn, const ObjectLockPrefix& key) {
  std::lock_guard txn_lock(txn.mutex);
  LockState existing_state = 0;
  for (const auto& [subtxn_id, subtxn_locks] : txn.granted_locks) {
    auto itr = subtxn_locks.find(key);
    if (itr != subtxn_locks.end()) {
      existing_state += itr->second.state;
    }
  }
  return existing_state;
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
      <caption>Granted object locks</caption>
      <tr>
        <th>Lock Owner</th>
        <th>Object Id</th>
        <th>Num Holders</th>
      </tr>)";
  for (const auto& [txn, txn_entry] : txn_locks_) {
    std::lock_guard txn_lock(txn_entry.mutex);
    const auto& locks =
        locks_map == LocksMapType::kGranted ? txn_entry.granted_locks : txn_entry.waiting_locks;
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
    std::lock_guard txn_lock(txn_entry.mutex);
    const auto& locks =
        locks_map == LocksMapType::kGranted ? txn_entry.granted_locks : txn_entry.waiting_locks;
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

void ObjectLockManagerImpl::DoReleaseTrackedLock(
    const ObjectLockPrefix& object_id, const TrackedLockEntry& entry) {
  // We don't pass an intents set to unlock so as to trigger notify on every lock release. It is
  // necessary as two (or more) transactions could be holding a read lock and one of the txns
  // could request a conflicting lock mode. And since conflicts with self should be ignored, we
  // need to signal the cond variable on every release, else the lock release call from the other
  // transaction wouldn't unblock the waiter.
  DoUnlockSingleEntry(entry.locked_batch_entry, object_id, entry.state);

  entry.locked_batch_entry.ref_count -= entry.ref_count;
  if (entry.locked_batch_entry.ref_count == 0) {
    locks_.erase(object_id);
    free_lock_entries_.push_back(&entry.locked_batch_entry);
  }
}

void ObjectLockManagerImpl::AcquiredLock(
    const LockBatchEntry<ObjectLockManager>& lock_entry, TrackedTransactionLockEntry& txn,
    SubTransactionId subtxn_id, const OwnerAsString& owner_as_string,
    LocksMapType locks_map) {
  TRACE_FUNC();
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", owner: " << owner_as_string();
  auto delta = IntentTypeSetAdd(lock_entry.intent_types);

  std::lock_guard txn_lock(txn.mutex);
  auto& locks = locks_map == LocksMapType::kGranted ? txn.granted_locks : txn.waiting_locks;
  auto& subtxn_locks = locks[subtxn_id];
  auto it = subtxn_locks.find(lock_entry.key);
  if (it == subtxn_locks.end()) {
    it = subtxn_locks.emplace(lock_entry.key, TrackedLockEntry(*lock_entry.locked)).first;
  }
  it->second.state += delta;
  ++it->second.ref_count;
}

void ObjectLockManagerImpl::ReleasedLock(
    const LockBatchEntry<ObjectLockManager>& lock_entry, TrackedTransactionLockEntry& txn,
    SubTransactionId subtxn_id, const OwnerAsString& owner_as_string,
    LocksMapType locks_map) {
  TRACE_FUNC();
  VLOG_WITH_FUNC(1) << "lock_entry: " << lock_entry.ToString()
                    << ", owner: " << owner_as_string();
  auto delta = IntentTypeSetAdd(lock_entry.intent_types);

  std::lock_guard txn_lock(txn.mutex);
  auto& locks = locks_map == LocksMapType::kGranted ? txn.granted_locks : txn.waiting_locks;
  auto subtxn_itr = locks.find(subtxn_id);
  if (subtxn_itr == locks.end()) {
    LOG_WITH_FUNC(DFATAL) << "No locks found for " << owner_as_string()
                          << ", cannot release lock on " << AsString(lock_entry.key);
    return;
  }
  auto& subtxn_locks = subtxn_itr->second;
  auto it = subtxn_locks.find(lock_entry.key);
  if (it == subtxn_locks.end()) {
    LOG_WITH_FUNC(DFATAL) << "No lock found for " << owner_as_string() << " on "
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
}

ObjectLockManager::ObjectLockManager() : impl_(std::make_unique<ObjectLockManagerImpl>()) { }

ObjectLockManager::~ObjectLockManager() = default;

bool ObjectLockManager::Lock(
    LockBatchEntries<ObjectLockManager>& key_to_intent_type, CoarseTimePoint deadline,
    const ObjectLockOwner& object_lock_owner) {
  return impl_->Lock(key_to_intent_type, deadline, object_lock_owner);
}

void ObjectLockManager::Unlock(
    const std::vector<TrackedLockEntryKey>& lock_entry_keys) {
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
