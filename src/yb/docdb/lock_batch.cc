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

#include "yb/docdb/lock_batch.h"

#include "yb/docdb/object_lock_data.h"
#include "yb/docdb/shared_lock_manager.h"

#include "yb/util/status_format.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_bool(dump_lock_keys, true,
            "Whether to add keys to error message when lock batch timed out");

namespace yb {
namespace docdb {

LockBatch::LockBatch(
    SharedLockManager* lock_manager, LockBatchEntries<SharedLockManager>&& key_to_intent_type,
    CoarseTimePoint deadline) : data_(std::move(key_to_intent_type), lock_manager) {
  Init(deadline);
  if (!data_.status.ok()) {
    data_.key_to_type.clear();
  }
}

LockBatch::LockBatch(UnlockedBatch* unlocked_batch, CoarseTimePoint deadline)
    : data_(std::move(unlocked_batch->key_to_type_), unlocked_batch->shared_lock_manager_) {
  Init(deadline);
  if (!data_.status.ok()) {
    // Move LockBatchEntries back to the supplied UnlockedBatch so as to enable
    // support for re-locking by the caller.
    unlocked_batch->key_to_type_ = std::move(data_.key_to_type);
  }
}

void LockBatch::Init(CoarseTimePoint deadline) {
  if (!empty() && !data_.shared_lock_manager->Lock(data_.key_to_type, deadline)) {
    std::string batch_str;
    if (FLAGS_dump_lock_keys) {
      batch_str = Format(", batch: $0", data_.key_to_type);
    }
    data_.shared_lock_manager = nullptr;
    data_.status = STATUS_FORMAT(
        TryAgain, "Failed to obtain locks until deadline: $0$1", deadline, batch_str);
  }
}

LockBatch::~LockBatch() {
  Reset();
}

void LockBatch::DoUnlock() {
  DCHECK(!empty()) << "Called DoUnlock with empty LockBatch!";
  VLOG(1) << "Auto-unlocking a LockBatch with " << size() << " keys";
  DCHECK_NOTNULL(data_.shared_lock_manager)->Unlock(data_.key_to_type);
}

void LockBatch::Reset() {
  if (!empty()) {
    DoUnlock();
    data_.key_to_type.clear();
  }
}

void LockBatch::MoveFrom(LockBatch* other) {
  Reset();
  data_ = std::move(other->data_);
  // Explicitly clear other key_to_type to avoid extra unlock when it is destructed. We use
  // key_to_type emptiness to mark that it does not hold a lock.
  other->data_.key_to_type.clear();
}

template <typename LockManager>
std::string LockBatchEntry<LockManager>::ToString() const {
  return YB_STRUCT_TO_STRING(key, intent_types, existing_state);
}

UnlockedBatch::UnlockedBatch(
    LockBatchEntries<SharedLockManager>&& key_to_type,
    SharedLockManager* shared_lock_manager):
  key_to_type_(std::move(key_to_type)), shared_lock_manager_(shared_lock_manager) {}

LockBatch UnlockedBatch::TryLock(CoarseTimePoint deadline) {
  return LockBatch(this, deadline);
}

std::optional<UnlockedBatch> LockBatch::Unlock() {
  DCHECK(!empty());
  DoUnlock();
  return std::make_optional<UnlockedBatch>(std::move(data_.key_to_type), data_.shared_lock_manager);
}

void UnlockedBatch::MoveFrom(UnlockedBatch* other) {
  key_to_type_ = std::move(other->key_to_type_);
  other->key_to_type_.clear();
  shared_lock_manager_ = other->shared_lock_manager_;
  other->shared_lock_manager_ = nullptr;
}

template struct LockBatchEntry<SharedLockManager>;
template struct LockBatchEntry<ObjectLockManager>;

}  // namespace docdb
}  // namespace yb
