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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "yb/gutil/strings/join.h"

#include "yb/rpc/rpc_service.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/stack_trace.h"
#include "yb/util/thread.h"

using namespace std::literals;

DEFINE_RUNTIME_int64(rpc_queue_stack_dump_threshold, 0,
    "If greater than 0, dump RPC worker thread stacks to the log when the queue of any RPC "
    "service stays at or above this many calls for rpc_queue_stack_dump_min_interval_ms. Helps "
    "diagnosing why RPC worker threads are not draining the service queues. 0 disables the RPC "
    "queue depth monitor.");
TAG_FLAG(rpc_queue_stack_dump_threshold, advanced);

DEFINE_RUNTIME_int32(rpc_queue_stack_dump_poll_interval_ms, 1000,
    "Interval (in ms) between RPC service queue depth polls, when the RPC queue depth monitor "
    "is enabled via rpc_queue_stack_dump_threshold.");
TAG_FLAG(rpc_queue_stack_dump_poll_interval_ms, advanced);

DEFINE_RUNTIME_int32(rpc_queue_stack_dump_min_interval_ms, 10000,
    "Minimum time (in ms) for which an RPC service queue has to stay at or above "
    "rpc_queue_stack_dump_threshold before thread stacks are dumped. This is also the initial "
    "interval between repeated dumps while the condition persists.");
TAG_FLAG(rpc_queue_stack_dump_min_interval_ms, advanced);

DEFINE_RUNTIME_int32(rpc_queue_stack_dump_max_interval_ms, 300000,
    "Maximum interval (in ms) between consecutive thread stack dumps while an RPC service "
    "queue stays at or above rpc_queue_stack_dump_threshold. Repeated dumps are suppressed "
    "with an exponential backoff capped at this interval; the backoff resets once the queue "
    "drains below the threshold.");
TAG_FLAG(rpc_queue_stack_dump_max_interval_ms, advanced);

namespace yb::rpc {

namespace {

constexpr int32_t kMinPollIntervalMs = 10;
constexpr size_t kMaxThreadNamesPerGroup = 10;
constexpr size_t kMaxServicesInSummary = 10;

std::atomic<uint64_t> next_monitor_owner_id{1};

}  // namespace

class ServiceQueueMonitor::Impl {
 public:
  ~Impl() {
    Shutdown();
  }

  void Track(
      uint64_t owner_id, const std::string& messenger_name, const std::string& service_name,
      const RpcServicePtr& service, ServicePriority priority) {
    std::lock_guard lock(mutex_);
    if (stop_.load(std::memory_order_acquire)) {
      return;
    }
    services_.push_back(TrackedService {
      .owner_id = owner_id,
      .messenger_name = messenger_name,
      .service_name = service_name,
      .priority = priority,
      .service = service,
      .above_threshold_since = CoarseTimePoint(),
    });
    if (!thread_) {
      auto thread = Thread::Make(
          "rpc_queue", "rpc_queue_monitor", [this] { Execute(); });
      if (!thread.ok()) {
        YB_LOG_EVERY_N_SECS(WARNING, 30)
            << "Failed to start RPC service queue monitor: " << thread.status();
        return;
      }
      thread_ = *thread;
    }
    cond_.notify_one();
  }

  void Untrack(uint64_t owner_id) {
    std::lock_guard lock(mutex_);
    std::erase_if(services_, [owner_id](const auto& service) {
      return service.owner_id == owner_id;
    });
    const auto active_owner =
        std::find(active_dump_owner_ids_.begin(), active_dump_owner_ids_.end(), owner_id);
    if (active_owner != active_dump_owner_ids_.end()) {
      active_dump_owner_ids_.erase(active_owner);
      if (active_dump_owner_ids_.empty()) {
        cancel_dump_.store(true, std::memory_order_release);
      }
    }
    cond_.notify_one();
  }

 private:
  struct TrackedService {
    uint64_t owner_id;
    std::string messenger_name;
    std::string service_name;
    ServicePriority priority;
    RpcServicePtr service;
    CoarseTimePoint above_threshold_since;
  };

