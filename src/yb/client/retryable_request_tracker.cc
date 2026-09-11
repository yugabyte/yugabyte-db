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

#include "yb/client/retryable_request_tracker.h"

#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "yb/util/flags.h"
#include "yb/util/logging.h"

DEFINE_NON_RUNTIME_uint32(client_retryable_request_tracker_stripes, 16,
    "Number of independently locked stripes used to track retryable request IDs in each "
    "YB client. Values are rounded up to a power of two.");

namespace yb::client::internal {

namespace {

constexpr size_t kMaxStripes = 1024;

size_t EffectiveStripeCount(size_t requested) {
  if (requested == 0) {
    requested = FLAGS_client_retryable_request_tracker_stripes;
  }
  requested = std::clamp<size_t>(requested, 1, kMaxStripes);
  size_t stripes = 1;
  while (stripes < requested) {
    stripes <<= 1;
  }
  return stripes;
}

}  // namespace

RetryableRequestTracker::Registration::Registration(
    RetryableRequestId request_id, RetryableRequestId min_running_request_id,
    Stripe* stripe, ActiveRequests::iterator position)
    : request_id_(request_id),
      min_running_request_id_(min_running_request_id),
      stripe_(stripe),
      position_(position) {}

RetryableRequestTracker::Registration::Registration(Registration&& other) noexcept
    : request_id_(other.request_id_),
      min_running_request_id_(other.min_running_request_id_),
      stripe_(other.stripe_),
      position_(std::move(other.position_)) {
  other.stripe_ = nullptr;
  other.position_.reset();
}

RetryableRequestTracker::Registration& RetryableRequestTracker::Registration::operator=(
    Registration&& other) noexcept {
  if (this != &other) {
    CHECK(!position_) << "Overwriting an active retryable request registration";
    request_id_ = other.request_id_;
    min_running_request_id_ = other.min_running_request_id_;
    stripe_ = other.stripe_;
    position_ = std::move(other.position_);
    other.stripe_ = nullptr;
    other.position_.reset();
  }
  return *this;
}

RetryableRequestTracker::RetryableRequestTracker(size_t stripe_count)
    : stripe_count_(EffectiveStripeCount(stripe_count)),
      stripes_(std::make_unique<Stripe[]>(stripe_count_)) {
  for (size_t index = 0; index < stripe_count_; ++index) {
    stripes_[index].next_id = index;
    stripes_[index].lower_bound.store(index, std::memory_order_release);
  }
}

size_t RetryableRequestTracker::NextStripeIndex() {
  // Round-robin per thread: consecutive requests from one thread cycle through
  // every stripe, so all lower bounds keep advancing on any active client. The
  // hash seed spreads the starting stripe across threads.
  static thread_local uint64_t counter =
      std::hash<std::thread::id>()(std::this_thread::get_id());
  return counter++ & (stripe_count_ - 1);
}

RetryableRequestId RetryableRequestTracker::ComputeWatermark() const {
  auto watermark = stripes_[0].lower_bound.load(std::memory_order_acquire);
  for (size_t index = 1; index < stripe_count_; ++index) {
    watermark = std::min(watermark, stripes_[index].lower_bound.load(std::memory_order_acquire));
  }
  return watermark;
}

RetryableRequestTracker::Registration RetryableRequestTracker::Register() {
  auto& stripe = stripes_[NextStripeIndex()];
  RetryableRequestId request_id;
  ActiveRequests::iterator position;
  {
    std::lock_guard lock(stripe.mutex);
    request_id = stripe.next_id;
    stripe.next_id += stripe_count_;
    position = stripe.active_requests.emplace(stripe.active_requests.end(), request_id);
    stripe.lower_bound.store(stripe.active_requests.front(), std::memory_order_release);
  }
  // Computed after this request is visible in its stripe, so the watermark is
  // at most this stripe's lower bound and therefore at most request_id.
  return Registration(request_id, ComputeWatermark(), &stripe, position);
}

void RetryableRequestTracker::Unregister(std::span<Registration*> registrations) {
  // Destroy retired list nodes after all locks are released.
  ActiveRequests retired_requests;
  for (auto* registration : registrations) {
    CHECK_NOTNULL(registration);
    if (!registration->position_) {
      LOG(DFATAL) << "Retryable request registration already retired: "
                  << registration->request_id_;
      continue;
    }
    auto* stripe = CHECK_NOTNULL(registration->stripe_);
    {
      std::lock_guard lock(stripe->mutex);
      DCHECK_EQ(**registration->position_, registration->request_id_);
      retired_requests.splice(
          retired_requests.end(), stripe->active_requests, *registration->position_);
      stripe->lower_bound.store(
          stripe->active_requests.empty() ? stripe->next_id : stripe->active_requests.front(),
          std::memory_order_release);
    }
    registration->stripe_ = nullptr;
    registration->position_.reset();
  }
}

size_t RetryableRequestTracker::TEST_ActiveRequestsCount() const {
  size_t count = 0;
  for (size_t index = 0; index < stripe_count_; ++index) {
    std::lock_guard lock(stripes_[index].mutex);
    count += stripes_[index].active_requests.size();
  }
  return count;
}

}  // namespace yb::client::internal
