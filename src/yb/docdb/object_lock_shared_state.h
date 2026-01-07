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

TableLockType FastpathLockTypeToTableLockType(ObjectLockFastpathLockType lock_type);

std::optional<ObjectLockFastpathLockType> MakeObjectLockFastpathLockType(TableLockType lock_type);

[[nodiscard]] std::span<const LockTypeEntry> GetEntriesForFastpathLockType(
    ObjectLockFastpathLockType lock_type);

struct ObjectLockFastpathRequest {
  SessionLockOwnerTag owner;
  SubTransactionId subtxn_id;
  uint32_t database_oid;
  uint32_t relation_oid;
  uint32_t object_oid;
  uint32_t object_sub_oid;
  ObjectLockFastpathLockType lock_type;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        owner, subtxn_id, database_oid, relation_oid, object_oid, object_sub_oid, lock_type);
  }
};

static_assert(std::is_trivially_copyable_v<ObjectLockFastpathRequest>);

using FastLockRequestConsumer = LWFunction<void(ObjectLockFastpathRequest)>;

class ObjectLockSharedState {
  class Impl;

 public:
  class ActivationGuard {
   public:
    ActivationGuard() = default;
    explicit ActivationGuard(Impl* impl);
    ActivationGuard(ActivationGuard&& other);
    ~ActivationGuard();
    ActivationGuard& operator=(ActivationGuard&& other) PARENT_PROCESS_ONLY;
   private:
    Impl* impl_ = nullptr;
  };

  explicit ObjectLockSharedState(SharedMemoryBackingAllocator& allocator);
  ~ObjectLockSharedState();

  [[nodiscard]] bool Lock(const ObjectLockFastpathRequest& request);

  ActivationGuard Activate(const std::unordered_map<ObjectLockPrefix, LockState>& initial_intents)
      PARENT_PROCESS_ONLY;

  size_t ConsumePendingLockRequests(const FastLockRequestConsumer& consume) PARENT_PROCESS_ONLY;

  size_t ConsumeAndAcquireExclusiveLockIntents(
      const FastLockRequestConsumer& consume,
      std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries) PARENT_PROCESS_ONLY;

  void ReleaseExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state)
      PARENT_PROCESS_ONLY;

  [[nodiscard]] SessionLockOwnerTag TEST_last_owner() PARENT_PROCESS_ONLY;

  [[nodiscard]] bool TEST_has_exclusive_intents() PARENT_PROCESS_ONLY;

 private:
  const SharedMemoryUniquePtr<Impl> impl_;
};

} // namespace yb::docdb
