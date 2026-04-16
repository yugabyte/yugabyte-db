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

#include <chrono>

#include "yb/rpc/reactor_monitor.h"

#include "yb/rpc/reactor.h"

#include "yb/util/thread.h"

using namespace std::literals;

namespace yb::rpc {

class ReactorMonitor::Impl {
 public:
  Impl(const std::string& name, MonoDelta timeout) : name_(name), timeout_(timeout) {}

  void Shutdown() {
    ThreadPtr thread;
    {
      std::lock_guard lock(mutex_);
      if (!thread_) {
        return;
      }
      stop_ = true;
      cond_.notify_one();
      thread = thread_;
    }
    thread->Join();
  }

  void Track(Reactor& reactor) {
    std::lock_guard lock(mutex_);
    reactors_.emplace_back(&reactor, 0);
    if (!thread_) {
      thread_ = CHECK_RESULT(Thread::Make(
          name_ + "_reactor", name_ + "_reactor_monitor", [this] { Execute(); }));
    }
  }
 private:
  void Execute() {
    std::unique_lock lock(mutex_);
    auto prev_time = CoarseMonoClock::now();
    while (!stop_.load()) {
      auto current_time = CoarseMonoClock::now();
      MonoDelta passed(current_time - prev_time);
      if (passed >= timeout_) {
        for (auto& [reactor, tick] : reactors_) {
          auto new_tick = reactor->tick();
          if (tick == new_tick && (new_tick & 1) == 1) {
            auto tid = reactor->tid_for_stack();
            auto stack_trace = ThreadStack(tid);
            if (stack_trace.ok()) {
              LOG(WARNING) << "Reactor functor executed longer than " << passed << " (" << tid
                           << "):\n" << stack_trace->Symbolize();
            }
          } else {
            tick = new_tick;
          }
        }
      }
      cond_.wait_for(lock, timeout_.ToSteadyDuration());
    }
  }

  const std::string name_;
  const MonoDelta timeout_;
  std::mutex mutex_;
  std::condition_variable cond_;
  ThreadPtr thread_;
  std::atomic<bool> stop_{false};
  std::vector<std::pair<Reactor*, size_t>> reactors_;
};

ReactorMonitor::ReactorMonitor(const std::string& name, MonoDelta timeout)
    : impl_(std::make_unique<Impl>(name, timeout)) {}

ReactorMonitor::~ReactorMonitor() = default;

void ReactorMonitor::Shutdown() {
  impl_->Shutdown();
}

void ReactorMonitor::Track(Reactor& reactor) {
  impl_->Track(reactor);
}

}  // namespace yb::rpc
