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

// This was previously called test_thread_holder.h. The thread holder facility is useful for
// tools and possibly production code too in some cases, so it's been moved out of the test library.

#pragma once

#include <concepts>
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
class ThreadHolder {
 public:
  // verbose means we will log messages when joining threads, etc. It is useful in a unit test but
  // less useful in a command-line tool.
  explicit ThreadHolder(bool verbose = true) : verbose_(verbose) { cds::Initialize(); }

  ~ThreadHolder() {
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
  bool verbose_;
};

template<typename Functor, typename IntegralCounter>
concept StatusFunctorTypeAcceptingIntegral = requires(Functor f, IntegralCounter i) {
  { f(i) } -> std::same_as<Status>;
};  // NOLINT

// Processes a collection of items in multiple threads. The nature of items does not matter. The
// processing function only receives an index index of an item. If processing of any item fails, all
// threads stop. If only one thread is requested, the processing function is invoked directly in
// the current thread.
template<std::integral IntegralCounter, StatusFunctorTypeAcceptingIntegral<IntegralCounter> Functor>
Status ProcessInParallel(
    size_t num_threads,
    IntegralCounter start_index,
    IntegralCounter end_index_exclusive,
    Functor process_element) {
  SCHECK_GE(num_threads, static_cast<size_t>(1), InvalidArgument,
            "At least one thread is required");

  if (num_threads == 1) {
    for (auto item_index = start_index; item_index < end_index_exclusive; ++item_index) {
      RETURN_NOT_OK(process_element(item_index));
    }
    return Status::OK();
  }

  Status overall_status;
  std::mutex overall_status_mutex;

  ThreadHolder thread_holder(/* verbose= */ false);
  std::atomic<IntegralCounter> atomic_item_index{start_index};
  for (size_t thread_index = 0; thread_index < num_threads; ++thread_index) {
    thread_holder.AddThreadFunctor(
        [&thread_holder, end_index_exclusive, &overall_status, &overall_status_mutex,
         &process_element, &atomic_item_index]() {
          while (!thread_holder.stop_flag()) {
            auto item_index = atomic_item_index.fetch_add(1, std::memory_order_acq_rel);
            if (item_index >= end_index_exclusive) {
              break;
            }
            Status s = process_element(item_index);
            if (!s.ok()) {
              std::lock_guard status_lock(overall_status_mutex);
              overall_status = s;
              thread_holder.stop_flag().store(true);  // Can't call Stop here, it calls JoinAll.
              break;
            }
          }
        });
  }
  thread_holder.JoinAll();
  return overall_status;
}

}  // namespace yb
