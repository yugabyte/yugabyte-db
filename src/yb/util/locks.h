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

#include <algorithm>
#include <mutex>

#include "yb/util/logging.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#include "yb/gutil/spinlock.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/errno.h"
#include "yb/util/rw_semaphore.h"

namespace yb {

// Wrapper around the Google SpinLock class to adapt it to the method names
// expected by Boost.
class CAPABILITY("mutex") simple_spinlock {
 public:
  simple_spinlock() {}

  void lock() ACQUIRE() {
    l_.Lock();
  }

  void unlock() RELEASE() {
    l_.Unlock();
  }

  bool try_lock() TRY_ACQUIRE(true) {
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

// Saves timestamp of locking in order to log when it is held for too long
class CAPABILITY("mutex") simple_spinlock_with_timestamp {
 public:
  simple_spinlock_with_timestamp() {}

  void lock() ACQUIRE() {
    spinlock_.lock();
    lock_timestamp_ = std::chrono::steady_clock::now();
  }

  void unlock() RELEASE() {
    auto unlock_timestamp = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(unlock_timestamp - lock_timestamp_)
            .count();

    if (duration > warning_threshold_ms_) {
      LOG(WARNING) << "simple_spinlock locked for too long: " << duration << " ms";
    }
    spinlock_.unlock();
  }

  bool try_lock() TRY_ACQUIRE(true) {
    auto success_locked = spinlock_.try_lock();
    if (success_locked) {
      lock_timestamp_ = std::chrono::steady_clock::now();
    }
    return success_locked;
  }

  bool is_locked() {
    return spinlock_.is_locked();
  }

  // Set the threshold duration for warnings
  void set_warning_threshold(long long threshold_ms) {
    warning_threshold_ms_ = threshold_ms;
  }

 private:
  simple_spinlock spinlock_;
  std::chrono::steady_clock::time_point lock_timestamp_;
  long long warning_threshold_ms_ = 1000;  // Default threshold
};

struct padded_spinlock : public simple_spinlock {
  static constexpr size_t kPaddingSize =
      CACHELINE_SIZE - (sizeof(simple_spinlock) % CACHELINE_SIZE);
  char padding[kPaddingSize];
};

// Reader-writer lock.
// This is functionally equivalent to rw_semaphore in rw_semaphore.h, but should be
// used whenever the lock is expected to be released on the same thread which acquired it.
// It adds TSAN annotations which will detect misuse of the lock, but those
// annotations also assume that the same thread the takes the lock will unlock it.
//
// See rw_semaphore.h for documentation on the individual methods where unclear.
class CAPABILITY("mutex") rw_spinlock  {
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

// A reader-writer mutex implementation which is biased for use cases where
// the write lock is taken infrequently, but the read lock is used often.
//
// Internally, this creates N underlying mutexes, one per CPU. When a thread
// wants to lock in read (shared) mode, it locks only its own CPU's mutex for read. When it
// wants to lock in write (exclusive) mode, it locks all CPU's mutexes execlusively.
//
// This means that in the read-mostly case, different readers will not cause any
// cacheline contention.
//
// Usage:
//   PerCpuRwMutex mylock;
//
//   // Lock shared:
//   {
//     PerCpuRwSharedLock lock(mylock);
//     ...
//   }
//
//   // Lock exclusive:
//
//   {
//     boost::lock_guard<PerCpuRwMutex> lock(mylock);
//     ...
//   }
class CAPABILITY("mutex") PerCpuRwMutex {
 public:
  PerCpuRwMutex() {
    errno = 0;
    n_cpus_ = base::MaxCPUIndex() + 1;
    CHECK_EQ(errno, 0) << ErrnoToString(errno);
    CHECK_GT(n_cpus_, 0);
    locks_ = new padded_lock[n_cpus_];
  }

  ~PerCpuRwMutex() {
    delete [] locks_;
  }

  bool try_lock() TRY_ACQUIRE(true) {
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

  void lock() ACQUIRE() {
    for (int i = 0; i < n_cpus_; i++) {
      locks_[i].lock.lock();
    }
  }

  void unlock() RELEASE() {
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
  friend class PerCpuRwSharedLock;

  rw_spinlock &get_lock() {
#if defined(__APPLE__)
    // OSX doesn't have a way to get the CPU, so we'll pick a random one.
    int cpu = reinterpret_cast<uintptr_t>(this) % n_cpus_;
#else
    int cpu = sched_getcpu();
    if (cpu < 0) {
      LOG(FATAL) << ErrnoToString(errno);
    }
    CHECK_LT(cpu, n_cpus_);
#endif  // defined(__APPLE__)
    return locks_[cpu].lock;
  }

  struct padded_lock {
    rw_spinlock lock;
    static constexpr size_t kPaddingSize = CACHELINE_SIZE - (sizeof(rw_spinlock) % CACHELINE_SIZE);
    char padding[kPaddingSize];
  };

  int n_cpus_;
  padded_lock *locks_;
};

// A scoped lock for PerCpuRwMutex. Works by choosing the current CPU and locking the lock for that
// CPU. The lock is released when the guard goes out of scope.
class SCOPED_CAPABILITY PerCpuRwSharedLock {
 public:
  explicit PerCpuRwSharedLock(PerCpuRwMutex& per_cpu_rwlock)  // NOLINT
      ACQUIRE_SHARED(per_cpu_rwlock) {
    current_cpu_lock_ = &per_cpu_rwlock.get_lock();
    current_cpu_lock_->lock();
  }

  ~PerCpuRwSharedLock() RELEASE() {
    current_cpu_lock_->unlock();
  }
 private:
  rw_spinlock* current_cpu_lock_;
};

template <class Container>
auto ToVector(const Container& container, std::mutex* mutex) {
  std::vector<typename Container::value_type> result;
  {
    std::lock_guard lock(*mutex);
    result.reserve(container.size());
    result.assign(container.begin(), container.end());
  }
  return result;
}

template <class Mutex, class Rep, class Period>
std::unique_lock<Mutex> LockMutex(Mutex* mutex, std::chrono::duration<Rep, Period> duration) {
  if (duration == std::chrono::duration<Rep, Period>::max()) {
    return std::unique_lock<Mutex>(*mutex);
  }

  return std::unique_lock<Mutex>(*mutex, duration);
}

template <class Mutex, class Clock, class Duration>
std::unique_lock<Mutex> LockMutex(Mutex* mutex, std::chrono::time_point<Clock, Duration> time) {
  if (time == std::chrono::time_point<Clock, Duration>::max()) {
    return std::unique_lock<Mutex>(*mutex);
  }

  return std::unique_lock<Mutex>(*mutex, time);
}

template <class Lock>
class ReverseLock {
 public:
  ReverseLock(const ReverseLock&) = delete;
  void operator=(const ReverseLock&) = delete;

  explicit ReverseLock(Lock& lock) : lock_(lock) {
    lock_.unlock();
  }

  ~ReverseLock() {
    lock_.lock();
  }

 private:
  Lock& lock_;
};

} // namespace yb
