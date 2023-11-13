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

#include <boost/range/adaptor/reversed.hpp>
#include "yb/util/logging.h"

#include "yb/docdb/lock_batch.h"

#include "yb/util/enums.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/scope_exit.h"
#include "yb/util/trace.h"

using std::string;

namespace yb {
namespace docdb {

namespace {

// Lock state stores number of locks acquired for each intent type.
// Count for each intent type resides in sequential bits (block) in lock state.
// For example count of lock on particular intent type could be received as:
// (lock_state >> kIntentTypeShift[type]) & kSingleIntentMask.

// We have 64 bits in LockState and 4 types of intents. So 16 bits is max number of bits
// that we could reserve for block of single intent type.
const size_t kIntentTypeBits = 16;
// kSingleIntentMask represents the LockState which, when &'d with another LockState, would result
// in the LockState tracking only the count for intent type represented by the region of bits that
// is "least significant", as in furthest to the right.
const LockState kSingleIntentMask = (static_cast<LockState>(1) << kIntentTypeBits) - 1;

bool IntentTypesConflict(IntentType lhs, IntentType rhs) {
  auto lhs_value = to_underlying(lhs);
  auto rhs_value = to_underlying(rhs);
  // The rules are the following:
  // 1) At least one intent should be strong for conflict.
  // 2) Read and write conflict only with opposite type.
  return ((lhs_value & kStrongIntentFlag) || (rhs_value & kStrongIntentFlag)) &&
         ((lhs_value & kWriteIntentFlag) != (rhs_value & kWriteIntentFlag));
}

LockState IntentTypeMask(IntentType intent_type, LockState single_intent_mask = kSingleIntentMask) {
  return single_intent_mask << (to_underlying(intent_type) * kIntentTypeBits);
}

// Generate conflict mask for all possible subsets of intent type set. The i-th index of the
// returned array represents a conflict mask for the i-th possible IntentTypeSet. To determine if a
// given IntentTypeSet i conflicts with the key's existing LockState l, you can do the following:
// bool is_conflicting = kIntentTypeSetConflicts[i.ToUintPtr()] & l != 0;
std::array<LockState, kIntentTypeSetMapSize> GenerateConflicts() {
  std::array<LockState, kIntentTypeSetMapSize> result;
  for (size_t idx = 0; idx < kIntentTypeSetMapSize; ++idx) {
    result[idx] = 0;
    for (auto intent_type : IntentTypeSet(idx)) {
      for (auto other_intent_type : IntentTypeList()) {
        if (IntentTypesConflict(intent_type, other_intent_type)) {
          result[idx] |= IntentTypeMask(other_intent_type);
        }
      }
    }
  }
  return result;
}

// Generate array of LockState's with one entry for each possible subset of intent type set.
// The entry is combination of single_intent_mask for intents from set.
std::array<LockState, kIntentTypeSetMapSize> GenerateByMask(LockState single_intent_mask) {
  DCHECK_EQ(single_intent_mask & kSingleIntentMask, single_intent_mask);
  std::array<LockState, kIntentTypeSetMapSize> result;
  for (size_t idx = 0; idx != kIntentTypeSetMapSize; ++idx) {
    result[idx] = 0;
    for (auto intent_type : IntentTypeSet(idx)) {
      result[idx] |= IntentTypeMask(intent_type, single_intent_mask);
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
typedef std::array<LockState, kIntentTypeSetMapSize> IntentTypeSetMap;

// Maps IntentTypeSet to a LockState mask which determines if another LockState will conflict with
// any of the elements present in the IntentTypeSet.
const IntentTypeSetMap kIntentTypeSetConflicts = GenerateConflicts();

// Maps IntentTypeSet to the LockState representing one count for each intent type in the set. Can
// be used to "add one" occurence of an IntentTypeSet to an existing key's LockState.
const IntentTypeSetMap kIntentTypeSetAdd = GenerateByMask(1);

// Maps IntentTypeSet to the LockState representing max count for each intent type in the set. Can
// be used to extract a LockState corresponding to having only that set's elements counts present.
const IntentTypeSetMap kIntentTypeSetMask = GenerateByMask(kSingleIntentMask);

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

struct LockedBatchEntry {
  // Taken only for short duration, with no blocking wait.
  mutable std::mutex mutex;

  std::condition_variable cond_var;

  // Refcounting for garbage collection. Can only be used while the global mutex is locked.
  // Global mutex resides in lock manager and covers this field for all LockBatchEntries.
  size_t ref_count = 0;

  // Number of holders for each type
  std::atomic<LockState> num_holding{0};

  std::atomic<size_t> num_waiters{0};

  MUST_USE_RESULT bool Lock(IntentTypeSet lock, CoarseTimePoint deadline);

  void Unlock(IntentTypeSet lock);

  std::string ToString() const {
    std::lock_guard<std::mutex> lock(mutex);
    return Format("{ ref_count: $0 num_holding: $1 num_waiters: $2 }",
                  ref_count, num_holding.load(std::memory_order_acquire),
                  num_waiters.load(std::memory_order_acquire));
  }
};

class SharedLockManager::Impl {
 public:
  MUST_USE_RESULT bool Lock(LockBatchEntries* key_to_intent_type, CoarseTimePoint deadline);
  void Unlock(const LockBatchEntries& key_to_intent_type);

  ~Impl() {
    std::lock_guard<std::mutex> lock(global_mutex_);
    LOG_IF(DFATAL, !locks_.empty()) << "Locks not empty in dtor: " << yb::ToString(locks_);
  }

 private:
  typedef std::unordered_map<RefCntPrefix, LockedBatchEntry*, RefCntPrefixHash> LockEntryMap;

  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  void Reserve(LockBatchEntries* batch);

  // Update refcounts and maybe collect garbage.
  void Cleanup(const LockBatchEntries& key_to_intent_type);

  // The global mutex should be taken only for very short duration, with no blocking wait.
  std::mutex global_mutex_;

  LockEntryMap locks_ GUARDED_BY(global_mutex_);
  // Cache of lock entries, to avoid allocation/deallocation of heavy LockedBatchEntry.
  std::vector<std::unique_ptr<LockedBatchEntry>> lock_entries_ GUARDED_BY(global_mutex_);
  std::vector<LockedBatchEntry*> free_lock_entries_ GUARDED_BY(global_mutex_);
};

std::string SharedLockManager::ToString(const LockState& state) {
  std::string result = "{";
  bool first = true;
  for (auto type : IntentTypeList()) {
    if ((state & IntentTypeMask(type)) != 0) {
      if (first) {
        first = false;
      } else {
        result += ", ";
      }
      result += docdb::ToString(type);
    }
  }
  result += "}";
  return result;
}

bool LockedBatchEntry::Lock(IntentTypeSet lock_type, CoarseTimePoint deadline) {
  size_t type_idx = lock_type.ToUIntPtr();
  auto& num_holding = this->num_holding;
  auto old_value = num_holding.load(std::memory_order_acquire);
  auto add = kIntentTypeSetAdd[type_idx];
  for (;;) {
    if ((old_value & kIntentTypeSetConflicts[type_idx]) == 0) {
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
    if ((old_value & kIntentTypeSetConflicts[type_idx]) != 0) {
      if (deadline != CoarseTimePoint::max()) {
        // Note -- even if we wait here, we don't need to be aware for the purposes of deadlock
        // detection since this eventually succeeds (in which case thread gets to queue) or times
        // out (thereby eliminating any possibly untraced deadlock).
        if (cond_var.wait_until(lock, deadline) == std::cv_status::timeout) {
          return false;
        }
      } else {
        // TODO(wait-queues): Hitting this branch with wait queues could cause deadlocks if
        // we never reach the wait queue and register the "waiting for" relationship. We should add
        // a DCHECK that wait queues are not enabled in this branch, or remove the branch.
        cond_var.wait(lock);
      }
    }
  }
}

void LockedBatchEntry::Unlock(IntentTypeSet lock_types) {
  size_t type_idx = lock_types.ToUIntPtr();
  auto sub = kIntentTypeSetAdd[type_idx];

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

  bool has_zero = false;
  for (auto intent_type : lock_types) {
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
    std::lock_guard<std::mutex> lock(mutex);
  }

  cond_var.notify_all();
}

bool SharedLockManager::Impl::Lock(LockBatchEntries* key_to_intent_type, CoarseTimePoint deadline) {
  TRACE("Locking a batch of $0 keys", key_to_intent_type->size());
  Reserve(key_to_intent_type);
  for (auto it = key_to_intent_type->begin(); it != key_to_intent_type->end(); ++it) {
    const auto& key_and_intent_type = *it;
    const auto intent_types = key_and_intent_type.intent_types;
    VLOG(4) << "Locking " << yb::ToString(intent_types) << ": "
            << key_and_intent_type.key.as_slice().ToDebugHexString();
    if (!key_and_intent_type.locked->Lock(intent_types, deadline)) {
      while (it != key_to_intent_type->begin()) {
        --it;
        it->locked->Unlock(it->intent_types);
      }
      Cleanup(*key_to_intent_type);
      return false;
    }
  }
  TRACE("Acquired a lock batch of $0 keys", key_to_intent_type->size());

  return true;
}

void SharedLockManager::Impl::Reserve(LockBatchEntries* key_to_intent_type) {
  std::lock_guard<std::mutex> lock(global_mutex_);
  for (auto& key_and_intent_type : *key_to_intent_type) {
    auto& value = locks_[key_and_intent_type.key];
    if (!value) {
      if (!free_lock_entries_.empty()) {
        value = free_lock_entries_.back();
        free_lock_entries_.pop_back();
      } else {
        lock_entries_.emplace_back(std::make_unique<LockedBatchEntry>());
        value = lock_entries_.back().get();
      }
    }
    value->ref_count++;
    key_and_intent_type.locked = value;
  }
}

void SharedLockManager::Impl::Unlock(const LockBatchEntries& key_to_intent_type) {
  TRACE("Unlocking a batch of $0 keys", key_to_intent_type.size());

  for (const auto& key_and_intent_type : boost::adaptors::reverse(key_to_intent_type)) {
    key_and_intent_type.locked->Unlock(key_and_intent_type.intent_types);
  }

  Cleanup(key_to_intent_type);
}

void SharedLockManager::Impl::Cleanup(const LockBatchEntries& key_to_intent_type) {
  std::lock_guard<std::mutex> lock(global_mutex_);
  for (const auto& item : key_to_intent_type) {
    if (--(item.locked->ref_count) == 0) {
      locks_.erase(item.key);
      free_lock_entries_.push_back(item.locked);
    }
  }
}

SharedLockManager::SharedLockManager() : impl_(new Impl) {
}

SharedLockManager::~SharedLockManager() {}

bool SharedLockManager::Lock(LockBatchEntries* key_to_intent_type, CoarseTimePoint deadline) {
  return impl_->Lock(key_to_intent_type, deadline);
}

void SharedLockManager::Unlock(const LockBatchEntries& key_to_intent_type) {
  impl_->Unlock(key_to_intent_type);
}

}  // namespace docdb
}  // namespace yb
