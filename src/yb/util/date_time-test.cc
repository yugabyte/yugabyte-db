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
    EXPECT_EQ(baseline, ASSERT_RESULT(DateTime::IntervalFromString("3 d 4 hrs 5 mins 6 secs")));
    EXPECT_EQ(baseline,
              ASSERT_RESULT(DateTime::IntervalFromString("3 days 4 hours 5 minutes 6 seconds")));
    EXPECT_EQ(baseline, ASSERT_RESULT(DateTime::IntervalFromString("3 4:05:06")));

    // Basic Addition / Substraction.
    EXPECT_EQ(baseline + ASSERT_RESULT(DateTime::IntervalFromString("4h 6s")),
              ASSERT_RESULT(DateTime::IntervalFromString("3d 8h 5m 12s")));
    EXPECT_EQ(baseline - ASSERT_RESULT(DateTime::IntervalFromString("4h6s")),
              ASSERT_RESULT(DateTime::IntervalFromString("3d5m")));

    // Verify that basic unit conversions hold.
    EXPECT_EQ(ASSERT_RESULT(DateTime::IntervalFromString("7680 sec")),
              ASSERT_RESULT(DateTime::IntervalFromString("128 min")));
    EXPECT_EQ(ASSERT_RESULT(DateTime::IntervalFromString("7680 mins")),
              ASSERT_RESULT(DateTime::IntervalFromString("128 hrs")));
    EXPECT_EQ(ASSERT_RESULT(DateTime::IntervalFromString("3072 hours")),
              ASSERT_RESULT(DateTime::IntervalFromString("128 d")));
    EXPECT_EQ(ASSERT_RESULT(DateTime::IntervalFromString("7200 seconds")),
              ASSERT_RESULT(DateTime::IntervalFromString("2 hours")));
    EXPECT_EQ(ASSERT_RESULT(DateTime::IntervalFromString("86400 seconds")),
              ASSERT_RESULT(DateTime::IntervalFromString("1 day")));

    // Edge case formatting.
    EXPECT_EQ(ASSERT_RESULT(DateTime::IntervalFromString("")),
              ASSERT_RESULT(DateTime::IntervalFromString("0 sec")));
}

} // namespace util
} // namespace yb
