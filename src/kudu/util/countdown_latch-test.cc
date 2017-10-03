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

#include <boost/bind.hpp>
#include <gtest/gtest.h>

#include "kudu/util/countdown_latch.h"
#include "kudu/util/test_util.h"
#include "kudu/util/thread.h"
#include "kudu/util/threadpool.h"

namespace kudu {

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

  gscoped_ptr<ThreadPool> pool;
  ASSERT_OK(ThreadPoolBuilder("cdl-test").set_max_threads(1).Build(&pool));

  CountDownLatch latch(1000);

  // Decrement the count by 1 in another thread, this should not fire the
  // latch.
  ASSERT_OK(pool->SubmitFunc(boost::bind(DecrementLatch, &latch, 1)));
  ASSERT_FALSE(latch.WaitFor(MonoDelta::FromMilliseconds(200)));
  ASSERT_EQ(999, latch.count());

  // Now decrement by 1000 this should decrement to 0 and fire the latch
  // (even though 1000 is one more than the current count).
  ASSERT_OK(pool->SubmitFunc(boost::bind(DecrementLatch, &latch, 1000)));
  latch.Wait();
  ASSERT_EQ(0, latch.count());
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

} // namespace kudu
