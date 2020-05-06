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

#ifndef YB_UTIL_BACKOFF_WAITER_H
#define YB_UTIL_BACKOFF_WAITER_H

#include <chrono>

#include "yb/util/monotime.h"
#include "yb/util/random_util.h"

namespace yb {

// Utility class for waiting.
// It tracks number of attempts and exponentially increase sleep timeout.
template <class Clock>
class GenericBackoffWaiter {
 public:
  typedef typename Clock::time_point TimePoint;
  typedef typename Clock::duration Duration;

  explicit GenericBackoffWaiter(
      TimePoint deadline, Duration max_wait = Duration::max())
      : deadline_(deadline), max_wait_(max_wait) {}

  bool Wait() {
    auto now = Clock::now();
    if (now >= deadline_) {
      return false;
    }

    ++attempt_;

    std::this_thread::sleep_for(DelayForTime(now));
    return true;
  }

  Duration DelayForNow() const {
    return DelayForTime(Clock::now());
  }

  Duration DelayForTime(TimePoint now) const {
    auto max_wait = std::min(deadline_ - now, max_wait_);
    int64_t base_delay_ms = attempt_ >= 29
        ? std::numeric_limits<int32_t>::max()
        : 1LL << (attempt_ + 3); // 1st retry delayed 2^4 ms, 2nd 2^5, etc..
    int64_t jitter_ms = RandomUniformInt(0, 50);
    return std::min<decltype(max_wait)>(
        std::chrono::milliseconds(base_delay_ms + jitter_ms), max_wait);
  }

  size_t attempt() const {
    return attempt_;
  }

  // Resets attempt counter, w/o modifying deadline.
  void Restart() {
    attempt_ = 0;
  }

 private:
  TimePoint deadline_;
  size_t attempt_ = 0;
  Duration max_wait_;
};

typedef GenericBackoffWaiter<std::chrono::steady_clock> BackoffWaiter;
typedef GenericBackoffWaiter<CoarseMonoClock> CoarseBackoffWaiter;

} // namespace yb

#endif // YB_UTIL_BACKOFF_WAITER_H
