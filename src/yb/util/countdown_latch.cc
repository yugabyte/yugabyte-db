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

#include "yb/util/countdown_latch.h"

#include "yb/util/thread_restrictions.h"

namespace yb {

CountDownLatch::CountDownLatch(int count)
  : cond_(&lock_),
    count_(count) {
}

void CountDownLatch::CountDown(uint64_t amount) {
  MutexLock lock(lock_);
  if (count_ == 0) {
    return;
  }

  if (amount >= count_) {
    count_ = 0;
  } else {
    count_ -= amount;
  }

  if (count_ == 0) {
    // Latch has triggered.
    cond_.Broadcast();
  }
}

void CountDownLatch::Wait() const {
  ThreadRestrictions::AssertWaitAllowed();
  MutexLock lock(lock_);
  while (count_ > 0) {
    cond_.Wait();
  }
}

bool CountDownLatch::WaitFor(const MonoDelta& delta) const {
  ThreadRestrictions::AssertWaitAllowed();
  MutexLock lock(lock_);
  while (count_ > 0) {
    if (!cond_.TimedWait(delta)) {
      return false;
    }
  }
  return true;
}

void CountDownLatch::Reset(uint64_t count) {
  MutexLock lock(lock_);
  count_ = count;
  if (count_ == 0) {
    // Awake any waiters if we reset to 0.
    cond_.Broadcast();
  }
}

uint64_t CountDownLatch::count() const {
  MutexLock lock(lock_);
  return count_;
}

} // namespace yb
