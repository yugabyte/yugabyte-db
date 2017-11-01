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
#include "yb/docdb/shared_lock_manager.h"
#include "yb/util/trace.h"
#include "yb/util/debug-util.h"
#include "yb/util/tostring.h"

namespace yb {
namespace docdb {

LockBatch::LockBatch(SharedLockManager* lock_manager, KeyToIntentTypeMap&& key_to_intent_type)
    : key_to_type_(std::move(key_to_intent_type)),
      shared_lock_manager_(lock_manager) {
  if (!empty()) {
    lock_manager->Lock(key_to_type_);
  }
}

LockBatch::~LockBatch() {
  Reset();
}

void LockBatch::Reset() {
  if (!empty()) {
    VLOG(1) << "Auto-unlocking a LockBatch with " << size() << " keys";
    shared_lock_manager_->Unlock(key_to_type_);
    key_to_type_.clear();
  }
}

void LockBatch::MoveFrom(LockBatch* other) {
  Reset();
  key_to_type_ = std::move(other->key_to_type_);
  shared_lock_manager_ = other->shared_lock_manager_;
  other->key_to_type_.clear();
  other->shared_lock_manager_ = nullptr;
}


}  // namespace docdb
}  // namespace yb
