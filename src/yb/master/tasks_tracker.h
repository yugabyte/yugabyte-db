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

#include <vector>

#include <boost/circular_buffer.hpp>

#include "yb/server/monitored_task.h"
#include "yb/util/locks.h"

DECLARE_int32(tasks_tracker_num_tasks);
DECLARE_int32(tasks_tracker_keep_time_multiplier);
DECLARE_int32(tasks_tracker_num_long_term_tasks);
DECLARE_int32(long_term_tasks_tracker_keep_time_multiplier);
DECLARE_int32(catalog_manager_bg_task_wait_ms);

namespace yb {
namespace master {

YB_STRONGLY_TYPED_BOOL(IsUserInitiated);

class TasksTracker : public RefCountedThreadSafe<TasksTracker> {
 public:
  explicit TasksTracker(IsUserInitiated user_initiated = IsUserInitiated::kFalse);
  ~TasksTracker() = default;

  std::string ToString();

  // Reset tasks list.
  void Reset();

  // Add task to the tracking list.
  void AddTask(std::shared_ptr<server::MonitoredTask> task);

  // Cleanup old tasks that are older than a specific time.
  void CleanupOldTasks();

  // Retrieve most recent tasks for displaying in master UI.
  std::vector<std::shared_ptr<server::MonitoredTask>> GetTasks();

 private:
  const IsUserInitiated user_initiated_;
  // Lock protecting the buffer.
  mutable rw_spinlock lock_;
  // List of most recent tasks.
  boost::circular_buffer<std::shared_ptr<server::MonitoredTask>> tasks_;
};

} // namespace master
} // namespace yb
