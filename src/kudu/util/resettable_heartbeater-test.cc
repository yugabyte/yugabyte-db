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

#include "kudu/util/resettable_heartbeater.h"

#include <boost/bind/bind.hpp>
#include <boost/thread/locks.hpp>
#include <gtest/gtest.h>
#include <string>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/locks.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"
#include "kudu/util/test_util.h"

namespace kudu {

// Number of heartbeats we want to observe before allowing the test to end.
static const int kNumHeartbeats = 2;

class ResettableHeartbeaterTest : public KuduTest {
 public:
  ResettableHeartbeaterTest()
    : KuduTest(),
      latch_(kNumHeartbeats) {
  }

 protected:
  void CreateHeartbeater(uint64_t period_ms, const std::string& name) {
    period_ms_ = period_ms;
    heartbeater_.reset(
        new ResettableHeartbeater(name,
                                  MonoDelta::FromMilliseconds(period_ms),
                                  boost::bind(&ResettableHeartbeaterTest::HeartbeatFunction,
                                              this)));
  }

  Status HeartbeatFunction() {
    latch_.CountDown();
    return Status::OK();
  }

  void WaitForCountDown() {
    // Wait a large multiple (in the worst case) of the required time before we
    // time out and fail the test. Large to avoid test flakiness.
    const uint64_t kMaxWaitMillis = period_ms_ * kNumHeartbeats * 20;
    CHECK(latch_.WaitFor(MonoDelta::FromMilliseconds(kMaxWaitMillis)))
        << "Failed to count down " << kNumHeartbeats << " times in " << kMaxWaitMillis
        << " ms: latch count == " << latch_.count();
  }

  CountDownLatch latch_;
  uint64_t period_ms_;
  gscoped_ptr<ResettableHeartbeater> heartbeater_;
};

// Tests that if Reset() is not called the heartbeat method is called
// the expected number of times.
TEST_F(ResettableHeartbeaterTest, TestRegularHeartbeats) {
  const int64_t kHeartbeatPeriodMillis = 100; // Heartbeat every 100ms.
  CreateHeartbeater(kHeartbeatPeriodMillis, CURRENT_TEST_NAME());
  ASSERT_OK(heartbeater_->Start());
  WaitForCountDown();
  ASSERT_OK(heartbeater_->Stop());
}

// Tests that if we Reset() the heartbeater in a period smaller than
// the heartbeat period the heartbeat method never gets called.
// After we stop resetting heartbeats should resume as normal
TEST_F(ResettableHeartbeaterTest, TestResetHeartbeats) {
  const int64_t kHeartbeatPeriodMillis = 800;   // Heartbeat every 800ms.
  const int64_t kNumResetSlicesPerPeriod = 40;  // Reset 40 times per heartbeat period.
  // Reset once every 800ms / 40 = 20ms.
  const int64_t kResetPeriodMillis = kHeartbeatPeriodMillis / kNumResetSlicesPerPeriod;

  CreateHeartbeater(kHeartbeatPeriodMillis, CURRENT_TEST_NAME());
  ASSERT_OK(heartbeater_->Start());
  // Call Reset() in a loop for 2 heartbeat periods' worth of time, with sleeps
  // in-between as defined above.
  for (int i = 0; i < kNumResetSlicesPerPeriod * 2; i++) {
    heartbeater_->Reset();
    ASSERT_EQ(kNumHeartbeats, latch_.count()); // Ensure we haven't counted down, yet.
    SleepFor(MonoDelta::FromMilliseconds(kResetPeriodMillis));
  }
  WaitForCountDown();
  ASSERT_OK(heartbeater_->Stop());
}

}  // namespace kudu
