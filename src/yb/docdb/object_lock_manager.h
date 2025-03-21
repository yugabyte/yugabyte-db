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

#include <iosfwd>
#include <string>
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/object_lock_data.h"

#include "yb/util/monotime.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/tostring.h"

namespace yb::docdb {

// Helper struct used for keying table/object locks of a transaction.
struct TrackedLockEntryKey {
  TrackedLockEntryKey(const ObjectLockOwner& object_lock_owner_, ObjectLockPrefix object_id_)
      : object_lock_owner(object_lock_owner_), object_id(object_id_) {}

  const ObjectLockOwner object_lock_owner;
  const ObjectLockPrefix object_id;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(object_lock_owner, object_id);
  }

  bool operator==(const TrackedLockEntryKey& other) const = default;
};

class ObjectLockManagerImpl;

// This class manages locks on keys of type ObjectLockPrefix. On each key, the possibilities arise
// from a combination of kWeak/kStrong Read/Write intent types.
//
// Currently, this class is only being used for object/table level locks codepath. Each tablet
// server maintains an instance of the ObjectLockManager.
class ObjectLockManager {
 public:
  ObjectLockManager();

  ~ObjectLockManager();

  // Attempt to lock a batch of keys and track the lock against the given object_lock_owner key. The
  // call may be blocked waiting for other conflicting locks to be released. If the entries don't
  // exist, they are created. On success, the lock state is exists in-memory until an explicit
  // release is called (or the process restarts).
  //
  // Returns false if was not able to acquire lock until deadline.
  MUST_USE_RESULT bool Lock(
      const ObjectLockOwner& object_lock_owner,
      LockBatchEntries<ObjectLockManager>& key_to_intent_type, CoarseTimePoint deadline);

  // Release the batch of locks, if they were acquired at the first place.
  void Unlock(const std::vector<TrackedLockEntryKey>& lock_entry_keys);

  // Release all locks held against the given object_lock_owner.
  void Unlock(const ObjectLockOwner& object_lock_owner);

  void DumpStatusHtml(std::ostream& out);

  size_t TEST_GrantedLocksSize() const;
  size_t TEST_WaitingLocksSize() const;

 private:
  std::unique_ptr<ObjectLockManagerImpl> impl_;
};

}  // namespace yb::docdb
