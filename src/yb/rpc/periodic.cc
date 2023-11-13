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

#include "yb/rpc/periodic.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <boost/function.hpp>
#include "yb/util/logging.h"

#include "yb/rpc/messenger.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/status.h"

using std::shared_ptr;
using std::weak_ptr;

namespace yb {
namespace rpc {

PeriodicTimer::Options::Options()
    : jitter_pct(0.25),
      one_shot(false) {
}

shared_ptr<PeriodicTimer> PeriodicTimer::Create(
    Messenger* messenger,
    RunTaskFunctor functor,
    MonoDelta period,
    Options options) {
  return std::make_shared<PeriodicTimer>(messenger, std::move(functor), period, options);
}

PeriodicTimer::PeriodicTimer(
    Messenger* messenger,
    RunTaskFunctor functor,
    MonoDelta period,
    Options options)
    : messenger_(messenger),
      functor_(std::move(functor)),
      period_(period),
      options_(std::move(options)),
      rng_(GetRandomSeed32()),
      current_callback_generation_(0),
      num_callbacks_for_tests_(0),
      started_(false) {
  DCHECK_GE(options_.jitter_pct, 0);
  DCHECK_LE(options_.jitter_pct, 1);
}

PeriodicTimer::~PeriodicTimer() {
  Stop();
}

void PeriodicTimer::Start(MonoDelta next_task_delta) {
  std::unique_lock<simple_spinlock> l(lock_);
  if (!started_) {
    started_ = true;
    SnoozeUnlocked(next_task_delta);
    auto new_callback_generation = ++current_callback_generation_;

    // Invoke Callback() with the lock released.
    l.unlock();
    Callback(new_callback_generation);
  }
}

void PeriodicTimer::Stop() {
  std::lock_guard<simple_spinlock> l(lock_);
  StopUnlocked();
}

void PeriodicTimer::StopUnlocked() {
  DCHECK(lock_.is_locked());
  started_ = false;
}

void PeriodicTimer::Snooze(MonoDelta next_task_delta) {
  std::lock_guard<simple_spinlock> l(lock_);
  SnoozeUnlocked(next_task_delta);
}

void PeriodicTimer::SnoozeUnlocked(MonoDelta next_task_delta) {
  DCHECK(lock_.is_locked());
  if (!started_) {
    return;
  }

  if (!next_task_delta) {
    // Given jitter percentage J and period P, this yields a delay somewhere
    // between (1-J)*P and (1+J)*P.
    next_task_delta = MonoDelta::FromMilliseconds(
        GetMinimumPeriod().ToMilliseconds() +
        rng_.NextDoubleFraction() *
        options_.jitter_pct *
        (2 * period_.ToMilliseconds()));
  }
  next_task_time_ = MonoTime::Now() + next_task_delta;
}

MonoDelta PeriodicTimer::GetMinimumPeriod() {
  // Given jitter percentage J and period P, this returns (1-J)*P, which is
  // the lowest possible jittered value.
  return MonoDelta::FromMilliseconds((1.0 - options_.jitter_pct) *
                                     period_.ToMilliseconds());
}

int64_t PeriodicTimer::NumCallbacksForTests() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return num_callbacks_for_tests_;
}

void PeriodicTimer::Callback(int64_t my_callback_generation) {
  // To simplify the implementation, a timer may have only one outstanding
  // callback scheduled at a time. This means that once the callback is
  // scheduled, the timer's task cannot run any earlier than whenever the
  // callback runs. Thus, the delay used when scheduling the callback dictates
  // the lowest possible value of 'next_task_delta' that Snooze() can honor.
  //
  // If the callback's delay is very low, Snooze() can honor a low
  // 'next_task_delta', but the callback will run often and burn more CPU
  // cycles. If the delay is very high, the timer will be more efficient but
  // the granularity for 'next_task_delta' will rise accordingly.
  //
  // As a "happy medium" we use GetMinimumPeriod() as the delay. This ensures
  // that a no-arg Snooze() on a jittered timer will always be honored, and as
  // long as the caller passes a value of at least GetMinimumPeriod() to
  // Snooze(), that too will be honored.
  MonoDelta delay = GetMinimumPeriod();
  bool run_task = false;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    num_callbacks_for_tests_++;

    // If the timer was stopped, exit.
    if (!started_) {
      return;
    }

    // If there's a new callback loop in town, exit.
    //
    // We could check again just before calling Messenger::ScheduleOnReactor()
    // (in case someone else restarted the timer while the functor ran, or in
    // case the functor itself restarted the timer), but there's no real reason
    // to do so: the very next iteration of this callback loop will wind up here
    // and exit.
    if (current_callback_generation_ > my_callback_generation) {
      return;
    }

    MonoTime now = MonoTime::Now();
    if (now < next_task_time_) {
      // It's not yet time to run the task. Reduce the scheduled delay if
      // enough time has elapsed, but don't increase it.
      delay = std::min(delay, next_task_time_ - now);
    } else {
      // It's time to run the task. Although the next task time is reset now,
      // it may be reset again by virtue of running the task itself.
      run_task = true;

      if (options_.one_shot) {
        // Stop the timer first, in case the task wants to restart it.
        StopUnlocked();
      }
      SnoozeUnlocked();
      delay = next_task_time_ - now;
    }
  }

  if (run_task) {
    functor_();

    if (options_.one_shot) {
      // The task was run; exit the loop. Even if the task restarted the timer,
      // that will have started a new callback loop, so exiting here is always
      // the correct thing to do.
      return;
    }
  }

  // Capture a weak_ptr reference into the submitted functor so that we can
  // safely handle the functor outliving its timer.
  weak_ptr<PeriodicTimer> w = shared_from_this();
  messenger_->scheduler().Schedule([w, my_callback_generation](const Status& s) {
    if (!s.ok()) {
      // The reactor was shut down.
      return;
    }
    if (auto timer = w.lock()) {
      timer->Callback(my_callback_generation);
    }
  }, delay.ToSteadyDuration());
}

} // namespace rpc
} // namespace yb
