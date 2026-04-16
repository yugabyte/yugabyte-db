// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include "yb/master/async_rpc_tasks_base.h"

#include "yb/tserver/tserver_admin.pb.h"

#include "yb/util/countdown_latch.h"

namespace yb {
namespace master {

template <typename TaskRespType>
class TaskResultCollector {
 public:
  // A shared_ptr to this object must be passed to each task that uses it so the task has somewhere
  // to store the result even if it exceeds the deadline (we could alternatively use weak pointers).
  static std::shared_ptr<TaskResultCollector<TaskRespType>> Create(size_t num_tasks) {
    auto* task_result_aggregator = new TaskResultCollector<TaskRespType>(num_tasks);
    return std::shared_ptr<TaskResultCollector<TaskRespType>>(task_result_aggregator);
  }

  void TrackResponse(TabletServerId dest_uuid, TaskRespType&& resp) {
    std::lock_guard lock(mutex_);
    resps_[dest_uuid] = std::move(resp);
    latch_.CountDown();
  }

  // Wait for all tasks to finish or the deadline to be reached. Returns whatever responses were
  // collected by the time the deadline was reached.
  std::unordered_map<TabletServerId, TaskRespType> TakeResponses(CoarseDuration timeout) {
    latch_.WaitFor(timeout);
    std::lock_guard lock(mutex_);
    return std::move(resps_);
  }

 private:
  explicit TaskResultCollector(size_t num_tasks): latch_(num_tasks) {}

  mutable std::mutex mutex_;
  CountDownLatch latch_;
  size_t num_tasks_ GUARDED_BY(mutex_);
  std::unordered_map<TabletServerId, TaskRespType> resps_ GUARDED_BY(mutex_);
};

} // namespace master
} // namespace yb
