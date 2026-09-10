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
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <fcntl.h>

#include <algorithm>

#include "yb/gutil/macros.h"

#include "yb/util/fault_injection.h"
#include "yb/util/logging.h"
#include "yb/util/rwc_lock.h"
#include "yb/util/status.h"

namespace yb {

// Deadlock-detection support for the table<->tablet commit-order rule (see #10304 and the
// heartbeat-contention design notes). Committing a COW object calls UpgradeToCommitLock, which
// blocks on readers; if we commit a table while still holding a tablet write lock, a thread holding
// a table read lock and waiting for that tablet write lock deadlocks us. So: never commit a table
// while holding any tablet write lock. CowObject enforces this via the Pre/PostMutation hooks
// below, which are no-ops by default and specialized for the table/tablet State types
// (see catalog_entity_info.h).

inline int& MutableHeldTabletWriteLockCount() {
  static thread_local int count = 0;
  return count;
}

// TODO(#10304): temporary hack. Lets call sites (e.g. CreateTable) suppress the table commit-order
// assert below; they commit a published table while holding brand-new tablet write locks, which is
// safe since new tablets have no reader. Replace with a held-published-tablet check.
inline int& MutableTableCommitAssertSuppressionDepth() {
  static thread_local int depth = 0;
  return depth;
}

class TableCommitLockAssertSuppression {
 public:
  TableCommitLockAssertSuppression() { ++MutableTableCommitAssertSuppressionDepth(); }
  ~TableCommitLockAssertSuppression() { --MutableTableCommitAssertSuppressionDepth(); }
  TableCommitLockAssertSuppression(const TableCommitLockAssertSuppression&) = delete;
  void operator=(const TableCommitLockAssertSuppression&) = delete;
};

// An object which manages its state via copy-on-write.
//
// Access to this object can be done more conveniently using the
// CowLock template class defined below.
//
// The 'State' template parameter must be swappable using std::swap.
template<class State>
class CowObject {
 public:
  CowObject() {}
  ~CowObject() {}

  void ReadLock() const {
    lock_.ReadLock();
  }

  // Timed ReadLock: returns false without acquiring if not taken by `deadline`.
  bool ReadLock(CoarseTimePoint deadline) const {
    return lock_.ReadLock(deadline);
  }

  void lock_shared() const {
    ReadLock();
  }

  void ReadUnlock() const {
    lock_.ReadUnlock();
  }

  void unlock_shared() const {
    ReadUnlock();
  }

  // Lock the object for write (preventing concurrent mutators), and make a safe
  // copy of the object to mutate.
  void StartMutation() NO_THREAD_SAFETY_ANALYSIS {
    StartMutation(CoarseTimePoint::max());
  }


  // Returns false if locks cannot be acquired by deadline
  bool StartMutation(CoarseTimePoint deadline) NO_THREAD_SAFETY_ANALYSIS {
    if (!lock_.WriteLock(deadline)) {
      return false;
    }
    // Clone our object.
    dirty_state_.reset(new State(state_));
    PostStartMutation();
    return true;
  }

  // Abort the current mutation. This drops the write lock without applying any
  // changes made to the mutable copy.
  void AbortMutation() NO_THREAD_SAFETY_ANALYSIS {
    dirty_state_.reset();
    is_dirty_ = false;
    lock_.WriteUnlock();
    PostAbortMutation();
  }

  // Commit the current mutation. This escalates to the "Commit" lock, which
  // blocks any concurrent readers or writers, swaps in the new version of the
  // State, and then drops the commit lock.
  void CommitMutation() {
    PreCommitMutation();
    lock_.UpgradeToCommitLock();
    CHECK(dirty_state_);
    std::swap(state_, *dirty_state_);
    dirty_state_.reset();
    is_dirty_ = false;
    lock_.CommitUnlock();
    PostCommitMutation();
  }

  // Exclude this object's write lock from the per-thread held-tablet-write-lock total used by the
  // table commit-order assert. Used for the sys-catalog tablet (see CommitMutation / TabletInfo).
  void SetExcludeFromHeldTabletWriteLockCount() { exclude_from_held_tablet_count_ = true; }

  // Return the current state, not reflecting any in-progress mutations.
  const State& state() const {
    DCHECK(lock_.HasReaders() || lock_.DEBUG_HasWriteLock());
    return state_;
  }

  // Returns the current dirty state (i.e reflecting in-progress mutations).
  // Should only be called by a thread who previously called StartMutation().
  State* mutable_dirty() {
    DCHECK(lock_.DEBUG_HasWriteLock());
    is_dirty_ = true;
    return CHECK_NOTNULL(dirty_state_.get());
  }

  const State& dirty() const { return *CHECK_NOTNULL(dirty_state_.get()); }

  bool is_dirty() const {
    DCHECK(lock_.HasReaders() || lock_.DEBUG_HasWriteLock());
    return is_dirty_;
  }

  // [DEBUG mode only] Return true iff the current thread holds the write or commit lock.
  bool DEBUG_HasWriteLock() const { return lock_.DEBUG_HasWriteLock(); }

