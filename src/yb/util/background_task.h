// Copyright (c) YugaByte, Inc.
#ifndef YB_UTIL_BACKGROUND_TASK_H
#define YB_UTIL_BACKGROUND_TASK_H

#include "yb/util/thread.h"
#include "yb/util/status.h"

namespace yb {

// A task that runs periodically every interval_msec, with the option to be explicitly woken up.
// Executions of RunTask are serialized. If interval_msec is 0, the task only runs when explicitly
// woken up.
// TODO(bojanserafimov): Use in CatalogManagerBgTasks
// TODO(bojanserafimov): Add unit tests
class BackgroundTask {
 public:
  explicit BackgroundTask(std::function<void()> task, std::string category,
                          const std::string& name, std::chrono::milliseconds interval_msec)
    : task_(std::move(task)), category_(category),
    name_(std::move(name)), interval_(interval_msec) { }

  CHECKED_STATUS Init() {
    RETURN_NOT_OK(yb::Thread::Create(category_, name_, &BackgroundTask::Run, this, &thread_));
    return Status::OK();
  }

  // Wait for pending tasks and shut down
  void Shutdown() {
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

  CHECKED_STATUS Wake() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closing_) {
        return STATUS(ShutdownInProgress, "Task is shutting down.");
      }
      have_job_ = true;
    }
    cond_.notify_one();
    return Status::OK();
  }

 private:
  void Run() {
    while (WaitForJob()) {
      task_();
    }
    VLOG(1) << "BackgroundTask thread shutting down";
  }

  // Wait for a job or closing. Return true if got a job, false if closing.
  bool WaitForJob() {
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
      } else {
        cond_.wait(lock);
      }
    }
  }

  bool closing_ = false;
  bool have_job_ = false;

  std::function<void()> task_;
  std::string category_;
  std::string name_;
  std::chrono::milliseconds interval_;

  mutable std::mutex mutex_;
  std::condition_variable cond_;
  scoped_refptr<yb::Thread> thread_;
};

} // namespace yb

#endif // YB_UTIL_BACKGROUND_TASK_H
