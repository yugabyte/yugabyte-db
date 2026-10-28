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

#include <atomic>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/countdown_latch.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"
#include "yb/util/threadpool.h"

namespace yb {

static void DecrementLatch(CountDownLatch* latch, int amount) {
  if (amount == 1) {
    latch->CountDown();
    return;
  }
  latch->CountDown(amount);
}

// Tests that we can decrement the latch by arbitrary amounts, as well
// as 1 by one.
TEST(TestCountDownLatch, TestLatch) {

  std::unique_ptr<ThreadPool> pool;
  ASSERT_OK(ThreadPoolBuilder("cdl-test").set_max_threads(1).Build(&pool));

  CountDownLatch latch(1000);

  // Decrement the count by 1 in another thread, this should not fire the
  // latch.
  ASSERT_OK(pool->SubmitFunc(std::bind(DecrementLatch, &latch, 1)));
  ASSERT_FALSE(latch.WaitFor(MonoDelta::FromMilliseconds(1000)));
  ASSERT_EQ(999, latch.count());

  // Now decrement by 1000 this should decrement to 0 and fire the latch
  // (even though 1000 is one more than the current count).
  ASSERT_OK(pool->SubmitFunc(std::bind(DecrementLatch, &latch, 1000)));
  latch.Wait();
  ASSERT_EQ(0, latch.count());
}

// CountDown tells the caller whether its own decrement is the one that emptied the latch, so
// concurrent callers can elect exactly one of themselves to run a follow-up action.
TEST(TestCountDownLatch, TestCountDownReportsTheTriggeringCall) {
  CountDownLatch latch(3);
  ASSERT_FALSE(latch.CountDown());
  ASSERT_FALSE(latch.CountDown());
  ASSERT_TRUE(latch.CountDown());
  // Counting down an already-empty latch elects nobody.
  ASSERT_FALSE(latch.CountDown());

  // Overshooting the remaining count still reports a single trigger.
  CountDownLatch overshoot(2);
  ASSERT_TRUE(overshoot.CountDown(5));
  ASSERT_FALSE(overshoot.CountDown(5));
}

// The election has to hold when the decrements race, which is the case the callers rely on: a
// non-atomic decrement-then-read lets several threads observe a zero count and all act on it.
TEST(TestCountDownLatch, TestConcurrentCountDownElectsOne) {
  constexpr int kThreads = 8;
  constexpr int kRounds = 200;

  for (int round = 0; round < kRounds; ++round) {
    CountDownLatch latch(kThreads);
    CountDownLatch start(1);
    std::atomic<int> triggers(0);

    std::vector<scoped_refptr<Thread>> threads;
    for (int i = 0; i < kThreads; ++i) {
      scoped_refptr<Thread> t;
      ASSERT_OK(Thread::Create("test", "cdl-test", [&latch, &start, &triggers]() {
        start.Wait();
        if (latch.CountDown()) {
          triggers.fetch_add(1, std::memory_order_relaxed);
        }
      }, &t));
      threads.push_back(t);
    }
    start.CountDown();
    for (auto& t : threads) {
      t->Join();
    }

    ASSERT_EQ(0, latch.count());
    ASSERT_EQ(1, triggers.load(std::memory_order_relaxed)) << "round " << round;
  }
}

// Test that resetting to zero while there are waiters lets the waiters
// continue.
TEST(TestCountDownLatch, TestResetToZero) {
  CountDownLatch cdl(100);
  scoped_refptr<Thread> t;
  ASSERT_OK(Thread::Create("test", "cdl-test", &CountDownLatch::Wait, &cdl, &t));

  // Sleep for a bit until it's likely the other thread is waiting on the latch.
  SleepFor(MonoDelta::FromMilliseconds(10));
  cdl.Reset(0);
  t->Join();
}

} // namespace yb
