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

#include <vector>

#include <boost/range/adaptor/reversed.hpp>
#include <glog/logging.h>

#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/trace.h"
#include "yb/util/tostring.h"

using std::string;

namespace yb {
namespace docdb {

namespace {

// Lock state stores number of locks acquires for each intent type.
// Count for each intent type resides in sequential bits (block) in lock state.
// For example count of lock on particular intent type could be received as:
// (lock_state >> kIntentTypeShift[type]) & kSingleIntentMask.

// We have 128 bits in LockState and 6 types of intents. So 21 bits is max number of bits
// that we could reserve for block of single intent type.
const size_t kIntentTypeBits = 21;
const LockState kSingleIntentMask = (static_cast<LockState>(1) << kIntentTypeBits) - 1;

// Since we have 6 types of intents that takes values from 0 to 7 we cannot just shift
// by kIntentTypeBits * intent_type. So we use this mapping.
// Also we have two 8-byte words in LockState, so align blocks for intent types so they don't wrap
// across those words.
std::array<int, kIntentTypeMapSize> kIntentTypeShift = {
    0, kIntentTypeBits, kIntentTypeBits * 2, 64,
    -1 /* not used */, -1 /* not used */, 64 + kIntentTypeBits, 64 + kIntentTypeBits * 2};

LockState CombineToLockState(std::initializer_list<IntentType> lock_types) {
  LockState state = 0;
  for (auto type : lock_types) {
    int shift = kIntentTypeShift[static_cast<size_t>(type)];
    DCHECK_GE(shift, 0) << "Bad shift for " << ToString(type);
    state |= kSingleIntentMask << shift;
  }
  return state;
}

std::array<LockState, kIntentTypeMapSize> MakeConflicts() {
  std::array<LockState, kIntentTypeMapSize> result;
  memset(&result, 0, sizeof(result));
  result[static_cast<size_t>(IntentType::kWeakSerializableRead)] = CombineToLockState({
      IntentType::kStrongSerializableWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kStrongSerializableRead)] = CombineToLockState({
      IntentType::kWeakSerializableWrite,
      IntentType::kStrongSerializableWrite,
      IntentType::kWeakSnapshotWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kWeakSerializableWrite)] = CombineToLockState({
      IntentType::kStrongSerializableRead,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kStrongSerializableWrite)] = CombineToLockState({
      IntentType::kWeakSerializableRead,
      IntentType::kStrongSerializableRead,
      IntentType::kWeakSnapshotWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kWeakSnapshotWrite)] = CombineToLockState({
      IntentType::kStrongSerializableRead,
      IntentType::kStrongSerializableWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kStrongSnapshotWrite)] = CombineToLockState({
      IntentType::kWeakSerializableRead,
      IntentType::kStrongSerializableRead,
      IntentType::kWeakSerializableWrite,
      IntentType::kStrongSerializableWrite,
      IntentType::kWeakSnapshotWrite,
      IntentType::kStrongSnapshotWrite
  });

  return result;
}

std::array<LockState, kIntentTypeMapSize> MakeMasks() {
  std::array<LockState, kIntentTypeMapSize> result;
  for (IntentType intent_type : kIntentTypeList) {
    result[static_cast<size_t>(intent_type)] = CombineToLockState({intent_type});
  }
  return result;
}

const auto kLockStateEmpty = CombineToLockState({});
const auto kLockStateAllButWeakSerializableRead =
    ~CombineToLockState({IntentType::kWeakSerializableRead});
const auto kLockStateAllButWeakSerializableWrite =
    ~CombineToLockState({IntentType::kWeakSerializableWrite});

} // namespace

struct LockedBatchEntry {
  // Taken only for short duration, with no blocking wait.
  std::mutex mutex;

  std::condition_variable cond_var;

  // Refcounting for garbage collection. Can only be used while the global mutex is locked.
  // Global mutex resides in lock manager and the same for all LockBatchEntries.
  size_t ref_count = 0;

  // Number of holders for each type
  std::atomic<LockState> num_holding{0};

  std::atomic<size_t> num_waiters{0};

  void Lock(IntentType lock_type);

