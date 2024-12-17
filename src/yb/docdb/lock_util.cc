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

namespace yb::docdb {

namespace {

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
