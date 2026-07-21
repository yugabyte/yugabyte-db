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

#include "yb/util/date_time.h"

#include <cstdlib>
#include <ctime>
#include <string>

#include <boost/date_time/local_time/local_time.hpp>
#include <boost/smart_ptr/make_shared.hpp>

#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

class DateTimeTest : public YBTest {
 protected:
};

TEST_F(DateTimeTest, TestIntervalFromString) {
    // Basic format comparison.
    auto baseline = ASSERT_RESULT(DateTime::IntervalFromString("3d 4h 5m 6s"));

    auto baseline_from_string1 =
        ASSERT_RESULT(DateTime::IntervalFromString("3 d 4 hrs 5 mins 6 secs"));
    EXPECT_EQ(baseline, baseline_from_string1);

    auto baseline_from_string2 =
        ASSERT_RESULT(DateTime::IntervalFromString("3 days 4 hours 5 minutes 6 seconds"));
    EXPECT_EQ(baseline, baseline_from_string2);

    auto baseline_from_string3 = ASSERT_RESULT(DateTime::IntervalFromString("3 4:05:06"));
    EXPECT_EQ(baseline, baseline_from_string3);

    // Basic Addition / Subtraction.
    auto four_hr_six_sec_from_str = ASSERT_RESULT(DateTime::IntervalFromString("4h 6s"));
    auto three_days_8_hr_5_min_12_sec = ASSERT_RESULT(DateTime::IntervalFromString("3d 8h 5m 12s"));
    EXPECT_EQ(baseline + four_hr_six_sec_from_str, three_days_8_hr_5_min_12_sec);

    auto four_hr_six_sec_from_str2 = ASSERT_RESULT(DateTime::IntervalFromString("4h6s"));
    auto three_days_5_min = ASSERT_RESULT(DateTime::IntervalFromString("3d5m"));
    EXPECT_EQ(baseline - four_hr_six_sec_from_str2, three_days_5_min);

    // Verify that basic unit conversions hold.
    auto seconds_7680 = ASSERT_RESULT(DateTime::IntervalFromString("7680 sec"));
    auto minutes_128 = ASSERT_RESULT(DateTime::IntervalFromString("128 min"));
    EXPECT_EQ(seconds_7680, minutes_128);

    auto minutes_7680 = ASSERT_RESULT(DateTime::IntervalFromString("7680 mins"));
    auto hours_128 = ASSERT_RESULT(DateTime::IntervalFromString("128 hrs"));
    EXPECT_EQ(minutes_7680, hours_128);

    auto hours_3072 = ASSERT_RESULT(DateTime::IntervalFromString("3072 hours"));
    auto days_128 = ASSERT_RESULT(DateTime::IntervalFromString("128 d"));
    EXPECT_EQ(hours_3072, days_128);

    auto seconds_7200 = ASSERT_RESULT(DateTime::IntervalFromString("7200 seconds"));
    auto two_hours = ASSERT_RESULT(DateTime::IntervalFromString("2 hours"));
    EXPECT_EQ(seconds_7200, two_hours);

    auto seconds_86400 = ASSERT_RESULT(DateTime::IntervalFromString("86400 seconds"));
    auto one_day = ASSERT_RESULT(DateTime::IntervalFromString("1 day"));
    EXPECT_EQ(seconds_86400, one_day);

    // Edge case formatting.
    auto empty_interval_from_string = ASSERT_RESULT(DateTime::IntervalFromString(""));
    auto zero_sec = ASSERT_RESULT(DateTime::IntervalFromString("0 sec"));
    EXPECT_EQ(empty_interval_from_string, zero_sec);
}

// If there is a regression in #22191, uncomment the following line in CMakeLists.txt and build
// the LTO version of this test.
// yb_add_lto_target(date_time-test date_time-test-lto "")
TEST_F(DateTimeTest, InvalidTimestampFromString) {
  auto ts_result = DateTime::TimestampFromString("2021-10-10 10:00:00 UTC_b");
  ASSERT_NOK(ts_result);
  ASSERT_STR_CONTAINS(
      ts_result.status().ToString(),
      "bad lexical cast: source type value could not be interpreted as target");
}

