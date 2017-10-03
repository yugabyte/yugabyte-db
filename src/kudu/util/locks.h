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
#ifndef KUDU_UTIL_LOCKS_H
#define KUDU_UTIL_LOCKS_H

#include <algorithm>
#include <glog/logging.h>

#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/dynamic_annotations.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#include "kudu/gutil/spinlock.h"
#include "kudu/gutil/sysinfo.h"
#include "kudu/util/errno.h"
#include "kudu/util/rw_semaphore.h"

namespace kudu {

using base::subtle::Acquire_CompareAndSwap;
using base::subtle::NoBarrier_Load;
using base::subtle::Release_Store;

// Wrapper around the Google SpinLock class to adapt it to the method names
// expected by Boost.
class simple_spinlock {
 public:
  simple_spinlock() {}

  void lock() {
    l_.Lock();
  }

  void unlock() {
    l_.Unlock();
  }

  bool try_lock() {
    return l_.TryLock();
  }

  // Return whether the lock is currently held.
  //
  // This state can change at any instant, so this is only really useful
  // for assertions where you expect to hold the lock. The success of
  // such an assertion isn't a guarantee that the current thread is the
  // holder, but the failure of such an assertion _is_ a guarantee that
  // the current thread is _not_ holding the lock!
  bool is_locked() {
    return l_.IsHeld();
  }

 private:
  base::SpinLock l_;

  DISALLOW_COPY_AND_ASSIGN(simple_spinlock);
};

struct padded_spinlock : public simple_spinlock {
  char padding[CACHELINE_SIZE - (sizeof(simple_spinlock) % CACHELINE_SIZE)];
};

// Reader-writer lock.
// This is functionally equivalent to rw_semaphore in rw_semaphore.h, but should be
// used whenever the lock is expected to only be acquired on a single thread.
// It adds TSAN annotations which will detect misuse of the lock, but those
// annotations also assume that the same thread the takes the lock will unlock it.
//
// See rw_semaphore.h for documentation on the individual methods where unclear.
class rw_spinlock {
 public:
  rw_spinlock() {
    ANNOTATE_RWLOCK_CREATE(this);
  }
  ~rw_spinlock() {
    ANNOTATE_RWLOCK_DESTROY(this);
  }

  void lock_shared() {
    sem_.lock_shared();
    ANNOTATE_RWLOCK_ACQUIRED(this, 0);
  }

  void unlock_shared() {
    ANNOTATE_RWLOCK_RELEASED(this, 0);
    sem_.unlock_shared();
  }

  bool try_lock() {
    bool ret = sem_.try_lock();
    if (ret) {
      ANNOTATE_RWLOCK_ACQUIRED(this, 1);
    }
    return ret;
  }

  void lock() {
    sem_.lock();
    ANNOTATE_RWLOCK_ACQUIRED(this, 1);
  }

  void unlock() {
    ANNOTATE_RWLOCK_RELEASED(this, 1);
    sem_.unlock();
  }

  bool is_write_locked() const {
    return sem_.is_write_locked();
  }

  bool is_locked() const {
    return sem_.is_locked();
  }

 private:
  rw_semaphore sem_;
};

// A reader-writer lock implementation which is biased for use cases where
// the write lock is taken infrequently, but the read lock is used often.
//
// Internally, this creates N underlying mutexes, one per CPU. When a thread
// wants to lock in read (shared) mode, it locks only its own CPU's mutex. When it
// wants to lock in write (exclusive) mode, it locks all CPU's mutexes.
//
// This means that in the read-mostly case, different readers will not cause any
// cacheline contention.
//
// Usage:
//   percpu_rwlock mylock;
//
//   // Lock shared:
//   {
//     boost::shared_lock<rw_spinlock> lock(mylock.get_lock());
//     ...
//   }
//
//   // Lock exclusive:
//
//   {
//     boost::lock_guard<percpu_rwlock> lock(mylock);
//     ...
//   }
class percpu_rwlock {
 public:
  percpu_rwlock() {
    errno = 0;
    n_cpus_ = base::MaxCPUIndex() + 1;
    CHECK_EQ(errno, 0) << ErrnoToString(errno);
    CHECK_GT(n_cpus_, 0);
    locks_ = new padded_lock[n_cpus_];
  }