  // Should be invoked only from ctor of appropriate object.
  State& DirectStateForInitialSetup() {
    DCHECK(!lock_.HasReaders() && !lock_.DEBUG_HasWriteLock());
    return state_;
  }

 private:
  // Hooks for the table<->tablet commit-order rule (#10304). No-ops by default; explicitly
  // specialized for the table/tablet State types in catalog_entity_info.h -- the table asserts no
  // tablet write locks are held in PreCommitMutation, the tablet maintains the per-thread
  // held-write-lock count in the Post hooks.
  void PreCommitMutation() {}
  void PostStartMutation() {}
  void PostAbortMutation() {}
  void PostCommitMutation() {}

  mutable RWCLock lock_;

  State state_;
  std::unique_ptr<State> dirty_state_;

  // Set only when mutable_dirty() method is called. Unset whenever dirty_state_ is reset().
  bool is_dirty_ = false;

  // When true, this object's write lock is not counted toward the per-thread held-tablet-write-lock
  // total. Set for the sys-catalog tablet, which heartbeats never process and so cannot deadlock.
  bool exclude_from_held_tablet_count_ = false;

  DISALLOW_COPY_AND_ASSIGN(CowObject);
};

// A lock-guard-like scoped object to acquire the lock on a CowObject,
// and obtain a pointer to the correct copy to read.
//
// Example usage:
//
//   CowObject<Foo> my_obj;
//   {
//     CowReadLock<Foo> l(&my_obj);
//     l.data().get_foo();
//     ...
//   }
template<class State>
class CowReadLock {
 public:
  CowReadLock() : cow_(nullptr) {}

  explicit CowReadLock(const CowObject<State>* cow)
    : cow_(cow) {
    cow_->ReadLock();
  }

  CowReadLock(const CowObject<State>* cow, CoarseTimePoint deadline) : cow_(nullptr) {
    if (cow->ReadLock(deadline)) {
      cow_ = cow;
    }
  }

  CowReadLock(const CowReadLock&) = delete;
  void operator=(const CowReadLock&) = delete;

  CowReadLock(CowReadLock&& rhs) noexcept
      : cow_(rhs.cow_) {
    rhs.cow_ = nullptr;
  }

  void operator=(CowReadLock&& rhs) noexcept {
    Unlock();
    cow_ = rhs.cow_;
    rhs.cow_ = nullptr;
  }

  void Unlock() {
    if (cow_) {
      cow_->ReadUnlock();
      cow_ = nullptr;
    }
  }

  const State& data() const {
    return cow_->state();
  }

  const State* operator->() const {
    return &data();
  }

  bool locked() const {
    return cow_ != nullptr;
  }

  ~CowReadLock() {
    Unlock();
  }

 private:
  const CowObject<State>* cow_;
};

// A lock-guard-like scoped object to acquire the lock on a CowObject,
// and obtain a pointer to the correct copy to write.
//
// Example usage:
//
//   CowObject<Foo> my_obj;
//   {
//     CowWriteLock<Foo> l(&my_obj);
//     l.mutable_data()->set_foo(...);
//     ...
//     l.Commit();
//   }
template<class State>
class CowWriteLock {
 public:
  CowWriteLock() : cow_(nullptr) {}

  explicit CowWriteLock(CowObject<State>* cow)
    : cow_(cow) {
    cow_->StartMutation();
  }

  CowWriteLock(CowObject<State>* cow, CoarseTimePoint deadline) : cow_(nullptr) {
    if (cow->StartMutation(deadline)) {
      cow_ = cow;
    }
  }

  CowWriteLock(const CowWriteLock&) = delete;
  void operator=(const CowWriteLock&) = delete;

  CowWriteLock(CowWriteLock&& rhs) noexcept
      : cow_(rhs.cow_) {
    rhs.cow_ = nullptr;
  }

  void operator=(CowWriteLock&& rhs) noexcept {
    Unlock();
    cow_ = rhs.cow_;
    rhs.cow_ = nullptr;
  }

  // Commit the underlying object.
  // Requires that the caller hold the lock.
  void Commit() {
    cow_->CommitMutation();
    cow_ = nullptr;
  }

  void CommitOrWarn(const Status& status, const char* action) {
    if (!status.ok()) {
      LOG(WARNING) << "An error occurred while " << action << ": " << status;
      return;
    }
    Commit();
  }

  void Unlock() {
    if (cow_) {
      cow_->AbortMutation();
      cow_ = nullptr;
    }
  }

  // Obtain the underlying data.
  // Returns the same data as mutable_data() (not the safe unchanging copy).
  const State& data() const {
    return cow_->dirty();
  }

  const State* operator->() const {
    return &data();
  }

  // Obtain the mutable data.
  State* mutable_data() const {
    return cow_->mutable_dirty();
  }

  bool is_dirty() const {
    return cow_->is_dirty();
  }

  bool locked() const {
    return cow_ != nullptr;
  }

  ~CowWriteLock() {
    Unlock();
  }

 private:
  CowObject<State>* cow_;
};

} // namespace yb
