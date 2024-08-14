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
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/intent.h"

#include "yb/util/monotime.h"
#include "yb/util/ref_cnt_buffer.h"

namespace yb::docdb {

// Helper struct used for keying table/object locks of a session.
template <typename T>
struct TrackedLockEntryKey {
  TrackedLockEntryKey(const SessionIDHostPair& session_id_pair_, T object_id_)
      : session_id_pair(session_id_pair_), object_id(object_id_) {}

  const SessionIDHostPair session_id_pair;
  const T object_id;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(session_id_pair, object_id);
  }
};

template <typename T>
bool operator==(const TrackedLockEntryKey<T>& lhs, const TrackedLockEntryKey<T>& rhs) {
  return YB_STRUCT_EQUALS(session_id_pair, object_id);
}

template <typename T>
inline size_t hash_value(const TrackedLockEntryKey<T>& key) noexcept {
  size_t seed = 0;
  boost::hash_combine(seed, key.session_id_pair);
  boost::hash_combine(seed, key.object_id);
  return seed;
}

template <typename T>
struct LockedBatchEntry;

// TrackedLockEntry is used to keep track of the LockState of the session for a given key. Note
// that a session can acquire multiple lock types repeatedly on a key.
//
// In context of object/table locks, when handling release requests by SessionIDHostPair
// (optionally with object id supplied), the LockState value is used to reset the info of the
// corresponding LockedBatchEntry.
template <typename T>
struct TrackedLockEntry {
  TrackedLockEntry(
      const SessionIDHostPair& session_id_pair_, T object_id_, LockState state_,
      LockedBatchEntry<T>* locked_batch_entry_) : key(session_id_pair_, object_id_), state(state_),
      locked_batch_entry(locked_batch_entry_) {}

  SessionIDHostPair session_id_pair() const {
    return key.session_id_pair;
  }

  T object_id() const {
    return key.object_id;
  }

  // Key against which this TrackedLockEntry is tracked/stored.
  TrackedLockEntryKey<T> key;
  LockState state;
  // LockedBatchEntry<T> object's memory is managed by LockManagerImpl<T>.
  LockedBatchEntry<T>* locked_batch_entry;
  size_t ref_count = 1;
};

template <typename T>
class LockManagerImpl;

// This class manages locks on keys of type RefCntPrefix. On each key, the possibilities arise
// from a combination of kWeak/kStrong Read/Write intent types.
//
// Every tablet maintains its own SharedLockManager and uses it to acquire required in-memory locks
// for the scope of the read/write request being served.
class SharedLockManager {
 public:
  SharedLockManager();
  ~SharedLockManager();

  // Attempt to lock a batch of keys. The call may be blocked waiting for other locks to be
  // released. If the entries don't exist, they are created. The lock batch gets associated with
  // this lock manager, which makes it auto-unlock on destruction.
  //
  // Returns false if was not able to acquire lock until deadline.
  MUST_USE_RESULT bool Lock(
      LockBatchEntries<RefCntPrefix>* key_to_intent_type, CoarseTimePoint deadline);

  // Release the batch of locks. Requires that the locks are held.
  void Unlock(const LockBatchEntries<RefCntPrefix>& key_to_intent_type);

  void DumpStatusHtml(std::ostream& out);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// This class manages locks on keys of type ObjectLockPrefix. On each key, the possibilities arise
// from a combination of kWeak/kStrong Read/Write intent types.
//
// Currently, this class is only being used for object/table level locks codepath. Each tablet
// server maintains an instance of the ObjectLockManager.
class ObjectLockManager {
 public:
  ObjectLockManager();
  ~ObjectLockManager();

  // Attempt to lock a batch of keys and track the lock against the given session_id_pair key. The
  // call may be blocked waiting for other conflicting locks to be released. If the entries don't
  // exist, they are created. On success, the lock state is exists in-memory until an explicit
  // release is called (or the process restarts).
  //
  // Returns false if was not able to acquire lock until deadline.
  MUST_USE_RESULT bool Lock(
      const SessionIDHostPair& session_id_pair,
      LockBatchEntries<ObjectLockPrefix>* key_to_intent_type, CoarseTimePoint deadline);

  // Release the batch of locks, if they were acquired at the first place.
  void Unlock(const std::vector<TrackedLockEntryKey<ObjectLockPrefix>>& lock_entry_keys);

  // Release all locks held against the given SessionIDHostPair.
  void Unlock(const SessionIDHostPair& session_id_pair);

  void DumpStatusHtml(std::ostream& out);

  size_t TEST_GrantedLocksSize() const;
  size_t TEST_WaitingLocksSize() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Masks of intent type sets.
// I.e. bits that related to any of intents from this set is filled with 1, others are 0.
extern const std::array<LockState, dockv::kIntentTypeSetMapSize> kIntentTypeSetMask;
// Conflicts of intent types. I.e. combination of masks of intent type sets that conflict with it.
extern const std::array<LockState, dockv::kIntentTypeSetMapSize> kIntentTypeSetConflicts;

bool IntentTypeSetsConflict(dockv::IntentTypeSet lhs, dockv::IntentTypeSet rhs);

}  // namespace yb::docdb
