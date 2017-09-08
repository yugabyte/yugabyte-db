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

#include "yb/util/shared_lock_manager.h"

#include <vector>

#include <boost/range/adaptor/reversed.hpp>
#include <glog/logging.h>

#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"

using std::string;
using yb::util::to_underlying;

namespace yb {
namespace util {

const char* ToString(LockType lock_type) {
  switch (lock_type) {
  case LockType::SR_READ_WEAK:
    return "SR_READ_WEAK";
  case LockType::SR_READ_STRONG:
    return "SR_READ_STRONG";
  case LockType::SR_WRITE_WEAK:
    return "SR_WRITE_WEAK";
  case LockType::SR_WRITE_STRONG:
    return "SR_WRITE_STRONG";
  case LockType::SI_WRITE_WEAK:
    return "SI_WRITE_WEAK";
  case LockType::SI_WRITE_STRONG:
    return "SI_WRITE_STRONG";
  }
  FATAL_INVALID_ENUM_VALUE(LockType, lock_type);
}

static constexpr LockState Combine(std::initializer_list<LockType> lock_types) {
  LockState state = 0;
  for (auto type : lock_types) {
    state |= (1 << static_cast<int>(type));
  }
  return state;
}

static constexpr LockState ConflictsForType(LockType lock_type) {
  switch (lock_type) {
  case LockType::SR_READ_WEAK:
    return Combine({
                    LockType::SR_WRITE_STRONG,
                    LockType::SI_WRITE_STRONG
      });
  case LockType::SR_READ_STRONG:
    return Combine({
                    LockType::SR_WRITE_WEAK,
                    LockType::SR_WRITE_STRONG,
                    LockType::SI_WRITE_WEAK,
                    LockType::SI_WRITE_STRONG
      });
  case LockType::SR_WRITE_WEAK:
    return Combine({
                    LockType::SR_READ_STRONG,
                    LockType::SI_WRITE_STRONG
      });
  case LockType::SR_WRITE_STRONG:
    return Combine({
                    LockType::SR_READ_WEAK,
                    LockType::SR_READ_STRONG,
                    LockType::SI_WRITE_WEAK,
                    LockType::SI_WRITE_STRONG
      });
  case LockType::SI_WRITE_WEAK:
    return Combine({
                    LockType::SR_READ_STRONG,
                    LockType::SR_WRITE_STRONG,
                    LockType::SI_WRITE_STRONG
      });
  case LockType::SI_WRITE_STRONG:
    return Combine({
                    LockType::SR_READ_WEAK,
                    LockType::SR_READ_STRONG,
                    LockType::SR_WRITE_WEAK,
                    LockType::SR_WRITE_STRONG,
                    LockType::SI_WRITE_WEAK,
                    LockType::SI_WRITE_STRONG
      });
  }
}

// The conflict matrix. (CONFLICTS[i] & (1 << j)) is one iff LockTypes i and j conflict.
// https://docs.google.com/document/d/1MLpW-9Hjx64U6mNQzVY9w5zUliJ9TYJ7772oAp7jwaA
// https://docs.google.com/spreadsheets/d/1h8GosY5XnJvrsyjEqyuXdKYlwvfKIaqx_RyDQGd7rSc
static constexpr LockState CONFLICTS[NUM_LOCK_TYPES] = {
  ConflictsForType(LockType(0)),
  ConflictsForType(LockType(1)),
  ConflictsForType(LockType(2)),
  ConflictsForType(LockType(3)),
  ConflictsForType(LockType(4)),
  ConflictsForType(LockType(5))
};

bool SharedLockManager::VerifyState(LockState state) {
  LockState not_allowed = 0;
  for (int i = 0; i < NUM_LOCK_TYPES; i++) {
    if (state & (1 << i)) {
      if (not_allowed & (1 << i)) {
        return false;
      }
      not_allowed |= CONFLICTS[i];
    }
  }
  return true;
}

void SharedLockManager::LockEntry::Lock(LockType lock_type) {
  // TODO(bojanserafimov): Implement CAS fast path. Only wait when CAS fails.
  int type_idx = static_cast<int>(lock_type);
  std::unique_lock<std::mutex> lock(mutex);
  while ((state & CONFLICTS[type_idx]) != 0) {
    cond_var.wait(lock);
  }
  num_holding[type_idx]++;
  state |= (1 << type_idx);
}

void SharedLockManager::LockEntry::Unlock(LockType lock_type) {
  int type_idx = static_cast<int>(lock_type);
  bool should_notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    num_holding[type_idx]--;
    if (num_holding[type_idx] == 0) {
      state &= ~(1 << type_idx);
      should_notify = (state == Combine({})
        || state == Combine({LockType::SR_READ_WEAK})
        || state == Combine({LockType::SR_WRITE_WEAK}));
    }
  }

  // Notify only if it is possible that a waiting thread can now lock
  if (should_notify) {
    cond_var.notify_all();
  }
}

void SharedLockManager::Lock(const LockBatch& batch) {
  std::vector<SharedLockManager::LockEntry*> reserved = Reserve(batch);
  size_t idx = 0;
  for (const auto& item : batch) {
    VLOG(4) << "Locking " << ToString(item.second) << ": " << FormatBytesAsStr(item.first);
    reserved[idx]->Lock(item.second);
    idx++;
  }
}

std::vector<SharedLockManager::LockEntry*> SharedLockManager::Reserve(const LockBatch& batch) {
  std::vector<SharedLockManager::LockEntry*> reserved;
  reserved.reserve(batch.size());
  {
    std::lock_guard<std::mutex> lock(global_mutex_);
    for (const auto& item : batch) {
      auto it = locks_.emplace(item.first, std::make_unique<LockEntry>()).first;
      it->second->num_using++;
      reserved.push_back(&*it->second);
    }
  }
  return reserved;
}

void SharedLockManager::Unlock(const LockBatch& batch) {
  std::lock_guard<std::mutex> lock(global_mutex_);
  for (const auto& item : boost::adaptors::reverse(batch)) {
    VLOG(4) << "Unlocking " << ToString(item.second) << ": " << FormatBytesAsStr(item.first);
    locks_[item.first]->Unlock(item.second);
  }
  Cleanup(batch);
}

void SharedLockManager::Cleanup(const LockBatch& batch) {
  for (const auto& item : batch) {
    auto it = locks_.find(item.first);
    it->second->num_using--;
    if (it->second->num_using == 0) {
      locks_.erase(it);
    }
  }
}

}  // namespace util
}  // namespace yb