  struct AffectedService {
    uint64_t owner_id;
    std::string messenger_name;
    std::string service_name;
    ServicePriority priority;
    size_t depth;
    CoarseDuration above_threshold_for;
    bool triggers_dump;
  };

  struct TriggerEvaluation {
    std::vector<AffectedService> affected_services;
    bool should_dump = false;
  };

  void Shutdown() {
    ThreadPtr thread;
    {
      std::lock_guard lock(mutex_);
      stop_.store(true, std::memory_order_release);
      cancel_dump_.store(true, std::memory_order_release);
      cond_.notify_one();
      thread = std::exchange(thread_, ThreadPtr());
    }
    if (thread) {
      thread->Join();
    }
  }

  void Execute() {
    std::unique_lock lock(mutex_);
    while (!stop_.load(std::memory_order_acquire)) {
      const auto poll_interval = std::chrono::milliseconds(
          std::max(FLAGS_rpc_queue_stack_dump_poll_interval_ms, kMinPollIntervalMs));
      auto wait_duration = CoarseDuration(poll_interval);
      if (next_dump_time_ != CoarseTimePoint()) {
        const auto now = CoarseMonoClock::Now();
        wait_duration = next_dump_time_ <= now
            ? CoarseDuration::zero()
            : std::min(wait_duration, next_dump_time_ - now);
      }
      if (wait_duration > CoarseDuration::zero()) {
        cond_.wait_for(lock, wait_duration);
      }
      if (stop_.load(std::memory_order_acquire)) {
        break;
      }

      const auto threshold = FLAGS_rpc_queue_stack_dump_threshold;
      if (threshold <= 0) {
        ResetTriggerState();
        ResetDumpState();
        last_threshold_ = threshold;
        continue;
      }

      if (threshold != last_threshold_) {
        ResetTriggerState();
        ResetDumpState();
        last_threshold_ = threshold;
      }

      const auto min_interval = CoarseDuration(std::chrono::milliseconds(
          std::max(FLAGS_rpc_queue_stack_dump_min_interval_ms, 1)));
      const auto max_interval = std::max(
          CoarseDuration(std::chrono::milliseconds(
              std::max(FLAGS_rpc_queue_stack_dump_max_interval_ms, 1))),
          min_interval);
      const auto now = CoarseMonoClock::Now();
      auto evaluation = CheckTriggersForDump(now, threshold, min_interval);
      if (!evaluation.should_dump) {
        ResetDumpState();
        continue;
      }
      MaybeDump(lock, now, threshold, min_interval, max_interval, std::move(evaluation));
    }
  }

  TriggerEvaluation CheckTriggersForDump(
      CoarseTimePoint now, int64_t threshold, CoarseDuration min_interval) {
    TriggerEvaluation result;
    result.affected_services.reserve(services_.size());
    for (auto& service : services_) {
      const auto depth = service.service->QueueSize();
      if (depth < static_cast<size_t>(threshold)) {
        service.above_threshold_since = CoarseTimePoint();
        continue;
      }
      if (service.above_threshold_since == CoarseTimePoint()) {
        service.above_threshold_since = now;
      }
      const auto above_threshold_for = now - service.above_threshold_since;
      const auto triggers_dump = above_threshold_for >= min_interval;
      result.should_dump = result.should_dump || triggers_dump;
      result.affected_services.push_back(AffectedService {
        .owner_id = service.owner_id,
        .messenger_name = service.messenger_name,
        .service_name = service.service_name,
        .priority = service.priority,
        .depth = depth,
        .above_threshold_for = above_threshold_for,
        .triggers_dump = triggers_dump,
      });
    }
    std::sort(
        result.affected_services.begin(), result.affected_services.end(),
        [](const auto& lhs, const auto& rhs) {
          if (lhs.triggers_dump != rhs.triggers_dump) {
            return lhs.triggers_dump > rhs.triggers_dump;
          }
          if (lhs.depth != rhs.depth) {
            return lhs.depth > rhs.depth;
          }
          return std::tie(lhs.messenger_name, lhs.service_name) <
                 std::tie(rhs.messenger_name, rhs.service_name);
        });
    return result;
  }

