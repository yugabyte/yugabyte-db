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

#include <array>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/logic/tribool.hpp>

#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/lock_manager_traits.h"
#include "yb/docdb/object_lock_data.h"

#include "yb/dockv/intent.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/value.h"

#include "yb/util/ref_cnt_buffer.h"

namespace yb::docdb {

using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryTypeAsChar;

template <typename LockManager>
struct LockBatchEntry {
  typename LockManagerTraits<LockManager>::KeyType key;
  dockv::IntentTypeSet intent_types;

  // For private use by LockManager.
  typename LockManagerTraits<LockManager>::LockedBatchEntry* locked = nullptr;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(key, intent_types);
  }
};

// The following three arrays are indexed by the integer representation of the IntentTypeSet which
// the value at that index corresponds to. For example, an IntentTypeSet with the 0th and 2nd
// element present would be represented by the number = (2^0 + 2^2) = 5. The fifth index of an
// IntentTypeSetMap stores some value which corresponds to this IntentTypeSet.
// TODO -- clarify the semantics of IntentTypeSetMap by making it a class.
using IntentTypeSetMap = std::array<LockState, dockv::kIntentTypeSetMapSize>;

// We have 64 bits in LockState and 4 types of intents. So 16 bits is the max number of bits
// that we could reserve for a block of single intent type.
constexpr size_t kIntentTypeBits = 16;
// kFirstIntentTypeMask represents the LockState which, when &'d with another LockState, would
// result in the LockState tracking only the count for intent type represented by the region of bits
// that is the "first" or "least significant", as in furthest to the right.
constexpr LockState kFirstIntentTypeMask = (static_cast<LockState>(1) << kIntentTypeBits) - 1;

// Maps IntentTypeSet to a LockState mask which determines if another LockState will conflict with
// any of the elements present in the IntentTypeSet.
extern const IntentTypeSetMap kIntentTypeSetConflicts;

// Maps IntentTypeSet to the LockState representing one count for each intent type in the set. Can
// be used to "add one" occurence of an IntentTypeSet to an existing key's LockState.
extern const IntentTypeSetMap kIntentTypeSetAdd;

// Maps IntentTypeSet to the LockState representing max count for each intent type in the set. Can
// be used to extract a LockState corresponding to having only that set's elements counts present.
extern const IntentTypeSetMap kIntentTypeSetMask;

LockState IntentTypeMask(
    dockv::IntentType intent_type, LockState single_intent_mask = kFirstIntentTypeMask);

inline bool IntentTypesConflict(dockv::IntentType lhs, dockv::IntentType rhs) {
  auto lhs_value = std::to_underlying(lhs);
  auto rhs_value = std::to_underlying(rhs);
  // The rules are the following:
  // 1) At least one intent should be strong for conflict.
  // 2) Read and write conflict only with opposite type.
  return ((lhs_value & dockv::kStrongIntentFlag) || (rhs_value & dockv::kStrongIntentFlag)) &&
         ((lhs_value & dockv::kWriteIntentFlag) != (rhs_value & dockv::kWriteIntentFlag));
}

inline LockState IntentTypeSetAdd(dockv::IntentTypeSet intent_types) {
  return kIntentTypeSetAdd[intent_types.ToUIntPtr()];
}

inline LockState IntentTypeSetConflict(dockv::IntentTypeSet intent_types) {
  return kIntentTypeSetConflicts[intent_types.ToUIntPtr()];
}

bool IntentTypeSetsConflict(dockv::IntentTypeSet lhs, dockv::IntentTypeSet rhs);

[[nodiscard]] bool IntentTypeReadOnly(dockv::IntentTypeSet intents);

[[nodiscard]] size_t LockStateWriteIntentCount(LockState state);

std::string LockStateDebugString(LockState state);

bool LockStateContains(LockState existing_state, LockState add);

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

using LockTypeEntry = std::pair<dockv::KeyEntryType, dockv::IntentTypeSet>;

// We achieve the same table lock conflict matrix as that of pg documented here,
// https://www.postgresql.org/docs/current/explicit-locking.html#LOCKING-TABLES
//
// We only have 4 lock/intent modes kWeak/kStrong Read/Write, but to generate the above conflict
// matrix, we would need more lock types. Instead of introducing additional lock types, we use two
// KeyEntryType values and associate a list of <KeyEntryType, IntentTypeSet> to each table lock.
// Since our conflict detection mechanism checks conflicts against each key, we indirectly achieve
// the exact same conflict matrix. Refer comments on the function definition for more details.
std::span<const LockTypeEntry> GetEntriesForLockType(TableLockType lock);

// Returns DetermineKeysToLockResult<ObjectLockManager> which can further be passed to
// ObjectLockManager to acquire locks against the required objects with the given lock type.
Result<DetermineKeysToLockResult<ObjectLockManager>> DetermineObjectsToLock(
    const google::protobuf::RepeatedPtrField<ObjectLockPB>& objects_to_lock);

// Weak lock modes are typically taken on the prefixes of the key being locked/ written. But if
// skip_prefix_lock is enabled, for explicit row level locks or reads in serializable isolation
// level transactions which don't specify the full pk but a prefix of it, we need to take the strong
// lock modes on the top-level key to be able to lock all the PKs enclosed in the top-level key.
Result<bool> ShouldTakeWeakLockForPrefix(dockv::AncestorDocKey ancestor_doc_key,
                                         dockv::IsTopLevelKey is_top_level_key,
                                         dockv::SkipPrefixLocks skip_prefix_locks,
                                         IsolationLevel isolation_level,
                                         boost::tribool pk_is_known,
                                         const KeyBytes* const key);

} // namespace yb::docdb
