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

#include "yb/util/rwc_lock.h"

#include <utility>

#include "yb/gutil/walltime.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug-util.h"
#include "yb/util/env.h"
#include "yb/util/logging.h"
#include "yb/util/thread.h"
#include "yb/util/thread_restrictions.h"

DEFINE_RUNTIME_bool(enable_rwc_lock_debugging, false,
    "Enable debug logging for RWC lock. This can hurt performance significantly since it causes us "
    "to capture stack traces on each lock acquisition.");
TAG_FLAG(enable_rwc_lock_debugging, advanced);

DEFINE_RUNTIME_int32(slow_rwc_lock_log_ms, 5000,
    "How long to wait for a write or commit lock before logging that it took a long time (and "
    "logging the stacks of the writer / reader threads if FLAGS_enable_rwc_lock_debugging is "
    "true).");
TAG_FLAG(slow_rwc_lock_log_ms, advanced);

using namespace std::literals;

namespace yb {

namespace {
  const auto kMaxDebugWait = MonoDelta::FromMinutes(3);
} // namespace

RWCLock::RWCLock()
  : no_mutators_(&lock_),
    no_readers_(&lock_),
    reader_count_(0),
    write_locked_(false) {
  if (FLAGS_enable_rwc_lock_debugging) {
    debug_info_ = std::make_unique<DebugInfo>();
  }
}

RWCLock::~RWCLock() {
  CHECK_EQ(reader_count_, 0);
}

void RWCLock::ReadLock() {
  MutexLock l(lock_);
  reader_count_++;
  if (debug_info_) {
    const int64_t tid = Thread::CurrentThreadId();
    auto& reader_stacks = debug_info_->reader_stacks;
    if (reader_stacks.find(tid) == reader_stacks.end()) {
      StackTrace stack_trace = StackTrace();
      stack_trace.Collect();
      reader_stacks[tid] = {
        .count = 1,
        .stack = std::move(stack_trace),
      };
    } else {
      reader_stacks[tid].count++;
    }
  }
}

void RWCLock::ReadUnlock() {
  MutexLock l(lock_);
  DCHECK_GT(reader_count_, 0);
  reader_count_--;
  if (debug_info_) {
    auto& reader_stacks = debug_info_->reader_stacks;
    const int64_t tid = Thread::CurrentThreadId();
    if (--reader_stacks[tid].count == 0) {
      reader_stacks.erase(tid);
    }
  }
  if (reader_count_ == 0) {
    YB_PROFILE(no_readers_.Signal());
  }
}

bool RWCLock::HasReaders() const {
  MutexLock l(lock_);
  return reader_count_ > 0;
}

bool RWCLock::HasWriteLock() const {
  MutexLock l(lock_);
  if (debug_info_) {
    DCHECK_EQ(debug_info_->last_writer_tid, Thread::CurrentThreadId());
  }
  return write_locked_;
}

void RWCLock::WriteLockThreadChanged() {
  if (!debug_info_) {
    return;
  }
  MutexLock l(lock_);
  DCHECK(write_locked_);
  debug_info_->last_writer_tid = Thread::CurrentThreadId();
  debug_info_->last_writer_tid_for_stack = Thread::CurrentThreadIdForStack();
}

void RWCLock::WriteLock() {
  ThreadRestrictions::AssertWaitAllowed();

  MutexLock l(lock_);
  // Wait for any other mutations to finish.
  if (write_locked_ && !no_mutators_.TimedWait(FLAGS_slow_rwc_lock_log_ms * 1ms)) {
    std::ostringstream ss;
    ss << "Waited " << FLAGS_slow_rwc_lock_log_ms << "ms to acquire write lock.";
    if (debug_info_) {
      ss << "\n\nlast writer TID: " << debug_info_->last_writer_tid;
      ss << "\n\nlast writer stack: " << debug_info_->last_writer_stacktrace.Symbolize();
      ss << "\n\nlast writer current stack: "
          << DumpThreadStack(debug_info_->last_writer_tid_for_stack);
      ss << "\n\ncurrent thread stack: " << GetStackTrace();
    }
    LOG(WARNING) << ss.str();
  }

  // Loop to guard against spurious wakeups.
  while (write_locked_) {
#ifdef NDEBUG
    no_mutators_.Wait();
#else
    if (!no_mutators_.TimedWait(kMaxDebugWait)) {
      LOG(FATAL) << "Timed out waiting to acquire write lock after " << kMaxDebugWait;
    }
#endif
  }

  if (debug_info_) {
    debug_info_->last_writelock_acquire_time = GetCurrentTimeMicros();
    debug_info_->last_writer_tid = Thread::CurrentThreadId();
    debug_info_->last_writer_tid_for_stack = Thread::CurrentThreadIdForStack();
    debug_info_->last_writer_stacktrace.Collect();
  }
  write_locked_ = true;
}

void RWCLock::WriteUnlock() {
  ThreadRestrictions::AssertWaitAllowed();

  MutexLock l(lock_);
  DCHECK(write_locked_);
  write_locked_ = false;
  if (debug_info_) {
    debug_info_->last_writer_stacktrace.Reset();
  }
  YB_PROFILE(no_mutators_.Signal());
}

void RWCLock::UpgradeToCommitLock() {
  lock_.lock();
  DCHECK(write_locked_);

  if (reader_count_ > 0 && !no_readers_.TimedWait(FLAGS_slow_rwc_lock_log_ms * 1ms)) {
    std::ostringstream ss;
    ss << "Waited " << FLAGS_slow_rwc_lock_log_ms << "ms to acquire commit lock"
       << ", num readers: " << reader_count_;
    if (debug_info_) {
      ss << ", current thread stack: " << GetStackTrace();
      for (const auto& entry : debug_info_->reader_stacks) {
        if (entry.second.count > 1) {
          ss << "reader thread " << entry.first
             << " (holding " << entry.second.count << " locks)";
        }
        ss << " stack: " << entry.second.stack.Symbolize();
      }
    }
    LOG(WARNING) << ss.str();
  }

  // Loop to guard against spurious wakeups.
  while (reader_count_ > 0) {
#ifdef NDEBUG
    no_readers_.Wait();
#else
    if (!no_readers_.TimedWait(kMaxDebugWait)) {
      LOG(FATAL) << "Timed out waiting to acquire commit lock for " << kMaxDebugWait;
    }
#endif
  }
  DCHECK(write_locked_);

  // Leaves the lock held, which prevents any new readers
  // or writers.
}

void RWCLock::CommitUnlock() {
  DCHECK_EQ(0, reader_count_);
  write_locked_ = false;
  if (debug_info_) {
    debug_info_->last_writer_stacktrace.Reset();
  }
  YB_PROFILE(no_mutators_.Broadcast());
  lock_.unlock();
}

} // namespace yb
