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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/gutil/port.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/logging.h"

namespace yb {

// Reader/writer lock with lowercase methods, so it works with std::lock_guard, std::unique_lock,
// std::shared_lock and yb::SharedLock.
//
// Waiting threads block in absl::Mutex, which wakes one designated waiter at a time. Once a writer
// is waiting, new readers block behind it, so a thread must not acquire the lock in shared mode
// again while it already holds it.
//
// Unlike the polling implementation this replaced, try_lock() fails while readers hold the lock,
// and the lock must be released by the thread that acquired it.
class CAPABILITY("mutex") rw_semaphore {
 public:
  void lock_shared() NO_THREAD_SAFETY_ANALYSIS {
    mutex_.ReaderLock();
  }

  void unlock_shared() NO_THREAD_SAFETY_ANALYSIS {
    mutex_.ReaderUnlock();
  }

  bool try_lock() NO_THREAD_SAFETY_ANALYSIS {
    return mutex_.TryLock();
  }

  void lock() NO_THREAD_SAFETY_ANALYSIS {
    mutex_.Lock();
  }

  void unlock() NO_THREAD_SAFETY_ANALYSIS {
    mutex_.Unlock();
  }

  // Checks that some thread holds the lock in the given mode. absl::Mutex does not record which
  // thread holds it in shared mode.
  void AssertHeld() const ASSERT_CAPABILITY(this) {
    mutex_.AssertHeld();
  }

  void AssertReaderHeld() const ASSERT_SHARED_CAPABILITY(this) {
    mutex_.AssertReaderHeld();
  }

 private:
  mutable absl::Mutex mutex_;
};

} // namespace yb
