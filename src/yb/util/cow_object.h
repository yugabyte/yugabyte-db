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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

namespace yb {

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
  void StartMutation() {
    lock_.WriteLock();
    // Clone our object.
    dirty_state_.reset(new State(state_));
  }

  // Abort the current mutation. This drops the write lock without applying any
  // changes made to the mutable copy.
  void AbortMutation() {
    dirty_state_.reset();
    is_dirty_ = false;
    lock_.WriteUnlock();
  }

  // Commit the current mutation. This escalates to the "Commit" lock, which
  // blocks any concurrent readers or writers, swaps in the new version of the
  // State, and then drops the commit lock.
  void CommitMutation() {
    lock_.UpgradeToCommitLock();
    CHECK(dirty_state_);
    std::swap(state_, *dirty_state_);
    dirty_state_.reset();
    is_dirty_ = false;
    lock_.CommitUnlock();
  }

  // Return the current state, not reflecting any in-progress mutations.
  State& state() {
    DCHECK(lock_.HasReaders() || lock_.HasWriteLock());
    return state_;
  }

  const State& state() const {
    DCHECK(lock_.HasReaders() || lock_.HasWriteLock());
    return state_;
  }

  // Returns the current dirty state (i.e reflecting in-progress mutations).
  // Should only be called by a thread who previously called StartMutation().
  State* mutable_dirty() {
    DCHECK(lock_.HasWriteLock());
    is_dirty_ = true;
    return DCHECK_NOTNULL(dirty_state_.get());
  }

  const State& dirty() const {
    return *DCHECK_NOTNULL(dirty_state_.get());
  }

  bool is_dirty() const {
    DCHECK(lock_.HasReaders() || lock_.HasWriteLock());
    return is_dirty_;
  }

  // Return true if the current thread holds the write lock.
  //
  // In DEBUG mode this is accurate -- we track the current holder's tid.
  // In non-DEBUG mode, this may sometimes return true even if another thread
  // is in fact the holder.
  // Thus, this is only really useful in the context of a DCHECK assertion.
  bool HasWriteLock() const { return lock_.HasWriteLock(); }

  void WriteLockThreadChanged() {
    lock_.WriteLockThreadChanged();
  }

 private:
  mutable RWCLock lock_;

  State state_;
  std::unique_ptr<State> dirty_state_;

  // Set only when mutable_dirty() method is called. Unset whenever dirty_state_ is reset().
  bool is_dirty_ = false;

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

  void ThreadChanged() {
    cow_->WriteLockThreadChanged();
  }

  ~CowWriteLock() {
    Unlock();
  }

 private:
  CowObject<State>* cow_;
};

} // namespace yb
