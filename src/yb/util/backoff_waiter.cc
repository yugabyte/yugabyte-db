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

#include "yb/util/backoff_waiter.h"

#include <algorithm>
#include <string>

#include "yb/util/logging.h"

using std::string;

namespace yb {

Status RetryFunc(
    CoarseTimePoint deadline,
    const string& retry_msg,
    const string& timeout_msg,
    const std::function<Status(CoarseTimePoint, bool*)>& func,
    const CoarseDuration max_wait,
    const uint32_t max_jitter_ms,
    const uint32_t init_exponent) {
  DCHECK(deadline != CoarseTimePoint());

  CoarseBackoffWaiter waiter(
      deadline, max_wait, std::chrono::milliseconds(1), max_jitter_ms, init_exponent);

  if (waiter.ExpiredNow()) {
    return STATUS(TimedOut, timeout_msg);
  }
  for (;;) {
    bool retry = true;
    Status s = func(deadline, &retry);
    if (!retry) {
      return s;
    }

    VLOG(1) << retry_msg << " attempt=" << waiter.attempt() << " status=" << s.ToString();
    if (!waiter.Wait()) {
      break;
    }
  }

  return STATUS(TimedOut, timeout_msg);
}

Status Wait(const std::function<Result<bool>()>& condition,
            CoarseTimePoint deadline,
            const std::string& description,
            MonoDelta initial_delay,
            double delay_multiplier,
            MonoDelta max_delay) {
  auto start = CoarseMonoClock::Now();
  MonoDelta delay = initial_delay;
  for (;;) {
    const auto current = condition();
    if (!current.ok()) {
      return current.status();
    }
    if (current.get()) {
      break;
    }
    const auto now = CoarseMonoClock::Now();
    const MonoDelta left(deadline - now);
    if (left <= MonoDelta::kZero) {
      return STATUS_FORMAT(TimedOut,
                           "Operation '$0' didn't complete within $1ms",
                           description,
                           MonoDelta(now - start).ToMilliseconds());
    }
    delay = std::min(std::min(MonoDelta::FromSeconds(delay.ToSeconds() * delay_multiplier), left),
                     max_delay);
    SleepFor(delay);
  }
  return Status::OK();
}

Status Wait(const std::function<Result<bool>()>& condition,
            MonoTime deadline,
            const std::string& description,
            MonoDelta initial_delay,
            double delay_multiplier,
            MonoDelta max_delay) {
  auto left = deadline - MonoTime::Now();
  return Wait(condition, CoarseMonoClock::Now() + left, description, initial_delay,
              delay_multiplier, max_delay);
}

Status LoggedWait(
    const std::function<Result<bool>()>& condition,
    CoarseTimePoint deadline,
    const string& description,
    MonoDelta initial_delay,
    double delay_multiplier,
    MonoDelta max_delay) {
  LOG(INFO) << description << " - started";
  auto status =
      Wait(condition, deadline, description, initial_delay, delay_multiplier, max_delay);
  LOG(INFO) << description << " - completed: " << status;
  return status;
}

// Waits for the given condition to be true or until the provided timeout has expired.
Status WaitFor(const std::function<Result<bool>()>& condition,
               MonoDelta timeout,
               const string& description,
               MonoDelta initial_delay,
               double delay_multiplier,
               MonoDelta max_delay) {
  return Wait(condition, MonoTime::Now() + timeout, description, initial_delay, delay_multiplier,
              max_delay);
}

Status LoggedWaitFor(
    const std::function<Result<bool>()>& condition,
    MonoDelta timeout,
    const string& description,
    MonoDelta initial_delay,
    double delay_multiplier,
    MonoDelta max_delay) {
  LOG(INFO) << description << " - started";
  auto status =
      WaitFor(condition, timeout, description, initial_delay, delay_multiplier, max_delay);
  LOG(INFO) << description << " - completed: " << status;
  return status;
}

} // namespace yb
