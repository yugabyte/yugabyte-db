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

#include <pthread.h>
#include <sys/types.h>

#include <memory>

#include "yb/util/logging.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {

class StackTrace;

// A lock built around pthread_mutex_t. Does not allow recursion.
//
// The following checks will be performed in DEBUG mode:
//   Acquire(), TryAcquire() - the lock isn't already held.
//   Release() - the lock is already held by this thread.
//
class CAPABILITY("mutex") Mutex {
 public:
  Mutex();
  ~Mutex();

  void Acquire() ACQUIRE();
  void Release() RELEASE();
  bool TryAcquire() TRY_ACQUIRE(true);

  void lock() ACQUIRE() { Acquire(); }
  void unlock() RELEASE() { Release(); }
  bool try_lock() TRY_ACQUIRE(true) { return TryAcquire(); }

#ifndef NDEBUG
  void AssertAcquired() const ASSERT_CAPABILITY(this);
#else
  void AssertAcquired() const ASSERT_CAPABILITY(this) {}
#endif

 private:
  friend class ConditionVariable;

  pthread_mutex_t native_handle_;

#ifndef NDEBUG
  // Members and routines taking care of locks assertions.
  void CheckHeldAndUnmark();
  void CheckUnheldAndMark();

  // All private data is implicitly protected by native_handle_.
  // Be VERY careful to only access members under that lock.
  uint64_t owning_tid_;
  std::unique_ptr<StackTrace> stack_trace_;
#endif

  DISALLOW_COPY_AND_ASSIGN(Mutex);
};

// A helper class that acquires the given Lock while the MutexLock is in scope.
class SCOPED_CAPABILITY MutexLock {
 public:
  struct AlreadyAcquired {};

  // Acquires 'lock' (must be unheld) and wraps around it.
  //
  // Sample usage:
  // {
  //   MutexLock l(lock_); // acquired
  //   ...
  // } // released
  explicit MutexLock(Mutex& lock) ACQUIRE(lock) // NOLINT
    : lock_(&lock),
      owned_(true) {
    lock_->Acquire();
  }

  // Wraps around 'lock' (must already be held by this thread).
  //
  // Sample usage:
  // {
  //   lock_.Acquire(); // acquired
  //   ...
  //   MutexLock l(lock_, AlreadyAcquired());
  //   ...
  // } // released
  MutexLock(Mutex& lock, const AlreadyAcquired&) REQUIRES(lock) // NOLINT
    : lock_(&lock),
      owned_(true) {
    lock_->AssertAcquired();
  }

  void Lock() ACQUIRE() {
    DCHECK(!owned_);
    lock_->Acquire();
    owned_ = true;
  }

  void Unlock() RELEASE() {
    DCHECK(owned_);
    lock_->AssertAcquired();
    lock_->Release();
    owned_ = false;
  }

  ~MutexLock() RELEASE() {
    if (owned_) {
      Unlock();
    }
  }

  bool OwnsLock() const {
    return owned_;
  }

 private:
  Mutex* lock_;
  bool owned_;
  DISALLOW_COPY_AND_ASSIGN(MutexLock);
};

} // namespace yb
