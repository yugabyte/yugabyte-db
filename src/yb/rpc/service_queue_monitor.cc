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

#include "yb/rpc/service_queue_monitor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "yb/gutil/strings/join.h"

#include "yb/rpc/rpc_service.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/stack_trace.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

using namespace std::literals;

DEFINE_RUNTIME_int64(rpc_queue_stack_dump_threshold, 0,
    "If greater than 0, dump the stacks of all threads to the log when the queue of any RPC "
    "service stays at or above this many calls for rpc_queue_stack_dump_min_polls consecutive "
    "polls. Helps diagnosing why RPC worker threads are not draining the service queues. "
    "0 disables the RPC queue depth monitor.");
TAG_FLAG(rpc_queue_stack_dump_threshold, advanced);

DEFINE_RUNTIME_int32(rpc_queue_stack_dump_poll_interval_ms, 1000,
    "Interval (in ms) between RPC service queue depth polls, when the RPC queue depth monitor "
    "is enabled via rpc_queue_stack_dump_threshold.");
TAG_FLAG(rpc_queue_stack_dump_poll_interval_ms, advanced);

DEFINE_RUNTIME_int32(rpc_queue_stack_dump_min_polls, 3,
    "Number of consecutive polls for which an RPC service queue has to stay at or above "
    "rpc_queue_stack_dump_threshold before thread stacks are dumped to the log.");
TAG_FLAG(rpc_queue_stack_dump_min_polls, advanced);

DEFINE_RUNTIME_int32(rpc_queue_stack_dump_max_backoff_ms, 300000,
    "Maximum interval (in ms) between consecutive thread stack dumps while an RPC service "
    "queue stays at or above rpc_queue_stack_dump_threshold. Repeated dumps are suppressed "
    "with an exponential backoff capped at this interval; the backoff resets once the queue "
    "drains below the threshold.");
TAG_FLAG(rpc_queue_stack_dump_max_backoff_ms, advanced);

namespace yb::rpc {

namespace {

constexpr int32_t kMinPollIntervalMs = 10;

// Maximum number of thread names listed per group of threads sharing the same stack.
constexpr size_t kMaxThreadNamesPerGroup = 10;

// Guards against multiple messengers in the same process (e.g. a tablet server and its CQL
// proxy) dumping thread stacks back to back for the same overload event.
std::mutex global_dump_time_mutex;
CoarseTimePoint global_last_dump_time;

bool ShouldSkipDumpGlobally(CoarseTimePoint now, CoarseMonoClock::Duration min_gap) {
  std::lock_guard lock(global_dump_time_mutex);
  if (global_last_dump_time != CoarseTimePoint() && now < global_last_dump_time + min_gap) {
    return true;
  }
  global_last_dump_time = now;
  return false;
}

}  // namespace

class ServiceQueueMonitor::Impl {
 public:
  explicit Impl(const std::string& name) : name_(name) {}

  void Shutdown() {
    ThreadPtr thread;
    {
      std::lock_guard lock(mutex_);
      stop_.store(true, std::memory_order_release);
      cond_.notify_one();
      if (!thread_) {
        return;
      }
      thread = thread_;
    }
    thread->Join();
  }

  void Track(const std::string& service_name, const RpcServicePtr& service) {
    std::lock_guard lock(mutex_);
    if (stop_.load(std::memory_order_acquire)) {
      return;
    }
    services_.emplace_back(service_name, service);
    if (!thread_) {
      thread_ = CHECK_RESULT(Thread::Make(
          name_ + "_rpc_queue", name_ + "_rpc_queue_monitor", [this] { Execute(); }));
    }
  }

 private:
  void Execute() {
    std::unique_lock lock(mutex_);
    while (!stop_.load(std::memory_order_acquire)) {
      auto poll_interval_ms = std::max(
          FLAGS_rpc_queue_stack_dump_poll_interval_ms, kMinPollIntervalMs);
      cond_.wait_for(lock, std::chrono::milliseconds(poll_interval_ms));
      if (stop_.load(std::memory_order_acquire)) {
        break;
      }
      auto threshold = FLAGS_rpc_queue_stack_dump_threshold;
      if (threshold <= 0) {
        ResetPollState();
        continue;
      }
      size_t max_depth = 0;
      const std::string* worst_service = nullptr;
      for (const auto& [service_name, service] : services_) {
        auto depth = service->QueuedCalls();
        if (worst_service == nullptr || depth > max_depth) {
          max_depth = depth;
          worst_service = &service_name;
        }
      }
      if (worst_service == nullptr || max_depth < static_cast<size_t>(threshold)) {
        ResetPollState();
        continue;
      }
      if (++consecutive_polls_over_threshold_ <
              std::max(FLAGS_rpc_queue_stack_dump_min_polls, 1)) {
        continue;
      }
      auto now = CoarseMonoClock::Now();
      if (next_dump_time_ != CoarseTimePoint() && now < next_dump_time_) {
        continue;
      }
      if (ShouldSkipDumpGlobally(now, poll_interval_ms * 1ms)) {
        continue;
      }
      auto max_backoff_ms = std::max<int64_t>(
          FLAGS_rpc_queue_stack_dump_max_backoff_ms, poll_interval_ms);
      suppress_ms_ = suppress_ms_ == 0
          ? static_cast<int64_t>(poll_interval_ms) *
                std::max(FLAGS_rpc_queue_stack_dump_min_polls, 1)
          : std::min(suppress_ms_ * 2, max_backoff_ms);
      next_dump_time_ = now + suppress_ms_ * 1ms;
      // Copy what the dump needs, then release the lock so that Track and Shutdown are not
      // blocked while stacks are being collected.
      auto service_name = *worst_service;
      auto consecutive_polls = consecutive_polls_over_threshold_;
      auto suppress_ms = suppress_ms_;
      lock.unlock();
      DumpThreadStacks(service_name, max_depth, threshold, consecutive_polls, suppress_ms);
      lock.lock();
    }
  }