  void MaybeDump(
      std::unique_lock<std::mutex>& lock, CoarseTimePoint now, int64_t threshold,
      CoarseDuration min_interval, CoarseDuration max_interval,
      TriggerEvaluation evaluation) {
    if (!backoff_) {
      backoff_.emplace(
          CoarseTimePoint::max(), max_interval, min_interval,
          CoarseBackoffWaiter::kDefaultMaxJitterMs, /* init_exponent= */ 1);
      next_dump_time_ = CoarseTimePoint();
    }
    if (next_dump_time_ != CoarseTimePoint() && now < next_dump_time_) {
      return;
    }

    const auto suppress_for = backoff_->DelayForTime(now);
    if (suppress_for < max_interval) {
      backoff_->NextAttempt();
    }
    next_dump_time_ = now + suppress_for;
    const auto dump_id = next_dump_id_++;
    active_dump_owner_ids_.clear();
    for (const auto& service : evaluation.affected_services) {
      if (service.triggers_dump &&
          std::find(
              active_dump_owner_ids_.begin(), active_dump_owner_ids_.end(), service.owner_id) ==
              active_dump_owner_ids_.end()) {
        active_dump_owner_ids_.push_back(service.owner_id);
      }
    }
    cancel_dump_.store(false, std::memory_order_release);
    lock.unlock();
    const auto dump_completed = DumpThreadStacks(dump_id, threshold, suppress_for, evaluation);
    lock.lock();
    active_dump_owner_ids_.clear();
    if (!dump_completed) {
      ResetDumpState();
    }
  }

  void ResetTriggerState() {
    for (auto& service : services_) {
      service.above_threshold_since = CoarseTimePoint();
    }
  }

  void ResetDumpState() {
    backoff_.reset();
    next_dump_time_ = CoarseTimePoint();
  }

  bool DumpCancelled() const {
    return stop_.load(std::memory_order_acquire) ||
           cancel_dump_.load(std::memory_order_acquire);
  }

  bool IsRelevantThreadCategory(
      const std::string& category, const TriggerEvaluation& evaluation) const {
    for (const auto& service : evaluation.affected_services) {
      if (service.priority == ServicePriority::kHigh) {
        if (category == service.messenger_name + "-high-pri") {
          return true;
        }
      } else if (category == service.messenger_name ||
                 category.starts_with(service.messenger_name + "_pool_")) {
        return true;
      }
    }
    return false;
  }

