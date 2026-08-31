// Copyright (c) YugabyteDB, Inc.
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

#include <optional>
#include <span>

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_data.h"
#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/util/lw_function.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/shmem/robust_mutex.h"
#include "yb/util/shmem/shared_mem_allocator.h"
#include "yb/util/tostring.h"

namespace yb::docdb {

// State tracking active kStrongWrite, kWeakWrite intent types at the tserver's Object lock Manager.
// - first 32 bits store the num_active kStrongWrite
// - last 32 bits store the num_active kWeakWrite
//
// Since fastpath object locking is enabled for kAccessShare, kRowShare & kRowExclusive alone, all
// of which request intent_type(s) kWeakRead/kStrongRead, it is sufficient to just track active
// write intent types for detecting fast path locking conflicts. Hence not reusing LockState here.
//
// Additionally, since write lock state for multiple objects (with same hash) is stored in the same
// entry, it is better to not use LockState here as it could potentially lead to overflow.
using SharedWriteLockState = uint64_t;

SharedWriteLockState LockStateToSharedWriteLockState(LockState lock_state);

void SharedWriteLockStateRelease(SharedWriteLockState& held, SharedWriteLockState release);

TableLockType FastpathLockTypeToTableLockType(ObjectLockFastpathLockType lock_type);

std::optional<ObjectLockFastpathLockType> MakeObjectLockFastpathLockType(TableLockType lock_type);

[[nodiscard]] std::span<const LockTypeEntry> GetEntriesForFastpathLockType(
    ObjectLockFastpathLockType lock_type);

struct ObjectLockFastpathRequest {
  SubTransactionId subtxn_id;
  uint32_t database_oid;
  uint32_t relation_oid;
  uint32_t object_oid;
  uint32_t object_sub_oid;
  ObjectLockFastpathLockType lock_type;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        subtxn_id, database_oid, relation_oid, object_oid, object_sub_oid, lock_type);
  }
};

static_assert(std::is_trivially_copyable_v<ObjectLockFastpathRequest>);

using FastLockRequestConsumer = LWFunction<void(ObjectLockFastpathRequest)>;

class ObjectLockSharedState {
  class Impl;

 public:
  ObjectLockSharedState(
      SharedMemoryBackingAllocator& allocator,
      const std::unordered_map<ObjectLockPrefix, SharedWriteLockState>& initial_intents);
  ~ObjectLockSharedState();

  // Try to add a lock request from postgres side.
  [[nodiscard]] bool Lock(const ObjectLockFastpathRequest& request);

  // Try to perform an unlock all from postgres side.
  [[nodiscard]] bool UnlockAll();

  // Try to add a lock request from tserver side. Similar to Lock() except for accounting.
  [[nodiscard]] bool TServerLock(const ObjectLockFastpathRequest& request) PARENT_PROCESS_ONLY;

  // Try to perform an unlock all from tserver side. Similar to UnlockAll() except for accounting.
  [[nodiscard]] bool TServerUnlockAll() PARENT_PROCESS_ONLY;

  void ForceDropAll() PARENT_PROCESS_ONLY;

  // Indicate that TServer has loaded the transaction corresponding to this state into the lock
  // manager.
  void MarkTServerLoaded() PARENT_PROCESS_ONLY;

  void Enable() PARENT_PROCESS_ONLY;

  void Disable() PARENT_PROCESS_ONLY;

  void Shutdown() PARENT_PROCESS_ONLY;

  void ConsumePendingLockRequests(const FastLockRequestConsumer& consume) PARENT_PROCESS_ONLY;

  void ConsumeAndAcquireExclusiveLockIntents(
      const FastLockRequestConsumer& consume,
      std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) PARENT_PROCESS_ONLY;

  void ReleaseExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state)
      PARENT_PROCESS_ONLY;

  uint64_t PgLockRequestCount() const;
  uint64_t PgLockReleaseCount() const;
  uint64_t TServerLockRequestCount() const;
  uint64_t TServerLockReleaseCount() const;

  [[nodiscard]] bool TEST_has_exclusive_intents() PARENT_PROCESS_ONLY;

 private:
  const SharedMemoryUniquePtr<Impl> impl_;
};

} // namespace yb::docdb
