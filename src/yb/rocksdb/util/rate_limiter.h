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

#pragma once

#include <atomic>
#include <deque>
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/rate_limiter.h"

#include "yb/util/monotime.h"


namespace rocksdb {

class GenericRateLimiter : public RateLimiter {
 public:
  GenericRateLimiter(
      int64_t refill_bytes,
      int64_t refill_period_us,
      int32_t fairness);

  virtual ~GenericRateLimiter();

  // This API allows user to dynamically change rate limiter's bytes per second.
  void SetBytesPerSecond(int64_t bytes_per_second) override;

  // Request for token to write bytes. If this request can not be satisfied,
  // the call is blocked. Caller is responsible to make sure
  // bytes <= GetSingleBurstBytes()
  void Request(const int64_t bytes, const yb::IOPriority pri) override;

  int64_t GetSingleBurstBytes() const override {
    return refill_bytes_per_period_.load(std::memory_order_relaxed);
  }

  int64_t GetTotalBytesThrough(const yb::IOPriority pri = yb::IOPriority::kTotal) const override;

  int64_t GetTotalRequests(const yb::IOPriority pri = yb::IOPriority::kTotal) const override;

  // Enable periodic logging when the description is set. Set to the empty string to disable
  // logging.
  void EnableLoggingWithDescription(std::string description_for_logging) override;

 private:
  void Refill();
  int64_t CalculateRefillBytesPerPeriod(int64_t rate_bytes_per_sec) {
    return rate_bytes_per_sec * refill_period_us_ / 1000000;
  }

  std::string ToStringUnlocked(yb::MonoDelta time_since_refresh) const;

  double GetPerSecondAverageUnlocked(const int64_t* array, yb::MonoDelta time_since_refresh) const;

  // This mutex guard all internal states
  mutable port::Mutex request_mutex_;

  const int64_t refill_period_us_;
  // This variable can be changed dynamically.
  std::atomic<int64_t> refill_bytes_per_period_;
  Env* const env_;

  bool stop_;
  port::CondVar exit_cv_;
  int32_t requests_to_wait_;

  // All state is guarded by request_mutex_.
  int64_t total_requests_[yb::kElementsInIOPriority];
  int64_t total_bytes_through_[yb::kElementsInIOPriority];
  int64_t total_bytes_requested_per_second_[yb::kElementsInIOPriority];
  int64_t total_requests_per_second_[yb::kElementsInIOPriority];
  int64_t unthrottled_requests_per_second_[yb::kElementsInIOPriority];
  int64_t throttled_requests_per_second_[yb::kElementsInIOPriority];
  int64_t available_bytes_;
  int64_t next_refill_us_;

  int32_t fairness_;
  Random rnd_;

  struct Req;
  Req* leader_;
  std::deque<Req*> queue_[yb::kElementsInIOPriority];

  yb::MonoTime last_refresh_ = yb::MonoTime::Now();

  std::string description_for_logging_;
};

}  // namespace rocksdb
