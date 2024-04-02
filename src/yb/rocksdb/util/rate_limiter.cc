//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <iomanip>

#include "yb/ash/wait_state.h"

#include "yb/gutil/strings/human_readable.h"

#include "yb/rocksdb/util/rate_limiter.h"
#include "yb/rocksdb/env.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"

using yb::IOPriority;

namespace rocksdb {

// Pending request
struct GenericRateLimiter::Req {
  explicit Req(int64_t _bytes, port::Mutex* _mu)
      : bytes(_bytes), cv(_mu), granted(false) {}
  int64_t bytes;
  port::CondVar cv;
  bool granted;
};

GenericRateLimiter::GenericRateLimiter(int64_t rate_bytes_per_sec,
                                       int64_t refill_period_us,
                                       int32_t fairness)
    : refill_period_us_(refill_period_us),
      refill_bytes_per_period_(
          CalculateRefillBytesPerPeriod(rate_bytes_per_sec)),
      env_(Env::Default()),
      stop_(false),
      exit_cv_(&request_mutex_),
      requests_to_wait_(0),
      available_bytes_(0),
      next_refill_us_(env_->NowMicros()),
      fairness_(fairness > 100 ? 100 : fairness),
      rnd_((uint32_t)time(nullptr)),
      leader_(nullptr) {
  for (size_t q = 0; q < yb::kElementsInIOPriority; ++q) {
    total_requests_[q] = 0;
    total_bytes_through_[q] = 0;
    total_bytes_requested_per_second_[q] = 0;
    total_requests_per_second_[q] = 0;
    unthrottled_requests_per_second_[q] = 0;
    throttled_requests_per_second_[q] = 0;
  }
}

GenericRateLimiter::~GenericRateLimiter() {
  MutexLock g(&request_mutex_);
  stop_ = true;
  // TODO: create a convenience template for a fixed-size array indexed using an enum ?
  // https://github.com/yugabyte/yugabyte-db/issues/13399
  requests_to_wait_ = static_cast<int32_t>(queue_[yb::to_underlying(IOPriority::kLow)].size() +
                                           queue_[yb::to_underlying(IOPriority::kHigh)].size());
  for (auto& r : queue_[yb::to_underlying(IOPriority::kHigh)]) {
    r->cv.Signal();
  }
  for (auto& r : queue_[yb::to_underlying(IOPriority::kLow)]) {
    r->cv.Signal();
  }
  while (requests_to_wait_ > 0) {
    exit_cv_.Wait();
  }
}

// This API allows user to dynamically change rate limiter's bytes per second. Returns early if the
// new bytes_per_second matches the existing rate.
void GenericRateLimiter::SetBytesPerSecond(int64_t bytes_per_second) {
  DCHECK_GT(bytes_per_second, 0);
  auto refill_bytes_per_period_new = CalculateRefillBytesPerPeriod(bytes_per_second);
  if (refill_bytes_per_period_new == refill_bytes_per_period_) {
    return;
  }
  refill_bytes_per_period_.store(refill_bytes_per_period_new, std::memory_order_relaxed);
  MutexLock g(&request_mutex_);
  if (!description_for_logging_.empty()) {
    LOG(INFO) << yb::Format("$0 rate limiter: Setting bytes_per_second to $1",
                            description_for_logging_, bytes_per_second);
  }
  available_bytes_ = 0;
}

void GenericRateLimiter::Request(int64_t bytes, const yb::IOPriority priority) {
  assert(bytes <= refill_bytes_per_period_.load(std::memory_order_relaxed));

  const auto pri = yb::to_underlying(priority);
  SCOPED_WAIT_STATUS(RocksDB_RateLimiter);

  MutexLock g(&request_mutex_);
  if (stop_) {
    return;
  }

  auto time_since_refresh = yb::MonoTime::Now() - last_refresh_;
  if (time_since_refresh >= yb::MonoDelta::FromSeconds(1)) {
    if (!description_for_logging_.empty()) {
      YB_LOG_EVERY_N_SECS(INFO, 5) << ToStringUnlocked(time_since_refresh);
    }
    for (size_t z = 0; z < yb::kElementsInIOPriority; z++) {
      total_bytes_requested_per_second_[z] = 0;
      total_requests_per_second_[z] = 0;
      unthrottled_requests_per_second_[z] = 0;
      throttled_requests_per_second_[z] = 0;
    }
    last_refresh_ = yb::MonoTime::Now();
  }

  ++total_requests_[pri];
  ++total_requests_per_second_[pri];
  total_bytes_requested_per_second_[pri] += bytes;

  if (available_bytes_ >= bytes) {
    // Refill thread assigns quota and notifies requests waiting on
    // the queue under mutex. So if we get here, that means nobody
    // is waiting?
    available_bytes_ -= bytes;
    total_bytes_through_[pri] += bytes;
    unthrottled_requests_per_second_[pri]++;
    return;
  }

  // Request cannot be satisfied at this moment, enqueue
  Req r(bytes, &request_mutex_);
  queue_[pri].push_back(&r);
  throttled_requests_per_second_[pri]++;

  do {
    bool timedout = false;
    // Leader election, candidates can be:
    // (1) a new incoming request,
    // (2) a previous leader, whose quota has not been not assigned yet due
    //     to lower priority
    // (3) a previous waiter at the front of queue, who got notified by
    //     previous leader
    if (leader_ == nullptr &&
        ((!queue_[yb::to_underlying(IOPriority::kHigh)].empty() &&
            &r == queue_[yb::to_underlying(IOPriority::kHigh)].front()) ||
         (!queue_[yb::to_underlying(IOPriority::kLow)].empty() &&
            &r == queue_[yb::to_underlying(IOPriority::kLow)].front()))) {
      leader_ = &r;
      timedout = r.cv.TimedWait(next_refill_us_);
    } else {
      // Not at the front of queue or an leader has already been elected
      r.cv.Wait();
    }

    // request_mutex_ is held from now on
    if (stop_) {
      --requests_to_wait_;
      exit_cv_.Signal();
      return;
    }

    // Make sure the waken up request is always the header of its queue
    assert(r.granted ||
           (!queue_[yb::to_underlying(IOPriority::kHigh)].empty() &&
            &r == queue_[yb::to_underlying(IOPriority::kHigh)].front()) ||
           (!queue_[yb::to_underlying(IOPriority::kLow)].empty() &&
            &r == queue_[yb::to_underlying(IOPriority::kLow)].front()));
    assert(leader_ == nullptr ||
           (!queue_[yb::to_underlying(IOPriority::kHigh)].empty() &&
            leader_ == queue_[yb::to_underlying(IOPriority::kHigh)].front()) ||
           (!queue_[yb::to_underlying(IOPriority::kLow)].empty() &&
            leader_ == queue_[yb::to_underlying(IOPriority::kLow)].front()));

    if (leader_ == &r) {
      // Waken up from TimedWait()
      if (timedout) {
        // Time to do refill!
        Refill();

        // Re-elect a new leader regardless. This is to simplify the
        // election handling.
        leader_ = nullptr;

        // Notify the header of queue if current leader is going away
        if (r.granted) {
          // Current leader already got granted with quota. Notify header
          // of waiting queue to participate next round of election.
          assert((queue_[yb::to_underlying(IOPriority::kHigh)].empty() ||
                    &r != queue_[yb::to_underlying(IOPriority::kHigh)].front()) &&
                 (queue_[yb::to_underlying(IOPriority::kLow)].empty() ||
                    &r != queue_[yb::to_underlying(IOPriority::kLow)].front()));
          if (!queue_[yb::to_underlying(IOPriority::kHigh)].empty()) {
            queue_[yb::to_underlying(IOPriority::kHigh)].front()->cv.Signal();
          } else if (!queue_[yb::to_underlying(IOPriority::kLow)].empty()) {
            queue_[yb::to_underlying(IOPriority::kLow)].front()->cv.Signal();
          }
          // Done
          break;
        }
      } else {
        // Spontaneous wake up, need to continue to wait
        assert(!r.granted);
        leader_ = nullptr;
      }
    } else {
      // Waken up by previous leader:
      // (1) if requested quota is granted, it is done.
      // (2) if requested quota is not granted, this means current thread
      // was picked as a new leader candidate (previous leader got quota).
      // It needs to participate leader election because a new request may
      // come in before this thread gets waken up. So it may actually need
      // to do Wait() again.
      assert(!timedout);
    }
  } while (!r.granted);
}

int64_t GenericRateLimiter::GetTotalBytesThrough(const IOPriority pri) const {
  MutexLock g(&request_mutex_);
  if (pri == IOPriority::kTotal) {
    return total_bytes_through_[yb::to_underlying(IOPriority::kLow)] +
           total_bytes_through_[yb::to_underlying(IOPriority::kHigh)];
  }
  return total_bytes_through_[yb::to_underlying(pri)];
}

int64_t GenericRateLimiter::GetTotalRequests(const IOPriority pri) const {
  MutexLock g(&request_mutex_);
  if (pri == IOPriority::kTotal) {
    return total_requests_[yb::to_underlying(IOPriority::kLow)] +
        total_requests_[yb::to_underlying(IOPriority::kHigh)];
  }
  return total_requests_[yb::to_underlying(pri)];
}

void GenericRateLimiter::Refill() {
  next_refill_us_ = env_->NowMicros() + refill_period_us_;
  // Carry over the left over quota from the last period
  auto refill_bytes_per_period =
      refill_bytes_per_period_.load(std::memory_order_relaxed);
  if (available_bytes_ < refill_bytes_per_period) {
    available_bytes_ += refill_bytes_per_period;
  }

  int use_low_pri_first = rnd_.OneIn(fairness_) ? 0 : 1;
  for (int q = 0; q < 2; ++q) {
    const auto priority_index = yb::to_underlying(
        (use_low_pri_first == q) ? IOPriority::kLow : IOPriority::kHigh);
    auto* queue = &queue_[priority_index];
    while (!queue->empty()) {
      auto* next_req = queue->front();
      if (available_bytes_ < next_req->bytes) {
        break;
      }
      available_bytes_ -= next_req->bytes;
      total_bytes_through_[priority_index] += next_req->bytes;
      queue->pop_front();

      next_req->granted = true;
      if (next_req != leader_) {
        // Quota granted, signal the thread
        next_req->cv.Signal();
      }
    }
  }
}

std::string GenericRateLimiter::ToStringUnlocked(yb::MonoDelta time_since_refresh) const {
  int64_t queue_size = 0;
  for (size_t z = 0; z < yb::kElementsInIOPriority; z++) {
    queue_size += queue_[z].size();
  }

  auto total_bytes_requested_per_second =
      GetPerSecondAverageUnlocked(total_bytes_requested_per_second_, time_since_refresh);
  auto total_requests_per_second =
      GetPerSecondAverageUnlocked(total_requests_per_second_, time_since_refresh);
  auto unthrottled_requests_per_second =
      GetPerSecondAverageUnlocked(unthrottled_requests_per_second_, time_since_refresh);
  auto throttled_requests_per_second =
      GetPerSecondAverageUnlocked(throttled_requests_per_second_, time_since_refresh);

  return yb::Format(
      "$0 rate limiter status:\navailable_bytes: $1\nqueue_size: $2\n"
      "total_bytes_requested_per_second: $3\n"
      "total_request_per_second: $4\nunthrottled_request_per_second: $5\n"
      "throttled_request_per_second: $6\ntime_since_refresh: $7" ,
      description_for_logging_, HumanReadableNum::ToString(available_bytes_), queue_size,
      HumanReadableNum::DoubleToString(total_bytes_requested_per_second),
      HumanReadableNum::DoubleToString(total_requests_per_second),
      HumanReadableNum::DoubleToString(unthrottled_requests_per_second),
      HumanReadableNum::DoubleToString(throttled_requests_per_second),
      time_since_refresh);
}

double GenericRateLimiter::GetPerSecondAverageUnlocked(
    const int64_t* array, yb::MonoDelta time_since_refresh) const {
  auto sum = 0;
  for (size_t z = 0; z < yb::kElementsInIOPriority; z++) {
    sum += array[z];
  }
  return (sum + 0.0) / time_since_refresh.ToSeconds();
}

void GenericRateLimiter::EnableLoggingWithDescription(std::string description_for_logging) {
  MutexLock g(&request_mutex_);
  description_for_logging_ = description_for_logging;
}

RateLimiter* NewGenericRateLimiter(
    int64_t rate_bytes_per_sec, int64_t refill_period_us, int32_t fairness) {
  assert(rate_bytes_per_sec > 0);
  assert(refill_period_us > 0);
  assert(fairness > 0);
  return new GenericRateLimiter(
      rate_bytes_per_sec, refill_period_us, fairness);
}

}  // namespace rocksdb
