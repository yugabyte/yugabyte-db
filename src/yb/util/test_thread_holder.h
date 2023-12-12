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

#include <thread>
#include <cds/init.h>

#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

namespace yb {

class SetFlagOnExit {
 public:
  explicit SetFlagOnExit(std::atomic<bool>* stop_flag)
      : stop_flag_(stop_flag) {}

  ~SetFlagOnExit() {
    stop_flag_->store(true, std::memory_order_release);
  }

 private:
  std::atomic<bool>* stop_flag_;
};

// Waits specified duration or when stop switches to true.
void WaitStopped(const CoarseDuration& duration, std::atomic<bool>* stop);

// Holds vector of threads, and provides convenient utilities. Such as JoinAll, Wait etc.
class TestThreadHolder {
 public:
  TestThreadHolder() { cds::Initialize(); }

  ~TestThreadHolder() {
    stop_flag_.store(true, std::memory_order_release);
    JoinAll();
    cds::Terminate();
  }

  template <class... Args>
  void AddThread(Args&&... args) {
    threads_.emplace_back(std::forward<Args>(args)...);
  }

  void AddThread(std::thread thread) {
    threads_.push_back(std::move(thread));
  }

  template <class Functor>
  void AddThreadFunctor(const Functor& functor) {
    AddThread([&stop = stop_flag_, functor] {
      CDSAttacher attacher;
      SetFlagOnExit set_stop_on_exit(&stop);
      functor();
    });
  }

  void Wait(const CoarseDuration& duration) {
    WaitStopped(duration, &stop_flag_);
  }

  void JoinAll();

  template <class Cond>
  Status WaitCondition(const Cond& cond) {
    while (!cond()) {
      if (stop_flag_.load(std::memory_order_acquire)) {
        return STATUS(Aborted, "Wait aborted");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return Status::OK();
  }

  void WaitAndStop(const CoarseDuration& duration) {
    yb::WaitStopped(duration, &stop_flag_);
    Stop();
  }

  void Stop() {
    stop_flag_.store(true, std::memory_order_release);
    JoinAll();
  }

  std::atomic<bool>& stop_flag() {
    return stop_flag_;
  }

 private:
  std::atomic<bool> stop_flag_{false};
  std::vector<std::thread> threads_;
};

}  // namespace yb
