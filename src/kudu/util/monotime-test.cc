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

#include "kudu/util/monotime.h"

#include <sys/time.h>
#include <unistd.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "kudu/util/test_util.h"

namespace kudu {

TEST(TestMonoTime, TestMonotonicity) {
  alarm(360);
  MonoTime prev(MonoTime::Now(MonoTime::FINE));
  MonoTime next;

  do {
    next = MonoTime::Now(MonoTime::FINE);
    //LOG(INFO) << " next = " << next.ToString();
  } while (!prev.ComesBefore(next));
  ASSERT_FALSE(next.ComesBefore(prev));
  alarm(0);
}

TEST(TestMonoTime, TestComparison) {
  MonoTime now(MonoTime::Now(MonoTime::COARSE));
  MonoTime future(now);
  future.AddDelta(MonoDelta::FromNanoseconds(1L));

  ASSERT_GT(future.GetDeltaSince(now).ToNanoseconds(), 0);
  ASSERT_LT(now.GetDeltaSince(future).ToNanoseconds(), 0);
  ASSERT_EQ(now.GetDeltaSince(now).ToNanoseconds(), 0);

  MonoDelta nano(MonoDelta::FromNanoseconds(1L));
  MonoDelta mil(MonoDelta::FromMilliseconds(1L));
  MonoDelta sec(MonoDelta::FromSeconds(1.0));

  ASSERT_TRUE(nano.LessThan(mil));
  ASSERT_TRUE(mil.LessThan(sec));
  ASSERT_TRUE(mil.MoreThan(nano));
  ASSERT_TRUE(sec.MoreThan(mil));
}

TEST(TestMonoTime, TestTimeVal) {
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  // Normal conversion case.
  MonoDelta one_sec_one_micro(MonoDelta::FromNanoseconds(1000001000L));
  one_sec_one_micro.ToTimeVal(&tv);
  ASSERT_EQ(1, tv.tv_sec);
  ASSERT_EQ(1, tv.tv_usec);

  // Case where we are still positive but sub-micro.
  // Round up to nearest microsecond. This is to avoid infinite timeouts
  // in APIs that take a struct timeval.
  MonoDelta zero_sec_one_nano(MonoDelta::FromNanoseconds(1L));
  zero_sec_one_nano.ToTimeVal(&tv);
  ASSERT_EQ(0, tv.tv_sec);
  ASSERT_EQ(1, tv.tv_usec); // Special case: 1ns rounds up to

  // Negative conversion case. Ensure the timeval is normalized.
  // That means sec is negative and usec is positive.
  MonoDelta neg_micro(MonoDelta::FromMicroseconds(-1L));
  ASSERT_EQ(-1000, neg_micro.ToNanoseconds());
  neg_micro.ToTimeVal(&tv);
  ASSERT_EQ(-1, tv.tv_sec);
  ASSERT_EQ(999999, tv.tv_usec);

  // Case where we are still negative but sub-micro.
  // Round up to nearest microsecond. This is to avoid infinite timeouts
  // in APIs that take a struct timeval and for consistency.
  MonoDelta zero_sec_neg_one_nano(MonoDelta::FromNanoseconds(-1L));
  zero_sec_neg_one_nano.ToTimeVal(&tv);
  ASSERT_EQ(-1, tv.tv_sec);
  ASSERT_EQ(999999, tv.tv_usec);
}

TEST(TestMonoTime, TestTimeSpec) {
  MonoTime one_sec_one_nano_expected(1000000001L);
  struct timespec ts;
  ts.tv_sec = 1;
  ts.tv_nsec = 1;
  MonoTime one_sec_one_nano_actual(ts);
  ASSERT_EQ(0, one_sec_one_nano_expected.GetDeltaSince(one_sec_one_nano_actual).ToNanoseconds());

  MonoDelta zero_sec_two_nanos(MonoDelta::FromNanoseconds(2L));
  zero_sec_two_nanos.ToTimeSpec(&ts);
  ASSERT_EQ(0, ts.tv_sec);
  ASSERT_EQ(2, ts.tv_nsec);

  // Negative conversion case. Ensure the timespec is normalized.
  // That means sec is negative and nsec is positive.
  MonoDelta neg_nano(MonoDelta::FromNanoseconds(-1L));
  ASSERT_EQ(-1, neg_nano.ToNanoseconds());
  neg_nano.ToTimeSpec(&ts);
  ASSERT_EQ(-1, ts.tv_sec);
  ASSERT_EQ(999999999, ts.tv_nsec);

}

TEST(TestMonoTime, TestDeltas) {
  alarm(360);
  const MonoDelta max_delta(MonoDelta::FromSeconds(0.1));
  MonoTime prev(MonoTime::Now(MonoTime::FINE));
  MonoTime next;
  MonoDelta cur_delta;
  do {
    next = MonoTime::Now(MonoTime::FINE);
    cur_delta = next.GetDeltaSince(prev);
  } while (cur_delta.LessThan(max_delta));
  alarm(0);
}

TEST(TestMonoTime, TestDeltaConversions) {
  // TODO: Reliably test MonoDelta::FromSeconds() considering floating-point rounding errors

  MonoDelta mil(MonoDelta::FromMilliseconds(500));
  ASSERT_EQ(500 * MonoTime::kNanosecondsPerMillisecond, mil.nano_delta_);

  MonoDelta micro(MonoDelta::FromMicroseconds(500));
  ASSERT_EQ(500 * MonoTime::kNanosecondsPerMicrosecond, micro.nano_delta_);

  MonoDelta nano(MonoDelta::FromNanoseconds(500));
  ASSERT_EQ(500, nano.nano_delta_);
}

static void DoTestMonoTimePerf(MonoTime::Granularity granularity) {
  const MonoDelta max_delta(MonoDelta::FromMilliseconds(500));
  uint64_t num_calls = 0;
  MonoTime prev(MonoTime::Now(granularity));
  MonoTime next;
  MonoDelta cur_delta;
  do {
    next = MonoTime::Now(granularity);
    cur_delta = next.GetDeltaSince(prev);
    num_calls++;
  } while (cur_delta.LessThan(max_delta));
  LOG(INFO) << "DoTestMonoTimePerf(granularity="
        << ((granularity == MonoTime::FINE) ? "FINE" : "COARSE")
        << "): " << num_calls << " in "
        << max_delta.ToString() << " seconds.";
}

TEST(TestMonoTime, TestSleepFor) {
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoDelta sleep = MonoDelta::FromMilliseconds(100);
  SleepFor(sleep);
  MonoTime end = MonoTime::Now(MonoTime::FINE);
  MonoDelta actualSleep = end.GetDeltaSince(start);
  ASSERT_GE(actualSleep.ToNanoseconds(), sleep.ToNanoseconds());
}

TEST(TestMonoTime, TestSleepForOverflow) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping test because it sleeps for ~4s";
    return;
  }

  // This quantity (~4s sleep) overflows a 32-bit integer such that
  // the value becomes 0.
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  MonoDelta sleep = MonoDelta::FromNanoseconds(1L << 32);
  SleepFor(sleep);
  MonoTime end = MonoTime::Now(MonoTime::FINE);
  MonoDelta actualSleep = end.GetDeltaSince(start);
  ASSERT_GE(actualSleep.ToNanoseconds(), sleep.ToNanoseconds());
}

TEST(TestMonoTimePerf, TestMonoTimePerfCoarse) {
  alarm(360);
  DoTestMonoTimePerf(MonoTime::COARSE);
  alarm(0);
}

TEST(TestMonoTimePerf, TestMonoTimePerfFine) {
  alarm(360);
  DoTestMonoTimePerf(MonoTime::FINE);
  alarm(0);
}

} // namespace kudu
