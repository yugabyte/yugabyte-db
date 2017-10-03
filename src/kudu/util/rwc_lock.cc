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

#include "kudu/util/rwc_lock.h"

#include <glog/logging.h>

#ifndef NDEBUG
#include "kudu/gutil/walltime.h"
#include "kudu/util/debug-util.h"
#include "kudu/util/env.h"
#include "kudu/util/thread.h"
#endif // NDEBUG

namespace kudu {

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
  last_writer_backtrace_[0] = '\0';
#endif // NDEBUG
}

RWCLock::~RWCLock() {
  CHECK_EQ(reader_count_, 0);
}

void RWCLock::ReadLock() {
  MutexLock l(lock_);
  reader_count_++;
}

void RWCLock::ReadUnlock() {
  MutexLock l(lock_);
  DCHECK_GT(reader_count_, 0);
  reader_count_--;
  if (reader_count_ == 0) {
    no_readers_.Signal();
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

void RWCLock::WriteLock() {
  MutexLock l(lock_);
  // Wait for any other mutations to finish.
  while (write_locked_) {
    no_mutators_.Wait();
  }
#ifndef NDEBUG
  last_writelock_acquire_time_ = GetCurrentTimeMicros();
  last_writer_tid_ = Thread::CurrentThreadId();
  HexStackTraceToString(last_writer_backtrace_, kBacktraceBufSize);
#endif // NDEBUG
  write_locked_ = true;
}

void RWCLock::WriteUnlock() {
  MutexLock l(lock_);
  DCHECK(write_locked_);
  write_locked_ = false;
#ifndef NDEBUG
  last_writer_backtrace_[0] = '\0';
#endif // NDEBUG
  no_mutators_.Signal();
}

void RWCLock::UpgradeToCommitLock() {
  lock_.lock();
  DCHECK(write_locked_);
  while (reader_count_ > 0) {
    no_readers_.Wait();
  }
  DCHECK(write_locked_);

  // Leaves the lock held, which prevents any new readers
  // or writers.
}

void RWCLock::CommitUnlock() {
  DCHECK_EQ(0, reader_count_);
  write_locked_ = false;
#ifndef NDEBUG
  last_writer_backtrace_[0] = '\0';
#endif // NDEBUG
  no_mutators_.Broadcast();
  lock_.unlock();
}

} // namespace kudu
