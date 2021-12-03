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

#include "yb/util/date_time.h"

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

} // namespace util
} // namespace yb
