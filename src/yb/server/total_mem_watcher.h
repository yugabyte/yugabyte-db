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

#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {
namespace server {

// Allows to monitor total memory usage (resident set size, or RSS) of the current process as seen
// by the operating system, and decide whether the process should exit if it allocates too much
// memory.
//
// This class is not thread-safe.
class TotalMemWatcher {
 public:
  virtual ~TotalMemWatcher() = default;

  void Shutdown() EXCLUDES(exit_loop_mutex_);

  // Re-check the total memory usage and populate the internal state of this watcher object.
  virtual Status Check() = 0;

  // Determines whether the program should terminate based on the most recent check. Returns a
  // non-empty string with the termination reason if the program should terminate, or or an empty
  // string if it should not.
  virtual std::string GetTerminationExplanation() { return std::string(); }

  // Returns a human-readable representation of memory usage details to be printed before program
  // terminates due to exceeding the memory limit.
  virtual std::string GetMemoryUsageDetails() { return "N/A"; }

  static std::unique_ptr<TotalMemWatcher> Create();

  // Enters an infinite loop monitoring memory usage. Invokes trigger_termination_fn and exits from
  // the loop if the memory usage limit has been exceeded. Exits the loop without calling
  // termination_callback if Shutdown has been called.
  void MemoryMonitoringLoop(std::function<void()> trigger_termination_fn)
      EXCLUDES(exit_loop_mutex_);

 protected:
  TotalMemWatcher();

  size_t rss_termination_limit_bytes_;

  std::mutex exit_loop_mutex_;
  bool exit_loop_ GUARDED_BY(exit_loop_mutex_) = false;
  std::condition_variable exit_loop_cv_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TotalMemWatcher);
};

}  // namespace server
}  // namespace yb
