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

#include "yb/gutil/thread_annotations.h"
#include "yb/util/locks.h"
#include "yb/util/rw_mutex.h"

namespace yb::master {

// Writer-preferring reader/writer lock for CatalogManager. It is either an rw_spinlock or a
// writer-preferring RWMutex (pthread rwlock), chosen by --catalog_manager_use_blocking_mutex when
// the lock is constructed.
//
// rw_spinlock waiters poll every microsecond (nanosleep) and never block. When thousands of RPC
// threads queue behind a long shared holder, the polling uses most of the CPU, which slows the
// holder and lengthens the stall. RWMutex waiters sleep on a futex instead.
class CAPABILITY("mutex") CatalogManagerMutex {
 public:
  CatalogManagerMutex();

  void lock() ACQUIRE() NO_THREAD_SAFETY_ANALYSIS {
    if (blocking_) {
      rw_mutex_.lock();
    } else {
      spinlock_.lock();
    }
  }

  void unlock() RELEASE() NO_THREAD_SAFETY_ANALYSIS {
    if (blocking_) {
      rw_mutex_.unlock();
    } else {
      spinlock_.unlock();
    }
  }

  bool try_lock() TRY_ACQUIRE(true) NO_THREAD_SAFETY_ANALYSIS {
    return blocking_ ? rw_mutex_.try_lock() : spinlock_.try_lock();
  }

  void lock_shared() ACQUIRE_SHARED() NO_THREAD_SAFETY_ANALYSIS {
    if (blocking_) {
      rw_mutex_.lock_shared();
    } else {
      spinlock_.lock_shared();
    }
  }

  void unlock_shared() RELEASE_SHARED() NO_THREAD_SAFETY_ANALYSIS {
    if (blocking_) {
      rw_mutex_.unlock_shared();
    } else {
      spinlock_.unlock_shared();
    }
  }

  bool is_blocking() const { return blocking_; }

 private:
  const bool blocking_;
  rw_spinlock spinlock_;
  RWMutex rw_mutex_{RWMutex::Priority::PREFER_WRITING};
};

} // namespace yb::master
