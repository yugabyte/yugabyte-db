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

  // Attempt to lock a batch of keys. The call may be blocked waiting for other locks to be
  // released. If the entries don't exist, they are created. The lock batch gets associated with
  // this lock manager, which makes it auto-unlock on destruction.
  void Lock(const KeyToIntentTypeMap& key_to_intent_type);

  // Release the batch of locks. Requires that the locks are held.
  void Unlock(const KeyToIntentTypeMap& key_to_intent_type);

  void LockInTest(const std::string& key, IntentType intent_type);
  void UnlockInTest(const std::string& key, IntentType intent_type);

  // Combine two intents and return the strongest lock type that covers both.
  static IntentType CombineIntents(IntentType i1, IntentType i2);

  // Whether or not the state is possible
  static bool VerifyState(const LockState& state);
  static std::string ToString(const LockState& state);

 private:

  struct LockEntry {
    // Taken only for short duration, with no blocking wait.
    std::mutex mutex;

    std::condition_variable cond_var;

    // Refcounting for garbage collection. Can only be used while the global lock is held.
    size_t num_using = 0;

    // Number of holders for each type
    std::array<size_t, kIntentTypeMapSize> num_holding;
    LockState state;

    void Lock(IntentType lock_type);

    void Unlock(IntentType lock_type);

    LockEntry() {
      num_holding.fill(0);
    }
  };

  typedef std::unordered_map<std::string, std::unique_ptr<LockEntry>> LockEntryMap;

  // Make sure the entries exist in the locks_ map and return pointers so we can access
  // them without holding the global lock. Returns a vector with pointers in the same order
  // as the keys in the batch.
  std::vector<LockEntry*> Reserve(const KeyToIntentTypeMap& batch);

  // Update refcounts and maybe collect garbage.
  void Cleanup(const KeyToIntentTypeMap& key_to_intent_type);

  // The global mutex should be taken only for very short duration, with no blocking wait.
  std::mutex global_mutex_;

  // Can only be modified if the global mutex is held.
  LockEntryMap locks_;
};

extern const std::array<LockState, kIntentTypeMapSize> kIntentConflicts;

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_SHARED_LOCK_MANAGER_H