  void Unlock(IntentType lock_type);
};

class SharedLockManager::Impl {
 public:
  void Lock(LockBatchEntries* key_to_intent_type);
  void Unlock(const LockBatchEntries& key_to_intent_type);

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

// The conflict matrix. (CONFLICTS[i] & (1 << j)) is one iff LockTypes i and j conflict.
// https://docs.google.com/document/d/1MLpW-9Hjx64U6mNQzVY9w5zUliJ9TYJ7772oAp7jwaA
// https://docs.google.com/spreadsheets/d/1h8GosY5XnJvrsyjEqyuXdKYlwvfKIaqx_RyDQGd7rSc
const std::array<LockState, kIntentTypeMapSize> kIntentConflicts = MakeConflicts();
const std::array<LockState, kIntentTypeMapSize> kIntentMask = MakeMasks();

bool SharedLockManager::IsStatePossible(const LockState& state) {
  LockState not_allowed = 0;
  for (auto intent : kIntentTypeList) {
    size_t i = static_cast<size_t>(intent);
    if (state & kIntentMask[i]) {
      if ((not_allowed & kIntentMask[i]) != 0) {
        return false;
      }
      not_allowed |= kIntentConflicts[i];
    }
  }
  return true;
}

std::string SharedLockManager::ToString(const LockState& state) {
  std::string result = "{";
  bool first = true;
  for (auto type : kIntentTypeList) {
    if ((state & kIntentMask[static_cast<size_t>(type)]) != 0) {
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

// Combine two intents and return the strongest lock type that covers both. The following rules
// apply:
// - strong > weak.
// - snapshot isolation > serializable write
// - snapshot isolation > serializable read
// - serializable read + serializable write = snapshot isolation
IntentType SharedLockManager::CombineIntents(const IntentType i1, const IntentType i2) {
  switch (i1) {
    case IntentType::kWeakSerializableRead:
      switch (i2) {
        case IntentType::kWeakSerializableRead:    return IntentType::kWeakSerializableRead;
        case IntentType::kStrongSerializableRead:  return IntentType::kStrongSerializableRead;
        case IntentType::kWeakSerializableWrite:   return IntentType::kWeakSnapshotWrite;
        case IntentType::kStrongSerializableWrite: return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSnapshotWrite:       return IntentType::kWeakSnapshotWrite;
        case IntentType::kStrongSnapshotWrite:     return IntentType::kStrongSnapshotWrite;
      }
      FATAL_INVALID_ENUM_VALUE(IntentType, i2);

    case IntentType::kStrongSerializableRead:
      switch (i2) {
        case IntentType::kWeakSerializableRead:    return IntentType::kStrongSerializableRead;
        case IntentType::kStrongSerializableRead:  return IntentType::kStrongSerializableRead;
        case IntentType::kWeakSerializableWrite:   return IntentType::kStrongSnapshotWrite;
        case IntentType::kStrongSerializableWrite: return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSnapshotWrite:       return IntentType::kStrongSnapshotWrite;
        case IntentType::kStrongSnapshotWrite:     return IntentType::kStrongSnapshotWrite;
      }
      FATAL_INVALID_ENUM_VALUE(IntentType, i2);

    case IntentType::kWeakSerializableWrite:
      switch (i2) {
        case IntentType::kWeakSerializableRead:    return IntentType::kWeakSnapshotWrite;
        case IntentType::kStrongSerializableRead:  return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSerializableWrite:   return IntentType::kWeakSerializableWrite;
        case IntentType::kStrongSerializableWrite: return IntentType::kStrongSerializableWrite;
        case IntentType::kWeakSnapshotWrite:       return IntentType::kWeakSnapshotWrite;
        case IntentType::kStrongSnapshotWrite:     return IntentType::kStrongSnapshotWrite;
      }
      FATAL_INVALID_ENUM_VALUE(IntentType, i2);

    case IntentType::kStrongSerializableWrite:
      switch (i2) {
        case IntentType::kWeakSerializableRead:    return IntentType::kStrongSnapshotWrite;
        case IntentType::kStrongSerializableRead:  return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSerializableWrite:   return IntentType::kStrongSerializableWrite;
        case IntentType::kStrongSerializableWrite: return IntentType::kStrongSerializableWrite;
        case IntentType::kWeakSnapshotWrite:       return IntentType::kStrongSnapshotWrite;
        case IntentType::kStrongSnapshotWrite:     return IntentType::kStrongSnapshotWrite;
      }
      FATAL_INVALID_ENUM_VALUE(IntentType, i2);

    case IntentType::kWeakSnapshotWrite:
      switch (i2) {
        case IntentType::kWeakSerializableRead:    return IntentType::kWeakSnapshotWrite;
        case IntentType::kStrongSerializableRead:  return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSerializableWrite:   return IntentType::kWeakSnapshotWrite;
        case IntentType::kStrongSerializableWrite: return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSnapshotWrite:       return IntentType::kWeakSnapshotWrite;
        case IntentType::kStrongSnapshotWrite:     return IntentType::kStrongSnapshotWrite;
      }
      FATAL_INVALID_ENUM_VALUE(IntentType, i2);

    case IntentType::kStrongSnapshotWrite:
      switch (i2) {
        case IntentType::kWeakSerializableRead:    return IntentType::kStrongSnapshotWrite;
        case IntentType::kStrongSerializableRead:  return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSerializableWrite:   return IntentType::kStrongSnapshotWrite;
        case IntentType::kStrongSerializableWrite: return IntentType::kStrongSnapshotWrite;
        case IntentType::kWeakSnapshotWrite:       return IntentType::kStrongSnapshotWrite;
        case IntentType::kStrongSnapshotWrite:     return IntentType::kStrongSnapshotWrite;
      }
      FATAL_INVALID_ENUM_VALUE(IntentType, i2);
  }
  FATAL_INVALID_ENUM_VALUE(IntentType, i1);
}

void LockedBatchEntry::Lock(IntentType lock_type) {
  size_t type_idx = static_cast<size_t>(lock_type);
  auto& num_holding = this->num_holding;
  auto old_value = num_holding.load(std::memory_order_acquire);
  auto add = static_cast<LockState>(1) << kIntentTypeShift[type_idx];
  for (;;) {
    if ((old_value & kIntentConflicts[type_idx]) == 0) {
      auto new_value = old_value + add;
      if (num_holding.compare_exchange_weak(old_value, new_value, std::memory_order_acq_rel)) {
        return;
      }
      continue;
    }
    num_waiters.fetch_add(1, std::memory_order_release);
    {
      std::unique_lock<std::mutex> lock(mutex);
      old_value = num_holding.load(std::memory_order_acquire);
      if ((old_value & kIntentConflicts[type_idx]) != 0) {
        cond_var.wait(lock);
      }
    }
    num_waiters.fetch_sub(1, std::memory_order_release);
  }
}

void LockedBatchEntry::Unlock(IntentType lock_type) {
  size_t type_idx = static_cast<size_t>(lock_type);
  auto sub = static_cast<LockState>(1) << kIntentTypeShift[type_idx];

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

  // If number of holding locks for this type is not 0, then we should not notify.
  if ((new_state & kIntentMask[type_idx]) != 0) {
    return;
  }

  // There are only several combinations of locks that could allow new type of lock.
  if (new_state == kLockStateEmpty ||
      (new_state & kLockStateAllButWeakSerializableRead) == 0 ||
      (new_state & kLockStateAllButWeakSerializableWrite) == 0) {
    {
      // Lock/unlock mutex as a barrier for Lock.
      // So we don't unlock and notify between check and wait in Lock.
      std::lock_guard<std::mutex> lock(mutex);
    }

    cond_var.notify_all();
  }
}

void SharedLockManager::Impl::Lock(LockBatchEntries* key_to_intent_type) {
  TRACE("Locking a batch of $0 keys", key_to_intent_type->size());
  Reserve(key_to_intent_type);
  for (const auto& key_and_intent_type : *key_to_intent_type) {
    const auto intent_type = key_and_intent_type.intent;
    VLOG(4) << "Locking " << docdb::ToString(intent_type) << ": "
            << key_and_intent_type.key.as_slice().ToDebugHexString();
    key_and_intent_type.locked->Lock(intent_type);
  }
  TRACE("Acquired a lock batch of $0 keys", key_to_intent_type->size());
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
    key_and_intent_type.locked->Unlock(key_and_intent_type.intent);
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

void SharedLockManager::Lock(LockBatchEntries* key_to_intent_type) {
  impl_->Lock(key_to_intent_type);
}

void SharedLockManager::Unlock(const LockBatchEntries& key_to_intent_type) {
  impl_->Unlock(key_to_intent_type);
}

}  // namespace docdb
}  // namespace yb