  bool DumpThreadStacks(
      uint64_t dump_id, int64_t threshold, CoarseDuration suppress_for,
      const TriggerEvaluation& evaluation) {
    const auto start = MonoTime::Now();
    size_t thread_count = 0;
    size_t group_count = 0;
    const char* outcome = "completed";
    auto log_completion = ScopeExit([&] {
      LOG(WARNING) << "RPC queue stack dump " << dump_id << " " << outcome << " in "
                   << (MonoTime::Now() - start).ToMilliseconds() << " ms; threads=" << thread_count
                   << ", groups=" << group_count << ".";
    });
    if (DumpCancelled()) {
      outcome = "cancelled";
      return false;
    }

    std::vector<std::string> service_summaries;
    service_summaries.reserve(std::min(evaluation.affected_services.size(), kMaxServicesInSummary));
    for (size_t i = 0;
         i != std::min(evaluation.affected_services.size(), kMaxServicesInSummary); ++i) {
      const auto& service = evaluation.affected_services[i];
      service_summaries.push_back(Format(
          "$0/$1 depth=$2 duration_ms=$3$4", service.messenger_name, service.service_name,
          service.depth, ToMilliseconds(service.above_threshold_for),
          service.triggers_dump ? " (triggered)" : ""));
    }
    const auto omitted = evaluation.affected_services.size() - service_summaries.size();
    LOG(WARNING) << "RPC queue stack dump " << dump_id << ": "
                 << evaluation.affected_services.size()
                 << " service queue(s) at or above threshold " << threshold << " ["
                 << JoinStrings(service_summaries, ", ") << "]"
                 << (omitted ? Format("; $0 additional service(s) omitted", omitted) : "")
                 << ". Dumping thread stacks. Suppressing further dumps for "
                 << ToMilliseconds(suppress_for) << " ms.";

    auto all_threads = ListThreadsForStackTrace();
    std::vector<ThreadIdAndName> threads;
    threads.reserve(all_threads.size());
    for (auto& thread : all_threads) {
      if (IsRelevantThreadCategory(thread.category, evaluation)) {
        threads.push_back(std::move(thread));
      }
    }
    if (threads.empty() && !all_threads.empty()) {
      LOG(WARNING) << "RPC queue stack dump " << dump_id
                   << ": no matching RPC worker thread categories; falling back to all managed "
                      "threads.";
      threads = std::move(all_threads);
    }
    thread_count = threads.size();
    if (threads.empty() || DumpCancelled()) {
      outcome = DumpCancelled() ? "cancelled" : "completed";
      return !DumpCancelled();
    }

    std::sort(threads.begin(), threads.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.tid_for_stack < rhs.tid_for_stack;
    });
    std::vector<ThreadIdForStack> tids;
    tids.reserve(threads.size());
    for (const auto& thread : threads) {
      tids.push_back(thread.tid_for_stack);
    }
    if (DumpCancelled()) {
      outcome = "cancelled";
      return false;
    }
    auto stacks = ThreadStacks(tids);
    if (DumpCancelled()) {
      outcome = "cancelled";
      return false;
    }
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
    group_count = groups.size();
    size_t group_index = 0;
    for (const auto& [_, group] : groups) {
      if (DumpCancelled()) {
        outcome = "cancelled";
        return false;
      }
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
      LOG(WARNING) << "RPC queue stack dump " << dump_id << ", group " << ++group_index << "/"
                   << groups.size() << ": " << group.thread_indexes.size()
                   << " thread(s) with stack [" << JoinStrings(names, ", ") << "]:\n"
                   << (stack.ok() ? stack->Symbolize()
                                  : "Failed to collect stack: " + stack.status().ToString());
    }
    return true;
  }

  std::mutex mutex_;
  std::condition_variable cond_;
  ThreadPtr thread_ GUARDED_BY(mutex_);
  std::atomic<bool> stop_{false};
  std::atomic<bool> cancel_dump_{false};
  // Guarded by mutex_. Not annotated because Execute accesses it under std::unique_lock, which
  // the thread safety analysis does not track.
  std::vector<TrackedService> services_;
  std::vector<uint64_t> active_dump_owner_ids_;

  // Poll state below is only accessed by the monitor thread.
  int64_t last_threshold_ = 0;
  std::optional<CoarseBackoffWaiter> backoff_;
  CoarseTimePoint next_dump_time_;
  uint64_t next_dump_id_ = 1;
};

std::shared_ptr<ServiceQueueMonitor::Impl> ServiceQueueMonitor::SharedImpl() {
  static std::mutex mutex;
  static std::weak_ptr<Impl> weak_impl;
  std::lock_guard lock(mutex);
  auto result = weak_impl.lock();
  if (!result) {
    result = std::make_shared<Impl>();
    weak_impl = result;
  }
  return result;
}

ServiceQueueMonitor::ServiceQueueMonitor(const std::string& name)
    : name_(name),
      impl_(SharedImpl()),
      owner_id_(next_monitor_owner_id.fetch_add(1, std::memory_order_relaxed)) {}

ServiceQueueMonitor::~ServiceQueueMonitor() {
  Shutdown();
}

void ServiceQueueMonitor::Shutdown() {
  if (impl_) {
    impl_->Untrack(owner_id_);
    impl_.reset();
  }
}

void ServiceQueueMonitor::Track(
    const std::string& service_name, const RpcServicePtr& service, ServicePriority priority) {
  if (impl_) {
    impl_->Track(owner_id_, name_, service_name, service, priority);
  }
}

}  // namespace yb::rpc
