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

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "kudu/server/logical_clock.h"
#include "kudu/util/monotime.h"
#include "kudu/util/test_util.h"

namespace kudu {
namespace server {

class LogicalClockTest : public KuduTest {
 public:
  LogicalClockTest()
      : clock_(LogicalClock::CreateStartingAt(Timestamp::kInitialTimestamp)) {
  }

 protected:
  scoped_refptr<LogicalClock> clock_;
};

// Test that two subsequent time reads are monotonically increasing.
TEST_F(LogicalClockTest, TestNow_ValuesIncreaseMonotonically) {
  const Timestamp now1 = clock_->Now();
  const Timestamp now2 = clock_->Now();
  ASSERT_EQ(now1.value() + 1, now2.value());
}

// Tests that the clock gets updated if the incoming value is higher.
TEST_F(LogicalClockTest, TestUpdate_LogicalValueIncreasesByAmount) {
  Timestamp initial = clock_->Now();
  Timestamp future(initial.value() + 10);
  clock_->Update(future);
  Timestamp now = clock_->Now();
  // now should be 1 after future
  ASSERT_EQ(initial.value() + 11, now.value());
}

// Tests that the clock doesn't get updated if the incoming value is lower.
TEST_F(LogicalClockTest, TestUpdate_LogicalValueDoesNotIncrease) {
  Timestamp ts(1);
  // update the clock to 1, the initial value, should do nothing
  clock_->Update(ts);
  Timestamp now = clock_->Now();
  ASSERT_EQ(now.value(), 2);
}

TEST_F(LogicalClockTest, TestWaitUntilAfterIsUnavailable) {
  Status status = clock_->WaitUntilAfter(
      Timestamp(10), MonoTime::Now(MonoTime::FINE));
  ASSERT_TRUE(status.IsServiceUnavailable());
}

TEST_F(LogicalClockTest, TestIsAfter) {
  Timestamp ts1 = clock_->Now();
  ASSERT_TRUE(clock_->IsAfter(ts1));

  // Update the clock in the future, make sure it still
  // handles "IsAfter" properly even when it's running in
  // "logical" mode.
  Timestamp now_increased = Timestamp(1000);
  ASSERT_OK(clock_->Update(now_increased));
  Timestamp ts2 = clock_->Now();

  ASSERT_TRUE(clock_->IsAfter(ts1));
  ASSERT_TRUE(clock_->IsAfter(ts2));
}

}  // namespace server
}  // namespace kudu

