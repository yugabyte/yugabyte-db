// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <atomic>
#include <list>
#include <memory>
#include <optional>
#include <span>

#include "yb/common/retryable_request.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#include "yb/util/locks.h"

namespace yb::client::internal {

// Tracks retryable requests issued by one YBClient.
//
// Request IDs must be unique within a client, and every outgoing RPC carries a
// min-running watermark. Tablet leaders reject request IDs below the watermark
// they last saw and garbage-collect retry-deduplication state up to it, so a
// watermark must never exceed the ID of any request that is registered but
// unfinished when the watermark is computed, or that registers later. A
// conservative (lagging) watermark is always safe: it only delays server-side
// cleanup, which is additionally bounded by time-based expiry.
//
// To avoid serializing every registration behind one lock, state is striped.
// Stripe s of S assigns IDs from the arithmetic sequence s, s+S, s+2S, ...
// under its own lock, so ID assignment and active-list insertion remain atomic
// per stripe, each stripe's active list stays in ascending ID order, and each
// stripe maintains a monotonically non-decreasing lower bound on any ID it has
// in flight or may still assign: the list front when non-empty, otherwise the
// next unassigned ID. The watermark is the minimum of the per-stripe lower
// bounds. Reading the bounds without a consistent snapshot is safe: a value
// read from a stripe is a valid lower bound for every request that stripe had
// registered at read time (it is at most the list front) and for every request
// it registers later (IDs and bounds only grow).
//
// Stripes are selected by a per-thread round-robin counter, so any client that
// keeps issuing requests keeps every stripe's lower bound advancing.
class RetryableRequestTracker {
 private:
  using ActiveRequests = std::list<RetryableRequestId>;
  struct Stripe;

 public:
  class Registration {
   public:
    Registration(Registration&& other) noexcept;
    Registration& operator=(Registration&& other) noexcept;

    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;

    RetryableRequestId request_id() const { return request_id_; }

    RetryableRequestId min_running_request_id() const { return min_running_request_id_; }

   private:
    Registration(
        RetryableRequestId request_id, RetryableRequestId min_running_request_id,
        Stripe* stripe, ActiveRequests::iterator position);

    RetryableRequestId request_id_;
    RetryableRequestId min_running_request_id_;
    Stripe* stripe_;
    std::optional<ActiveRequests::iterator> position_;

    friend class RetryableRequestTracker;
  };

  // stripe_count == 0 selects --client_retryable_request_tracker_stripes.
  // Counts are rounded up to a power of two.
  explicit RetryableRequestTracker(size_t stripe_count = 0);

  Registration Register();
  void Unregister(std::span<Registration*> registrations);

  size_t TEST_ActiveRequestsCount() const;
  size_t TEST_StripeCount() const { return stripe_count_; }

 private:
  struct alignas(CACHELINE_SIZE) Stripe {
    mutable simple_spinlock mutex;
    RetryableRequestId next_id GUARDED_BY(mutex) = 0;
    ActiveRequests active_requests GUARDED_BY(mutex);
    // active_requests.empty() ? next_id : active_requests.front().
    // Monotonically non-decreasing; updated under mutex, read lock-free.
    std::atomic<RetryableRequestId> lower_bound{0};
  };

  size_t NextStripeIndex();
  RetryableRequestId ComputeWatermark() const;

  size_t stripe_count_;
  std::unique_ptr<Stripe[]> stripes_;
};

}  // namespace yb::client::internal
