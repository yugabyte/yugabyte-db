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

#pragma once

#include <string>
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/dockv/intent.h"

#include "yb/util/monotime.h"

namespace yb {
namespace docdb {

// Lock state stores the number of locks acquired for each intent type.
// The count for each intent type resides in sequential bits (block) in lock state.
// For example the count of locks on a particular intent type could be received as:
// (lock_state >> (to_underlying(intent_type) * kIntentTypeBits)) & kFirstIntentTypeMask.
typedef uint64_t LockState;

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
  //
  // Returns false if was not able to acquire lock until deadline.
  MUST_USE_RESULT bool Lock(LockBatchEntries* key_to_intent_type, CoarseTimePoint deadline);

  // Release the batch of locks. Requires that the locks are held.
  void Unlock(const LockBatchEntries& key_to_intent_type);

  // Whether or not the state is possible
  static std::string ToString(const LockState& state);

  void DumpStatusHtml(std::ostream& out);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Masks of intent type sets.
// I.e. bits that related to any of intents from this set is filled with 1, others are 0.
extern const std::array<LockState, dockv::kIntentTypeSetMapSize> kIntentTypeSetMask;
// Conflicts of intent types. I.e. combination of masks of intent type sets that conflict with it.
extern const std::array<LockState, dockv::kIntentTypeSetMapSize> kIntentTypeSetConflicts;

bool IntentTypeSetsConflict(dockv::IntentTypeSet lhs, dockv::IntentTypeSet rhs);

}  // namespace docdb
}  // namespace yb
