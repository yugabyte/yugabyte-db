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

#include "yb/docdb/lock_util.h"

#include <type_traits>

namespace yb::docdb {

using dockv::IntentTypeSet;

bool IntentTypesConflict(dockv::IntentType lhs, dockv::IntentType rhs) {
  auto lhs_value = std::to_underlying(lhs);
  auto rhs_value = std::to_underlying(rhs);
  // The rules are the following:
  // 1) At least one intent should be strong for conflict.
  // 2) Read and write conflict only with opposite type.
  return ((lhs_value & dockv::kStrongIntentFlag) || (rhs_value & dockv::kStrongIntentFlag)) &&
         ((lhs_value & dockv::kWriteIntentFlag) != (rhs_value & dockv::kWriteIntentFlag));
}

LockState IntentTypeMask(dockv::IntentType intent_type, LockState single_intent_mask) {
  return single_intent_mask << (std::to_underlying(intent_type) * kIntentTypeBits);
}

namespace {

// Generate conflict mask for all possible subsets of intent type set. The i-th index of the
// returned array represents a conflict mask for the i-th possible IntentTypeSet. To determine if a
// given IntentTypeSet i conflicts with the key's existing LockState l, you can do the following:
// bool is_conflicting = kIntentTypeSetConflicts[i.ToUintPtr()] & l != 0;
std::array<LockState, dockv::kIntentTypeSetMapSize> GenerateConflicts() {
  std::array<LockState, dockv::kIntentTypeSetMapSize> result;
  for (size_t idx = 0; idx < dockv::kIntentTypeSetMapSize; ++idx) {
    result[idx] = 0;
    for (auto intent_type : IntentTypeSet(idx)) {
      for (auto other_intent_type : dockv::IntentTypeList()) {
        if (IntentTypesConflict(intent_type, other_intent_type)) {
          result[idx] |= IntentTypeMask(other_intent_type);
        }
      }
    }
  }
  return result;
}

// Generate array of LockState's with one entry for each possible subset of intent type set, where
// each intent type has the provided count.
std::array<LockState, dockv::kIntentTypeSetMapSize> GenerateLockStatesWithCount(uint64_t count) {
  DCHECK_EQ(count & kFirstIntentTypeMask, count);
  std::array<LockState, dockv::kIntentTypeSetMapSize> result;
  for (size_t idx = 0; idx != dockv::kIntentTypeSetMapSize; ++idx) {
    result[idx] = 0;
    for (auto intent_type : IntentTypeSet(idx)) {
      result[idx] |= IntentTypeMask(intent_type, count);
    }
  }
  return result;
}

uint16_t LockStateIntentCount(LockState num_waiting, dockv::IntentType intent_type) {
  return (num_waiting >> (std::to_underlying(intent_type) * kIntentTypeBits))
      & kFirstIntentTypeMask;
}

Status FormSharedLock(
    ObjectLockPrefix&& key, dockv::IntentTypeSet intent_types,
    LockBatchEntries<ObjectLockManager>* keys_locked) {
  SCHECK(!intent_types.None(), InternalError, "Empty intent types is not allowed");
  keys_locked->push_back(
      LockBatchEntry<ObjectLockManager> {.key = std::move(key), .intent_types = intent_types});
  return Status::OK();
}

Status AddObjectsToLock(
    LockBatchEntries<ObjectLockManager>& lock_batch, uint64_t database_oid, uint64_t object_oid,
    TableLockType lock_type) {
  for (const auto& [lock_key, intent_types] : GetEntriesForLockType(lock_type)) {
    RETURN_NOT_OK(FormSharedLock(
        ObjectLockPrefix(database_oid, object_oid, lock_key), intent_types, &lock_batch));
  }
  return Status::OK();
}

} // namespace

// Maps IntentTypeSet to a LockState mask which determines if another LockState will conflict with
// any of the elements present in the IntentTypeSet.
const IntentTypeSetMap kIntentTypeSetConflicts = GenerateConflicts();

// Maps IntentTypeSet to the LockState representing one count for each intent type in the set. Can
// be used to "add one" occurence of an IntentTypeSet to an existing key's LockState.
const IntentTypeSetMap kIntentTypeSetAdd = GenerateLockStatesWithCount(1);

// Maps IntentTypeSet to the LockState representing max count for each intent type in the set. Can
// be used to extract a LockState corresponding to having only that set's elements counts present.
const IntentTypeSetMap kIntentTypeSetMask = GenerateLockStatesWithCount(kFirstIntentTypeMask);

bool IntentTypeSetsConflict(IntentTypeSet lhs, IntentTypeSet rhs) {
  for (auto intent1 : lhs) {
    for (auto intent2 : rhs) {
      if (IntentTypesConflict(intent1, intent2)) {
        return true;
      }
    }
  }
  return false;
}

std::string LockStateDebugString(LockState state) {
  return Format(
      "{ num_weak_read: $0 num_weak_write: $1 num_strong_read: $2 num_strong_write: $3 }",
      LockStateIntentCount(state, dockv::IntentType::kWeakRead),
      LockStateIntentCount(state, dockv::IntentType::kWeakWrite),
      LockStateIntentCount(state, dockv::IntentType::kStrongRead),
      LockStateIntentCount(state, dockv::IntentType::kStrongWrite));
}

// We associate a list of <KeyEntryType, IntentTypeSet> to each table lock type such that the
// table lock conflict matrix of postgres is preserved.
//
// For instance, let's consider 'ROW_SHARE' and 'EXCLUSIVE' lock modes.
// 1. 'ROW_SHARE' lock mode on object would lead to the following keys
//    [<object/object hash/other prefix> kWeakObjectLock]   [kStrongRead]
// 2. 'EXCLUSIVE' lock mode on the same object would lead to the following keys
//    [<object/object hash/other prefix> kWeakObjectLock]   [kWeakWrite]
//    [<object/object hash/other prefix> kStrongObjectLock] [kStrongRead, kStrongWrite]
//
// When checking conflicts for the same key, '[<object/object hash/other prefix> kWeakObjectLock]'
// in this case, we see that the intents requested are [kStrongRead] and [kWeakWrite] for modes
// 'ROW_SHARE' and 'EXCLUSIVE' respectively. And since the above intenttype sets conflict among
// themselves, we successfully detect the conflict.
const std::vector<std::pair<KeyEntryType, dockv::IntentTypeSet>>& GetEntriesForLockType(
    TableLockType lock) {
  static const std::array<
      std::vector<std::pair<KeyEntryType, dockv::IntentTypeSet>>,
      TableLockType_ARRAYSIZE> lock_entries = {{
    // NONE
    {{}},
    // ACCESS_SHARE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kWeakRead}}
    }},
    // ROW_SHARE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kStrongRead}}
    }},
    // ROW_EXCLUSIVE
    {{
      {KeyEntryType::kStrongObjectLock, dockv::IntentTypeSet {dockv::IntentType::kWeakRead}}
    }},
    // SHARE_UPDATE_EXCLUSIVE
    {{
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kStrongRead, dockv::IntentType::kWeakWrite}
      }
    }},
    // SHARE
    {{
      {KeyEntryType::kStrongObjectLock, dockv::IntentTypeSet {dockv::IntentType::kStrongWrite}}
    }},
    // SHARE_ROW_EXCLUSIVE
    {{
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kWeakRead, dockv::IntentType::kStrongWrite}
      }
    }},
    // EXCLUSIVE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kWeakWrite}},
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kStrongRead, dockv::IntentType::kStrongWrite}
      }
    }},
    // ACCESS_EXCLUSIVE
    {{
      {KeyEntryType::kWeakObjectLock, dockv::IntentTypeSet {dockv::IntentType::kStrongWrite}},
      {
        KeyEntryType::kStrongObjectLock,
        dockv::IntentTypeSet {dockv::IntentType::kStrongRead, dockv::IntentType::kStrongWrite}
      }
    }}
  }};
  return lock_entries[lock];
}

// Returns a DetermineKeysToLockResult object with its lock_batch containing a list of entries with
// 'key' as <object id, KeyEntry> and 'intent_types' set.
Result<DetermineKeysToLockResult<ObjectLockManager>> DetermineObjectsToLock(
    const google::protobuf::RepeatedPtrField<ObjectLockPB>& objects_to_lock) {
  DetermineKeysToLockResult<ObjectLockManager> result;
  for (const auto& object_lock : objects_to_lock) {
    SCHECK(object_lock.has_object_oid(), IllegalState, "ObjectLockPB has empty object oid");
    SCHECK(object_lock.has_database_oid(), IllegalState, "ObjectLockPB has empty database oid");
    RETURN_NOT_OK(AddObjectsToLock(result.lock_batch, object_lock.database_oid(),
                                   object_lock.object_oid(), object_lock.lock_type()));
  }
  FilterKeysToLock<ObjectLockManager>(&result.lock_batch);
  return result;
}

} // namespace yb::docdb
