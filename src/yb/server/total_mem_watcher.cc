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

#if defined(__linux__)
#include <unistd.h>
#endif

#include <fstream>
#include <chrono>
#include <thread>

#include "yb/util/atomic.h"
#include "yb/util/callsite_profiling.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/memory/memory.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"

#include "yb/server/total_mem_watcher.h"

using namespace std::literals;

#if defined(THREAD_SANITIZER)
const int kDefaultMemoryLimitTerminationPercent = 500;
#elif defined(ADDRESS_SANITIZER)
const int kDefaultMemoryLimitTerminationPercent = 300;
#else
const int kDefaultMemoryLimitTerminationPercent = 200;
#endif

DEFINE_UNKNOWN_int32(memory_limit_termination_threshold_pct, kDefaultMemoryLimitTerminationPercent,
             "If the RSS (resident set size) of the program reaches this percentage of the "
             "root memory tracker limit, the program will exit. RSS is measured using operating "
             "system means, not the memory allocator. Set to 0 to disable this behavior.");

DEFINE_RUNTIME_int32(total_mem_watcher_interval_millis, 1000,
    "Interval in milliseconds between checking the total memory usage of the current "
    "process as seen by the operating system, and deciding whether to terminate in case "
    "of excessive memory consumption.");

namespace yb {
namespace server {

TotalMemWatcher::TotalMemWatcher() {
  if (FLAGS_memory_limit_termination_threshold_pct <= 0) {
    rss_termination_limit_bytes_ = 0;
  } else {
    rss_termination_limit_bytes_ =
        (MemTracker::GetRootTracker()->limit() *
         FLAGS_memory_limit_termination_threshold_pct) / 100;
  }
}

void TotalMemWatcher::Shutdown() {
  {
    std::lock_guard l(exit_loop_mutex_);
    exit_loop_ = true;
  }
  YB_PROFILE(exit_loop_cv_.notify_all());
}

void TotalMemWatcher::MemoryMonitoringLoop(std::function<void()> trigger_termination_fn) {
  if (FLAGS_memory_limit_termination_threshold_pct > 0) {
    int64_t root_tracker_limit = MemTracker::GetRootTracker()->limit();
    LOG(INFO) << "Root memtracker limit: " << root_tracker_limit << " ("
              << (root_tracker_limit / 1024 / 1024) << " MiB); this server will stop if memory "
              << "usage exceeds " << FLAGS_memory_limit_termination_threshold_pct << "% of that: "
              << rss_termination_limit_bytes_ << " bytes ("
              << (rss_termination_limit_bytes_ / 1024 / 1024) << " MiB).";
  }

  while (true) {
    {
      const auto timeout = GetAtomicFlag(&FLAGS_total_mem_watcher_interval_millis) * 1ms;
      std::unique_lock lock(exit_loop_mutex_);
      if (exit_loop_cv_.wait_for(
              lock, timeout, [this]() REQUIRES(exit_loop_mutex_) { return exit_loop_; })) {
        return;
      }
    }

    Status mem_check_status = Check();
    if (!mem_check_status.ok()) {
      YB_LOG_EVERY_N_SECS(WARNING, 10)
          << "Failed to check total memory usage: " << mem_check_status.ToString();
      continue;
    }
    std::string termination_explanation = GetTerminationExplanation();
    if (!termination_explanation.empty()) {
      LOG(ERROR) << "Memory usage exceeded configured limit, terminating the process: "
                 << termination_explanation << "\nDetails:\n"
                 << GetMemoryUsageDetails();
      trigger_termination_fn();
      return;
    }
  }
}

#ifdef __linux__

// Data available in /proc/[pid]/statm on Linux.
// Provides information about memory usage, measured in pages.
//
// More details at: http://man7.org/linux/man-pages/man5/proc.5.html
struct StatM {
  // size       (1) total program size
  //            (same as VmSize in /proc/[pid]/status)
  int64_t size = 0;

  // resident   (2) resident set size
  //            (same as VmRSS in /proc/[pid]/status)
  int64_t resident = 0;

  // shared     (3) number of resident shared pages (i.e., backed by a file)
  //            (same as RssFile+RssShmem in /proc/[pid]/status)
  int64_t shared = 0;

  // text       (4) text (code)
  int64_t text = 0;

  // lib        (5) library (unused since Linux 2.6; always 0) [skipped]

