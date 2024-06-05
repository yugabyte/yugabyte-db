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

#include "yb/util/rw_mutex.h"

#include <mutex>

#include "yb/util/logging.h"

#include "yb/gutil/map-util.h"
#include "yb/util/env.h"

using std::lock_guard;

namespace {

void unlock_rwlock(pthread_rwlock_t* rwlock) {
  int rv = pthread_rwlock_unlock(rwlock);
  DCHECK_EQ(0, rv) << strerror(rv);
}

} // anonymous namespace

namespace yb {

RWMutex::RWMutex()
#ifndef NDEBUG
    : writer_tid_(0)
#endif
{
  Init(Priority::PREFER_READING);
}

RWMutex::RWMutex(Priority prio)
#ifndef NDEBUG
    : writer_tid_(0)
#endif
{
  Init(prio);
}

void RWMutex::Init(Priority prio) {
#ifdef __linux__
  // Adapt from priority to the pthread type.
  int kind = PTHREAD_RWLOCK_PREFER_READER_NP;
  switch (prio) {
    case Priority::PREFER_READING:
      kind = PTHREAD_RWLOCK_PREFER_READER_NP;
      break;
    case Priority::PREFER_WRITING:
      kind = PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP;
      break;
  }

  // Initialize the new rwlock with the user's preference.
  pthread_rwlockattr_t attr;
  int rv = pthread_rwlockattr_init(&attr);
  DCHECK_EQ(0, rv) << strerror(rv);
  rv = pthread_rwlockattr_setkind_np(&attr, kind);
  DCHECK_EQ(0, rv) << strerror(rv);
  rv = pthread_rwlock_init(&native_handle_, &attr);
  DCHECK_EQ(0, rv) << strerror(rv);
  rv = pthread_rwlockattr_destroy(&attr);
  DCHECK_EQ(0, rv) << strerror(rv);
#else
  int rv = pthread_rwlock_init(&native_handle_, NULL);
  DCHECK_EQ(0, rv) << strerror(rv);
#endif
}

RWMutex::~RWMutex() {
  int rv = pthread_rwlock_destroy(&native_handle_);
  DCHECK_EQ(0, rv) << strerror(rv);
}

void RWMutex::ReadLock() {
  CheckLockState(LockState::NEITHER);
  int rv = pthread_rwlock_rdlock(&native_handle_);
  DCHECK_EQ(0, rv) << strerror(rv);
  MarkForReading();
}

void RWMutex::ReadUnlock() {
  CheckLockState(LockState::READER);
  UnmarkForReading();
  unlock_rwlock(&native_handle_);
}

bool RWMutex::TryReadLock() {
  CheckLockState(LockState::NEITHER);
  int rv = pthread_rwlock_tryrdlock(&native_handle_);
  if (rv == EBUSY) {
    return false;
  }
  DCHECK_EQ(0, rv) << strerror(rv);
  MarkForReading();
  return true;
}

void RWMutex::WriteLock() {
  CheckLockState(LockState::NEITHER);
  int rv = pthread_rwlock_wrlock(&native_handle_);
  DCHECK_EQ(0, rv) << strerror(rv);
  MarkForWriting();
}

void RWMutex::WriteUnlock() {
  CheckLockState(LockState::WRITER);
  UnmarkForWriting();
  unlock_rwlock(&native_handle_);
}

bool RWMutex::TryWriteLock() {
  CheckLockState(LockState::NEITHER);
  int rv = pthread_rwlock_trywrlock(&native_handle_);
  if (rv == EBUSY) {
    return false;
  }
  DCHECK_EQ(0, rv) << strerror(rv);
  MarkForWriting();
  return true;
}

#ifndef NDEBUG

void RWMutex::AssertAcquiredForReading() const {
  lock_guard<simple_spinlock> l(tid_lock_);
  CHECK(ContainsKey(reader_tids_, Env::Default()->gettid()));
}

void RWMutex::AssertAcquiredForWriting() const {
  lock_guard<simple_spinlock> l(tid_lock_);
  CHECK_EQ(Env::Default()->gettid(), writer_tid_);
}

void RWMutex::CheckLockState(LockState state) const {
  auto my_tid = Env::Default()->gettid();
  bool is_reader;
  bool is_writer;
  {
    lock_guard<simple_spinlock> l(tid_lock_);
    is_reader = ContainsKey(reader_tids_, my_tid);
    is_writer = writer_tid_ == my_tid;
  }

  switch (state) {
    case LockState::NEITHER:
      CHECK(!is_reader) << "Invalid state, already holding lock for reading";
      CHECK(!is_writer) << "Invalid state, already holding lock for writing";
      break;
    case LockState::READER:
      CHECK(!is_writer) << "Invalid state, already holding lock for writing";
      CHECK(is_reader) << "Invalid state, wasn't holding lock for reading";
      break;
    case LockState::WRITER:
      CHECK(!is_reader) << "Invalid state, already holding lock for reading";
      CHECK(is_writer) << "Invalid state, wasn't holding lock for writing";
      break;
  }
}

void RWMutex::MarkForReading() {
  lock_guard<simple_spinlock> l(tid_lock_);
  reader_tids_.insert(Env::Default()->gettid());
}

void RWMutex::MarkForWriting() {
  lock_guard<simple_spinlock> l(tid_lock_);
  writer_tid_ = Env::Default()->gettid();
}

void RWMutex::UnmarkForReading() {
  lock_guard<simple_spinlock> l(tid_lock_);
  reader_tids_.erase(Env::Default()->gettid());
}

void RWMutex::UnmarkForWriting() {
  lock_guard<simple_spinlock> l(tid_lock_);
  writer_tid_ = 0;
}

#endif

} // namespace yb
