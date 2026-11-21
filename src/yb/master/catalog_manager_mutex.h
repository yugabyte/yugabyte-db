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

#include <absl/synchronization/mutex.h>

#include "yb/gutil/thread_annotations.h"
#include "yb/util/locks.h"
#include "yb/util/rw_mutex.h"

namespace yb::master {

// Reader/writer lock for CatalogManager, chosen by --catalog_manager_mutex_type when the lock is
// constructed: rw_spinlock, a writer-preferring RWMutex (pthread rwlock), or absl::Mutex.
//
// rw_spinlock waiters poll every microsecond (nanosleep) and never block. When thousands of RPC
// threads queue behind a long shared holder, the polling uses most of the CPU, which slows the
// holder and lengthens the stall. RWMutex and absl::Mutex waiters sleep on a futex instead.
// absl::Mutex also wakes one designated waker at a time rather than every waiter.
class CAPABILITY("mutex") CatalogManagerMutex {
 public:
  enum class Type { kSpinlock, kRWMutex, kAbsl };

  CatalogManagerMutex();

  void lock() ACQUIRE() NO_THREAD_SAFETY_ANALYSIS {
    switch (type_) {
      case Type::kSpinlock: spinlock_.lock(); return;
      case Type::kRWMutex: rw_mutex_.lock(); return;
      case Type::kAbsl: absl_mutex_.Lock(); return;
    }
  }

  void unlock() RELEASE() NO_THREAD_SAFETY_ANALYSIS {
    switch (type_) {
      case Type::kSpinlock: spinlock_.unlock(); return;
      case Type::kRWMutex: rw_mutex_.unlock(); return;
      case Type::kAbsl: absl_mutex_.Unlock(); return;
    }
  }

  bool try_lock() TRY_ACQUIRE(true) NO_THREAD_SAFETY_ANALYSIS {
    switch (type_) {
      case Type::kSpinlock: return spinlock_.try_lock();
      case Type::kRWMutex: return rw_mutex_.try_lock();
      case Type::kAbsl: return absl_mutex_.TryLock();
    }
    return false;
  }

  void lock_shared() ACQUIRE_SHARED() NO_THREAD_SAFETY_ANALYSIS {
    switch (type_) {
      case Type::kSpinlock: spinlock_.lock_shared(); return;
      case Type::kRWMutex: rw_mutex_.lock_shared(); return;
      case Type::kAbsl: absl_mutex_.ReaderLock(); return;
    }
  }

  void unlock_shared() RELEASE_SHARED() NO_THREAD_SAFETY_ANALYSIS {
    switch (type_) {
      case Type::kSpinlock: spinlock_.unlock_shared(); return;
      case Type::kRWMutex: rw_mutex_.unlock_shared(); return;
      case Type::kAbsl: absl_mutex_.ReaderUnlock(); return;
    }
  }

  Type type() const { return type_; }

 private:
  const Type type_;
  rw_spinlock spinlock_;
  RWMutex rw_mutex_{RWMutex::Priority::PREFER_WRITING};
  absl::Mutex absl_mutex_;
};

} // namespace yb::master
