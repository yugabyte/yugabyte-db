// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/shmem/robust_mutex.h"

#include <fcntl.h>
#include <pthread.h>

#include "yb/util/logging.h"

namespace yb {

RobustMutexImpl::RobustMutexImpl() {
  pthread_mutexattr_t mta;
  int rv = pthread_mutexattr_init(&mta);
  DCHECK_EQ(0, rv) << strerror(rv);
  rv = pthread_mutexattr_setpshared(&mta, PTHREAD_PROCESS_SHARED);
  DCHECK_EQ(0, rv) << strerror(rv);
#if PTHREAD_MUTEX_ROBUST_SUPPORTED
  rv = pthread_mutexattr_setrobust(&mta, PTHREAD_MUTEX_ROBUST);
  DCHECK_EQ(0, rv) << strerror(rv);
#endif
  rv = pthread_mutex_init(&native_handle_, &mta);
  DCHECK_EQ(0, rv) << strerror(rv);
  rv = pthread_mutexattr_destroy(&mta);
  DCHECK_EQ(0, rv) << strerror(rv);
}

RobustMutexImpl::~RobustMutexImpl() {
  int rv = pthread_mutex_destroy(&native_handle_);
  DCHECK_EQ(0, rv) << strerror(rv);
}

AcquireResult RobustMutexImpl::Acquire() {
  int rv = pthread_mutex_lock(&native_handle_);
  auto result = PostAcquire(rv);
  DCHECK_NE(AcquireResult::kFailed, result) << strerror(rv);
  return result;
}

void RobustMutexImpl::Release() {
  int rv = pthread_mutex_unlock(&native_handle_);
  DCHECK(!rv) << strerror(rv);
}

AcquireResult RobustMutexImpl::TryAcquire() {
  int rv = pthread_mutex_trylock(&native_handle_);
  return PostAcquire(rv);
}

void RobustMutexImpl::MarkCleanedUp() {
#if PTHREAD_MUTEX_ROBUST_SUPPORTED
  int rv = pthread_mutex_consistent(&native_handle_);
  DCHECK_EQ(0, rv) << strerror(rv);
#endif
}

AcquireResult RobustMutexImpl::PostAcquire(int lock_rv) {
#if PTHREAD_MUTEX_ROBUST_SUPPORTED
  if (lock_rv == EOWNERDEAD) {
    return AcquireResult::kCleanupRequired;
  }
#endif
  return lock_rv == 0 ? AcquireResult::kAcquired : AcquireResult::kFailed;
}

} // namespace yb
