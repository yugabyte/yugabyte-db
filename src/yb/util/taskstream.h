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

#ifndef YB_UTIL_TASKSTREAM_H
#define YB_UTIL_TASKSTREAM_H

#include <gflags/gflags.h>

#include "yb/util/status.h"
#include "yb/util/threadpool.h"

namespace yb {
class ThreadPool;

template <typename T>
class TaskStreamImpl;

template <typename T>
// TaskStream has a thread pool token in the given thread pool.
// TaskStream does not manage a thread but only submits to the token in the thread pool.
// When we submit tasks to the taskstream, it adds tasks to the queue to be processed.
// The internal Run function will call the user-provided function,
// for each element in the queue in a loop.
// When the queue is empty, it calls the user-provided function with no parameter,
// to indicate it needs to process the end of the group of tasks processed.
// This feature is used for the preparer and appender functionality.
class TaskStream {
 public:
  explicit TaskStream(std::function<void(T*)> process_item, ThreadPool* thread_pool);
  ~TaskStream();

  CHECKED_STATUS Start();
  void Stop();

  CHECKED_STATUS Submit(T* item);

 private:
  std::unique_ptr<TaskStreamImpl<T>> impl_;
};

}  // namespace yb
#endif  // YB_UTIL_TASKSTREAM_H
