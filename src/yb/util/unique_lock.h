// Copyright (c) Yugabyte, Inc.
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

#pragma once

#include <condition_variable>
#include <mutex>

#include "yb/gutil/thread_annotations.h"

namespace yb {

#if THREAD_ANNOTATIONS_ENABLED

// ------------------------------------------------------------------------------------------------
// Thread annotations enabled, using a UniqueLock wrapper class around std::unique_lock.
// ------------------------------------------------------------------------------------------------

// A wrapper unique_lock that supports thread annotations.
template<typename Mutex>
class SCOPED_CAPABILITY UniqueLock {
 public:
  UniqueLock() = default;
  explicit UniqueLock(Mutex &mutex) ACQUIRE(mutex) : unique_lock_(mutex) {}

  explicit UniqueLock(Mutex &mutex, std::defer_lock_t defer) : unique_lock_(mutex, defer) {}
  UniqueLock(UniqueLock&& rhs) = default;

  ~UniqueLock() RELEASE() = default;

  void unlock() RELEASE() { unique_lock_.unlock(); }
  void lock() ACQUIRE() { unique_lock_.lock(); }

  std::unique_lock<Mutex>& internal_unique_lock() { return unique_lock_; }

  bool owns_lock() const { return unique_lock_.owns_lock(); }

  Mutex* mutex() RETURN_CAPABILITY(unique_lock_.mutex()) { return unique_lock_.mutex(); }

 private:
  std::unique_lock<Mutex> unique_lock_;
};

template<typename Mutex>
void WaitOnConditionVariable(std::condition_variable* cond_var, UniqueLock<Mutex>* lock)
    REQUIRES(*lock) {
  cond_var->wait(lock->internal_unique_lock());
}

template<typename Mutex, typename Functor>
void WaitOnConditionVariable(
    std::condition_variable* cond_var, UniqueLock<Mutex>* lock, Functor f) {
  cond_var->wait(lock->internal_unique_lock(), f);
}

template <class Mutex>
std::unique_lock<Mutex>& GetLockForCondition(UniqueLock<Mutex>* lock) {
  return lock->internal_unique_lock();
}

#else

// ------------------------------------------------------------------------------------------------
// Thread annotations disabled, no wrapper class needed.
// ------------------------------------------------------------------------------------------------

template<class Mutex>
using UniqueLock = std::unique_lock<Mutex>;

template<typename Mutex>
void WaitOnConditionVariable(std::condition_variable* cond_var, UniqueLock<Mutex>* lock) {
  cond_var->wait(*lock);
}

template<typename Mutex, typename Functor>
void WaitOnConditionVariable(
    std::condition_variable* cond_var, UniqueLock<Mutex>* lock, Functor f) {
  cond_var->wait(*lock, f);
}

template <class Mutex>
std::unique_lock<Mutex>& GetLockForCondition(UniqueLock<Mutex>* lock) {
  return *lock;
}

#endif

} // namespace yb
