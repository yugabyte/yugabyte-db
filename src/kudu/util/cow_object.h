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
#ifndef KUDU_UTIL_COW_OBJECT_H
#define KUDU_UTIL_COW_OBJECT_H

#include <glog/logging.h>
#include <algorithm>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/util/rwc_lock.h"

namespace kudu {

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

  void ReadUnlock() const {
    lock_.ReadUnlock();
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
    return DCHECK_NOTNULL(dirty_state_.get());
  }

  const State& dirty() const {
    return *DCHECK_NOTNULL(dirty_state_.get());
  }

 private:
  mutable RWCLock lock_;

  State state_;
  gscoped_ptr<State> dirty_state_;

  DISALLOW_COPY_AND_ASSIGN(CowObject);
};

// A lock-guard-like scoped object to acquire the lock on a CowObject,
// and obtain a pointer to the correct copy to read/write.
//
// Example usage:
//
//   CowObject<Foo> my_obj;
//   {
//     CowLock<Foo> l(&my_obj, CowLock<Foo>::READ);
//     l.data().get_foo();
//     ...
//   }
//   {
//     CowLock<Foo> l(&my_obj, CowLock<Foo>::WRITE);
//     l->mutable_data()->set_foo(...);
//     ...
//     l.Commit();
//   }
template<class State>
class CowLock {
 public:
  enum LockMode {
    READ, WRITE, RELEASED
  };

  // Lock in either read or write mode.
  CowLock(CowObject<State>* cow,
          LockMode mode)
    : cow_(cow),
      mode_(mode) {
    if (mode == READ) {
      cow_->ReadLock();
    } else if (mode_ == WRITE) {
      cow_->StartMutation();
    } else {
      LOG(FATAL) << "Cannot lock in mode " << mode;
    }
  }

  // Lock in read mode.
  // A const object may not be locked in write mode.
  CowLock(const CowObject<State>* info,
          LockMode mode)
    : cow_(const_cast<CowObject<State>*>(info)),
      mode_(mode) {
    if (mode == READ) {
      cow_->ReadLock();
    } else if (mode_ == WRITE) {
      LOG(FATAL) << "Cannot write-lock a const pointer";
    } else {
      LOG(FATAL) << "Cannot lock in mode " << mode;
    }
  }

  // Commit the underlying object.
  // Requires that the caller hold the lock in write mode.
  void Commit() {
    DCHECK_EQ(WRITE, mode_);
    cow_->CommitMutation();
    mode_ = RELEASED;
  }

  void Unlock() {
    if (mode_ == READ) {
      cow_->ReadUnlock();
    } else if (mode_ == WRITE) {
      cow_->AbortMutation();
    } else {
      DCHECK_EQ(RELEASED, mode_);
    }
    mode_ = RELEASED;
  }

  // Obtain the underlying data. In WRITE mode, this returns the
  // same data as mutable_data() (not the safe unchanging copy).
  const State& data() const {
    if (mode_ == READ) {
      return cow_->state();
    } else if (mode_ == WRITE) {
      return cow_->dirty();
    } else {
      LOG(FATAL) << "Cannot access data after committing";
    }
  }

  // Obtain the mutable data. This may only be called in WRITE mode.
  State* mutable_data() {
    if (mode_ == READ) {
      LOG(FATAL) << "Cannot mutate data with READ lock";
    } else if (mode_ == WRITE) {
      return cow_->mutable_dirty();
    } else {
      LOG(FATAL) << "Cannot access data after committing";
    }
  }

  bool is_write_locked() const {
    return mode_ == WRITE;
  }

  // Drop the lock. If the lock is held in WRITE mode, and the
  // lock has not yet been released, aborts the mutation, restoring
  // the underlying object to its original data.
  ~CowLock() {
    Unlock();
  }

 private:
  CowObject<State>* cow_;
  LockMode mode_;
  DISALLOW_COPY_AND_ASSIGN(CowLock);
};

} // namespace kudu
#endif /* KUDU_UTIL_COW_OBJECT_H */
