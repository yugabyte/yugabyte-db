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

#pragma once

#include "yb/util/monotime.h"

namespace yb {

class RestartSafeCoarseMonoClock;
typedef CoarseDuration RestartSafeCoarseDuration;

class RestartSafeCoarseTimePoint {
 public:
  RestartSafeCoarseTimePoint() = default;

  static RestartSafeCoarseTimePoint FromCoarseTimePointAndDelta(
      CoarseTimePoint time_point, CoarseDuration delta) {
    return RestartSafeCoarseTimePoint(time_point + delta);
  }

  static RestartSafeCoarseTimePoint FromUInt64(uint64_t value) {
    return RestartSafeCoarseTimePoint(CoarseTimePoint() + CoarseDuration(value));
  }

  uint64_t ToUInt64() const {
    return (value_ - CoarseTimePoint()).count();
  }

  std::string ToString() const {
    return yb::ToString(value_);
  }

 private:
  friend class RestartSafeCoarseMonoClock;

  explicit RestartSafeCoarseTimePoint(CoarseTimePoint value) : value_(value) {}

  CoarseTimePoint value_;

  friend inline bool operator==(
      const RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseTimePoint& rhs) {
    return lhs.value_ == rhs.value_;
  }

  friend inline bool operator<(
      const RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseTimePoint& rhs) {
    return lhs.value_ < rhs.value_;
  }

  friend inline RestartSafeCoarseTimePoint& operator-=(
      RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseDuration& rhs) { // NOLINT
    lhs.value_ -= rhs;
    return lhs;
  }

  friend inline RestartSafeCoarseTimePoint& operator+=(
      RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseDuration& rhs) { // NOLINT
    lhs.value_ += rhs;
    return lhs;
  }
};

inline bool operator!=(
    const RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseTimePoint& rhs) {
  return !(lhs == rhs);
}

inline bool operator>(
    const RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseTimePoint& rhs) {
  return rhs < lhs;
}

inline bool operator<=(
    const RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseTimePoint& rhs) {
  return !(rhs < lhs);
}

inline bool operator>=(
    const RestartSafeCoarseTimePoint& lhs, const RestartSafeCoarseTimePoint& rhs) {
  return !(lhs < rhs);
}

inline RestartSafeCoarseTimePoint operator-(
    RestartSafeCoarseTimePoint lhs, const RestartSafeCoarseDuration& rhs) {
  return lhs -= rhs;
}

inline RestartSafeCoarseTimePoint operator+(
    RestartSafeCoarseTimePoint lhs, const RestartSafeCoarseDuration& rhs) {
  return lhs += rhs;
}

class RestartSafeCoarseMonoClock {
 public:
  RestartSafeCoarseMonoClock() = default;
  explicit RestartSafeCoarseMonoClock(CoarseDuration delta) : delta_(delta) {}

  // Returns now of coarse mono time clock, the value of this clock persists between restarts.
  // So first time after restart will be greater than or equal to last time before restart.
  RestartSafeCoarseTimePoint Now() const {
    return RestartSafeCoarseTimePoint::FromCoarseTimePointAndDelta(CoarseMonoClock::Now(), delta_);
  }

  // Adjust clocks so specified point is treated as now.
  void Adjust(RestartSafeCoarseTimePoint mark_as_now) {
    delta_ = mark_as_now.value_ - CoarseMonoClock::Now();
  }
 private:
  CoarseDuration delta_;
};

} // namespace yb
