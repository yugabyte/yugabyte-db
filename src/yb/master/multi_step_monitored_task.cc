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

#include "yb/master/multi_step_monitored_task.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"

#include "yb/rpc/messenger.h"

#include "yb/util/source_location.h"
#include "yb/util/threadpool.h"

namespace yb::master {

MultiStepMonitoredTask::MultiStepMonitoredTask(
    ThreadPool& async_task_pool, rpc::Messenger& messenger)
    : messenger_(messenger), async_task_pool_(async_task_pool) {}

void MultiStepMonitoredTask::Start() {
  LOG_WITH_PREFIX(INFO) << "Starting task " << this;
  auto status = RegisterTask();
  if (!status.ok()) {
    AbortAndReturnPrevState(status.CloneAndPrepend("Failed to register task"));
  }

  ScheduleNextStep(std::bind(&MultiStepMonitoredTask::FirstStep, this), "FirstStep");
}

bool MultiStepMonitoredTask::TrySetState(server::MonitoredTaskState new_state) {
  auto old_state = state();
  while (!IsStateTerminal(old_state)) {
    if (state_.compare_exchange_strong(old_state, new_state)) {
      return true;
    }
  }

  return false;
}

void MultiStepMonitoredTask::TaskCompleted(const Status& status) {
  // NoOp
}

void MultiStepMonitoredTask::Complete() {
  if (TrySetState(server::MonitoredTaskState::kComplete)) {
    EndTask(Status::OK());
  }
  // else Task already finished.
}

server::MonitoredTaskState MultiStepMonitoredTask::AbortAndReturnPrevState(const Status& status) {
  DCHECK(!status.ok());
  auto old_state = state();
  if (TrySetState(server::MonitoredTaskState::kAborted)) {
    AbortReactorTaskIfScheduled();
    EndTask(status);
  } else {
    LOG_WITH_PREFIX(WARNING) << this << ": Task already ended. Unable to abort it: " << status;
  }

  return old_state;
}

void MultiStepMonitoredTask::AbortReactorTaskIfScheduled() {
  rpc::ScheduledTaskId reactor_task_id;
  {
    std::lock_guard l(schedule_task_mutex_);
    reactor_task_id = reactor_task_id_;
    reactor_task_id_ = rpc::kInvalidTaskId;
  }

  if (reactor_task_id != rpc::kInvalidTaskId) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "Aborting reactor task id: " << reactor_task_id;
    messenger_.AbortOnReactor(reactor_task_id);
  }
}

void MultiStepMonitoredTask::ScheduleNextStepWithDelay(
    std::function<Status()> next_step, std::string step_description, MonoDelta delay) {
  UniqueLock l(schedule_task_mutex_);
  if (!TrySetState(server::MonitoredTaskState::kWaiting)) {
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Task has ended. State: " << state();
    return;
  }

  VLOG_WITH_PREFIX(1) << "Scheduling " << step_description << " on reactor with delay " << delay;

  next_step_description_ = std::move(step_description);
  next_step_ = std::move(next_step);

  auto task_id_result = messenger_.ScheduleOnReactor(
      std::bind(&MultiStepMonitoredTask::ScheduleInternal, shared_from(this)), delay,
      SOURCE_LOCATION());

  if (!task_id_result.ok()) {
    l.unlock();
    AbortAndReturnPrevState(
        task_id_result.status().CloneAndPrepend("Failed to submit task to reactor"));
    return;
  }

  VLOG_WITH_PREFIX_AND_FUNC(2) << "Reactor task id: " << *task_id_result;
  reactor_task_id_ = *task_id_result;
}

void MultiStepMonitoredTask::ScheduleNextStep(
    std::function<Status()> next_step, std::string step_description) {
  {
    std::lock_guard l(schedule_task_mutex_);
    if (next_step_) {
      LOG(DFATAL) << "Attempt to schedule a step '" << step_description
                  << "' before the previous step '" << next_step_description_ << "' started.";
    }
    next_step_description_ = std::move(step_description);
    next_step_ = std::move(next_step);
  }
  ScheduleInternal();
}

void MultiStepMonitoredTask::ScheduleInternal() {
  if (!TrySetState(server::MonitoredTaskState::kScheduling)) {
    LOG_WITH_PREFIX(WARNING) << "Task already ended. Unable to schedule more work on it";
    return;
  }

  Status status;
  {
    std::lock_guard l(schedule_task_mutex_);
    VLOG_WITH_PREFIX(1) << "Scheduling " << next_step_description_ << " on async task pool";

    status = async_task_pool_.SubmitFunc([task = shared_from(this)]() {
      WARN_NOT_OK(task->Run(), Format("Failed task $0", task->LogPrefix()));
    });
  }

  // If we are not able to enqueue, abort the task.
  if (!status.ok()) {
    AbortAndReturnPrevState(status.CloneAndPrepend("Failed to submit task to async task pool"));
  }
}

