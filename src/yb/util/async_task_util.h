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

#ifndef YB_UTIL_ASYNC_TASK_UTIL_H
#define YB_UTIL_ASYNC_TASK_UTIL_H

#include <mutex>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/status.h"

namespace yb {

class AsyncTaskTracker {
 public:
  CHECKED_STATUS Start();
  bool Started() const;
  void Abort();

 private:
  bool started_ = false;
};

// TODO(Sanket): Can have an entire inheritance hierarchy later on
// depending on the type of task and custom requirements for each type.
class AsyncTaskThrottler {
 public:
  AsyncTaskThrottler();
  explicit AsyncTaskThrottler(uint64_t limit);

  void RefreshLimit(uint64_t limit);
  bool Throttle();
  bool RemoveOutstandingTask();

 private:
  bool ShouldThrottle() REQUIRES(mutex_);
  void AddOutstandingTask() REQUIRES(mutex_);

  std::mutex mutex_;
  uint64_t outstanding_task_count_limit_ GUARDED_BY(mutex_);
  uint64_t current_outstanding_task_count_ GUARDED_BY(mutex_) = 0;
};

} // namespace yb

#endif // YB_UTIL_ASYNC_TASK_UTIL_H
