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

#include <chrono>
#include <thread>

#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"

namespace yb {

// Utility class for waiting.
// It tracks number of attempts and exponentially increase sleep timeout.
template <class Clock>
class GenericBackoffWaiter {
 public:
  typedef typename Clock::time_point TimePoint;
  typedef typename Clock::duration Duration;

  // deadline - time when waiter decides that it is expired.
  // max_wait - max duration for single wait.
  // base_delay - multiplier for wait duration.
  explicit GenericBackoffWaiter(
      TimePoint deadline, Duration max_wait = Duration::max(),
      Duration base_delay = std::chrono::milliseconds(1),
      uint32_t max_jitter_ms = kDefaultMaxJitterMs,
      uint32_t init_exponent = kDefaultInitExponent)
      : deadline_(deadline),
        max_wait_(max_wait),
        base_delay_(base_delay),
        max_jitter_ms_(max_jitter_ms),
        init_exponent_(init_exponent) {}

  bool ExpiredNow() const {
    return ExpiredAt(Clock::now());
  }

  bool ExpiredAt(TimePoint time) const {
    return deadline_ < time + ClockResolution<Clock>();
  }

  bool Wait() {
    auto now = Clock::now();
    if (ExpiredAt(now)) {
      return false;
    }

    NextAttempt();

    std::this_thread::sleep_for(DelayForTime(now));
    return true;
  }

  void NextAttempt() {
    ++attempt_;
  }

  Duration DelayForNow() const {
    return DelayForTime(Clock::now());
  }

  Duration DelayForTime(TimePoint now) const {
    Duration max_wait = std::min(deadline_ - now, max_wait_);
    // 1st retry delayed 2^init_exponent of base delays, 2nd 2^(init_exponent + 1) base delays,
    // etc..
    Duration attempt_delay =
        base_delay_ * (attempt_ >= 29 ? std::numeric_limits<int32_t>::max()
                                      : 1LL << (attempt_ + init_exponent_ - 1));
    Duration jitter = std::chrono::milliseconds(RandomUniformInt<uint32_t>(0, max_jitter_ms_));
    return std::min(attempt_delay + jitter, max_wait);
  }

  size_t attempt() const {
    return attempt_;
  }

  // Resets attempt counter, w/o modifying deadline.
  void Restart() {
    attempt_ = 0;
  }

  static constexpr const uint32_t kDefaultMaxJitterMs = 50;
  static constexpr const uint32_t kDefaultInitExponent = 4;

 private:
  TimePoint deadline_;
  size_t attempt_ = 0;
  Duration max_wait_;
  Duration base_delay_;
  uint32_t max_jitter_ms_;
  uint32_t init_exponent_;
};

typedef GenericBackoffWaiter<std::chrono::steady_clock> BackoffWaiter;
typedef GenericBackoffWaiter<CoarseMonoClock> CoarseBackoffWaiter;

constexpr int kDefaultInitialWaitMs = 1;
constexpr double kDefaultWaitDelayMultiplier = 1.1;
constexpr int kDefaultMaxWaitDelayMs = 2000;

// Retry helper, takes a function like:
//     Status funcName(const MonoTime& deadline, bool *retry, ...)
// The function should set the retry flag (default true) if the function should
// be retried again. On retry == false the return status of the function will be
// returned to the caller, otherwise a Status::Timeout() will be returned.
// If the deadline is already expired, no attempt will be made.
Status RetryFunc(
    CoarseTimePoint deadline,
    const std::string& retry_msg,
    const std::string& timeout_msg,
    const std::function<Status(CoarseTimePoint, bool*)>& func,
    const CoarseDuration max_wait = std::chrono::seconds(2),
    const uint32_t max_jitter_ms = CoarseBackoffWaiter::kDefaultMaxJitterMs,
    const uint32_t init_exponent = CoarseBackoffWaiter::kDefaultInitExponent);

// Waits for the given condition to be true or until the provided deadline happens.
Status Wait(
    const std::function<Result<bool>()>& condition,
    MonoTime deadline,
    const std::string& description,
    MonoDelta initial_delay = MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
    double delay_multiplier = kDefaultWaitDelayMultiplier,
    MonoDelta max_delay = MonoDelta::FromMilliseconds(kDefaultMaxWaitDelayMs));

Status Wait(
    const std::function<Result<bool>()>& condition,
    CoarseTimePoint deadline,
    const std::string& description,
    MonoDelta initial_delay = MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
    double delay_multiplier = kDefaultWaitDelayMultiplier,
    MonoDelta max_delay = MonoDelta::FromMilliseconds(kDefaultMaxWaitDelayMs));

Status LoggedWait(
    const std::function<Result<bool>()>& condition,
    CoarseTimePoint deadline,
    const std::string& description,
    MonoDelta initial_delay = MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
    double delay_multiplier = kDefaultWaitDelayMultiplier,
    MonoDelta max_delay = MonoDelta::FromMilliseconds(kDefaultMaxWaitDelayMs));

// Waits for the given condition to be true or until the provided timeout has expired.
Status WaitFor(
    const std::function<Result<bool>()>& condition,
    MonoDelta timeout,
    const std::string& description,
    MonoDelta initial_delay = MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
    double delay_multiplier = kDefaultWaitDelayMultiplier,
    MonoDelta max_delay = MonoDelta::FromMilliseconds(kDefaultMaxWaitDelayMs));

Status LoggedWaitFor(
    const std::function<Result<bool>()>& condition,
    MonoDelta timeout,
    const std::string& description,
    MonoDelta initial_delay = MonoDelta::FromMilliseconds(kDefaultInitialWaitMs),
    double delay_multiplier = kDefaultWaitDelayMultiplier,
    MonoDelta max_delay = MonoDelta::FromMilliseconds(kDefaultMaxWaitDelayMs));

} // namespace yb
