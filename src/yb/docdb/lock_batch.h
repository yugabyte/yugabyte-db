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

#ifndef YB_DOCDB_LOCK_BATCH_H
#define YB_DOCDB_LOCK_BATCH_H

#include <string>

#include <glog/logging.h>

#include "yb/docdb/doc_key.h"
#include "yb/docdb/value_type.h"

namespace yb {

class RefCntPrefix;

namespace docdb {

class SharedLockManager;

// We don't care about actual content of this struct here, since it is an implementation detail
// of SharedLockManager.
struct LockedBatchEntry;

struct LockBatchEntry {
  RefCntPrefix key;
  IntentType intent;

  // Memory is owned by SharedLockManager.
  LockedBatchEntry* locked = nullptr;
};

typedef std::vector<LockBatchEntry> LockBatchEntries;

// A LockBatch encapsulates a mapping from lock keys to lock types (intent types) to be acquired
// for each key. It also keeps track of a lock manager when locked, and auto-releases the locks
// in the destructor.
class LockBatch {
 public:
  LockBatch() {}
  LockBatch(SharedLockManager* lock_manager, LockBatchEntries&& key_to_intent_type);
  LockBatch(LockBatch&& other) { MoveFrom(&other); }
  LockBatch& operator=(LockBatch&& other) { MoveFrom(&other); return *this; }
  ~LockBatch();

  // This class is move-only.
  LockBatch(const LockBatch&) = delete;
  LockBatch& operator=(const LockBatch&) = delete;

  // @return the number of keys in this batch
  size_t size() const { return key_to_type_.size(); }

  // @return whether the batch is empty. This is also used for checking if the batch is locked.
  bool empty() const { return key_to_type_.empty(); }

  // Unlocks this batch if it is non-empty.
  void Reset();

 private:
  void MoveFrom(LockBatch* other);

  LockBatchEntries key_to_type_;

  // A LockBatch is associated with a SharedLockManager instance the moment it is locked, and this
  // field is set back to nullptr when the batch is unlocked.
  SharedLockManager* shared_lock_manager_ = nullptr;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_LOCK_BATCH_H
