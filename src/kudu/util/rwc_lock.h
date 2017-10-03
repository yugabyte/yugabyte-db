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
#ifndef KUDU_UTIL_RWC_LOCK_H
#define KUDU_UTIL_RWC_LOCK_H

#include "kudu/gutil/macros.h"
#include "kudu/util/condition_variable.h"
#include "kudu/util/mutex.h"

namespace kudu {

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
// or fairness. If the read lock is being constantly acquired (i.e reader count
// never drops to 0) then UpgradeToCommitLock() may block arbitrarily long.
class RWCLock {
 public:
  RWCLock();
  ~RWCLock();

  // Acquire the lock in read mode. Upon return, guarantees that:
  // - Other threads may concurrently hold the lock for Read.
  // - Either zero or one thread may hold the lock for Write.
  // - No threads hold the lock for Commit.
  void ReadLock();
  void ReadUnlock();

  // Return true if there are any readers currently holding the lock.
  // Useful for debug assertions.
  bool HasReaders() const;

  // Return true if the current thread holds the write lock.
  //
  // In DEBUG mode this is accurate -- we track the current holder's tid.
  // In non-DEBUG mode, this may sometimes return true even if another thread
  // is in fact the holder.
  // Thus, this is only really useful in the context of a DCHECK assertion.
  bool HasWriteLock() const;

  // Boost-like wrappers, so boost lock guards work
  void lock_shared() { ReadLock(); }
  void unlock_shared() { ReadUnlock(); }

  // Acquire the lock in write mode. Upon return, guarantees that:
  // - Other threads may concurrently hold the lock for Read.
  // - No other threads hold the lock for Write or Commit.
  void WriteLock();
  void WriteUnlock();

  // Boost-like wrappers
  void lock() { WriteLock(); }
  void unlock() { WriteUnlock(); }

  // Upgrade the lock from Write mode to Commit mode.
  // Requires that the current thread holds the lock in Write mode.
  // Upon return, guarantees:
  // - No other thread holds the lock in any mode.
  void UpgradeToCommitLock();
  void CommitUnlock();

 private:
  // Lock which protects reader_count_ and write_locked_.
  // Additionally, while the commit lock is held, the
  // locking thread holds this mutex, which prevents any new
  // threads from obtaining the lock in any mode.
  mutable Mutex lock_;
  ConditionVariable no_mutators_, no_readers_;
  int reader_count_;
  bool write_locked_;

#ifndef NDEBUG
  static const int kBacktraceBufSize = 1024;
  int64_t last_writer_tid_;
  int64_t last_writelock_acquire_time_;
  char last_writer_backtrace_[kBacktraceBufSize];
#endif // NDEBUG

  DISALLOW_COPY_AND_ASSIGN(RWCLock);
};

} // namespace kudu
#endif /* KUDU_UTIL_RWC_LOCK_H */
