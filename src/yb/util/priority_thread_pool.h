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

#include <memory>

#include "yb/util/flags.h"

#include "yb/gutil/casts.h"

#include "yb/util/status_fwd.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

namespace yb {

YB_DEFINE_ENUM(PriorityThreadPoolTaskState, (kPaused)(kNotStarted)(kRunning));
const uint64_t kDefaultGroupNo = 0;
const int kHighPriority = 300;

// Used to contain both priorities
struct PriorityThreadPoolPriorities {
  int task_priority;
  int group_no_priority;
};

// PriorityThreadPoolSuspender is provided to task ran by thread pool, task could use it to check
// whether is should be preempted in favor of another task with higher priority.
class PriorityThreadPoolSuspender {
 public:
  virtual void PauseIfNecessary() = 0;
  virtual ~PriorityThreadPoolSuspender() {}
};

class PriorityThreadPoolTask {
 public:
  PriorityThreadPoolTask();

  virtual ~PriorityThreadPoolTask() = default;

  // If status is OK - execute this task in the current thread.
  // Otherwise - abort task with specified status.
  virtual void Run(const Status& status, PriorityThreadPoolSuspender* suspender) = 0;

  // Returns true if the task belongs to specified key, which was passed to
  // PriorityThreadPool::Remove and and should be removed when we remove key.
  virtual bool ShouldRemoveWithKey(void* key) = 0;

  virtual std::string ToString() const = 0;

  // Updates any stats (ex: metrics) associated with the tasks changing state to another.
  virtual void UpdateStatsStateChangedTo(PriorityThreadPoolTaskState state) {}

  // Updates any stats (ex: metrics) associated with the tasks changing state from another.
  virtual void UpdateStatsStateChangedFrom(PriorityThreadPoolTaskState state) {}

  // Calculates group no priority for the task based on the number of active_tasks.
  // Group No priority is used for prioritizing which tasks to run.
  // Group No priority should be inversly proportional to active_tasks.
  virtual int CalculateGroupNoPriority(int active_tasks) const = 0;

  size_t SerialNo() const {
    return serial_no_;
  }

 private:
  const size_t serial_no_;
};

// Tasks submitted to this pool have assigned priority and are picked from queue using it.
class PriorityThreadPool {
 public:
  explicit PriorityThreadPool(size_t max_running_tasks, bool use_group_no_priority = false);
  ~PriorityThreadPool();

  // Submit task to the pool.
  // On success task ownership is transferred to the pool, i.e. `task` would point to nullptr.
  Status Submit(
      int task_priority, std::unique_ptr<PriorityThreadPoolTask>* task,
      const uint64_t group_no = kDefaultGroupNo);

  template <class Task>
  Status Submit(
      int task_priority, std::unique_ptr<Task>* task, const uint64_t group_no = kDefaultGroupNo) {
    std::unique_ptr<PriorityThreadPoolTask> temp_task = std::move(*task);
    auto result = Submit(task_priority, &temp_task, group_no);
    task->reset(down_cast<Task*>(temp_task.release()));
    return result;
  }

  // Remove all removable (see PriorityThreadPoolTask::ShouldRemoveWithKey) tasks with provided key
  // from the pool.
  void Remove(void* key);

  // Change task priority of task with specified serial no.
  // Returns true if change was performed.
  bool ChangeTaskPriority(size_t serial_no, int priority);

  // Prioritizes task heavily (above all others)
  // Returns true if change was performed.
  // The change may not be performed if the disk priority is already frozen, or
  // the serial no is not found amongst the current set of tasks.
  bool PrioritizeTask(size_t serial_no);


  void Shutdown() {
    StartShutdown();
    CompleteShutdown();
  }

  // Two step shutdown paradigm is used to prevent deadlock when shutting down multiple components.
  // There could be case when one component wait until other component aborts specific job, but
  // it is not done since shutdown of second component is invoked after shutdown of the first one.
  // To avoid this case StartShutdown could be invoked on both of them, then CompleteShutdown waits
  // until they complete it.

  // Initiates shutdown of this pool. All new tasks will be aborted after this point.
  void StartShutdown();

  // Completes shutdown of this pool. It is safe to destroy pool after it.
  void CompleteShutdown();

  // Dumps state to string, useful for debugging.
  std::string StateToString();

  void TEST_SetThreadCreationFailureProbability(double probability);

  size_t TEST_num_tasks_pending();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace yb
