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

#include <absl/synchronization/mutex.h>
#include <boost/smart_ptr/detail/yield_k.hpp>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/port.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/logging.h"

namespace yb {

// Value of --use_absl_rw_spinlock. Each lock reads it once, when it is constructed.
bool UseAbslRwSpinlock();

// The original rw_semaphore: waiters retry a compare-and-swap and sleep in nanosleep(1us) (about
// 55us with default timer slack) between attempts, and unlock wakes nobody.
class PollingRwSemaphore {
#ifndef NDEBUG
  static constexpr int64_t kInvalidThreadId = -1;
#endif
 public:
  PollingRwSemaphore() : state_(0) {}
  ~PollingRwSemaphore() {}

  void lock_shared() {
    int loop_count = 0;
    Atomic32 cur_state = base::subtle::NoBarrier_Load(&state_);
    while (true) {
      Atomic32 expected = cur_state & kNumReadersMask;   // I expect no write lock
      Atomic32 try_new_state = expected + 1;          // Add me as reader
      cur_state = base::subtle::Acquire_CompareAndSwap(&state_, expected, try_new_state);
      if (cur_state == expected)
        break;
      // Either was already locked by someone else, or CAS failed.
      boost::detail::yield(loop_count++);
    }
  }

  void unlock_shared() {
    int loop_count = 0;
    Atomic32 cur_state = base::subtle::NoBarrier_Load(&state_);
    while (true) {
      DCHECK_GT(cur_state & kNumReadersMask, 0)
        << "unlock_shared() called when there are no shared locks held";
      Atomic32 expected = cur_state;           // I expect a write lock and other readers
      Atomic32 try_new_state = expected - 1;   // Drop me as reader
      cur_state = base::subtle::Release_CompareAndSwap(&state_, expected, try_new_state);
      if (cur_state == expected)
        break;
      // Either was already locked by someone else, or CAS failed.
      boost::detail::yield(loop_count++);
    }
  }

  // Tries to acquire a write lock, if no one else has it.
  // This function retries on CAS failure and waits for readers to complete.
  bool try_lock() {
    int loop_count = 0;
    Atomic32 cur_state = base::subtle::NoBarrier_Load(&state_);
    while (true) {
      // someone else has already the write lock
      if (cur_state & kWriteFlag)
        return false;

      Atomic32 expected = cur_state & kNumReadersMask;   // I expect some 0+ readers
      Atomic32 try_new_state = kWriteFlag | expected;    // I want to lock the other writers
      cur_state = base::subtle::Acquire_CompareAndSwap(&state_, expected, try_new_state);
      if (cur_state == expected)
        break;
      // Either was already locked by someone else, or CAS failed.
      boost::detail::yield(loop_count++);
    }

    WaitPendingReaders();
    RecordLockHolderStack();
    return true;
  }

  void lock() {
    int loop_count = 0;
    Atomic32 cur_state = base::subtle::NoBarrier_Load(&state_);
    while (true) {
      Atomic32 expected = cur_state & kNumReadersMask;   // I expect some 0+ readers
      Atomic32 try_new_state = kWriteFlag | expected;    // I want to lock the other writers
      // Note: we use NoBarrier here because we'll do the Acquire barrier down below
      // in WaitPendingReaders
      cur_state = base::subtle::NoBarrier_CompareAndSwap(&state_, expected, try_new_state);
      if (cur_state == expected)
        break;
      // Either was already locked by someone else, or CAS failed.
      boost::detail::yield(loop_count++);
    }

    WaitPendingReaders();

#ifndef NDEBUG
    AssignWriterTid();
#endif // NDEBUG
    RecordLockHolderStack();
  }

  void unlock() {
    // I expect to be the only writer
    DCHECK_EQ(base::subtle::NoBarrier_Load(&state_), kWriteFlag);

#ifndef NDEBUG
    writer_tid_ = kInvalidThreadId; // Invalid tid.
#endif // NDEBUG

    ResetLockHolderStack();
    // Reset: no writers & no readers.
    Release_Store(&state_, 0);
  }