// See the comment for InvalidTimestampFromString.
TEST_F(DateTimeTest, InvalidBoostTimeZoneFromString) {
  auto exception_caught = false;
  std::string exception_msg;
  try {
    auto time_zone_ptr = boost::make_shared<boost::local_time::posix_time_zone>("UTC_b");
  } catch (std::exception& e) {
    exception_caught = true;
    exception_msg = e.what();
  }
  ASSERT_TRUE(exception_caught);
  ASSERT_EQ("bad lexical cast: source type value could not be interpreted as target",
            exception_msg);
}

namespace {

// Sets TZ for the scope and restores it on exit, so the GetSystemTimezone() path (which reads TZ
// via the C library) can be exercised hermetically. POSIX offsets need no tzdata and have no DST.
// The POSIX sign is inverted from the ISO output: a positive POSIX offset is west of UTC, so
// "XYZ3:30" is UTC-03:30.
class ScopedTz {
 public:
  explicit ScopedTz(const char* tz) {
    if (const char* old = getenv("TZ")) {
      had_old_ = true;
      old_tz_ = old;
    }
    CHECK_EQ(0, setenv("TZ", tz, /* overwrite */ 1));
    tzset();
  }

  ~ScopedTz() {
    if (had_old_) {
      CHECK_EQ(0, setenv("TZ", old_tz_.c_str(), /* overwrite */ 1));
    } else {
      CHECK_EQ(0, unsetenv("TZ"));
    }
    tzset();
  }

 private:
  bool had_old_ = false;
  std::string old_tz_;
};

void ExpectSystemTimezone(const char* posix_tz, const std::string& expected) {
  ScopedTz tz(posix_tz);
  ASSERT_EQ(expected, DateTime::SystemTimezone()) << "POSIX TZ " << posix_tz;
}

// A named zone must resolve (via GetTimezone) to the same instant as its numeric standard-time
// offset; GetTimezone uses the DST-independent raw offset.
void ExpectNamedZoneMatchesOffset(const std::string& zone, const std::string& offset) {
  const std::string base = "2021-01-01 12:00:00";
  const auto from_named = ASSERT_RESULT(DateTime::TimestampFromString(base + " " + zone));
  const auto from_offset = ASSERT_RESULT(DateTime::TimestampFromString(base + offset));
  ASSERT_EQ(from_named, from_offset) << zone << " should resolve to offset " << offset;
}

} // namespace

// Regression test for #32549: GetSystemTimezone() (via SystemTimezone()) must format a negative
// fractional-hour offset as "-HH:MM", not "-HH:-MM" (7 chars), which overflowed buffer[7] and
// aborted the process via a CHECK.
TEST_F(DateTimeTest, SystemTimezoneFormatsNegativeHalfHourOffset) {
  ExpectSystemTimezone("XYZ3:30", "-03:30");   // UTC-03:30, negative half-hour
  ExpectSystemTimezone("XYZ9:30", "-09:30");   // UTC-09:30, negative half-hour
  ExpectSystemTimezone("XYZ0:30", "-00:30");   // UTC-00:30, negative sub-hour (hours == 0)
  ExpectSystemTimezone("XYZ-5:30", "+05:30");  // UTC+05:30, positive half-hour
  ExpectSystemTimezone("XYZ-0:30", "+00:30");  // UTC+00:30, positive sub-hour (hours == 0)
  ExpectSystemTimezone("XYZ5", "-05:00");      // UTC-05:00, negative whole-hour
  ExpectSystemTimezone("XYZ0", "+00:00");      // UTC
}

// Companion for the sibling formatter GetTimezone() (reached via TimestampFromString with a named
// zone), which already uses abs() but must not regress.
TEST_F(DateTimeTest, GetTimezoneFormatsNamedZoneOffset) {
  ExpectNamedZoneMatchesOffset("America/St_Johns", "-03:30");   // negative half-hour
  ExpectNamedZoneMatchesOffset("Pacific/Marquesas", "-09:30");  // negative half-hour
  ExpectNamedZoneMatchesOffset("America/New_York", "-05:00");   // negative whole-hour
  ExpectNamedZoneMatchesOffset("Asia/Kolkata", "+05:30");       // positive half-hour
  ExpectNamedZoneMatchesOffset("Asia/Kathmandu", "+05:45");     // positive 45-minute
}

} // namespace util
} // namespace yb
