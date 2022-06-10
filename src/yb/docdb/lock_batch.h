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

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/intent.h"

#include "yb/util/monotime.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"

namespace yb {

class RefCntPrefix;

namespace docdb {

class SharedLockManager;

// We don't care about actual content of this struct here, since it is an implementation detail
// of SharedLockManager.
struct LockedBatchEntry;

struct LockBatchEntry {
  RefCntPrefix key;
  IntentTypeSet intent_types;

  // Memory is owned by SharedLockManager.
  LockedBatchEntry* locked = nullptr;

  std::string ToString() const;
};

// A LockBatch encapsulates a mapping from lock keys to lock types (intent types) to be acquired
// for each key. It also keeps track of a lock manager when locked, and auto-releases the locks
// in the destructor.
class LockBatch {
 public:
  LockBatch() {}
  LockBatch(SharedLockManager* lock_manager, LockBatchEntries&& key_to_intent_type,
            CoarseTimePoint deadline);
  LockBatch(LockBatch&& other) { MoveFrom(&other); }
  LockBatch& operator=(LockBatch&& other) { MoveFrom(&other); return *this; }
  ~LockBatch();

  // This class is move-only.
  LockBatch(const LockBatch&) = delete;
  LockBatch& operator=(const LockBatch&) = delete;

  // @return the number of keys in this batch
  size_t size() const { return data_.key_to_type.size(); }

  // @return whether the batch is empty. This is also used for checking if the batch is locked.
  bool empty() const { return data_.key_to_type.empty(); }

  const Status& status() const { return data_.status; }

  // Unlocks this batch if it is non-empty.
  void Reset();

 private:
  void MoveFrom(LockBatch* other);

  struct Data {
    Data() = default;
    Data(LockBatchEntries&& key_to_type_, SharedLockManager* shared_lock_manager_) :
      key_to_type(std::move(key_to_type_)), shared_lock_manager(shared_lock_manager_) {}

    Data(Data&&) = default;
    Data& operator=(Data&& other) = default;

    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;

    LockBatchEntries key_to_type;

    SharedLockManager* shared_lock_manager = nullptr;

    Status status;
  };

  Data data_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_LOCK_BATCH_H