  // Return true if the lock is currently held for write by any thread.
  // See simple_semaphore::is_locked() for details about where this is useful.
  bool is_write_locked() const {
    return base::subtle::NoBarrier_Load(&state_) & kWriteFlag;
  }

  // Return true if the lock is currently held, either for read or write
  // by any thread.
  // See simple_semaphore::is_locked() for details about where this is useful.
  bool is_locked() const {
    return base::subtle::NoBarrier_Load(&state_);
  }

 private:
  static const uint32_t kNumReadersMask = 0x7fffffff;
  static const uint32_t kWriteFlag = 1 << 31;

#ifdef RW_SEMAPHORE_TRACK_HOLDER
  StackTrace writer_stack_;
  void RecordLockHolderStack() {
    writer_stack_.Collect();
  }
  void ResetLockHolderStack() {
    writer_stack_.Reset();
  }
#else
  void RecordLockHolderStack() {
  }
  void ResetLockHolderStack() {
  }
#endif

  void WaitPendingReaders() {
    int loop_count = 0;
    while ((base::subtle::Acquire_Load(&state_) & kNumReadersMask) > 0) {
      boost::detail::yield(loop_count++);
    }
  }

 private:
  volatile Atomic32 state_;
#ifndef NDEBUG
  void AssignWriterTid();

  int64_t writer_tid_ = kInvalidThreadId;
#endif // NDEBUG
};

// Reader/writer lock with lowercase methods, so it works with std::lock_guard, std::unique_lock,
// std::shared_lock and yb::SharedLock.
//
// --use_absl_rw_spinlock picks the implementation when the lock is constructed:
// - false (default): PollingRwSemaphore.
// - true: absl::Mutex. Waiters block, and absl wakes one designated waiter at a time. try_lock()
//   fails while readers hold the lock, and the lock must be released by the thread that acquired
//   it.
// Either way, once a writer is waiting new readers block behind it, so a thread must not acquire
// the lock in shared mode again while it already holds it.
class CAPABILITY("mutex") rw_semaphore {
 public:
  rw_semaphore() : use_absl_(UseAbslRwSpinlock()) {}

  void lock_shared() NO_THREAD_SAFETY_ANALYSIS {
    if (use_absl_) {
      mutex_.ReaderLock();
    } else {
      polling_.lock_shared();
    }
  }

  void unlock_shared() NO_THREAD_SAFETY_ANALYSIS {
    if (use_absl_) {
      mutex_.ReaderUnlock();
    } else {
      polling_.unlock_shared();
    }
  }

  bool try_lock() NO_THREAD_SAFETY_ANALYSIS {
    return use_absl_ ? mutex_.TryLock() : polling_.try_lock();
  }

  void lock() NO_THREAD_SAFETY_ANALYSIS {
    if (use_absl_) {
      mutex_.Lock();
    } else {
      polling_.lock();
    }
  }

  void unlock() NO_THREAD_SAFETY_ANALYSIS {
    if (use_absl_) {
      mutex_.Unlock();
    } else {
      polling_.unlock();
    }
  }

  // Checks that some thread holds the lock in the given mode. Neither implementation records which
  // thread holds it in shared mode.
  void AssertHeld() const ASSERT_CAPABILITY(this) {
    if (use_absl_) {
      mutex_.AssertHeld();
    } else {
      CHECK(polling_.is_write_locked());
    }
  }

  void AssertReaderHeld() const ASSERT_SHARED_CAPABILITY(this) {
    if (use_absl_) {
      mutex_.AssertReaderHeld();
    } else {
      CHECK(polling_.is_locked());
    }
  }

 private:
  const bool use_absl_;
  PollingRwSemaphore polling_;
  mutable absl::Mutex mutex_;
};

} // namespace yb
