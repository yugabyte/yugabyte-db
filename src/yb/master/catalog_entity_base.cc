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

#include "yb/master/catalog_entity_base.h"
#include "yb/master/tasks_tracker.h"

#include "yb/util/shared_lock.h"

namespace yb::master {

CatalogEntityWithTasks::CatalogEntityWithTasks(scoped_refptr<TasksTracker> tasks_tracker)
    : tasks_tracker_(tasks_tracker) {}

CatalogEntityWithTasks::~CatalogEntityWithTasks() {
  CloseAndWaitForAllTasksToAbort();
  DCHECK(!HasTasks());
}

std::size_t CatalogEntityWithTasks::NumTasks() const {
  SharedLock l(mutex_);
  return pending_tasks_.size();
}

bool CatalogEntityWithTasks::HasTasks() const {
  SharedLock l(mutex_);
  VLOG_IF(3, !pending_tasks_.empty()) << __func__ << ": " << AsString(pending_tasks_);
  return !pending_tasks_.empty();
}

bool CatalogEntityWithTasks::HasTasks(server::MonitoredTaskType type) const {
  SharedLock l(mutex_);
  for (const auto& task : pending_tasks_) {
    if (task->type() == type) {
      return true;
    }
  }
  return false;
}

void CatalogEntityWithTasks::AddTask(server::MonitoredTaskPtr task) {
  AddTaskInternal(task, /*allow_multiple_of_same_type=*/true);
}

bool CatalogEntityWithTasks::AddTaskIfNotPresent(server::MonitoredTaskPtr task) {
  return AddTaskInternal(task, /*allow_multiple_of_same_type=*/false);
}

bool CatalogEntityWithTasks::AddTaskInternal(
    server::MonitoredTaskPtr task, bool allow_multiple_of_same_type) {
  Status status = Status::OK();
  {
    std::lock_guard l(mutex_);
    if (closing_) {
      status = STATUS(Expired, "Table closing");
    } else if (!allow_multiple_of_same_type) {
      const auto task_type = task->type();
      for (const auto& existing_task : pending_tasks_) {
        if (existing_task->type() == task_type) {
          status =
              STATUS_FORMAT(InvalidArgument, "Task of type $0 already exists", task->type_name());
          break;
        }
      }
    }

    if (status.ok()) {
      pending_tasks_.insert(task);
      if (tasks_tracker_) {
        tasks_tracker_->AddTask(task);
      }
    }
  }
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  if (!status.ok()) {
    task->AbortAndReturnPrevState(status, /*call_task_finisher=*/true);
    return false;
  }
  return true;
}

bool CatalogEntityWithTasks::RemoveTask(const server::MonitoredTaskPtr& task) {
  bool result;
  {
    std::lock_guard l(mutex_);
    pending_tasks_.erase(task);
    result = pending_tasks_.empty();
  }
  VLOG(1) << "Removed task " << task.get() << " " << task->description();
  return result;
}

// Aborts tasks which have their rpc in progress, rest of them are aborted and also erased
// from the pending list.
void CatalogEntityWithTasks::AbortTasks(
    const std::unordered_set<server::MonitoredTaskType>& tasks_to_ignore) {
  AbortTasksAndCloseIfRequested(/* close */ false, /* call_task_finisher */ true, tasks_to_ignore);
}

void CatalogEntityWithTasks::AbortTasksAndClose(bool call_task_finisher) {
  AbortTasksAndCloseIfRequested(/* close */ true, call_task_finisher);
}

void CatalogEntityWithTasks::AbortTasksAndCloseIfRequested(
    bool close, bool call_task_finisher,
    const std::unordered_set<server::MonitoredTaskType>& tasks_to_ignore) {
  std::vector<server::MonitoredTaskPtr> abort_tasks;
  {
    std::lock_guard l(mutex_);
    if (close) {
      closing_ = true;
    }
    abort_tasks.reserve(pending_tasks_.size());
    abort_tasks.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
  }

  if (abort_tasks.empty()) {
    return;
  }
  auto status = close ? STATUS(Expired, "Table closing") : STATUS(Aborted, "Table closing");
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  for (const auto& task : abort_tasks) {
    if (tasks_to_ignore.contains(task->type())) {
      continue;
    }
    VLOG_WITH_FUNC(1) << (close ? "Close and abort" : "Abort") << " task " << task.get() << " "
                      << task->description();
    task->AbortAndReturnPrevState(status, call_task_finisher);
  }
}

void CatalogEntityWithTasks::WaitTasksCompletion() {
  const auto kMaxSleep = MonoDelta::FromSeconds(5);
  const double kSleepMultiplier = 5.0 / 4;

  auto sleep_time = MonoDelta::FromMilliseconds(5);
  std::vector<server::MonitoredTaskPtr> waiting_list;
  while (true) {
    const bool should_log_info = sleep_time >= kMaxSleep;
    {
      SharedLock l(mutex_);
      if (pending_tasks_.empty()) {
        return;
      }

      if (VLOG_IS_ON(1) || should_log_info) {
        waiting_list.clear();
        waiting_list.reserve(pending_tasks_.size());
        waiting_list.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
      }
    }

    for (const auto& task : waiting_list) {
      if (should_log_info) {
        LOG(WARNING) << "Long wait for aborting task " << task.get() << " " << task->description();
      } else {
        VLOG(1) << "Waiting for aborting task " << task.get() << " " << task->description();
      }
    }

    SleepFor(sleep_time);
    sleep_time = std::min(sleep_time * kSleepMultiplier, kMaxSleep);
  }
}

std::unordered_set<server::MonitoredTaskPtr> CatalogEntityWithTasks::GetTasks() const {
  SharedLock l(mutex_);
  return pending_tasks_;
}

void CatalogEntityWithTasks::CloseAndWaitForAllTasksToAbort() {
  VLOG(1) << "Aborting tasks";
  AbortTasksAndClose(/* call_task_finisher */ true);
  VLOG(1) << "Waiting on Aborting tasks";
  WaitTasksCompletion();
  VLOG(1) << "Waiting on Aborting tasks done";
}

}  // namespace yb::master
