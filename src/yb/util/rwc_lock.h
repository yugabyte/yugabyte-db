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

#include <condition_variable>

#include "yb/gutil/macros.h"

#include "yb/util/mutex.h"

// Enable mechanism to detect deadlocks with mutex that does not belong to RWCLock.
// For instance it could detect the following scenario:
// T1:
//   acquire read lock on RWCLock
//   acquire read lock on some external mutex
// T2:
//   acquire write lock on the same external mutex
//   acquire write and then commit lock on the same RWCLock
#define RWC_LOCK_TRACK_EXTERNAL_DEADLOCK 0

namespace yb {

// A read-write-commit lock.
//
// This lock has three modes: read, write, and commit.
// The lock compatibility matrix is as follows:
//
//           Read    Write    Commit
//  Read      X        X
//  Write     X
//  Commit
//
// An 'X' indicates that the two types of locks may be
// held at the same time.
//
// In prose:
// - Multiple threads may hold the Read lock at the same time.
// - A single thread may hold the Write lock, potentially at the
//   same time as any number of readers.
// - A single thread may hold the Commit lock, but this lock is completely
//   exclusive (no concurrent readers or writers).
//
// A typical use case for this type of lock is when a structure is read often,
// occasionally updated, and the update operation can take a long time. In this
// use case, the readers simply use ReadLock() and ReadUnlock(), while the
// writer uses a copy-on-write technique like:
//
//   obj->lock.WriteLock();
//   // NOTE: cannot safely mutate obj->state directly here, since readers
//   // may be concurrent! So, we make a local copy to mutate.
//   my_local_copy = obj->state;
//   SomeLengthyMutation(my_local_copy);
//   obj->lock.UpgradeToCommitLock();
//   obj->state = my_local_copy;
//   obj->lock.CommitUnlock();
//
// This is more efficient than a standard Reader-Writer lock since the lengthy
// mutation is only protected against other concurrent mutators, and readers
// may continue to run with no contention.
//
// For the common pattern described above, the 'CowObject<>' template class defined
// in cow_object.h is more convenient than manual locking.
//
// NOTE: this implementation currently does not implement any starvation protection
// or fairness. If the read lock is being constantly acquired (i.e., reader count
// never drops to 0) then UpgradeToCommitLock() may block arbitrarily long.
class RWCLock {
 public:
  RWCLock() = default;
  ~RWCLock();

#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
  static void CheckNoReadLockConflict(void* mutex);
  static void SetConflictingMutex(void* mutex);
#else
  static void CheckNoReadLockConflict(void* mutex) {}
  static void SetConflictingMutex(void* mutex) {}
#endif

  // Acquire the lock in read mode. Upon return, guarantees that:
  // - Other threads may concurrently hold the lock for Read.
  // - Either zero or one thread may hold the lock for Write.
  // - No threads hold the lock for Commit.
  void ReadLock();
  void ReadUnlock();

  // Return true if there are any readers currently holding the lock.
  // Useful for debug assertions.
  bool HasReaders() const;

  // [DEBUG mode only] Return true iff the current thread holds the write or commit lock.
  bool DEBUG_HasWriteLock() const;

  // Boost-like wrappers, so boost lock guards work
  void lock_shared() { ReadLock(); }
  void unlock_shared() { ReadUnlock(); }

  // Acquire the lock in write mode. Upon return, guarantees that:
  // - Other threads may concurrently hold the lock for Read.
  // - No other threads hold the lock for Write or Commit.
  void WriteLock() ACQUIRE(write_mutex_);
  void WriteUnlock() RELEASE(write_mutex_);

  // Boost-like wrappers
  void lock() ACQUIRE(write_mutex_) { WriteLock(); }
  void unlock() RELEASE(write_mutex_) { WriteUnlock(); }

  // Upgrade the lock from Write mode to Commit mode.
  // Requires that the current thread holds the lock in Write mode.
  // Upon return, guarantees:
  // - No other thread holds the lock in any mode.
  void UpgradeToCommitLock() ACQUIRE();
  void CommitUnlock() RELEASE();

 private:
  // The thread (there can be only one) that holds this COW lock in write or commit mode holds this
  // mutex.
  //
  // Thread sanitizer does not understand timed_mutex and cannot catch related issues.
  // So use std::mutex with it.
#if defined(THREAD_SANITIZER)
  mutable std::mutex write_mutex_;
#else
  mutable std::timed_mutex write_mutex_;
#endif

  std::mutex commit_mutex_;
  // Lower bits hold number of readers;
  // high bits (kCommitActive, kCommitPending) hold information about acquiring/holding commit mode.
  // High bit changes require holding commit_mutex_.
  std::atomic<size_t> reader_counter_{0};
  std::condition_variable no_readers_; // Always hold commit_mutex_ while using this.
  std::condition_variable no_committer_; // Always hold commit_mutex_ while using this.

  //------------------------------------------------------------------------------------------------
  // Information stored about the last holder of the write lock.

  mutable std::mutex write_lock_holder_info_mutex_;

  // Always available.
  CoarseTimePoint write_acquire_time_ GUARDED_BY(write_mutex_);
  // Available only in DEBUG mode or if FLAGS_enable_rwc_lock_debugging is true..
  int64_t write_lock_holder_thread_id_ GUARDED_BY(write_lock_holder_info_mutex_) {-1};
  // Available only if FLAGS_enable_rwc_lock_debugging is true.
  // This will be the stack trace at the time the write lock was taken.
  std::string write_lock_holder_stack_trace_ GUARDED_BY(write_lock_holder_info_mutex_);

  DISALLOW_COPY_AND_ASSIGN(RWCLock);
};

} // namespace yb
