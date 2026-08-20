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

#include "yb/util/rwc_lock.h"

#include <algorithm>

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/thread.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/tsan_util.h"

DEFINE_RUNTIME_bool(enable_rwc_lock_debugging, false,
    "Enable additional debug logging for RWC lock.  This can hurt performance significantly since "
    "it causes us to capture stack traces on each write lock acquisition.");
TAG_FLAG(enable_rwc_lock_debugging, advanced);

DEFINE_RUNTIME_int32(slow_rwc_lock_log_ms, 5000 * yb::kTimeMultiplier,
    "How long to wait for a write or commit lock before logging that acquiring it took a long "
    "time.");
TAG_FLAG(slow_rwc_lock_log_ms, advanced);

using namespace std::literals;

#define RWC_LOCK_COLLECT_READ_LOCK_STACK_TRACE 1

namespace yb {

namespace {

const auto kMaxDebugWait = MonoDelta::FromMinutes(3);
const size_t kCommitActive = 1ULL << 60;
const size_t kCommitPending = kCommitActive << 1;
const size_t kReadersMask = kCommitActive - 1;

#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
thread_local size_t rwc_read_lock_counter = 0;
std::atomic<void*> rwc_conflicting_mutex{nullptr};
#if RWC_LOCK_COLLECT_READ_LOCK_STACK_TRACE
thread_local StackTrace rwc_read_lock_first_stack_trace;
#endif
#endif

} // namespace

RWCLock::~RWCLock() {
  CHECK_EQ(reader_counter_.load(), 0);
}

bool RWCLock::DoReadLock(CoarseTimePoint deadline) {
  // We assume committing is very fast so we do not call ThreadRestrictions::AssertWaitAllowed()
  // here.
#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
  if (++rwc_read_lock_counter == 1) {
#if RWC_LOCK_COLLECT_READ_LOCK_STACK_TRACE
    rwc_read_lock_first_stack_trace.Collect();
#endif
  }
#endif
  for (;;) {
    if (!(reader_counter_.fetch_add(1) & kCommitActive)) {
      return true;
    }
    if (!(reader_counter_.fetch_sub(1) & kCommitActive)) {
      continue;
    }
    std::unique_lock lock(commit_mutex_);
    if (!(reader_counter_.load() & kCommitActive)) {
      continue;
    }

    // wait for lock until it is time to log slow lock or until deadline
    const auto slow_log_deadline = CoarseMonoClock::now() + FLAGS_slow_rwc_lock_log_ms * 1ms;
    const auto first_deadline = std::min(deadline, slow_log_deadline);
    if (no_committer_.wait_until(lock, first_deadline) == std::cv_status::timeout) {
      if (deadline <= first_deadline) {
#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
        --rwc_read_lock_counter;  // Balance the increment above; we did not acquire.
#endif
        return false;
      }
      LOG(WARNING) << "Long time waiting for read lock due to committers holding lock";
      // Is the blocking lock eventually released?  If so, log so the reader knows that lock was not
      // held forever.
      auto start = CoarseMonoClock::now();
      // wait until actual deadline
      if (no_committer_.wait_until(lock, deadline) == std::cv_status::timeout) {
#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
        --rwc_read_lock_counter;  // Balance the increment above; we did not acquire.
#endif
        return false;
      }
      LOG(INFO) << "Committers no longer holding lock after " << (CoarseMonoClock::now() - start);
    }
  }
}

void RWCLock::ReadLock() {
  DoReadLock(CoarseTimePoint::max());
}

bool RWCLock::ReadLock(CoarseTimePoint deadline) {
  return DoReadLock(deadline);
}

#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
void RWCLock::CheckNoReadLockConflict(void* mutex) {
  if (mutex == rwc_conflicting_mutex.load()) {
    CHECK_EQ(rwc_read_lock_counter, 0)
#if RWC_LOCK_COLLECT_READ_LOCK_STACK_TRACE
        << rwc_read_lock_first_stack_trace.Symbolize();
#else
        << "";
#endif
  }
}

void RWCLock::SetConflictingMutex(void* mutex) {
  rwc_conflicting_mutex.store(mutex);
}
#endif

void RWCLock::ReadUnlock() {
#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
  --rwc_read_lock_counter;
#endif
  if (reader_counter_.fetch_sub(1) - 1 == kCommitPending) {
    std::unique_lock lock(commit_mutex_);
    no_readers_.notify_one();
  }
}

bool RWCLock::HasReaders() const {
  return (reader_counter_.load() & kReadersMask) != 0;
}

bool RWCLock::DEBUG_HasWriteLock() const {
#ifdef NDEBUG
  LOG(FATAL) << "attempt to use DEBUG_HasWriteLock() when DEBUG is false";
#endif
  std::lock_guard lock(write_lock_holder_info_mutex_);
  return write_lock_holder_thread_id_ == Thread::CurrentThreadId();
}

bool RWCLock::DoWriteLock(CoarseTimePoint deadline) NO_THREAD_SAFETY_ANALYSIS {
  ThreadRestrictions::AssertWaitAllowed();
#if defined(THREAD_SANITIZER)
  write_mutex_.lock();
#else
  const auto slow_log_deadline = CoarseMonoClock::now() + 1ms * FLAGS_slow_rwc_lock_log_ms;
  const auto first_deadline = std::min(deadline, slow_log_deadline);
  // wait until it is time to log slow lock or until actual deadline
  if (!write_mutex_.try_lock_until(first_deadline)) {
    if (deadline <= first_deadline) {
      return false;
    }
    {
      std::lock_guard lock(write_lock_holder_info_mutex_);
      std::string message =
          "Long time trying to take write lock due to others holding write/commit lock; Current "
          "write holder is ";
      if (write_lock_holder_thread_id_ == -1) {
        message += "unknown";
      } else {
        message += "thread ID " + AsString(write_lock_holder_thread_id_);
        if (!write_lock_holder_stack_trace_.empty()) {
          message += "; stack trace when acquiring write lock:\n" + write_lock_holder_stack_trace_;
        }
      }
      LOG(WARNING) << message;
    }
    auto start = CoarseMonoClock::now();
    if (!write_mutex_.try_lock_until(deadline)) {
      return false;
    }
    // Do we eventually get the lock?  If so, log so the reader knows lock was not held forever.
    LOG(INFO) << "Finally got write lock after additional " << (CoarseMonoClock::now() - start);
  }
#endif

  // Save information about us as we are now the last holder of the write lock.
  write_acquire_time_ = CoarseMonoClock::now();
#ifdef NDEBUG
  if (FLAGS_enable_rwc_lock_debugging) {
    std::lock_guard lock(write_lock_holder_info_mutex_);
    write_lock_holder_thread_id_ = Thread::CurrentThreadId();
    write_lock_holder_stack_trace_ = GetStackTrace();
  }
#else
  {
    std::lock_guard lock(write_lock_holder_info_mutex_);
    write_lock_holder_thread_id_ = Thread::CurrentThreadId();
    if (FLAGS_enable_rwc_lock_debugging) {
      write_lock_holder_stack_trace_ = GetStackTrace();
    }
  }
#endif
  return true;
}

void RWCLock::WriteLock() NO_THREAD_SAFETY_ANALYSIS {
  DoWriteLock(CoarseTimePoint::max());
}

bool RWCLock::WriteLock(CoarseTimePoint deadline) NO_THREAD_SAFETY_ANALYSIS {
  return DoWriteLock(deadline);
}

void RWCLock::WriteUnlock() NO_THREAD_SAFETY_ANALYSIS {
  auto write_acquire_time = write_acquire_time_;
  write_mutex_.unlock();
  MonoDelta passed = CoarseMonoClock::now() - write_acquire_time;
  if (passed > FLAGS_slow_rwc_lock_log_ms * 1ms) {
    LOG(WARNING) << "Long time holding write lock: " << passed
                 << "; stack trace of release:\n"
                 << GetStackTrace();
  }
}

void RWCLock::UpgradeToCommitLock() {
  std::unique_lock lock(commit_mutex_);
  reader_counter_ += kCommitPending;
  for (;;) {
    size_t expected = kCommitPending;
    if (reader_counter_.compare_exchange_strong(expected, kCommitActive)) {
      break;
    }
    if (no_readers_.wait_for(lock, FLAGS_slow_rwc_lock_log_ms * 1ms) == std::cv_status::timeout) {
      LOG(WARNING) << "Long time waiting to acquire commit lock due to readers";
      // Is the blocking lock eventually released?  If so, log so the reader knows that lock was not
      // held forever.
      auto start = CoarseMonoClock::now();
      no_readers_.wait(lock);
      MonoDelta passed = CoarseMonoClock::now() - start;
      LOG(INFO) << "Readers no longer holding lock after " << passed;
    }
  }
}

void RWCLock::CommitUnlock() {
  auto write_acquire_time = write_acquire_time_;
  {
    std::unique_lock lock(commit_mutex_);
    reader_counter_ -= kCommitActive;
    no_committer_.notify_all();
  }
  write_mutex_.unlock();
  MonoDelta passed = CoarseMonoClock::now() - write_acquire_time;
  if (passed > FLAGS_slow_rwc_lock_log_ms * 1ms) {
    LOG(WARNING) << "Long time holding write then commit lock: " << passed
                 << "; stack trace of release:\n"
                 << GetStackTrace();
  }
}

} // namespace yb