  void ResetPollState() {
    consecutive_polls_over_threshold_ = 0;
    suppress_ms_ = 0;
    next_dump_time_ = CoarseTimePoint();
  }

  void DumpThreadStacks(
      const std::string& service_name, size_t depth, int64_t threshold, int32_t consecutive_polls,
      int64_t suppress_ms) {
    LOG(WARNING) << name_ << ": RPC service queue for " << service_name << " has " << depth
                 << " queued calls, at or above threshold " << threshold << " for "
                 << consecutive_polls << " consecutive polls. Dumping thread stacks. "
                 << "Suppressing further dumps for " << suppress_ms << " ms.";
    auto threads = ListThreadsForStackTrace();
    if (threads.empty()) {
      return;
    }
    std::sort(threads.begin(), threads.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.tid_for_stack < rhs.tid_for_stack;
    });
    std::vector<ThreadIdForStack> tids;
    tids.reserve(threads.size());
    for (const auto& thread : threads) {
      tids.push_back(thread.tid_for_stack);
    }
    auto stacks = ThreadStacks(tids);
    // Group threads that share the same stack (or the same collection failure), so that each
    // stack is symbolized and logged only once.
    struct Group {
      std::vector<size_t> thread_indexes;
    };
    std::map<std::string, Group> groups;
    for (size_t i = 0; i != stacks.size(); ++i) {
      auto key = stacks[i].ok() ? std::string(stacks[i]->as_string_view())
                                : "!" + stacks[i].status().ToString();
      groups[std::move(key)].thread_indexes.push_back(i);
    }
    for (const auto& [_, group] : groups) {
      const auto& stack = stacks[group.thread_indexes.front()];
      std::vector<std::string> names;
      names.reserve(std::min(group.thread_indexes.size(), kMaxThreadNamesPerGroup + 1));
      for (auto index : group.thread_indexes) {
        if (names.size() == kMaxThreadNamesPerGroup) {
          names.push_back("...");
          break;
        }
        names.push_back(threads[index].name);
      }
      LOG(WARNING) << name_ << ": " << group.thread_indexes.size() << " thread(s) with stack ["
                   << JoinStrings(names, ", ") << "]:\n"
                   << (stack.ok() ? stack->Symbolize()
                                  : "Failed to collect stack: " + stack.status().ToString());
    }
  }

  const std::string name_;
  std::mutex mutex_;
  std::condition_variable cond_;
  ThreadPtr thread_ GUARDED_BY(mutex_);
  std::atomic<bool> stop_{false};
  // Guarded by mutex_. Not annotated because Execute accesses it under std::unique_lock, which
  // the thread safety analysis does not track.
  std::vector<std::pair<std::string, RpcServicePtr>> services_;

  // Poll state below is only accessed by the monitor thread.
  int32_t consecutive_polls_over_threshold_ = 0;
  // Interval used to suppress dumps after the most recent one, doubling on every dump up to
  // FLAGS_rpc_queue_stack_dump_max_backoff_ms. 0 means no dump happened since the monitored
  // queues were last below the threshold.
  int64_t suppress_ms_ = 0;
  CoarseTimePoint next_dump_time_;
};

ServiceQueueMonitor::ServiceQueueMonitor(const std::string& name)
    : impl_(std::make_unique<Impl>(name)) {}

ServiceQueueMonitor::~ServiceQueueMonitor() {
  Shutdown();
}

void ServiceQueueMonitor::Shutdown() {
  impl_->Shutdown();
}

void ServiceQueueMonitor::Track(const std::string& service_name, const RpcServicePtr& service) {
  impl_->Track(service_name, service);
}

}  // namespace yb::rpc
