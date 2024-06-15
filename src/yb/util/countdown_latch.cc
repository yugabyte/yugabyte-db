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

#include "yb/util/callsite_profiling.h"
#include "yb/util/countdown_latch.h"

#include "yb/util/thread_restrictions.h"

namespace yb {

CountDownLatch::CountDownLatch(uint64_t count)
  : cond_(&lock_),
    count_(count) {
}

CountDownLatch::~CountDownLatch() {
  // Lock mutex to synchronize deletion.
  // All locks of this mutex are short, so we don't wait.
  MutexLock lock(lock_);
}

void CountDownLatch::CountDown(uint64_t amount) {
  MutexLock lock(lock_);
  auto existing_value = count_.load(std::memory_order_relaxed);
  if (existing_value == 0) {
    return;
  }

  if (amount >= existing_value) {
    count_.store(0, std::memory_order_release);
    // Latch has triggered.
    YB_PROFILE(cond_.Broadcast());
  } else {
    count_.store(existing_value - amount, std::memory_order_release);
  }
}

void CountDownLatch::Wait() const {
  if (count_.load(std::memory_order_acquire) == 0) {
    return;
  }
  ThreadRestrictions::AssertWaitAllowed();
  MutexLock lock(lock_);
  while (count_.load(std::memory_order_relaxed) > 0) {
    cond_.Wait();
  }
}

bool CountDownLatch::WaitUntil(CoarseTimePoint when) const {
  return WaitUntil(ToSteady(when));
}

bool CountDownLatch::WaitUntil(MonoTime deadline) const {
  if (count_.load(std::memory_order_acquire) == 0) {
    return true;
  }
  ThreadRestrictions::AssertWaitAllowed();
  MutexLock lock(lock_);
  while (count_.load(std::memory_order_relaxed) > 0) {
    if (!cond_.WaitUntil(deadline)) {
      return false;
    }
  }
  return true;
}

bool CountDownLatch::WaitFor(MonoDelta delta) const {
  return WaitUntil(MonoTime::Now() + delta);
}

void CountDownLatch::Reset(uint64_t count) {
  MutexLock lock(lock_);
  count_.store(count, std::memory_order_release);
  if (count != 0) {
    return;
  }
  // Awake any waiters if we reset to 0.
  YB_PROFILE(cond_.Broadcast());
}

uint64_t CountDownLatch::count() const {
  return count_.load(std::memory_order_acquire);
}

} // namespace yb
