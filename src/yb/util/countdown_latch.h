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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/util/condition_variable.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/status_fwd.h"

namespace yb {

// This is a C++ implementation of the Java CountDownLatch
// class.
// See http://docs.oracle.com/javase/6/docs/api/java/util/concurrent/CountDownLatch.html
class CountDownLatch {
 public:
  // Initialize the latch with the given initial count.
  explicit CountDownLatch(uint64_t count);
  ~CountDownLatch();

  // Decrement the count of this latch by 'amount'
  // If the new count is less than or equal to zero, then all waiting threads are woken up.
  // If the count is already zero, this has no effect.
  void CountDown(uint64_t amount);

  // Decrement the count of this latch.
  // If the new count is zero, then all waiting threads are woken up.
  // If the count is already zero, this has no effect.
  void CountDown() {
    CountDown(1);
  }

  // Wait until the count on the latch reaches zero.
  // If the count is already zero, this returns immediately.
  void Wait() const;

  // Waits for the count on the latch to reach zero, or until 'when' time is reached.
  // Returns true if the count became zero, false otherwise.
  bool WaitUntil(MonoTime when) const;
  bool WaitUntil(CoarseTimePoint when) const;

  // Waits for the count on the latch to reach zero, or until 'delta' time elapses.
  // Returns true if the count became zero, false otherwise.
  bool WaitFor(MonoDelta delta) const;

  // Reset the latch with the given count. This is equivalent to reconstructing
  // the latch. If 'count' is 0, and there are currently waiters, those waiters
  // will be triggered as if you counted down to 0.
  void Reset(uint64_t count);
  uint64_t count() const;

  auto CountDownCallback() {
    return [this] {
      this->CountDown();
    };
  }

 private:
  mutable Mutex lock_;
  ConditionVariable cond_;

  std::atomic<uint64_t> count_;

  DISALLOW_COPY_AND_ASSIGN(CountDownLatch);
};

// Utility class which calls latch->CountDown() in its destructor.
class NODISCARD_CLASS CountDownOnScopeExit {
 public:
  explicit CountDownOnScopeExit(CountDownLatch *latch) : latch_(latch) {}
  ~CountDownOnScopeExit() {
    latch_->CountDown();
  }

 private:
  CountDownLatch *latch_;

  DISALLOW_COPY_AND_ASSIGN(CountDownOnScopeExit);
};

} // namespace yb
