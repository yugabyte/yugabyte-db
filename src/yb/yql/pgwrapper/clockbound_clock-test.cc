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

#include <optional>
#include <random>

#include "yb/common/hybrid_time.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/server/clockbound_clock.h"
#include "yb/util/math_util.h"
#include "yb/util/physical_time.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

DECLARE_uint64(clockbound_clock_error_estimate_usec);
DECLARE_bool(clockbound_mixed_clock_mode);
DECLARE_uint64(max_clock_skew_usec);

namespace yb::pgwrapper {

class ClockboundClockTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();

    // Create a ClockboundClock and register
    // a fake underlying clock to test its logic.
    fake_clock_ = std::make_shared<MockClock>();
    clockbound_clock_ = server::CreateClockboundClock(fake_clock_);

    // Random number generator for clock error.
    std::random_device rd;
    auto random_value = rd();
    LOG_WITH_FUNC(INFO) << "Seed: " << random_value;
    gen_.emplace(random_value);
  }

  // Assert that each global limit is higher than the read point
  // of all other hybrid times.
  void AssertOverlap(ReadHybridTime ht0, ReadHybridTime ht1) {
    ASSERT_LE(ht0.read, ht0.global_limit);
    ASSERT_LE(ht1.read, ht0.global_limit);
    ASSERT_LE(ht0.read, ht1.global_limit);
    ASSERT_LE(ht1.read, ht1.global_limit);
  }

  // Clock error varies between 1/4 and 3/4 of the estimated error.
  PhysicalTime SampleWellSynchronizedClock(MicrosTime reference_time) {
    std::uniform_int_distribution<MicrosTime> dist{
        FLAGS_clockbound_clock_error_estimate_usec / 4,
        3 * FLAGS_clockbound_clock_error_estimate_usec / 4
    };
    auto earliest = reference_time - dist(*gen_);
    auto latest = reference_time + dist(*gen_);
    auto error = ceil_div(latest - earliest, MicrosTime(2));

    auto real_time = earliest + error;
    return PhysicalTime{ real_time, error };
  }

  // Clock error varies between 2 and 4 times the estimated error.
  PhysicalTime SamplePoorlySynchronizedClock(MicrosTime reference_time) {
    std::uniform_int_distribution<MicrosTime> dist{
        2 * FLAGS_clockbound_clock_error_estimate_usec,
        4 * FLAGS_clockbound_clock_error_estimate_usec
    };
    auto earliest = reference_time - dist(*gen_);
    auto latest = reference_time + dist(*gen_);
    auto error = yb::ceil_div(latest - earliest, MicrosTime(2));

    auto real_time = earliest + error;
    return PhysicalTime{ real_time, error };
  }

  // Returns time such that earliest = reference_time.
  PhysicalTime RefTimeEarliest(MicrosTime reference_time, bool well_sync) {
    MicrosTime error = FLAGS_clockbound_clock_error_estimate_usec / 2;
    if (!well_sync) {
      error *= 3;
    }

    auto real_time = reference_time + error;
    return PhysicalTime{ real_time, error };
  }

  // Returns time such that latest = reference_time.
  PhysicalTime RefTimeLatest(MicrosTime reference_time, bool well_sync) {
    auto error = FLAGS_clockbound_clock_error_estimate_usec / 2;
    if (!well_sync) {
      error *= 3;
    }
    auto earliest = reference_time - 2 * error;

    auto real_time = earliest + error;
    return PhysicalTime{ real_time, error };
  }

  Result<ReadHybridTime> GetReadTime(PhysicalTime reference_time) {
    fake_clock_->Set(reference_time);
    auto now = VERIFY_RESULT(clockbound_clock_->Now());
    auto window = ReadHybridTime::FromMicros(now.time_point);
    window.global_limit =
        HybridTime::FromMicros(clockbound_clock_->MaxGlobalTime(now));
    return window;
  }

  PhysicalClockPtr clockbound_clock_;
  std::shared_ptr<MockClock> fake_clock_;
  std::optional<std::mt19937> gen_;
};

TEST_F(ClockboundClockTest, PropagateError) {
  fake_clock_->Set(STATUS(IOError, "Clock error"));
  ASSERT_NOK_STR_CONTAINS(clockbound_clock_->Now(), "Clock error");
}

TEST_F(ClockboundClockTest, NoClockError) {
  fake_clock_->Set(PhysicalTime{ 100, 0 });
  auto time = ASSERT_RESULT(clockbound_clock_->Now());
  ASSERT_EQ(100, time.time_point);
  ASSERT_EQ(0, time.max_error);
}

TEST_F(ClockboundClockTest, MixedClockMode) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_clockbound_mixed_clock_mode) = true;
  const auto maxskew = FLAGS_max_clock_skew_usec;
  fake_clock_->Set(PhysicalTime{ maxskew , 0 });
  auto time = ASSERT_RESULT(clockbound_clock_->Now());
  ASSERT_EQ(2 * maxskew, clockbound_clock_->MaxGlobalTime(time));
}

// Write happens on a node thats ahead of reference clock.
// Read happens on a node thats behind reference clock.
// Ensures that the read window overlaps the write window.
//
// Write:            [-----------]
// Read: [-----------]
TEST_F(ClockboundClockTest, ReadAfterWrite) {
  for (auto read_sync : { true, false }) {
    for (auto write_sync : { true, false }) {
      LOG_WITH_FUNC(INFO) << "Read sync: " << read_sync
                          << ", Write sync: " << write_sync;
      auto reference_time = GetCurrentTimeMicros();
      auto read_window = ASSERT_RESULT(GetReadTime(
          RefTimeLatest(reference_time, read_sync)));
      auto write_window = ASSERT_RESULT(GetReadTime(
          RefTimeEarliest(reference_time, write_sync)));
      AssertOverlap(read_window, write_window);
    }
  }
}

TEST_F(ClockboundClockTest, TwoWellSynchronizedClocks) {
  for (int i = 0; i < 10000; i++) {
    auto reference_time = GetCurrentTimeMicros();
    auto window0 = ASSERT_RESULT(GetReadTime(SampleWellSynchronizedClock(reference_time)));
    auto window1 = ASSERT_RESULT(GetReadTime(SampleWellSynchronizedClock(reference_time)));
    AssertOverlap(window0, window1);
  }
}

TEST_F(ClockboundClockTest, TwoPoorlySynchronizedClocks) {
  for (int i = 0; i < 10000; i++) {
    auto reference_time = GetCurrentTimeMicros();
    auto window0 = ASSERT_RESULT(GetReadTime(
        SamplePoorlySynchronizedClock(reference_time)));
    auto window1 = ASSERT_RESULT(GetReadTime(
        SamplePoorlySynchronizedClock(reference_time)));
    AssertOverlap(window0, window1);
  }
}

TEST_F(ClockboundClockTest, DifferentlySynchronizedClocks) {
  for (int i = 0; i < 10000; i++) {
    auto reference_time = GetCurrentTimeMicros();
    auto window0 = ASSERT_RESULT(GetReadTime(
        SampleWellSynchronizedClock(reference_time)));
    auto window1 = ASSERT_RESULT(GetReadTime(
        SamplePoorlySynchronizedClock(reference_time)));
    AssertOverlap(window0, window1);
  }
}

} // namespace yb::pgwrapper
