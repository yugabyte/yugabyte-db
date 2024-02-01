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

std::size_t CatalogEntityWithTasks::NumTasks() const {
  SharedLock l(mutex_);
  return pending_tasks_.size();
}

bool CatalogEntityWithTasks::HasTasks() const {
  SharedLock l(mutex_);
  VLOG_WITH_FUNC(3) << AsString(pending_tasks_);
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
  bool abort_task = false;
  {
    std::lock_guard l(mutex_);
    if (!closing_) {
      pending_tasks_.insert(task);
      if (tasks_tracker_) {
        tasks_tracker_->AddTask(task);
      }
    } else {
      abort_task = true;
    }
  }
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  if (abort_task) {
    task->AbortAndReturnPrevState(STATUS(Expired, "Table closing"));
  }
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
void CatalogEntityWithTasks::AbortTasks() { AbortTasksAndCloseIfRequested(/* close */ false); }

void CatalogEntityWithTasks::AbortTasksAndClose() {
  AbortTasksAndCloseIfRequested(/* close */ true);
}

void CatalogEntityWithTasks::AbortTasksAndCloseIfRequested(bool close) {
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
    VLOG_WITH_FUNC(1) << (close ? "Close and abort" : "Abort") << " task " << task.get() << " "
                      << task->description();
    task->AbortAndReturnPrevState(status);
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

}  // namespace yb::master
