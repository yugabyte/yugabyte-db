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

#include "yb/master/cluster_balance_activity_info.h"

namespace yb::master {

ClusterBalancerActivityInfo::TasksSummaryMap ClusterBalancerActivityInfo::GetTasksSummary() {
  TasksSummaryMap tasks_summary;
  for (const auto& task : tasks_) {
    auto& task_count_and_message = tasks_summary[{task->type(), task->state()}];
    if (task_count_and_message.count == 0) {
      task_count_and_message.example_status = StatusToString(task->GetStatus());
      task_count_and_message.example_description = task->description();
    }
    task_count_and_message.count++;
  }
  return tasks_summary;
}

void ClusterBalancerActivityBuffer::RecordActivity(ClusterBalancerActivityInfo activity_info) {
  std::lock_guard l(mutex_);
  activities_.push_back(std::move(activity_info));
}

ClusterBalancerActivityInfo ClusterBalancerActivityBuffer::GetLatestActivityInfo() const {
  SharedLock<rw_spinlock> l(mutex_);
  if (activities_.empty()) {
    return ClusterBalancerActivityInfo();
  }
  return activities_.back();
}

bool ClusterBalancerActivityBuffer::IsIdle() const {
  SharedLock<rw_spinlock> l(mutex_);
  for (const auto& activity : activities_) {
    if (!activity.IsIdle()) {
      return false;
    }
  }
  return true;
}

} // namespace yb::master
