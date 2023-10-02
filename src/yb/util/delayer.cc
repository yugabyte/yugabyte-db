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

#include "yb/util/delayer.h"

#include <vector>

#include <cds/init.h>

#include "yb/util/scope_exit.h"
#include "yb/util/thread.h"

namespace yb {

void Delayer::Delay(MonoTime when, std::function<void()> action) {
  std::lock_guard lock(mutex_);
  if (!thread_) {
    CHECK_OK(yb::Thread::Create("delayer", "delay", &Delayer::Execute, this, &thread_));
  }
  queue_.emplace_back(when, std::move(action));
  cond_.notify_one();
}

Delayer::~Delayer() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
    cond_.notify_one();
  }
  if (thread_) {
    thread_->Join();
  }
}

void Delayer::Execute() {
  std::vector<std::function<void()>> actions;
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stop_) {
    if (!queue_.empty()) {
      auto now = MonoTime::Now();
      auto it = queue_.begin();
      while (it != queue_.end() && it->first <= now) {
        actions.push_back(std::move(it->second));
        ++it;
      }
      if (it != queue_.begin()) {
        queue_.erase(queue_.begin(), it);
        lock.unlock();
        auto se = ScopeExit([&lock, &actions] {
          actions.clear();
          lock.lock();
        });
        for (auto& action : actions) {
          action();
        }
      } else {
        cond_.wait_until(lock, queue_.front().first.ToSteadyTimePoint());
      }
    } else {
      cond_.wait(lock);
    }
  }
}

} // namespace yb
