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

#pragma once

#include <memory>
#include <thread>
#include <utility>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/logging.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/shmem/robust_mutex.h"
#include "yb/util/unique_lock.h"

namespace yb {

template<typename T>
class RobustLendGuard;

template<typename T>
class RobustLentObjectReference;

// Utility to provide access to a shared memory object to a child process temporarily. This is used
// to replicate a subset of std::shared_ptr functionality (keeping the reference to an object alive
// while in use) across the process boundary.
//
// The object MUST be at the same address in both parent and child, which can be accomplished via
// use of SharedMemoryBackingAllocator and ReservedAddressSpace.
//
// The lender is responsible for keeping the shared memory object alive until the RobustLendGuard is
// destroyed, at which point the child process loses access to the object.
//
// The child process accesses the shared memory object via RobustLentObjectReference, which
// guarantees the child process maintains access for the lifetime of the reference and releases the
// reference in event of a crash.
template<typename T>
class RobustLentObject {
 public:
  RobustLentObject() = default;

  RobustLendGuard<T> Lend(T& ptr) PARENT_PROCESS_ONLY EXCLUDES(mutex_);

  RobustLentObjectReference<T> get() EXCLUDES(mutex_);

 private:
  friend class RobustLendGuard<T>;

  void Revoke() PARENT_PROCESS_ONLY EXCLUDES(mutex_);

  RobustMutexNoCleanup mutex_;
  T* ptr_ GUARDED_BY(mutex_) = nullptr;
};

template<typename T>
class RobustLendGuard {
 public:
  explicit RobustLendGuard(RobustLentObject<T>& lendee) : lendee_{&lendee} {}

  RobustLendGuard(RobustLendGuard&& other) : lendee_{std::exchange(other.lendee_, nullptr)} {}

  ~RobustLendGuard() {
    if (lendee_) {
      lendee_->Revoke();
    }
  }

 private:
  RobustLentObject<T>* lendee_;
};

template<typename T>
class RobustLentObjectReference {
 public:
  RobustLentObjectReference(UniqueLock<RobustMutexNoCleanup> lock, T* ptr)
      : lock_{std::move(lock)}, ptr_{ptr} {}

  constexpr T* get() const {
    return ptr_;
  }

  constexpr T& operator*() const {
    return *ptr_;
  }

  constexpr T* operator->() const {
    return ptr_;
  }

  constexpr operator bool() const {
    return ptr_;
  }

 private:
  UniqueLock<RobustMutexNoCleanup> lock_;
  T* const ptr_;
};

template<typename T>
RobustLendGuard<T> RobustLentObject<T>::Lend(T& ptr) {
  std::lock_guard lock(mutex_);
  DCHECK(!ptr_);
  ptr_ = &ptr;
  return RobustLendGuard<T>(*this);
}

template<typename T>
RobustLentObjectReference<T> RobustLentObject<T>::get() {
  UniqueLock lock{mutex_};
  T* ptr = ptr_;
  return RobustLentObjectReference<T>{std::move(lock), ptr};
}

template<typename T>
void RobustLentObject<T>::Revoke() {
  std::lock_guard lock(mutex_);
  DCHECK(ptr_);
  ptr_ = nullptr;
}

} // namespace yb
