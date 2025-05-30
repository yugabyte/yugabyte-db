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

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/range/adaptor/reversed.hpp>

#include "yb/ash/wait_state.h"

#include "yb/docdb/lock_batch.h"
#include "yb/docdb/lock_util.h"

#include "yb/dockv/intent.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/logging.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/scope_exit.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

namespace yb::docdb {

using dockv::IntentTypeSet;

struct SharedLockedBatchEntry {
  // Taken only for short duration, with no blocking wait.
  mutable std::mutex mutex;
  std::condition_variable cond_var;

  // Refcounting for garbage collection. Can only be used while the global mutex is locked.
  // Global mutex resides in lock manager and covers this field for all LockBatchEntries.
  size_t ref_count = 0;

  // Number of holders for each type
  std::atomic<LockState> num_holding{0};

  std::atomic<size_t> num_waiters{0};

  MUST_USE_RESULT bool Lock(IntentTypeSet intent_types, CoarseTimePoint deadline);

  void Unlock(IntentTypeSet intent_types);

  std::string ToString() const {
    return Format("{ ref_count: $0 lock_state: $1 num_waiters: $2 }",
                  ref_count,
                  LockStateDebugString(num_holding.load(std::memory_order_acquire)),
                  num_waiters.load(std::memory_order_acquire));
  }
};

bool SharedLockedBatchEntry::Lock(IntentTypeSet intent_types, CoarseTimePoint deadline) {
  auto old_value = num_holding.load(std::memory_order_acquire);
  auto add = IntentTypeSetAdd(intent_types);
  auto conflicting_lock_state = IntentTypeSetConflict(intent_types);
  for (;;) {
    if ((old_value & conflicting_lock_state) == 0) {
      auto new_value = old_value + add;
      if (num_holding.compare_exchange_weak(old_value, new_value, std::memory_order_acq_rel)) {
        return true;
      }
      continue;
    }
    num_waiters.fetch_add(1, std::memory_order_release);
    auto se = ScopeExit([this] {
      num_waiters.fetch_sub(1, std::memory_order_release);
    });
    std::unique_lock<std::mutex> lock(mutex);
    old_value = num_holding.load(std::memory_order_acquire);
    if ((old_value & conflicting_lock_state) != 0) {
      SCOPED_WAIT_STATUS(LockedBatchEntry_Lock);
      if (deadline != CoarseTimePoint::max()) {
        // Note -- even if we wait here, we don't need to be aware for the purposes of deadlock
        // detection since this eventually succeeds (in which case thread gets to queue) or times
        // out (thereby eliminating any possibly untraced deadlock).
        VLOG(4) << "Waiting to acquire lock type: " << intent_types.ToUIntPtr()
                << " with num_holding: " << old_value << ", num_waiters: " << num_waiters
                << " with deadline: " << AsString(deadline.time_since_epoch()) << " .";
        if (cond_var.wait_until(lock, deadline) == std::cv_status::timeout) {
          return false;
        }
      } else {
        VLOG(4) << "Waiting to acquire lock type: " << intent_types.ToUIntPtr()
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

void SharedLockedBatchEntry::Unlock(IntentTypeSet intent_types) {
  auto sub = IntentTypeSetAdd(intent_types);
  auto new_state = num_holding.fetch_sub(sub, std::memory_order_acq_rel) - sub;
  if (!num_waiters.load(std::memory_order_acquire)) {
    return;
  }

  bool has_zero = false;
  for (auto intent_type : intent_types) {
    if (!(new_state & IntentTypeMask(intent_type))) {
      has_zero = true;
      break;
    }
  }

  // At least one of counters should become 0 to unblock waiting locks.
  if (!has_zero) {
    return;
  }

  {
    // Lock/unlock mutex as a barrier for Lock.
    // So we don't unlock and notify between check and wait in Lock.
    std::lock_guard lock(mutex);
  }

  YB_PROFILE(cond_var.notify_all());
}

class SharedLockManager::Impl {
 public:
  ~Impl() {
    std::lock_guard lock(global_mutex_);
    LOG_IF(DFATAL, !locks_.empty()) << "Locks not empty in dtor: " << AsString(locks_);
  }

  MUST_USE_RESULT bool Lock(
      LockBatchEntries<SharedLockManager>& key_to_intent_type, CoarseTimePoint deadline);

  void Unlock(const LockBatchEntries<SharedLockManager>& key_to_intent_type);

  void DumpStatusHtml(std::ostream& out) EXCLUDES(global_mutex_);

 private:
  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  void Reserve(LockBatchEntries<SharedLockManager>& batch) EXCLUDES(global_mutex_);

  // Update refcounts and maybe collect garbage.
  void Cleanup(const LockBatchEntries<SharedLockManager>& key_to_intent_type)
      EXCLUDES(global_mutex_);

  // The global mutex should be taken only for very short duration, with no blocking wait.
  mutable std::mutex global_mutex_;

  std::unordered_map<RefCntPrefix, SharedLockedBatchEntry*> locks_ GUARDED_BY(global_mutex_);
  // Cache of lock entries, to avoid allocation/deallocation of heavy SharedLockedBatchEntry.
  std::vector<std::unique_ptr<SharedLockedBatchEntry>> lock_entries_ GUARDED_BY(global_mutex_);
  std::vector<SharedLockedBatchEntry*> free_lock_entries_ GUARDED_BY(global_mutex_);
};

bool SharedLockManager::Impl::Lock(
    LockBatchEntries<SharedLockManager>& key_to_intent_type, CoarseTimePoint deadline) {
  TRACE("Locking a batch of $0 keys", key_to_intent_type.size());
  Reserve(key_to_intent_type);
  for (auto it = key_to_intent_type.begin(); it != key_to_intent_type.end(); ++it) {
    const auto& intent_types = it->intent_types;
    VLOG(4) << "Locking " << AsString(intent_types) << ": "
            << AsString(it->key);
    if (!it->locked->Lock(it->intent_types, deadline)) {
      while (it != key_to_intent_type.begin()) {
        --it;
        it->locked->Unlock(it->intent_types);
      }
      Cleanup(key_to_intent_type);
      return false;
    }
  }
  TRACE("Acquired a lock batch of $0 keys", key_to_intent_type.size());
  return true;
}


void SharedLockManager::Impl::Unlock(
    const LockBatchEntries<SharedLockManager>& key_to_intent_type) {
  TRACE("Unlocking a batch of $0 keys", key_to_intent_type.size());

  for (const auto& key_and_intent_type : boost::adaptors::reverse(key_to_intent_type)) {
    key_and_intent_type.locked->Unlock(key_and_intent_type.intent_types);
  }
  Cleanup(key_to_intent_type);
}

void SharedLockManager::Impl::DumpStatusHtml(std::ostream& out) {
  out << R"(
    <table class="table table-striped">
    <tr>
      <th>Prefix</th>
      <th>LockBatchEntry</th>
    </tr>
  )";
  std::lock_guard l(global_mutex_);
  for (const auto& [prefix, entry] : locks_) {
    auto key_str = AsString(prefix);
    out << "<tr>"
          << "<td>" << (!key_str.empty() ? key_str : "[empty]") << "</td>"
          << "<td>" << entry->ToString() << "</td>"
        << "</tr>";
  }
  out << "</table>\n";
}

void SharedLockManager::Impl::Reserve(LockBatchEntries<SharedLockManager>& key_to_intent_type) {
  std::lock_guard lock(global_mutex_);
  for (auto& key_and_intent_type : key_to_intent_type) {
    auto& value = locks_[key_and_intent_type.key];
    if (!value) {
      if (!free_lock_entries_.empty()) {
        value = free_lock_entries_.back();
        free_lock_entries_.pop_back();
      } else {
        value = lock_entries_.emplace_back(std::make_unique<SharedLockedBatchEntry>()).get();
      }
    }
    value->ref_count++;
    key_and_intent_type.locked = value;
  }
}

void SharedLockManager::Impl::Cleanup(
    const LockBatchEntries<SharedLockManager>& key_to_intent_type) {
  std::lock_guard lock(global_mutex_);
  for (const auto& item : key_to_intent_type) {
    if (--item.locked->ref_count == 0) {
      locks_.erase(item.key);
      free_lock_entries_.push_back(item.locked);
    }
  }
}

SharedLockManager::SharedLockManager() : impl_(std::make_unique<Impl>()) {}

SharedLockManager::~SharedLockManager() = default;

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

}  // namespace yb::docdb
