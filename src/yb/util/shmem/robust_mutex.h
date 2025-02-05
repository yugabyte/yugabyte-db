// Copyright (c) YugaByte, Inc.
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

#include <type_traits>

#include "yb/util/enums.h"

#include "yb/gutil/thread_annotations.h"

namespace yb {

YB_DEFINE_ENUM(AcquireResult, (kAcquired)(kCleanupRequired)(kFailed));

// PTHREAD_MUTEX_ROBUST is only available as of POSIX.1-2008, which is available for Linux kernels
// 2.6.23+ (so all the versions we support), but is not available on OS X. On OS X, we just use
// a non-robust mutex and hope for the best (no crashes while locked).
#if (_XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L)
#define PTHREAD_MUTEX_ROBUST_SUPPORTED 1
#else
#define PTHREAD_MUTEX_ROBUST_SUPPORTED 0
#endif

class RobustMutexImpl {
 public:
  RobustMutexImpl();
  ~RobustMutexImpl();

  RobustMutexImpl(const RobustMutexImpl&) = delete;
  RobustMutexImpl(RobustMutexImpl&&) = delete;
  RobustMutexImpl& operator=(const RobustMutexImpl&) = delete;
  RobustMutexImpl& operator=(RobustMutexImpl&&) = delete;

  AcquireResult Acquire();
  void Release();
  AcquireResult TryAcquire();

  void MarkCleanedUp();

 private:
  AcquireResult PostAcquire(int lock_rv);

  pthread_mutex_t native_handle_;
};

static_assert(std::is_standard_layout<RobustMutexImpl>::value);

// Mutex for interprocess use in shared memory, which ensures we do not enter a deadlock when one
// process dies while holding the mutex. This mutex assumes that any state guarded by the mutex
// remains consistent in event of a crash or can be cleaned up to a consistent state.
//
// The Cleanup function takes a pointer to the mutex, and should perform cleanup with the associated
// state to make it consistent again. This function is passed as a template parameter instead of
// stored inside the class, because RobustMutex is meant to be shared across processes and cleanup
// may be called in any process (and any stored function pointer would be process-specific).
template<void (*Cleanup)(void*)>
class CAPABILITY("mutex") RobustMutex {
 public:
  void Acquire() ACQUIRE() {
    DoCleanupIfNeeded(impl_.Acquire());
  }

  void Release() RELEASE() {
    impl_.Release();
  }

  bool TryAcquire() TRY_ACQUIRE(true) {
    auto result = impl_.TryAcquire();
    DoCleanupIfNeeded(result);
    return result != AcquireResult::kFailed;
  }

  void lock() ACQUIRE() { Acquire(); }
  void unlock() RELEASE() { Release(); }
  bool try_lock() TRY_ACQUIRE(true) { return TryAcquire(); }

 private:
  void DoCleanupIfNeeded(AcquireResult result) {
    if (result == AcquireResult::kCleanupRequired) {
      Cleanup(this);
      impl_.MarkCleanedUp();
    }
  }

  RobustMutexImpl impl_;
};

} // namespace yb
