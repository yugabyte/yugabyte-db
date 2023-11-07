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
#ifndef YB_UTIL_BACKGROUND_TASK_H
#define YB_UTIL_BACKGROUND_TASK_H

#include <condition_variable>
#include <functional>
#include <memory>

#include "yb/gutil/ref_counted.h"

#include "yb/util/status_fwd.h"

namespace yb {

class Thread;

// A task that runs periodically every interval_msec, with the option to be explicitly woken up.
// Executions of RunTask are serialized. If interval_msec is 0, the task only runs when explicitly
// woken up.
// TODO(bojanserafimov): Use in CatalogManagerBgTasks.
class BackgroundTask {
 public:
  BackgroundTask(
      std::function<void()> task, std::string category, const std::string& name,
      std::chrono::milliseconds interval_msec = std::chrono::milliseconds(0));
  ~BackgroundTask();

  Status Init();

  // Set a new run period of the task. Can be used for custom backoff strategy.
  void SetInterval(std::chrono::milliseconds interval_msec);

  // Wait for pending tasks and shut down.
  void Shutdown();

  Status Wake();

 private:
  void Run();

  // Wait for a job or closing. Return true if got a job, false if closing.
  bool WaitForJob();

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
