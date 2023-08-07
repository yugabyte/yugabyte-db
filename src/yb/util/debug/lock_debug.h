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

#include <atomic>
#include <mutex>

#include "yb/gutil/thread_annotations.h"

namespace yb {

class NonRecursiveSharedLockBase {
 public:
  explicit NonRecursiveSharedLockBase(void* mutex);
  ~NonRecursiveSharedLockBase();

 protected:
  void* mutex() const {
    return mutex_;
  }

 private:
  void* mutex_;
  NonRecursiveSharedLockBase* next_;
};

template<class Mutex>
class SCOPED_CAPABILITY NonRecursiveSharedLock : public NonRecursiveSharedLockBase {
  bool acquired;
 public:
  explicit NonRecursiveSharedLock(Mutex& mutex) ACQUIRE_SHARED(mutex) // NOLINT
      : NonRecursiveSharedLockBase(&mutex) {
    mutex.lock_shared();
    acquired = true;
  }

  void Release() RELEASE() {
    if (acquired) {
      static_cast<Mutex*>(mutex())->unlock_shared();
    }
    acquired = false;
  }

  ~NonRecursiveSharedLock() RELEASE() {
    Release();
  }
};

// Mutex that allows only single lock at a time.
// Logs DFATAL in case of concurrent access.
// Could be used by classes that a designed to be single threaded to check single threaded access.
class SingleThreadedMutex {
 public:
  void lock();
  void unlock();
  bool try_lock();
 private:
  std::atomic<bool> locked_{false};
};

// Atomic that allows only single threaded access.
// Could be used by classes that a designed to be single threaded to check single threaded access.
template <class T>
class SingleThreadedAtomic {
 public:
  SingleThreadedAtomic() = default;
  explicit SingleThreadedAtomic(const T& t) : value_(t) {}

  T load(std::memory_order) const {
    std::lock_guard lock(mutex_);
    return value_;
  }

  void store(const T& value, std::memory_order) {
    std::lock_guard lock(mutex_);
    value_ = value;
  }

  bool compare_exchange_strong(T& old_value, const T& new_value) { // NOLINT
    std::lock_guard lock(mutex_);
    if (value_ == old_value) {
      value_ = new_value;
      return true;
    }
    old_value = value_;
    return false;
  }

 private:
  T value_;
  mutable SingleThreadedMutex mutex_;
};

}  // namespace yb
