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

#include "yb/util/async_task_util.h"

#include <glog/logging.h>

namespace yb {
  // AsyncTaskTracker methods.
  Status AsyncTaskTracker::Start() {
    if (started_) {
      return STATUS(IllegalState, "Task has already started");
    }
    started_ = true;
    return Status::OK();
  }

  bool AsyncTaskTracker::Started() const {
    return started_;
  }

  void AsyncTaskTracker::Abort() {
    started_ = false;
  }

  // AsyncTaskThrottler methods.
  AsyncTaskThrottler::AsyncTaskThrottler()
      : outstanding_task_count_limit_(std::numeric_limits<int>::max()) {
  }

  AsyncTaskThrottler::AsyncTaskThrottler(uint64_t limit)
      : outstanding_task_count_limit_(limit) {
  }

  void AsyncTaskThrottler::RefreshLimit(uint64_t limit) {
    std::lock_guard<std::mutex> l(mutex_);
    outstanding_task_count_limit_ = limit;
  }

  bool AsyncTaskThrottler::Throttle() {
    std::lock_guard<std::mutex> l(mutex_);
    if (ShouldThrottle()) {
      return true;
    }
    AddOutstandingTask();
    return false;
  }

  bool AsyncTaskThrottler::RemoveOutstandingTask() {
    std::lock_guard<std::mutex> l(mutex_);
    DCHECK_GT(current_outstanding_task_count_, 0);
    current_outstanding_task_count_--;
    return current_outstanding_task_count_ == 0;
  }

  bool AsyncTaskThrottler::ShouldThrottle() {
    return current_outstanding_task_count_ >= outstanding_task_count_limit_;
  }

  void AsyncTaskThrottler::AddOutstandingTask() {
    DCHECK_LT(current_outstanding_task_count_, outstanding_task_count_limit_);
    current_outstanding_task_count_++;
  }
} // namespace yb
