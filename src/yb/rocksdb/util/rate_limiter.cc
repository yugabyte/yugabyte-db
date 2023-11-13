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

#include "yb/rocksdb/util/rate_limiter.h"
#include "yb/rocksdb/env.h"
#include "yb/util/logging.h"
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
  for (int q = 0; q < 2; ++q) {
    total_requests_[q] = 0;
    total_bytes_through_[q] = 0;
    total_bytes_requested_per_second_[q] = 0;
    total_request_per_second_[q] = 0;
    allowed_request_per_second_[q] = 0;
    throttled_request_per_second_[q] = 0;
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

// This API allows user to dynamically change rate limiter's bytes per second.
void GenericRateLimiter::SetBytesPerSecond(int64_t bytes_per_second) {
  DCHECK_GT(bytes_per_second, 0);
  refill_bytes_per_period_.store(
      CalculateRefillBytesPerPeriod(bytes_per_second),
      std::memory_order_relaxed);
  MutexLock g(&request_mutex_);
  available_bytes_ = 0;
}

void GenericRateLimiter::Request(int64_t bytes, const yb::IOPriority priority) {
  assert(bytes <= refill_bytes_per_period_.load(std::memory_order_relaxed));

  const auto pri = yb::to_underlying(priority);

  MutexLock g(&request_mutex_);
  if (stop_) {
    return;
  }

  ++total_requests_[pri];
  ++total_request_per_second_[pri];

  if (yb::MonoTime::Now() - time_since_last_per_second_refresh_ >=
      yb::MonoDelta::FromSeconds(1)) {
    if (!description_.empty()) {
      YB_LOG_EVERY_N_SECS(INFO, 5) << ToString();
    }
    for (int z = 0; z < 2; z++) {
      total_bytes_requested_per_second_[z] = 0;
      total_request_per_second_[z] = 0;
      allowed_request_per_second_[z] = 0;
      throttled_request_per_second_[z] = 0;
    }
    time_since_last_per_second_refresh_ = yb::MonoTime::Now();
  }

  if (available_bytes_ >= bytes) {
    // Refill thread assigns quota and notifies requests waiting on
    // the queue under mutex. So if we get here, that means nobody
    // is waiting?
    available_bytes_ -= bytes;
    total_bytes_through_[pri] += bytes;
    total_bytes_requested_per_second_[pri] += bytes;
    allowed_request_per_second_[pri] += 1;
    return;
  }

  // Request cannot be satisfied at this moment, enqueue
  Req r(bytes, &request_mutex_);
  queue_[pri].push_back(&r);
  throttled_request_per_second_[pri] += 1;

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
      total_bytes_requested_per_second_[priority_index] += next_req->bytes;
      queue->pop_front();

      next_req->granted = true;
      if (next_req != leader_) {
        // Quota granted, signal the thread
        next_req->cv.Signal();
      }
    }
  }
}

std::string GenericRateLimiter::ToString() const {
  auto queue_size = queue_[yb::to_underlying(IOPriority::kHigh)].size() +
                    queue_[yb::to_underlying(IOPriority::kLow)].size();
  auto total_bytes_requested_per_second =
      total_bytes_requested_per_second_[yb::to_underlying(IOPriority::kHigh)] +
      total_bytes_requested_per_second_[yb::to_underlying(IOPriority::kLow)];
  auto total_request_per_second =
      total_request_per_second_[yb::to_underlying(IOPriority::kHigh)] +
      total_request_per_second_[yb::to_underlying(IOPriority::kLow)];
  auto allowed_request_per_second =
      allowed_request_per_second_[yb::to_underlying(IOPriority::kHigh)] +
      allowed_request_per_second_[yb::to_underlying(IOPriority::kLow)];
  auto throttled_request_per_second =
      throttled_request_per_second_[yb::to_underlying(IOPriority::kHigh)] +
      throttled_request_per_second_[yb::to_underlying(IOPriority::kLow)];

  if (throttled_request_per_second > 0) {
    LOG(INFO) << yb::Format("$0: THROTTLED REQUESTS", description_);
  }

  return yb::Format(
      "$0 rate limiter status:\navailable_bytes: $1\nqueue_size: $2\ntotal_bytes_requested_per_second: $3\n"
      "total_request_per_second: $4\nallowed_request_per_second: $5\n"
      "throttled_request_per_second: $6" ,
      description_, available_bytes_, queue_size, total_bytes_requested_per_second, total_request_per_second,
      allowed_request_per_second, throttled_request_per_second);
}

void GenericRateLimiter::EnableLoggingWithDescription(std::string description) {
  description_ = description;
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
