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

using std::string;
using yb::util::to_underlying;

namespace yb {
namespace docdb {

namespace {

LockState Combine(std::initializer_list<IntentType> lock_types) {
  LockState state;
  for (auto type : lock_types) {
    state.set(static_cast<size_t>(type));
  }
  return state;
}

std::array<LockState, kIntentTypeMapSize> MakeConflicts() {
  std::array<LockState, kIntentTypeMapSize> result;
  memset(&result, 0, sizeof(result));
  result[static_cast<size_t>(IntentType::kWeakSerializableRead)] = Combine({
      IntentType::kStrongSerializableWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kStrongSerializableRead)] = Combine({
      IntentType::kWeakSerializableWrite,
      IntentType::kStrongSerializableWrite,
      IntentType::kWeakSnapshotWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kWeakSerializableWrite)] = Combine({
      IntentType::kStrongSerializableRead,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kStrongSerializableWrite)] = Combine({
      IntentType::kWeakSerializableRead,
      IntentType::kStrongSerializableRead,
      IntentType::kWeakSnapshotWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kWeakSnapshotWrite)] = Combine({
      IntentType::kStrongSerializableRead,
      IntentType::kStrongSerializableWrite,
      IntentType::kStrongSnapshotWrite
  });
  result[static_cast<size_t>(IntentType::kStrongSnapshotWrite)] = Combine({
      IntentType::kWeakSerializableRead,
      IntentType::kStrongSerializableRead,
      IntentType::kWeakSerializableWrite,
      IntentType::kStrongSerializableWrite,
      IntentType::kWeakSnapshotWrite,
      IntentType::kStrongSnapshotWrite
  });

  return result;
}

} // namespace

// The conflict matrix. (CONFLICTS[i] & (1 << j)) is one iff LockTypes i and j conflict.
// https://docs.google.com/document/d/1MLpW-9Hjx64U6mNQzVY9w5zUliJ9TYJ7772oAp7jwaA
// https://docs.google.com/spreadsheets/d/1h8GosY5XnJvrsyjEqyuXdKYlwvfKIaqx_RyDQGd7rSc
const std::array<LockState, kIntentTypeMapSize> kIntentConflicts = MakeConflicts();

bool SharedLockManager::VerifyState(const LockState& state) {
  LockState not_allowed;
  for (auto intent : kIntentTypeList) {
    size_t i = static_cast<size_t>(intent);
    if (state.test(i)) {
      if (not_allowed.test(i)) {
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
    if (state.test(static_cast<size_t>(type))) {
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

void SharedLockManager::LockEntry::Lock(IntentType lock_type) {
  // TODO(bojanserafimov): Implement CAS fast path. Only wait when CAS fails.
  int type_idx = static_cast<size_t>(lock_type);
  std::unique_lock<std::mutex> lock(mutex);
  auto& state = this->state;
  cond_var.wait(lock, [&state, type_idx]() {
    return (state & kIntentConflicts[type_idx]).none();
  });
  ++num_holding[type_idx];
  state.set(type_idx);
}

void SharedLockManager::LockEntry::Unlock(IntentType lock_type) {
  int type_idx = static_cast<int>(lock_type);
  bool should_notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    num_holding[type_idx]--;
    if (num_holding[type_idx] == 0) {
      state.reset(type_idx);
      should_notify = (state == Combine({})
        || state == Combine({IntentType::kWeakSerializableRead})
        || state == Combine({IntentType::kWeakSerializableWrite}));
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
    VLOG(4) << "Locking " << docdb::ToString(item.second) << ": "
            << util::FormatBytesAsStr(item.first);
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
    VLOG(4) << "Unlocking " << docdb::ToString(item.second) << ": "
            << util::FormatBytesAsStr(item.first);
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

}  // namespace docdb
}  // namespace yb