Status MultiStepMonitoredTask::Run() {
  SCHECK(
      TrySetState(server::MonitoredTaskState::kRunning), IllegalState,
      "Task already ended. Unable to run more work on it");

  auto status = RunInternal();
  if (!status.ok()) {
    AbortAndReturnPrevState(status);
  }
  return status;
}

Status MultiStepMonitoredTask::RunInternal() {
  std::function<Status()> step;

  // Grab the step_execution_mutex_, since one step schedules the next step, and we dont want the
  // second one to start before the first once goes fully out of scope.
  std::lock_guard step_execution_l(step_execution_mutex_);
  {
    std::lock_guard schedule_task_l(schedule_task_mutex_);
    reactor_task_id_ = rpc::kInvalidTaskId;
    std::swap(next_step_, step);

    if (VLOG_IS_ON(1) && !next_step_description_.empty()) {
      VLOG_WITH_PREFIX(1) << "Running " << next_step_description_;
    }
  }

  if (!step) {
    LOG_WITH_PREFIX(DFATAL) << "No further steps to run for task";
    return STATUS(IllegalState, "Task has no steps to run");
  }

  RETURN_NOT_OK(ValidateRunnable());

  return step();
}

void MultiStepMonitoredTask::EndTask(const Status& status) {
  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << this << " task failed: " << status;
  }

  TaskCompleted(status);

  completion_timestamp_ = MonoTime::Now();
  LOG_WITH_PREFIX(INFO) << this << " task ended" << (status.ok() ? " successfully" : "");

  auto retain_self = shared_from_this();
  UnregisterTask();

  if (completion_callback_) {
    completion_callback_(status);
    completion_callback_ = nullptr;
  }
}

namespace {

// Helper task to keep track of a group of tasks and execute a callback exactly once when all tasks
// complete.
class TaskGroupInfo {
 public:
  static std::shared_ptr<TaskGroupInfo> Create(
      size_t task_count, StdStatusCallback completion_callback) {
    return std::shared_ptr<TaskGroupInfo>(
        new TaskGroupInfo(task_count, std::move(completion_callback)));
  }

  void TaskCompleted(const Status& status) {
    auto first_error_status = SetStatusAndGetFirstError(status);

    if (task_count_.fetch_sub(1) == 1) {
      // This is the last task in the group to complete.
      completion_callback_(first_error_status);
    }
  }

 private:
  TaskGroupInfo(size_t task_count, StdStatusCallback completion_callback)
      : task_count_(task_count), completion_callback_(std::move(completion_callback)) {
    DCHECK_GT(task_count_, 0);
  }

  Status SetStatusAndGetFirstError(const Status& status) {
    std::lock_guard l(status_mutex_);
    if (status_.ok() && !status.ok()) {
      status_ = status;
    }
    return status_;
  }

  std::atomic<size_t> task_count_;
  StdStatusCallback completion_callback_;

  std::mutex status_mutex_;
  Status status_ GUARDED_BY(status_mutex_);

  DISALLOW_COPY_AND_ASSIGN(TaskGroupInfo);
};
}  // namespace

Status MultiStepMonitoredTask::StartTasks(
    const std::vector<MultiStepMonitoredTask*>& tasks, StdStatusCallback group_completion_cb) {
  SCHECK(!tasks.empty(), InvalidArgument, "No tasks provided");

  auto task_group_info = TaskGroupInfo::Create(tasks.size(), std::move(group_completion_cb));

  for (auto* task : tasks) {
    CHECK(task);
    // Preserve exiting callback of the task and run that at the end of the new callback.
    task->completion_callback_ =
        [task_group_info, task_cb = std::move(task->completion_callback_)](const Status& status) {
          if (task_cb) {
            task_cb(status);
          }
          task_group_info->TaskCompleted(status);
        };

    // If the last task failed to schedule then it will be aborted. If all the other tasks completed
    // before this, then group_completion_cb in invoked on this same thread.
    task->Start();
  }

  return Status::OK();
}

MultiStepCatalogEntityTask::MultiStepCatalogEntityTask(
    std::function<Status(const LeaderEpoch& epoch)> validate_epoch_func,
    ThreadPool& async_task_pool, rpc::Messenger& messenger, CatalogEntityWithTasks& catalog_entity,
    const LeaderEpoch& epoch)
    : MultiStepMonitoredTask(async_task_pool, messenger),
      epoch_(std::move(epoch)),
      catalog_entity_(catalog_entity),
      validate_epoch_func_(std::move(validate_epoch_func)) {}

Status MultiStepCatalogEntityTask::RegisterTask() {
  catalog_entity_.AddTask(shared_from_this());
  return Status::OK();
}

void MultiStepCatalogEntityTask::UnregisterTask() {
  DCHECK(catalog_entity_.HasTasks(type()));

  catalog_entity_.RemoveTask(shared_from_this());
}

Status MultiStepCatalogEntityTask::ValidateRunnable() { return validate_epoch_func_(epoch_); }

}  // namespace yb::master
