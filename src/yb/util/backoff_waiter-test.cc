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

#include <functional>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/util/backoff_waiter.h"

using namespace std::chrono_literals;
using namespace std::placeholders;

namespace yb {

namespace {

Status TestFunc(CoarseTimePoint deadline, bool* retry, int* counter) {
  ++*counter;
  *retry = true;
  return STATUS(RuntimeError, "x");
}

} // anonymous namespace

TEST(BackoffWaiterTest, TestRetryFunc) {
  auto deadline = CoarseMonoClock::Now() + 1s;
  int counter = 0;
  Status s =
      RetryFunc(deadline, "retrying test func", "timed out", std::bind(TestFunc, _1, _2, &counter));
  ASSERT_TRUE(s.IsTimedOut());
  // According to BackoffWaiter, find n such that
  //   sum(2^i, 3 + 1, 3 + n) + delays < deadline_duration <= sum(2^i, 3, 3 + (n + 1)) + delays
  // n corresponds to the number of _full_ waits.  Assuming we start RetryFunc reasonably before the
  // deadline, we do
  //   1. call func
  //   1. full wait
  //   1. call func
  //   1. full wait
  //   1. ...
  //   1. call func
  //   1. capped wait
  //   1. call func
  // After n full waits, we wait once more for a duration capped to the deadline.  We do one more
  // func call then exit for passing the deadline.  This means n + 2 func calls.
  // For this specific test,
  //   sum(2^i, 3 + 1, 3 + n) + delays < 1000 <= sum(2^i, 3, 3 + (n + 1)) + delays
  // For n := 5,
  //   sum(2^i, 3 + 1, 3 + 5) = 496
  // For n := 6,
  //   sum(2^i, 3 + 1, 3 + 6) = 1008
  // Factoring in delays, expect around 5 full waits and 1 capped wait.  The expected number of
  // calls is 7.  Give it +/-1.
  LOG(INFO) << "num retries: " << counter;
  ASSERT_GE(counter, 6);
  ASSERT_LE(counter, 8);
}

} // namespace yb
