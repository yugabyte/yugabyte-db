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

#include <list>
#include <optional>
#include <span>

#include "yb/common/retryable_request.h"
#include "yb/gutil/macros.h"
#include "yb/util/locks.h"

namespace yb::client::internal {

// Tracks retryable requests issued by one YBClient. Request IDs and active-list insertion are
// serialized, so the list remains ordered by request ID and its front is the minimum active ID.
class RetryableRequestTracker {
 private:
  using ActiveRequests = std::list<RetryableRequestId>;

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
        ActiveRequests::iterator position);

    RetryableRequestId request_id_;
    RetryableRequestId min_running_request_id_;
    std::optional<ActiveRequests::iterator> position_;

    friend class RetryableRequestTracker;
  };

  Registration Register();
  void Unregister(std::span<Registration*> registrations);

  size_t TEST_ActiveRequestsCount() const;

 private:
  mutable simple_spinlock mutex_;
  RetryableRequestId next_request_id_ GUARDED_BY(mutex_) = 0;
  ActiveRequests active_requests_ GUARDED_BY(mutex_);
};

}  // namespace yb::client::internal
