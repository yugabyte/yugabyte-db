// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_TABLET_LOCK_MANAGER_H
#define KUDU_TABLET_LOCK_MANAGER_H

#include "kudu/gutil/macros.h"
#include "kudu/gutil/move.h"
#include "kudu/util/slice.h"

namespace kudu { namespace tablet {

class LockManager;
class LockTable;
class LockEntry;
class TransactionState;

// Super-simple lock manager implementation. This only supports exclusive
// locks, and makes no attempt to prevent deadlocks if a single thread
// takes multiple locks.
//
// In the future when we want to support multi-row transactions of some kind
// we'll have to implement a proper lock manager with all its trappings,
// but this should be enough for the single-row use case.
class LockManager {
 public:
  LockManager();
  ~LockManager();

  enum LockStatus {
    LOCK_ACQUIRED = 0,
    LOCK_BUSY = 1,
  };

  enum LockMode {
    LOCK_EXCLUSIVE
  };

 private:
  friend class ScopedRowLock;
  friend class LockManagerTest;

  LockStatus Lock(const Slice& key, const TransactionState* tx,
                  LockMode mode, LockEntry **entry);
  LockStatus TryLock(const Slice& key, const TransactionState* tx,
                     LockMode mode, LockEntry **entry);
  void Release(LockEntry *lock, LockStatus ls);

  LockTable *locks_;

  DISALLOW_COPY_AND_ASSIGN(LockManager);
};


// Hold a lock on a given row, for the scope of this object.
// Usage:
//   {
//     ScopedRowLock(&manager, my_encoded_row_key, LOCK_EXCLUSIVE);
//     .. do stuff with the row ..
//   }
//   // lock is released when the object exits its scope.
//
// This class emulates C++11 move constructors and thus can be
// copied by using the special '.Pass()' function. For example:
//
// void DoSomething(ScopedRowLock l) {
//   // l owns the lock and will release at the end of this function
// }
// ScopedRowLock my_lock(&manager, ...);
// DoSomething(l.Pass());
// CHECK(!l.acquired()); // doesn't own lock anymore, since it Pass()ed
class ScopedRowLock {
  MOVE_ONLY_TYPE_FOR_CPP_03(ScopedRowLock, RValue);
 public:

  // Construct an initially-unlocked lock holder.
  // You can later assign this to actually hold a lock using
  // the emulated move-constructor:
  //   ScopedRowLock l;
  //   l = ScopedRowLock(...); // use the ctor below
  // or
  //   l = other_row_lock.Pass();
  ScopedRowLock()
    : manager_(NULL),
      acquired_(false),
      entry_(NULL) {
  }

  // Lock row in the given LockManager. The 'key' slice must remain
  // valid and un-changed for the duration of this object's lifetime.
  ScopedRowLock(LockManager *manager, const TransactionState* ctx,
                const Slice &key, LockManager::LockMode mode);

  // Emulated Move constructor
  ScopedRowLock(RValue other); // NOLINT(runtime/explicit)
  ScopedRowLock& operator=(RValue other);

  void Release();

  bool acquired() const { return acquired_; }

  LockManager::LockStatus GetLockStatusForTests() { return ls_; }

  ~ScopedRowLock();

 private:
  void TakeState(ScopedRowLock* other);

  LockManager *manager_;

  bool acquired_;
  LockEntry *entry_;
  LockManager::LockStatus ls_;
};

} // namespace tablet
} // namespace kudu
#endif
