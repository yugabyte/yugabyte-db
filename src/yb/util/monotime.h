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

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest_prod.h>

struct timeval;
struct timespec;

namespace yb {
class MonoTime;

// Represent an elapsed duration of time -- i.e the delta between
// two MonoTime instances.
//
// A MonoDelta built with the default constructor is "uninitialized" and
// may not be used for any operation.
class MonoDelta {
 public:
  static MonoDelta FromDays(double days);
  static MonoDelta FromHours(double hours);
  static MonoDelta FromMinutes(double minutes);
  static MonoDelta FromSeconds(double seconds);
  static MonoDelta FromMilliseconds(int64_t ms);
  static MonoDelta FromMicroseconds(int64_t us);
  static MonoDelta FromNanoseconds(int64_t ns);

  static const MonoDelta kMin;
  static const MonoDelta kMax;
  static const MonoDelta kZero;

  MonoDelta() noexcept;

  template<class Rep, class Period>
  MonoDelta(const std::chrono::duration<Rep, Period>& duration) // NOLINT
      : nano_delta_(std::chrono::nanoseconds(duration).count()) {}

  bool Initialized() const;
  bool LessThan(const MonoDelta &rhs) const;
  bool MoreThan(const MonoDelta &rhs) const;
  bool Equals(const MonoDelta &rhs) const;
  bool IsNegative() const;
  std::string ToString() const;
  double ToSeconds() const;
  double ToMinutes() const;
  double ToHours() const;
  double ToDays() const;
  int64_t ToMilliseconds() const;
  int64_t ToMicroseconds() const;
  int64_t ToNanoseconds() const;
  std::chrono::steady_clock::duration ToSteadyDuration() const;

  std::chrono::microseconds ToChronoMicroseconds() const {
    return std::chrono::microseconds(ToMicroseconds());
  }

  std::chrono::milliseconds ToChronoMilliseconds() const {
    return std::chrono::milliseconds(ToMilliseconds());
  }

  MonoDelta& operator+=(const MonoDelta& rhs);
  MonoDelta& operator-=(const MonoDelta& rhs);
  MonoDelta& operator*=(int64_t mul);
  MonoDelta& operator/=(int64_t mul);

  // Update struct timeval to current value of delta, with microsecond accuracy.
  // Note that if MonoDelta::IsPositive() returns true, the struct timeval
  // is guaranteed to hold a positive number as well (at least 1 microsecond).
  void ToTimeVal(struct timeval *tv) const;

  // Update struct timespec to current value of delta, with nanosecond accuracy.
  void ToTimeSpec(struct timespec *ts) const;

  // Convert a nanosecond value to a timespec.
  static void NanosToTimeSpec(int64_t nanos, struct timespec* ts);

  explicit operator bool() const { return Initialized(); }
  bool operator !() const { return !Initialized(); }

  MonoDelta operator-() const { return MonoDelta(-nano_delta_); }

 private:
  typedef int64_t NanoDeltaType;
  static const NanoDeltaType kUninitialized;

  FRIEND_TEST(TestMonoTime, TestDeltaConversions);
  explicit MonoDelta(NanoDeltaType delta);
  NanoDeltaType nano_delta_;
};

inline bool operator<(MonoDelta lhs, MonoDelta rhs) { return lhs.LessThan(rhs); }
inline bool operator>(MonoDelta lhs, MonoDelta rhs) { return rhs < lhs; }
inline bool operator>=(MonoDelta lhs, MonoDelta rhs) { return !(lhs < rhs); }
inline bool operator<=(MonoDelta lhs, MonoDelta rhs) { return !(rhs < lhs); }

inline bool operator==(MonoDelta lhs, MonoDelta rhs) { return lhs.Equals(rhs); }
inline bool operator!=(MonoDelta lhs, MonoDelta rhs) { return !(rhs == lhs); }

std::string FormatForComparisonFailureMessage(const MonoDelta& op, const MonoDelta& other);

inline MonoDelta operator-(MonoDelta lhs, MonoDelta rhs) { return lhs -= rhs; }
inline MonoDelta operator+(MonoDelta lhs, MonoDelta rhs) { return lhs += rhs; }
inline MonoDelta operator*(MonoDelta lhs, int64_t rhs) { return lhs *= rhs; }
inline MonoDelta operator/(MonoDelta lhs, int64_t rhs) { return lhs /= rhs; }

inline std::ostream& operator<<(std::ostream& out, MonoDelta delta) {
  return out << delta.ToString();
}

// Represent a particular point in time, relative to some fixed but unspecified
// reference point.
//
// This time is monotonic, meaning that if the user changes his or her system
// clock, the monotime does not change.
class MonoTime {
  using Clock = std::chrono::steady_clock;
  using Duration = Clock::duration;

