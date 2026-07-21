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

#include "yb/client/retryable_request_tracker.h"

#include <mutex>
#include <utility>

#include "yb/util/logging.h"

namespace yb::client::internal {

RetryableRequestTracker::Registration::Registration(
    RetryableRequestId request_id, RetryableRequestId min_running_request_id,
    ActiveRequests::iterator position)
    : request_id_(request_id),
      min_running_request_id_(min_running_request_id),
      position_(position) {}

RetryableRequestTracker::Registration::Registration(Registration&& other) noexcept
    : request_id_(other.request_id_),
      min_running_request_id_(other.min_running_request_id_),
      position_(std::move(other.position_)) {
  other.position_.reset();
}

RetryableRequestTracker::Registration& RetryableRequestTracker::Registration::operator=(
    Registration&& other) noexcept {
  if (this != &other) {
    CHECK(!position_) << "Overwriting an active retryable request registration";
    request_id_ = other.request_id_;
    min_running_request_id_ = other.min_running_request_id_;
    position_ = std::move(other.position_);
    other.position_.reset();
  }
  return *this;
}

RetryableRequestTracker::Registration RetryableRequestTracker::Register() {
  std::lock_guard lock(mutex_);
  const auto request_id = next_request_id_++;
  const auto position = active_requests_.emplace(active_requests_.end(), request_id);
  return Registration(request_id, active_requests_.front(), position);
}

void RetryableRequestTracker::Unregister(std::span<Registration*> registrations) {
  if (registrations.empty()) {
    return;
  }

  ActiveRequests retired_requests;
  {
    std::lock_guard lock(mutex_);
    for (auto* registration : registrations) {
      CHECK_NOTNULL(registration);
      if (!registration->position_) {
        LOG(DFATAL) << "Retryable request registration already retired: "
                    << registration->request_id_;
        continue;
      }
      DCHECK_EQ(**registration->position_, registration->request_id_);
      retired_requests.splice(retired_requests.end(), active_requests_, *registration->position_);
      registration->position_.reset();
    }
  }
}

size_t RetryableRequestTracker::TEST_ActiveRequestsCount() const {
  std::lock_guard lock(mutex_);
  return active_requests_.size();
}

}  // namespace yb::client::internal