  ~percpu_rwlock() {
    delete [] locks_;
  }

  rw_spinlock &get_lock() {
#if defined(__APPLE__)
    // OSX doesn't have a way to get the CPU, so we'll pick a random one.
    int cpu = reinterpret_cast<uintptr_t>(this) % n_cpus_;
#else
    int cpu = sched_getcpu();
    CHECK_LT(cpu, n_cpus_);
#endif  // defined(__APPLE__)
    return locks_[cpu].lock;
  }

  bool try_lock() {
    for (int i = 0; i < n_cpus_; i++) {
      if (!locks_[i].lock.try_lock()) {
        while (i--) {
          locks_[i].lock.unlock();
        }
        return false;
      }
    }
    return true;
  }

  // Return true if this lock is held on any CPU.
  // See simple_spinlock::is_locked() for details about where this is useful.
  bool is_locked() const {
    for (int i = 0; i < n_cpus_; i++) {
      if (locks_[i].lock.is_locked()) return true;
    }
    return false;
  }

  void lock() {
    for (int i = 0; i < n_cpus_; i++) {
      locks_[i].lock.lock();
    }
  }

  void unlock() {
    for (int i = 0; i < n_cpus_; i++) {
      locks_[i].lock.unlock();
    }
  }

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

 private:
  struct padded_lock {
    rw_spinlock lock;
    char padding[CACHELINE_SIZE - (sizeof(rw_spinlock) % CACHELINE_SIZE)];
  };

  int n_cpus_;
  padded_lock *locks_;
};

// Simpler version of boost::lock_guard. Only supports the basic object
// lifecycle and defers any error checking to the underlying mutex.
template <typename Mutex>
class lock_guard {
 public:
  explicit lock_guard(Mutex* m)
    : m_(DCHECK_NOTNULL(m)) {
    m_->lock();
  }

  ~lock_guard() {
    m_->unlock();
  }

 private:
  Mutex* m_;
  DISALLOW_COPY_AND_ASSIGN(lock_guard<Mutex>);
};

// Simpler version of boost::unique_lock. Tracks lock acquisition and will
// report attempts to double lock() or unlock().
template <typename Mutex>
class unique_lock {
 public:
  unique_lock()
    : locked_(false),
      m_(NULL) {
  }

  explicit unique_lock(Mutex* m)
    : locked_(true),
      m_(m) {
    m_->lock();
  }

  ~unique_lock() {
    if (locked_) {
      m_->unlock();
      locked_ = false;
    }
  }

  void lock() {
    DCHECK(!locked_);
    m_->lock();
    locked_ = true;
  }

  void unlock() {
    DCHECK(locked_);
    m_->unlock();
    locked_ = false;
  }

  void swap(unique_lock<Mutex>* other) {
    DCHECK(other != NULL) << "The passed unique_lock is null";
    std::swap(locked_, other->locked_);
    std::swap(m_, other->m_);
  }

 private:
  bool locked_;
  Mutex* m_;
  DISALLOW_COPY_AND_ASSIGN(unique_lock<Mutex>);
};

// Simpler version of boost::shared_lock. Defers error checking to the
// underlying mutex.
template <typename Mutex>
class shared_lock {
 public:
  shared_lock()
    : m_(NULL) {
  }

  explicit shared_lock(Mutex* m)
    : m_(DCHECK_NOTNULL(m)) {
    m_->lock_shared();
  }

  void swap(shared_lock& other) {
    std::swap(m_,other.m_);
  }

  ~shared_lock() {
    if (m_ != NULL) {
      m_->unlock_shared();
    }
  }

 private:
  Mutex* m_;
  DISALLOW_COPY_AND_ASSIGN(shared_lock<Mutex>);
};

} // namespace kudu

#endif
