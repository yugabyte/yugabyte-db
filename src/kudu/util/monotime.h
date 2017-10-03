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
#ifndef KUDU_UTIL_MONOTIME_H
#define KUDU_UTIL_MONOTIME_H

#include <stdint.h>
#include <string>

#ifdef KUDU_HEADERS_NO_STUBS
#include <gtest/gtest_prod.h>
#else
// This is a poor module interdependency, but the stubs are header-only and
// it's only for exported header builds, so we'll make an exception.
#include "kudu/client/stubs.h"
#endif

#include "kudu/util/kudu_export.h"

struct timeval;
struct timespec;

namespace kudu {
class MonoTime;

// Represent an elapsed duration of time -- i.e the delta between
// two MonoTime instances.
//
// A MonoDelta built with the default constructor is "uninitialized" and
// may not be used for any operation.
class KUDU_EXPORT MonoDelta {
 public:
  static MonoDelta FromSeconds(double seconds);
  static MonoDelta FromMilliseconds(int64_t ms);
  static MonoDelta FromMicroseconds(int64_t us);
  static MonoDelta FromNanoseconds(int64_t ns);
  MonoDelta();
  bool Initialized() const;
  bool LessThan(const MonoDelta &rhs) const;
  bool MoreThan(const MonoDelta &rhs) const;
  bool Equals(const MonoDelta &rhs) const;
  std::string ToString() const;
  double ToSeconds() const;
  int64_t ToMilliseconds() const;
  int64_t ToMicroseconds() const;
  int64_t ToNanoseconds() const;

  // Update struct timeval to current value of delta, with microsecond accuracy.
  // Note that if MonoDelta::IsPositive() returns true, the struct timeval
  // is guaranteed to hold a positive number as well (at least 1 microsecond).
  void ToTimeVal(struct timeval *tv) const;

  // Update struct timespec to current value of delta, with nanosecond accuracy.
  void ToTimeSpec(struct timespec *ts) const;

  // Convert a nanosecond value to a timespec.
  static void NanosToTimeSpec(int64_t nanos, struct timespec* ts);

 private:
  static const int64_t kUninitialized;

  friend class MonoTime;
  FRIEND_TEST(TestMonoTime, TestDeltaConversions);
  explicit MonoDelta(int64_t delta);
  int64_t nano_delta_;
};

// Represent a particular point in time, relative to some fixed but unspecified
// reference point.
//
// This time is monotonic, meaning that if the user changes his or her system
// clock, the monotime does not change.
class KUDU_EXPORT MonoTime {
 public:
  enum Granularity {
    COARSE,
    FINE
  };

  static const int64_t kNanosecondsPerSecond = 1000000000L;
  static const int64_t kNanosecondsPerMillisecond = 1000000L;
  static const int64_t kNanosecondsPerMicrosecond = 1000L;

  static const int64_t kMicrosecondsPerSecond = 1000000L;

  // The coarse monotonic time is faster to retrieve, but "only"
  // accurate to within a millisecond or two.  The speed difference will
  // depend on your timer hardware.
  static MonoTime Now(enum Granularity granularity);

  // Return MonoTime equal to farthest possible time into the future.
  static MonoTime Max();

  // Return MonoTime equal to farthest possible time into the past.
  static MonoTime Min();

  // Return the earliest (minimum) of the two monotimes.
  static const MonoTime& Earliest(const MonoTime& a, const MonoTime& b);

  MonoTime();
  bool Initialized() const;
  MonoDelta GetDeltaSince(const MonoTime &rhs) const;
  void AddDelta(const MonoDelta &delta);
  bool ComesBefore(const MonoTime &rhs) const;
  std::string ToString() const;
  bool Equals(const MonoTime& other) const;

 private:
  friend class MonoDelta;
  FRIEND_TEST(TestMonoTime, TestTimeSpec);
  FRIEND_TEST(TestMonoTime, TestDeltaConversions);

  explicit MonoTime(const struct timespec &ts);
  explicit MonoTime(int64_t nanos);
  double ToSeconds() const;
  uint64_t nanos_;
};

// Sleep for a MonoDelta duration.
//
// This is preferred over sleep(3), usleep(3), and nanosleep(3). It's less prone to mixups with
// units since it uses a MonoDelta. It also ignores EINTR, so will reliably sleep at least the
// MonoDelta duration.
void KUDU_EXPORT SleepFor(const MonoDelta& delta);

} // namespace kudu

#endif