 public:
  using TimePoint = Clock::time_point;

  static constexpr int64_t kNanosecondsPerMicrosecond = 1000L;
  static constexpr int64_t kMicrosecondsPerMillisecond = 1000L;
  static constexpr int64_t kMillisecondsPerSecond = 1000L;
  static constexpr int64_t kSecondsPerMinute = 60L;
  static constexpr int64_t kSecondsPerHour = 60L * kSecondsPerMinute;
  static constexpr int64_t kMinutesPerHour = 60L;
  static constexpr int64_t kHoursPerDay = 24L;

  static constexpr int64_t kMillisecondsPerMinute =
      kMillisecondsPerSecond * kSecondsPerMinute;

  static constexpr int64_t kMillisecondsPerHour =
      kMillisecondsPerMinute * kMinutesPerHour;

  static constexpr int64_t kMicrosecondsPerSecond =
      kMillisecondsPerSecond * kMicrosecondsPerMillisecond;

  static constexpr int64_t kNanosecondsPerMillisecond =
      kNanosecondsPerMicrosecond * kMicrosecondsPerMillisecond;

  static constexpr int64_t kNanosecondsPerSecond =
      kNanosecondsPerMillisecond * kMillisecondsPerSecond;

  static constexpr int64_t kNanosecondsPerMinute =
      kNanosecondsPerSecond * kSecondsPerMinute;

  static constexpr int64_t kNanosecondsPerHour =
      kNanosecondsPerMinute * kMinutesPerHour;

  static constexpr int64_t kNanosecondsPerDay =
      kNanosecondsPerHour * kHoursPerDay;

  static const MonoTime kMin;
  static const MonoTime kMax;
  static const MonoTime kUninitialized;

  // Connvert timespec to microseconds.
  static uint64_t TimespecToMicros(const struct timespec& ts) {
    return ts.tv_sec * kMicrosecondsPerSecond +
           ts.tv_nsec / kNanosecondsPerMicrosecond;
  }

  // The coarse monotonic time is faster to retrieve, but "only" accurate to within a millisecond or
  // two.  The speed difference will depend on your timer hardware.
  static MonoTime Now();

  // Return MonoTime equal to farthest possible time into the future.
  static MonoTime Max();

  // Return MonoTime equal to farthest possible time into the past.
  static MonoTime Min();

  // Return the earliest (minimum) of the two monotimes.
  static const MonoTime& Earliest(const MonoTime& a, const MonoTime& b);

  MonoTime() noexcept {}
  MonoTime(TimePoint value) : value_(value) {} // NOLINT

  bool Initialized() const { return value_ != TimePoint(); }

  MonoDelta GetDeltaSince(const MonoTime &rhs) const;
  MonoDelta GetDeltaSinceMin() const { return GetDeltaSince(Min()); }
  void AddDelta(const MonoDelta &delta);
  void SubtractDelta(const MonoDelta &delta);
  bool ComesBefore(const MonoTime &rhs) const;
  std::string ToString() const;
  std::string ToFormattedString(const std::string& format = "%Y-%m-%d %H:%M:%S %Z") const;
  bool Equals(const MonoTime& other) const;
  bool IsMax() const;
  bool IsMin() const;

  template <class Rep, class Period>
  static MonoTime FromDuration(const std::chrono::duration<Rep, Period>& dur) {
    return TimePoint{std::chrono::duration_cast<Duration>(dur)};
  }

  uint64_t ToUint64() const { return value_.time_since_epoch().count(); }
  static MonoTime FromUint64(uint64_t value) {
    return TimePoint{Duration{value}};
  }

  explicit operator bool() const { return Initialized(); }
  bool operator !() const { return !Initialized(); }

  // Set this time to the given value if it is lower than that or uninitialized.
  void MakeAtLeast(MonoTime rhs);

  void MakeAtMost(MonoTime rhs);

  TimePoint ToSteadyTimePoint() const {
    return value_;
  }

 private:
  double ToSeconds() const;

