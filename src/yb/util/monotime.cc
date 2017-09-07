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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/util/monotime.h"

#include <limits>
#include <glog/logging.h>

#include "yb/gutil/mathlimits.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/walltime.h"
#include "yb/util/thread_restrictions.h"

namespace yb {

#define MAX_MONOTONIC_SECONDS \
  (((1ULL<<63) - 1ULL) /(int64_t)MonoTime::kNanosecondsPerSecond)

namespace {

bool SafeToAdd64(int64_t a, int64_t b) {
  bool negativeA = a < 0;
  bool negativeB = b < 0;
  if (negativeA != negativeB) {
    return true;
  }
  bool negativeSum = (a + b) < 0;
  return negativeSum == negativeA;
}

} // namespace

///
/// MonoDelta
///

const MonoDelta::NanoDeltaType MonoDelta::kUninitialized =
    std::numeric_limits<NanoDeltaType>::min();
const MonoDelta MonoDelta::kMin = MonoDelta(std::numeric_limits<NanoDeltaType>::min() + 1);
const MonoDelta MonoDelta::kMax = MonoDelta(std::numeric_limits<NanoDeltaType>::max());
const MonoDelta MonoDelta::kZero = MonoDelta(0);

MonoDelta MonoDelta::FromSeconds(double seconds) {
  CHECK_LE(seconds, std::numeric_limits<int64_t>::max() / MonoTime::kNanosecondsPerSecond);
  int64_t delta = seconds * MonoTime::kNanosecondsPerSecond;
  return MonoDelta(delta);
}

MonoDelta MonoDelta::FromMilliseconds(int64_t ms) {
  CHECK_LE(ms, std::numeric_limits<int64_t>::max() / MonoTime::kNanosecondsPerMillisecond);
  return MonoDelta(ms * MonoTime::kNanosecondsPerMillisecond);
}

MonoDelta MonoDelta::FromMicroseconds(int64_t us) {
  CHECK_LE(us, std::numeric_limits<int64_t>::max() / MonoTime::kNanosecondsPerMicrosecond);
  return MonoDelta(us * MonoTime::kNanosecondsPerMicrosecond);
}

MonoDelta MonoDelta::FromNanoseconds(int64_t ns) {
  return MonoDelta(ns);
}

MonoDelta::MonoDelta()
  : nano_delta_(kUninitialized) {
}

bool MonoDelta::Initialized() const {
  return nano_delta_ != kUninitialized;
}

bool MonoDelta::LessThan(const MonoDelta &rhs) const {
  DCHECK(Initialized());
  DCHECK(rhs.Initialized());
  return nano_delta_ < rhs.nano_delta_;
}

bool MonoDelta::MoreThan(const MonoDelta &rhs) const {
  DCHECK(Initialized());
  DCHECK(rhs.Initialized());
  return nano_delta_ > rhs.nano_delta_;
}

bool MonoDelta::Equals(const MonoDelta &rhs) const {
  DCHECK(Initialized());
  DCHECK(rhs.Initialized());
  return nano_delta_ == rhs.nano_delta_;
}

std::string MonoDelta::ToString() const {
  return StringPrintf("%.3fs", ToSeconds());
}

MonoDelta::MonoDelta(int64_t delta)
  : nano_delta_(delta) {
}

double MonoDelta::ToSeconds() const {
  DCHECK(Initialized());
  double d(nano_delta_);
  d /= MonoTime::kNanosecondsPerSecond;
  return d;
}

int64_t MonoDelta::ToNanoseconds() const {
  DCHECK(Initialized());
  return nano_delta_;
}

int64_t MonoDelta::ToMicroseconds() const {
  DCHECK(Initialized());
  return nano_delta_ / MonoTime::kNanosecondsPerMicrosecond;
}

int64_t MonoDelta::ToMilliseconds() const {
  DCHECK(Initialized());
  return nano_delta_ / MonoTime::kNanosecondsPerMillisecond;
}

MonoDelta& MonoDelta::operator+=(const MonoDelta& rhs) {
  DCHECK(Initialized());
  DCHECK(rhs.Initialized());
  DCHECK(SafeToAdd64(nano_delta_, rhs.nano_delta_));
  DCHECK(nano_delta_ + rhs.nano_delta_ != kUninitialized);
  nano_delta_ += rhs.nano_delta_;
  return *this;
}

void MonoDelta::ToTimeVal(struct timeval *tv) const {
  DCHECK(Initialized());
  tv->tv_sec = nano_delta_ / MonoTime::kNanosecondsPerSecond;
  tv->tv_usec = (nano_delta_ - (tv->tv_sec * MonoTime::kNanosecondsPerSecond))
      / MonoTime::kNanosecondsPerMicrosecond;

  // tv_usec must be between 0 and 999999.
  // There is little use for negative timevals so wrap it in PREDICT_FALSE.
  if (PREDICT_FALSE(tv->tv_usec < 0)) {
    --(tv->tv_sec);
    tv->tv_usec += 1000000;
  }

  // Catch positive corner case where we "round down" and could potentially set a timeout of 0.
  // Make it 1 usec.
  if (PREDICT_FALSE(tv->tv_usec == 0 && tv->tv_sec == 0 && nano_delta_ > 0)) {
    tv->tv_usec = 1;
  }

  // Catch negative corner case where we "round down" and could potentially set a timeout of 0.
  // Make it -1 usec (but normalized, so tv_usec is not negative).
  if (PREDICT_FALSE(tv->tv_usec == 0 && tv->tv_sec == 0 && nano_delta_ < 0)) {
    tv->tv_sec = -1;
    tv->tv_usec = 999999;
  }
}


void MonoDelta::NanosToTimeSpec(int64_t nanos, struct timespec* ts) {
  ts->tv_sec = nanos / MonoTime::kNanosecondsPerSecond;
  ts->tv_nsec = nanos - (ts->tv_sec * MonoTime::kNanosecondsPerSecond);

  // tv_nsec must be between 0 and 999999999.
  // There is little use for negative timespecs so wrap it in PREDICT_FALSE.
  if (PREDICT_FALSE(ts->tv_nsec < 0)) {
    --(ts->tv_sec);
    ts->tv_nsec += MonoTime::kNanosecondsPerSecond;
  }
}

void MonoDelta::ToTimeSpec(struct timespec *ts) const {
  DCHECK(Initialized());
  NanosToTimeSpec(nano_delta_, ts);
}

///
/// MonoTime
///

const MonoTime MonoTime::kMin = MonoTime::Min();
const MonoTime MonoTime::kMax = MonoTime::Max();

MonoTime MonoTime::Now(enum Granularity granularity) {
#if defined(__APPLE__)
  return MonoTime(walltime_internal::GetMonoTimeNanos());
# else
  struct timespec ts;
  clockid_t clock;

// Older systems do not support CLOCK_MONOTONIC_COARSE
#ifdef CLOCK_MONOTONIC_COARSE
  clock = (granularity == COARSE) ? CLOCK_MONOTONIC_COARSE : CLOCK_MONOTONIC;
#else
  clock = CLOCK_MONOTONIC;
#endif
  PCHECK(clock_gettime(clock, &ts) == 0);
  return MonoTime(ts);
#endif // defined(__APPLE__)
}

MonoTime MonoTime::Max() {
  return MonoTime(std::numeric_limits<int64_t>::max());
}

MonoTime MonoTime::Min() {
  return MonoTime(1);
}

bool MonoTime::IsMax() const {
  static const MonoTime MAX_MONO = Max();

  return Equals(MAX_MONO);
}

const MonoTime& MonoTime::Earliest(const MonoTime& a, const MonoTime& b) {
  if (b.nanos_ < a.nanos_) {
    return b;
  }
  return a;
}

MonoTime::MonoTime()
  : nanos_(0) {
}

bool MonoTime::Initialized() const {
  return nanos_ != 0;
}

MonoDelta MonoTime::GetDeltaSince(const MonoTime &rhs) const {
  DCHECK(Initialized());
  DCHECK(rhs.Initialized());
  int64_t delta(nanos_);
  delta -= rhs.nanos_;
  return MonoDelta(delta);
}

void MonoTime::AddDelta(const MonoDelta &delta) {
  DCHECK(Initialized());
  DCHECK(delta.Initialized());
  nanos_ += delta.nano_delta_;
}

bool MonoTime::ComesBefore(const MonoTime &rhs) const {
  DCHECK(Initialized());
  DCHECK(rhs.Initialized());
  return nanos_ < rhs.nanos_;
}

std::string MonoTime::ToString() const {
  return StringPrintf("%.3fs", ToSeconds());
}

bool MonoTime::Equals(const MonoTime& other) const {
  return nanos_ == other.nanos_;
}

MonoTime::MonoTime(const struct timespec &ts) {
  // Monotonic time resets when the machine reboots.  The 64-bit limitation
  // means that we can't represent times larger than 292 years, which should be
  // adequate.
  CHECK_LT(ts.tv_sec, MAX_MONOTONIC_SECONDS);
  nanos_ = ts.tv_sec;
  nanos_ *= MonoTime::kNanosecondsPerSecond;
  nanos_ += ts.tv_nsec;
}

MonoTime::MonoTime(int64_t nanos)
  : nanos_(nanos) {
}

double MonoTime::ToSeconds() const {
  double d(nanos_);
  d /= MonoTime::kNanosecondsPerSecond;
  return d;
}

void MonoTime::MakeAtLeast(MonoTime rhs) {
  if (rhs.Initialized() && (!Initialized() || nanos_ < rhs.nanos_)) {
    nanos_ = rhs.nanos_;
  }
}

// ------------------------------------------------------------------------------------------------

std::string FormatForComparisonFailureMessage(const MonoDelta& op, const MonoDelta& other) {
  return op.ToString();
}

void SleepFor(const MonoDelta& delta) {
  ThreadRestrictions::AssertWaitAllowed();
  base::SleepForNanoseconds(delta.ToNanoseconds());
}

} // namespace yb
