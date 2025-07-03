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

std::optional<ObjectLockFastpathLockType> MakeObjectLockFastpathLockType(TableLockType lock_type);

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
 public:
  explicit ObjectLockSharedState(SharedMemoryBackingAllocator& allocator);
  ~ObjectLockSharedState();

  [[nodiscard]] bool Lock(const ObjectLockFastpathRequest& request);

  void ConsumePendingLockRequests(const FastLockRequestConsumer& consume) PARENT_PROCESS_ONLY;

  [[nodiscard]] SessionLockOwnerTag TEST_last_owner() PARENT_PROCESS_ONLY;

 private:
  class Impl;
  const SharedMemoryUniquePtr<Impl> impl_;
};

} // namespace yb::docdb
