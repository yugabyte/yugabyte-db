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

#include <vector>
#include <tuple>

#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/lock_batch.h"
#include "yb/docdb/object_lock_data.h"

#include "yb/dockv/value.h"

#include "yb/util/ref_cnt_buffer.h"

namespace yb::docdb {

using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryTypeAsChar;

template <typename LockManager>
struct DetermineKeysToLockResult {
  LockBatchEntries<LockManager> lock_batch;
  bool need_read_snapshot;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(lock_batch, need_read_snapshot);
  }
};

// Collapse keys_locked into a unique set of keys with intent_types representing the union of
// intent_types originally present. In other words, suppose keys_locked is originally the following:
// [
//   (k1, {kWeakRead, kWeakWrite}),
//   (k1, {kStrongRead}),
//   (k2, {kWeakRead}),
//   (k3, {kStrongRead}),
//   (k2, {kStrongWrite}),
// ]
// Then after calling FilterKeysToLock we will have:
// [
//   (k1, {kWeakRead, kWeakWrite, kStrongRead}),
//   (k2, {kWeakRead}),
//   (k3, {kStrongRead, kStrongWrite}),
// ]
// Note that only keys which appear in order in keys_locked will be collapsed in this manner.
template <typename T>
void FilterKeysToLock(LockBatchEntries<T> *keys_locked) {
  if (keys_locked->size() <= 1) {
    return;
  }

  std::sort(keys_locked->begin(), keys_locked->end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.key < rhs.key;
            });

  auto w = keys_locked->begin();
  for (auto it = keys_locked->begin(); ++it != keys_locked->end();) {
    if (it->key == w->key) {
      w->intent_types |= it->intent_types;
    } else {
      ++w;
      *w = *it;
    }
  }

  ++w;
  keys_locked->erase(w, keys_locked->end());
}

// We achieve the same table lock conflict matrix as that of pg documented here,
// https://www.postgresql.org/docs/current/explicit-locking.html#LOCKING-TABLES
//
// We only have 4 lock/intent modes kWeak/kStrong Read/Write, but to generate the above conflict
// matrix, we would need more lock types. Instead of introducing additional lock types, we use two
// KeyEntryType values and associate a list of <KeyEntryType, IntentTypeSet> to each table lock.
// Since our conflict detection mechanism checks conflicts against each key, we indirectly achieve
// the exact same conflict matrix. Refer comments on the function definition for more details.
const std::vector<std::pair<dockv::KeyEntryType, dockv::IntentTypeSet>>&
    GetEntriesForLockType(TableLockType lock);

// Returns DetermineKeysToLockResult<ObjectLockManager> which can further be passed to
// ObjectLockManager to acquire locks against the required objects with the given lock type.
Result<DetermineKeysToLockResult<ObjectLockManager>> DetermineObjectsToLock(
    const google::protobuf::RepeatedPtrField<ObjectLockPB>& objects_to_lock);

} // namespace yb::docdb
