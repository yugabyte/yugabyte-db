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
//

#pragma once

#include "yb/master/leader_epoch.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/server/monitored_task.h"
#include "yb/util/status_callback.h"

namespace yb {

class ThreadPool;

namespace master {

class CatalogManager;
class CatalogEntityWithTasks;

// Tasks that contain multiple asynchronous steps.
// - All steps run on the passed in async thread pool. Steps can be scheduled to run immediately or
// with a delay.
// - Invoking Start will register the task and schedule it in the async thread pool.
// - StartTasks will start a group of tasks together and guarantees the group_completion_cb is
//      called exactly once after all tasks complete.
//
// Notes for Derived classes:
// - Complete must be called when all steps of the task are done. No further steps can be scheduled
// after this point.
// - If any step returns a bad status the task is aborted.
// - At least one shared pointer to the task must be held until UnregisterTask completes.
class MultiStepMonitoredTask : public server::RunnableMonitoredTask {
 public:
  virtual void Start();

  // Abort this task and return its state before it was successfully aborted. If the task
  // entered a different terminal state before we were able to abort it, return that state.
  server::MonitoredTaskState AbortAndReturnPrevState(const Status& status) override
      EXCLUDES(schedule_task_mutex_);

  // Schedule a group of tasks and invoke the callback when all tasks in the group complete.
  // group_completion_cb is invoked with the first bad status if any of the tasks failed or an
  // OK status if all tasks succeeded. group_completion_cb may be invoked on the calling thread
  // with a bad status.
  static Status StartTasks(
      const std::vector<MultiStepMonitoredTask*>& tasks, StdStatusCallback group_completion_cb);

 protected:
  explicit MultiStepMonitoredTask(ThreadPool& async_task_pool, rpc::Messenger& messenger);

  virtual ~MultiStepMonitoredTask() = default;

  // ==================================================================
  // Virtual methods that should be implemented in derived classes.
  // ==================================================================

  virtual Status FirstStep() = 0;
  // Register the task before it starts. Typically the task's shared pointer is registered to a
  // CatalogEntity object, so that it can be tracked, and aborted when the object is dropped or the
  // master leader changes.
  virtual Status RegisterTask() = 0;
  // Unregister the task after it completes.
  virtual void UnregisterTask() = 0;

  // Check if the task is still valid to run.
  virtual Status ValidateRunnable() = 0;

  // This is invoked when the task reaches a terminal state and before it is Unregistered. Status
  // will be OK if the task succeeded and not OK if it failed. Note: Do not invoke this directly. It
  // will be invoked when any step returns a bad status.
  virtual void TaskCompleted(const Status& status);
  virtual void PerformAbort();

  // ==================================================================
  // Helper methods that derived class steps can invoke.
  // ==================================================================

  void Complete();

  std::string LogPrefix() { return Format("$0: ", description()); }

  void ScheduleNextStep(std::function<Status()> next_step, std::string step_description)
      EXCLUDES(schedule_task_mutex_);

  // If the task needs to run with a delay we schedule on the reactor with the delay and when
  // that executes we schedule the actual step on the async thread pool.
  void ScheduleNextStepWithDelay(
      std::function<Status()> next_step, std::string step_description, MonoDelta delay)
      EXCLUDES(schedule_task_mutex_);

  // Optional callback invoked when the task finishes. Callback is not allowed to access the task.
  StdStatusCallback completion_callback_;

 private:
  Status Run() override EXCLUDES(schedule_task_mutex_, step_execution_mutex_);
  Status RunInternal() EXCLUDES(schedule_task_mutex_, step_execution_mutex_);
  void ScheduleInternal() EXCLUDES(schedule_task_mutex_);
  void AbortReactorTaskIfScheduled() EXCLUDES(schedule_task_mutex_);

  // Attempts to set the state to the specified value.
  // The state cannot be changed once it is set to a terminal value.
  // If new_state is terminal then returns true if and only if we reached that terminal state for
  // the first time.
  // Otherwise, returns true if state is now new_state.
  bool TrySetState(server::MonitoredTaskState new_state);
  void EndTask(const Status& s);

  rpc::Messenger& messenger_;
  ThreadPool& async_task_pool_;

  // Mutex to ensure sure we execute steps sequentially.
  std::mutex step_execution_mutex_;

  std::mutex schedule_task_mutex_;
  rpc::ScheduledTaskId reactor_task_id_ GUARDED_BY(schedule_task_mutex_) = rpc::kInvalidTaskId;

  std::string next_step_description_ GUARDED_BY(schedule_task_mutex_);
  std::function<Status()> next_step_ GUARDED_BY(schedule_task_mutex_) = nullptr;
};

// A MultiStepMonitoredTask that is tied to a single CatalogEntity object (ex: Table) and the master
// leader epoch.
class MultiStepCatalogEntityTask : public MultiStepMonitoredTask {
 protected:
  MultiStepCatalogEntityTask(
      std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func,
      ThreadPool& async_task_pool, rpc::Messenger& messenger,
      CatalogEntityWithTasks& catalog_entity, const LeaderEpoch& epoch);
  Status ValidateRunnable() override;

  const LeaderEpoch epoch_;
  CatalogEntityWithTasks& catalog_entity_;

 private:
  Status RegisterTask() override;
  void UnregisterTask() override;

  const std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func_;
};

}  // namespace master
}  // namespace yb
