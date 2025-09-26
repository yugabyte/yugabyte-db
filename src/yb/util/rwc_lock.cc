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

#include <utility>

#include "yb/gutil/walltime.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug-util.h"
#include "yb/util/env.h"
#include "yb/util/logging.h"
#include "yb/util/thread.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/tsan_util.h"

DEFINE_RUNTIME_bool(enable_rwc_lock_debugging, false,
    "Enable debug logging for RWC lock. This can hurt performance significantly since it causes us "
    "to capture stack traces on each lock acquisition.");
TAG_FLAG(enable_rwc_lock_debugging, advanced);

DEFINE_RUNTIME_int32(slow_rwc_lock_log_ms, 5000 * yb::kTimeMultiplier,
    "How long to wait for a write or commit lock before logging that it took a long time (and "
    "logging the stacks of the writer / reader threads if FLAGS_enable_rwc_lock_debugging is "
    "true).");
TAG_FLAG(slow_rwc_lock_log_ms, advanced);

using namespace std::literals;

#define RWC_LOCK_COLLECT_READ_LOCK_STACK_TRACE 1

namespace yb {

namespace {

const auto kMaxDebugWait = MonoDelta::FromMinutes(3);
const size_t kWriteActive = 1ULL << 60;
const size_t kWritePending = kWriteActive << 1;
const size_t kReadersMask = kWriteActive - 1;

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

void RWCLock::ReadLock() {
#if RWC_LOCK_TRACK_EXTERNAL_DEADLOCK
  if (++rwc_read_lock_counter == 1) {
#if RWC_LOCK_COLLECT_READ_LOCK_STACK_TRACE
    rwc_read_lock_first_stack_trace.Collect();
#endif
  }
#endif
  for (;;) {
    if (!(reader_counter_.fetch_add(1) & kWriteActive)) {
      return;
    }
    if (!(reader_counter_.fetch_sub(1) & kWriteActive)) {
      continue;
    }
    std::unique_lock lock(commit_mutex_);
    auto value = reader_counter_.load();
    if (!(value & kWriteActive)) {
      continue;
    }
    if (no_writers_.wait_for(lock, FLAGS_slow_rwc_lock_log_ms * 1ms) == std::cv_status::timeout) {
      LOG(WARNING) << "Long time waiting no writers";
    }
  }
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
  if (reader_counter_.fetch_sub(1) - 1 == kWritePending) {
    no_readers_.notify_one();
  }
}

bool RWCLock::HasReaders() const {
  return (reader_counter_.load() & kReadersMask) != 0;
}

bool RWCLock::HasWriteLock() const {
  std::unique_lock lock(write_mutex_, std::try_to_lock);
  return !lock.owns_lock();
}

void RWCLock::WriteLock() NO_THREAD_SAFETY_ANALYSIS {
  ThreadRestrictions::AssertWaitAllowed();
#if defined(THREAD_SANITIZER)
  write_mutex_.lock();
#else
  if (!write_mutex_.try_lock_for(1ms * FLAGS_slow_rwc_lock_log_ms)) {
    LOG(WARNING) << "Long time taking write lock";
    write_mutex_.lock();
  }
#endif
#ifndef NDEBUG
  write_start_ = CoarseMonoClock::now();
#endif
}

void RWCLock::WriteUnlock() NO_THREAD_SAFETY_ANALYSIS {
  ThreadRestrictions::AssertWaitAllowed();
#ifndef NDEBUG
  auto write_start = write_start_;
#endif
  write_mutex_.unlock();
#ifndef NDEBUG
  MonoDelta passed = CoarseMonoClock::now() - write_start;
  if (passed > FLAGS_slow_rwc_lock_log_ms * 1ms) {
    LOG(WARNING) << "Long time holding write lock " << passed << ":\n" << GetStackTrace();
  }
#endif
}

void RWCLock::UpgradeToCommitLock() {
  std::unique_lock lock(commit_mutex_);
  reader_counter_ += kWritePending;
  for (;;) {
    size_t expected = kWritePending;
    if (reader_counter_.compare_exchange_strong(expected, kWriteActive)) {
      break;
    }
    if (no_readers_.wait_for(lock, FLAGS_slow_rwc_lock_log_ms * 1ms) == std::cv_status::timeout) {
      LOG(WARNING) << "Long time waiting no readers";
    }
  }
}

void RWCLock::CommitUnlock() {
  {
    std::unique_lock lock(commit_mutex_);
    reader_counter_ -= kWriteActive;
    no_writers_.notify_all();
  }
  write_mutex_.unlock();
}

} // namespace yb