  TimePoint value_;
};

inline MonoTime& operator+=(MonoTime& lhs, const MonoDelta& rhs) { // NOLINT
  lhs.AddDelta(rhs);
  return lhs;
}

template <class Clock>
inline auto operator+=(std::chrono::time_point<Clock>& lhs, const MonoDelta& rhs) { // NOLINT
  lhs += rhs.ToSteadyDuration();
  return lhs;
}

inline MonoTime operator+(MonoTime lhs, const MonoDelta& rhs) {
  lhs += rhs;
  return lhs;
}

template <class Clock>
inline auto operator+(const std::chrono::time_point<Clock>& lhs, const MonoDelta& rhs) {
  return lhs + rhs.ToSteadyDuration();
}

inline MonoDelta operator-(const MonoTime& lhs, const MonoTime& rhs) {
  return lhs.GetDeltaSince(rhs);
}

template <class Clock>
inline auto operator-(const std::chrono::time_point<Clock>& lhs, const MonoDelta& rhs) {
  return lhs - rhs.ToSteadyDuration();
}

inline MonoTime& operator-=(MonoTime& lhs, const MonoDelta& rhs) { // NOLINT
  lhs.SubtractDelta(rhs);
  return lhs;
}

inline MonoTime operator-(const MonoTime& lhs, const MonoDelta& rhs) {
  MonoTime result = lhs;
  result.AddDelta(-rhs);
  return MonoTime(result);
}

inline bool operator<(const MonoTime& lhs, const MonoTime& rhs) {
  return lhs.ComesBefore(rhs);
}

inline bool operator>(const MonoTime& lhs, const MonoTime& rhs) { return rhs < lhs; }
inline bool operator<=(const MonoTime& lhs, const MonoTime& rhs) { return !(rhs < lhs); }
inline bool operator>=(const MonoTime& lhs, const MonoTime& rhs) { return !(lhs < rhs); }

inline bool operator==(const MonoTime& lhs, const MonoTime& rhs) { return lhs.Equals(rhs); }
inline bool operator!=(const MonoTime& lhs, const MonoTime& rhs) { return !(lhs == rhs); }

// Sleep for a MonoDelta duration.
//
// This is preferred over sleep(3), usleep(3), and nanosleep(3). It's less prone to mixups with
// units since it uses a MonoDelta. It also ignores EINTR, so will reliably sleep at least the
// MonoDelta duration.
void SleepFor(const MonoDelta& delta);

void SleepUntil(const MonoTime& deadline);

// A monotonically increasing clock which lacks the precision of a hybrid clock but is suitable for
// any use cases that just need a locally monotonic clock.
class CoarseMonoClock {
 public:
  typedef std::chrono::nanoseconds duration;
  typedef duration Duration;
  typedef std::chrono::time_point<CoarseMonoClock> time_point;
  typedef time_point TimePoint;
  typedef time_point::period period;
  typedef time_point::rep rep;

  static constexpr bool is_steady = true;

  static time_point now();
  static TimePoint Now() { return now(); }
};

template <class Clock>
typename Clock::duration ClockResolution() {
  return typename Clock::duration(1);
}

template <>
CoarseMonoClock::Duration ClockResolution<CoarseMonoClock>();

typedef CoarseMonoClock::TimePoint CoarseTimePoint;
typedef CoarseMonoClock::Duration CoarseDuration;

template <class Rep, class Period>
int64_t ToMilliseconds(const std::chrono::duration<Rep, Period>& duration) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

template <class Rep, class Period>
int64_t ToMicroseconds(const std::chrono::duration<Rep, Period>& duration) {
  return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

template <class Rep, class Period>
int64_t ToNanoseconds(const std::chrono::duration<Rep, Period>& duration) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

template <class Rep, class Period>
double ToSeconds(const std::chrono::duration<Rep, Period>& duration) {
  return duration.count() /
      static_cast<double>(std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(
          std::chrono::seconds(1)).count());
}

inline double ToSeconds(MonoDelta delta) {
  return delta.ToSeconds();
}

std::string ToString(CoarseMonoClock::TimePoint value);

CoarseTimePoint ToCoarse(MonoTime monotime);
std::chrono::steady_clock::time_point ToSteady(CoarseTimePoint time_point);

// Returns false if the given time point is the minimum possible value of CoarseTimePoint. The
// implementation is consistent with MonoDelta's notion of being initialized, looking at the time
// since epoch. Note that CoarseTimePoint::min() is not the default value of a CoarseTimePoint.
// Its default value is a time point represented by zero, which may be an arbitrary point in time,
// since CLOCK_MONOTONIC represents monotonic time since some unspecified starting point.
bool IsInitialized(CoarseTimePoint time_point);

// Returns true if the given time point is either the minimum or maximum possible value.
bool IsExtremeValue(CoarseTimePoint time_point);

// Formats the given time point in the form "<time> (<relation_to_now>)" where <relation_to_now>
// is either "<interval> from now" or "<interval> ago", depending on whether the given point in
// time is before or after the current moment, passed in as "now".
std::string ToStringRelativeToNow(CoarseTimePoint t, CoarseTimePoint now);

// The same as above but skips the relative part if `now` is not specified.
std::string ToStringRelativeToNow(CoarseTimePoint t, std::optional<CoarseTimePoint> now);

// Only returns the relation of t to now (the parenthesized part of the ToStringRelativeToNow
// return value, without the parentheses).
std::string ToStringRelativeToNowOnly(CoarseTimePoint t, CoarseTimePoint now);

} // namespace yb