  // data       (6) data + stack
  int64_t data = 0;

  // dt         (7) dirty pages (unused since Linux 2.6; always 0) [skipped]
};

class LinuxTotalMemWatcher : public TotalMemWatcher {
 public:
  LinuxTotalMemWatcher()
      : statm_path_(Format("/proc/$0/statm", getpid())),
        page_size_(sysconf(_SC_PAGESIZE)) {
  }
  ~LinuxTotalMemWatcher() {}

  Status Check() override {
    RETURN_NOT_OK(ReadStatM());
    return Status::OK();
  }

  std::string GetTerminationExplanation() override {
    if (rss_termination_limit_bytes_ == 0) {
      return std::string();
    }
    size_t non_shared_rss_bytes = (statm_.resident - statm_.shared) * page_size_;
    if (non_shared_rss_bytes <= rss_termination_limit_bytes_) {
      return std::string();
    }
    return Format("Non-shared RSS ($0 bytes, $1 MiB) has exceeded the configured limit "
                  "($2 bytes, $3 MiB)",
                  non_shared_rss_bytes, non_shared_rss_bytes / 1024 / 1024,
                  rss_termination_limit_bytes_,  rss_termination_limit_bytes_ / 1024 / 1024);
  }

  std::string GetMemoryUsageDetails() override {
    std::string result;
#if YB_TCMALLOC_ENABLED
    result += TcMallocStats();
    result += "\n";
#endif
    result += Format(
        "---------------------\n"
        "OS-level memory stats\n"
        "---------------------\n"
        "VmSize:             $0 bytes ($1 MiB)\n"
        "VmRSS:              $2 bytes ($3 MiB)\n"
        "RssFile + RssShmem: $4 bytes ($5 MiB)\n"
        "Text (code):        $6 bytes ($7 MiB)\n"
        "Data + stack:       $8 bytes ($9 MiB)",
        statm_.size * page_size_,     statm_.size * page_size_ / 1024 / 1024,
        statm_.resident * page_size_, statm_.resident * page_size_ / 1024 / 1024,
        statm_.shared * page_size_,   statm_.shared * page_size_ / 1024 / 1024,
        statm_.text * page_size_,     statm_.text * page_size_ / 1024 / 1024,
        statm_.data * page_size_,     statm_.data * page_size_ / 1024 / 1024);
    return result;
  }

 private:
  Status ReadStatM() {
    std::ifstream input_file(statm_path_.c_str());
    if (!input_file) {
      return STATUS_FORMAT(IOError, "Could not open $0: $1", statm_path_, strerror(errno));
    }

    input_file >> statm_.size;
    if (!input_file.good()) {
      return STATUS_FORMAT(IOError, "Error reading total program size from $0", statm_path_);
    }

    input_file >> statm_.resident;
    if (!input_file.good()) {
      return STATUS_FORMAT(IOError, "Error reading resident set size from $0", statm_path_);
    }

    input_file >> statm_.shared;
    if (!input_file.good()) {
      return STATUS_FORMAT(IOError, "Error reading number of resident shared pages from $0",
                           statm_path_);
    }

    input_file >> statm_.text;
    if (!input_file.good()) {
      return STATUS_FORMAT(IOError, "Error reading text (code) size from $0", statm_path_);
    }

    size_t lib_ignored;
    input_file >> lib_ignored;
    if (!input_file.good()) {
      return STATUS_FORMAT(IOError, "Error reading library size (unused) from $0", statm_path_);
    }

    input_file >> statm_.data;
    if (!input_file.good()) {
      return STATUS_FORMAT(IOError, "Error reading data + stack size from $0", statm_path_);
    }

    // No need to read dt, which is always 0.

    return Status::OK();
  }

  const std::string statm_path_;
  StatM statm_;
  size_t page_size_;
};
#endif

#ifdef __APPLE__
class DarwinTotalMemWatcher : public TotalMemWatcher {
 public:
  ~DarwinTotalMemWatcher() {}
  Status Check() override {
    return Status::OK();
  }
};
#endif

std::unique_ptr<TotalMemWatcher> TotalMemWatcher::Create() {
#if defined(__linux__)
  return std::make_unique<LinuxTotalMemWatcher>();
#elif defined(__APPLE__)
  return std::make_unique<DarwinTotalMemWatcher>();
#else
#error Unknown platform
#endif
}

}  // namespace server
}  // namespace yb
