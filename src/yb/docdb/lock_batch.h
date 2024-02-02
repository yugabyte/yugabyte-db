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

#include "yb/util/logging.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/intent.h"

#include "yb/gutil/macros.h"

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
  dockv::IntentTypeSet intent_types;

  // Memory is owned by SharedLockManager.
  LockedBatchEntry* locked = nullptr;

  std::string ToString() const;
};

class UnlockedBatch;

// A LockBatch encapsulates a mapping from lock keys to lock types (intent types) to be acquired
// for each key. It also keeps track of a lock manager when locked, and auto-releases the locks
// in the destructor.
class LockBatch {
 public:
  LockBatch() {}
  LockBatch(SharedLockManager* lock_manager, LockBatchEntries&& key_to_intent_type,
            CoarseTimePoint deadline);
  // Construct a LockBatch from the provided unlocked_batch instance. If successful, assumes
  // ownership of UnlockedBatch::key_to_type_.
  LockBatch(UnlockedBatch* unlocked_batch, CoarseTimePoint deadline);
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

  // Unlock the keys of this LockBatch and move all associated data into the returned Unlocked
  // instance. The returned instance can be used to construct another LockBatch, which in turn will
  // re-lock the keys.
  std::optional<UnlockedBatch> Unlock();

  const LockBatchEntries& Get() const { return data_.key_to_type; }

 private:
  void MoveFrom(LockBatch* other);

  // Initializes the LockBatch and locks the specified keys. Updates data_.status and
  // data_.shared_lock_manager in case of error and leaves data_.key_to_type unchanged.
  void Init(CoarseTimePoint deadline);

  void DoUnlock();

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

// A container which houses all data needed to re-lock the LockBatch which generated an
// UnlockedBatch via LockBatch::Unlock().
class UnlockedBatch {
 public:
  UnlockedBatch(LockBatchEntries&& key_to_type_, SharedLockManager* shared_lock_manager_);

  UnlockedBatch(UnlockedBatch&& other) { MoveFrom(&other); }

  // Tries locking the keys specified in "key_to_type_". On success, clears the state in
  // this->key_to_type_ (the caller is expected not to re-use the fields since they wouldn't
  // be in a valid state). In case of failure, preserves the state of the fields, so that
  // the caller can re-attempt locking. The status of the returned LockBatch must be checked
  // before usage.
  LockBatch TryLock(CoarseTimePoint deadline);

  UnlockedBatch& operator=(UnlockedBatch&& other) { MoveFrom(&other); return *this; }

  const LockBatchEntries& Get() const { return key_to_type_; }

 private:
  void MoveFrom(UnlockedBatch* other);

  LockBatchEntries key_to_type_;

  SharedLockManager* shared_lock_manager_ = nullptr;

  friend class LockBatch;

  DISALLOW_COPY_AND_ASSIGN(UnlockedBatch);
};

}  // namespace docdb
}  // namespace yb
