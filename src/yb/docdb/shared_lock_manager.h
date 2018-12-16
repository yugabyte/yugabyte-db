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

#ifndef YB_DOCDB_SHARED_LOCK_MANAGER_H
#define YB_DOCDB_SHARED_LOCK_MANAGER_H

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/docdb/lock_batch.h"
#include "yb/gutil/spinlock.h"
#include "yb/util/cross_thread_mutex.h"

namespace yb {
namespace docdb {

// This class manages six types of locks on string keys. On each key, the possibilities are:
// - No locks
// - A single kStrongSnapshotWrite
// - Multiple kStrongSerializableRead and kWeakSerializableRead
// - Multiple kStrongSerializableWrite and kWeakSerializableWrite
// - Multiple kWeakSnapshotWrite, kWeakSerializableRead, and kWeakSerializableWrite
class SharedLockManager {
 public:
  SharedLockManager();
  ~SharedLockManager();

  // Attempt to lock a batch of keys. The call may be blocked waiting for other locks to be
  // released. If the entries don't exist, they are created. The lock batch gets associated with
  // this lock manager, which makes it auto-unlock on destruction.
  void Lock(LockBatchEntries* key_to_intent_type);

  // Release the batch of locks. Requires that the locks are held.
  void Unlock(const LockBatchEntries& key_to_intent_type);

  // Combine two intents and return the strongest lock type that covers both.
  static IntentType CombineIntents(IntentType i1, IntentType i2);

  // Whether or not the state is possible
  static bool IsStatePossible(const LockState& state);
  static std::string ToString(const LockState& state);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Masks of intent types. I.e. bits that related to this intent type is filled with 1, others are 0.
extern const std::array<LockState, kIntentTypeMapSize> kIntentMask;
// Conflicts of intent types. I.e. combination of masks of intent types that conflict with it.
extern const std::array<LockState, kIntentTypeMapSize> kIntentConflicts;

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_SHARED_LOCK_MANAGER_H
