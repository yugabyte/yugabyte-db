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

#include "yb/util/background_task.h"

#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

namespace yb {

BackgroundTask::BackgroundTask(
    std::function<void()> task, std::string category, const std::string& name,
    std::chrono::milliseconds interval_msec)
    : task_(std::move(task)),
      category_(category),
      name_(std::move(name)),
      interval_(interval_msec) {}

BackgroundTask::~BackgroundTask() {
}

Status BackgroundTask::Init() {
  RETURN_NOT_OK(Thread::Create(category_, name_, &BackgroundTask::Run, this, &thread_));
  return Status::OK();
}

// Wait for pending tasks and shut down
void BackgroundTask::Shutdown() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (closing_) {
      VLOG(2) << "BackgroundTask already shut down";
      return;
    }
    closing_ = true;
  }
  cond_.notify_one();
  CHECK_OK(ThreadJoiner(thread_.get()).Join());
}

Status BackgroundTask::Wake() {
  {
    std::lock_guard lock(mutex_);
    if (closing_) {
      return STATUS(ShutdownInProgress, "Task is shutting down.");
    }
    have_job_ = true;
  }
  cond_.notify_one();
  return Status::OK();
}

void BackgroundTask::Run() {
  while (WaitForJob()) {
    task_();
  }
  VLOG(1) << "BackgroundTask thread shutting down";
}

bool BackgroundTask::WaitForJob() {
  std::unique_lock<std::mutex> lock(mutex_);
  while(true) {
    if (closing_) {
      return false;
    }
    if (have_job_) {
      have_job_ = false;
      return true;
    }

    // Wait
    if (interval_ != std::chrono::milliseconds::zero()) {
      cond_.wait_for(lock, interval_);
      // If we wake here from the interval_ timeout, then we should behave as if we have a job. If
      // we wake from an explicit notify from a Wake() call, we should still behave as if we have
      // a job.
      have_job_ = true;
    } else {
      cond_.wait(lock);
    }
  }
}

}  // namespace yb
