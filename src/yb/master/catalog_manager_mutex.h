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

namespace yb::master {

// Reader/writer lock for CatalogManager: absl::Mutex behind the std lock method names that
// std::lock_guard, std::unique_lock and NonRecursiveSharedLock call.
//
// It replaces rw_spinlock, whose waiters poll every microsecond (nanosleep) and never block. When
// thousands of RPC threads queue behind a long shared holder, the polling uses most of the CPU,
// which slows the holder and lengthens the stall. absl::Mutex waiters sleep on a futex, new
// readers queue once a writer waits, and a release wakes one designated waker rather than every
// waiter.
class CAPABILITY("mutex") CatalogManagerMutex {
 public:
  // NO_THREAD_SAFETY_ANALYSIS: each method acquires or releases mutex_, not *this, which the
  // analysis cannot connect.
  void lock() ACQUIRE() NO_THREAD_SAFETY_ANALYSIS { mutex_.Lock(); }
  void unlock() RELEASE() NO_THREAD_SAFETY_ANALYSIS { mutex_.Unlock(); }
  bool try_lock() TRY_ACQUIRE(true) NO_THREAD_SAFETY_ANALYSIS { return mutex_.TryLock(); }
  void lock_shared() ACQUIRE_SHARED() NO_THREAD_SAFETY_ANALYSIS { mutex_.ReaderLock(); }
  void unlock_shared() RELEASE_SHARED() NO_THREAD_SAFETY_ANALYSIS { mutex_.ReaderUnlock(); }

 private:
  absl::Mutex mutex_;
};

} // namespace yb::master
