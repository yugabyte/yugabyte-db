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

namespace yb {

namespace {

const auto kFirstWait = MonoDelta::FromSeconds(1);
const auto kSecondWait = MonoDelta::FromSeconds(180);

} // namespace

RWCLock::RWCLock()
  : no_mutators_(&lock_),
    no_readers_(&lock_),
    reader_count_(0),
#ifdef NDEBUG
    write_locked_(false) {
#else
    write_locked_(false),
    last_writer_tid_(0),
    last_writelock_acquire_time_(0) {
#endif // NDEBUG
}

RWCLock::~RWCLock() {
  CHECK_EQ(reader_count_, 0);
}

void RWCLock::ReadLock() {
  MutexLock l(lock_);
  reader_count_++;
#ifndef NDEBUG
  if (VLOG_IS_ON(1)) {
    const int64_t tid = Thread::CurrentThreadId();
    if (reader_stacks_.find(tid) == reader_stacks_.end()) {
      StackTrace stack_trace = StackTrace();
      stack_trace.Collect();
      reader_stacks_[tid] = {
        .count = 1,
        .stack = std::move(stack_trace),
      };
    } else {
      reader_stacks_[tid].count++;
    }
  }
#endif // NDEBUG
}

void RWCLock::ReadUnlock() {
  MutexLock l(lock_);
  DCHECK_GT(reader_count_, 0);
  reader_count_--;
#ifndef NDEBUG
  if (VLOG_IS_ON(1)) {
    const int64_t tid = Thread::CurrentThreadId();
    if (--reader_stacks_[tid].count == 0) {
      reader_stacks_.erase(tid);
    }
  }
#endif // NDEBUG
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
#ifndef NDEBUG
  return last_writer_tid_ == Thread::CurrentThreadId();
#else
  return write_locked_;
#endif
}

void RWCLock::WriteLockThreadChanged() {
#ifndef NDEBUG
  MutexLock l(lock_);
  DCHECK(write_locked_);
  last_writer_tid_ = Thread::CurrentThreadId();
  last_writer_tid_for_stack_ = Thread::CurrentThreadIdForStack();
#endif
}

void RWCLock::WriteLock() {
  ThreadRestrictions::AssertWaitAllowed();

  MutexLock l(lock_);
  // Wait for any other mutations to finish.
#ifndef NDEBUG
  bool first_wait = true;
  while (write_locked_) {
    if (!no_mutators_.TimedWait(first_wait ? kFirstWait : kSecondWait)) {
      std::ostringstream ss;
      ss << "Too long write lock wait, last writer TID: " << last_writer_tid_
         << ", last writer stack: " << last_writer_stacktrace_.Symbolize();
      if (VLOG_IS_ON(1) || !first_wait) {
        ss << "\n\nlast writer current stack: " << DumpThreadStack(last_writer_tid_for_stack_);
        ss << "\n\ncurrent thread stack: " << GetStackTrace();
      }
      (first_wait ? LOG(WARNING) : LOG(FATAL)) << ss.str();
    }
    first_wait = false;
  }
#else
  while (write_locked_) {
    no_mutators_.Wait();
  }
#endif
#ifndef NDEBUG
  last_writelock_acquire_time_ = GetCurrentTimeMicros();
  last_writer_tid_ = Thread::CurrentThreadId();
  last_writer_tid_for_stack_ = Thread::CurrentThreadIdForStack();
  last_writer_stacktrace_.Collect();
#endif // NDEBUG
  write_locked_ = true;
}

void RWCLock::WriteUnlock() {
  ThreadRestrictions::AssertWaitAllowed();

  MutexLock l(lock_);
  DCHECK(write_locked_);
  write_locked_ = false;
#ifndef NDEBUG
  last_writer_stacktrace_.Reset();
#endif // NDEBUG
  YB_PROFILE(no_mutators_.Signal());
}

void RWCLock::UpgradeToCommitLock() {
  lock_.lock();
  DCHECK(write_locked_);
#ifndef NDEBUG
  bool first_wait = true;
  while (reader_count_ > 0) {
    if (!no_readers_.TimedWait(first_wait ? kFirstWait : kSecondWait)) {
      std::ostringstream ss;
      ss << "Too long commit lock wait, num readers: " << reader_count_
         << ", current thread stack: " << GetStackTrace();
      if (VLOG_IS_ON(1)) {
        for (const auto& entry : reader_stacks_) {
          ss << "reader thread " << entry.first;
          if (entry.second.count > 1) {
            ss << " (holding " << entry.second.count << " locks) first";
          }
          ss << " stack: " << entry.second.stack.Symbolize();
        }
      }
      (first_wait ? LOG(WARNING) : LOG(FATAL)) << ss.str();
    }
    first_wait = false;
  }
#else
  while (reader_count_ > 0) {
    no_readers_.Wait();
  }
#endif
  DCHECK(write_locked_);

  // Leaves the lock held, which prevents any new readers
  // or writers.
}

void RWCLock::CommitUnlock() {
  DCHECK_EQ(0, reader_count_);
  write_locked_ = false;
#ifndef NDEBUG
  last_writer_stacktrace_.Reset();
#endif // NDEBUG
  YB_PROFILE(no_mutators_.Broadcast());
  lock_.unlock();
}

} // namespace yb
