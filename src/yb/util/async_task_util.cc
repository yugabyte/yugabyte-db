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

#include "yb/util/logging.h"

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

// AsyncTaskThrottlerBase methods.

bool AsyncTaskThrottlerBase::Throttle() {
  std::lock_guard l(mutex_);
  if (ShouldThrottle()) {
    return true;
  }
  AddOutstandingTask();
  return false;
}

bool AsyncTaskThrottlerBase::RemoveOutstandingTask() {
  std::lock_guard l(mutex_);
  DCHECK_GT(current_outstanding_task_count_, 0);
  current_outstanding_task_count_--;
  return current_outstanding_task_count_ == 0;
}

uint64_t AsyncTaskThrottlerBase::CurrentOutstandingTaskCount() const {
  return current_outstanding_task_count_;
}

void AsyncTaskThrottlerBase::AddOutstandingTask() {
  current_outstanding_task_count_++;
}

// AsyncTaskThrottler methods.

AsyncTaskThrottler::AsyncTaskThrottler()
    : outstanding_task_count_limit_(std::numeric_limits<int>::max()) {
}

AsyncTaskThrottler::AsyncTaskThrottler(uint64_t limit)
    : outstanding_task_count_limit_(limit) {
}

void AsyncTaskThrottler::RefreshLimit(const uint64_t limit) {
  std::lock_guard l(mutex_);
  outstanding_task_count_limit_ = limit;
}

bool AsyncTaskThrottler::ShouldThrottle() {
  return CurrentOutstandingTaskCount() >= outstanding_task_count_limit_;
}

// DynamicAsyncTaskThrottler methods.

DynamicAsyncTaskThrottler::DynamicAsyncTaskThrottler()
    : get_limit_fn_([]() {
        return std::numeric_limits<int>::max();
      }) {
}

DynamicAsyncTaskThrottler::DynamicAsyncTaskThrottler(std::function<uint64_t()>&& get_limit_fn)
    : get_limit_fn_(std::move(get_limit_fn)) {
}

bool DynamicAsyncTaskThrottler::ShouldThrottle() {
  return CurrentOutstandingTaskCount() >= get_limit_fn_();
}

} // namespace yb
