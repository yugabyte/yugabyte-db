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

#pragma once

#include <mutex>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/status.h"

namespace yb {

class AsyncTaskTracker {
 public:
  Status Start();
  bool Started() const;
  void Abort();

 private:
  bool started_ = false;
};

// Provides an abstract base class for tracking and throttling async tasks.
class AsyncTaskThrottlerBase {
 public:
  AsyncTaskThrottlerBase() = default;
  virtual ~AsyncTaskThrottlerBase() = default;

  bool Throttle();
  bool RemoveOutstandingTask();

 protected:
  uint64_t CurrentOutstandingTaskCount() const REQUIRES(mutex_);

  std::mutex mutex_;

 private:
  virtual bool ShouldThrottle() REQUIRES(mutex_) = 0;

  void AddOutstandingTask() REQUIRES(mutex_);

  uint64_t current_outstanding_task_count_ GUARDED_BY(mutex_) = 0;
};

// An async task throttler where the limit is set by the caller.
class AsyncTaskThrottler : public AsyncTaskThrottlerBase {
 public:
  AsyncTaskThrottler();
  explicit AsyncTaskThrottler(uint64_t limit);

  void RefreshLimit(uint64_t limit);

 private:
  virtual bool ShouldThrottle() REQUIRES(mutex_);

  uint64_t outstanding_task_count_limit_ GUARDED_BY(mutex_);
};

// As async task throttler where the limit is dynamically fetched through a callback function.
class DynamicAsyncTaskThrottler : public AsyncTaskThrottlerBase {
 public:
  DynamicAsyncTaskThrottler();
  explicit DynamicAsyncTaskThrottler(std::function<uint64_t()>&& get_limit_fn);

 private:
  virtual bool ShouldThrottle() REQUIRES(mutex_);

  std::function<uint64_t()> get_limit_fn_;
};

} // namespace yb
