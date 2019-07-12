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

#include <algorithm>
#include <thread>

#include <boost/scope_exit.hpp>

#include <gtest/gtest.h>

#include "yb/util/priority_thread_pool.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {

const auto kStepTime = 25ms;
const auto kWaitTime = kStepTime * 3;

class Share {
 public:
  bool Step(int index) {
    running_.fetch_or(1ULL << index, std::memory_order_acq_rel);
    return (stop_.load(std::memory_order_acquire) & (1ULL << index)) == 0;
  }

  void Stop(int index) {
    stop_.fetch_or(1ULL << index, std::memory_order_acq_rel);
  }

  void ResetRunning() {
    running_.store(0, std::memory_order_release);
  }

  uint64_t running() {
    return running_.load(std::memory_order_acquire);
  }

 private:
  std::atomic<uint64_t> running_{0};
  std::atomic<uint64_t> stop_{0};
};

class Task : public PriorityThreadPoolTask {
 public:
  explicit Task(int index, int priority, Share* share)
      : index_(index), priority_(priority), share_(share) {}

  void Run(const Status& status, PriorityThreadPoolSuspender* suspender) override {
    if (!status.ok()) {
      return;
    }
    while (share_->Step(index_)) {
      suspender->PauseIfNecessary();
      std::this_thread::sleep_for(kStepTime);
    }
  }

  bool BelongsTo(void* key) override {
    return false;
  }

  int Index() const {
    return index_;
  }

  // Priority of this task.
  int Priority() const override {
    return priority_;
  }

  void AddToStringFields(std::string* out) const override {
    *out += Format("index: $0 ", index_);
  }

 private:
  const int index_;
  const int priority_;
  Share* const share_;
};

// Test randomly submits and stops task to priority thread pool.
// Checking that only expected tasks are running.
// Task priority is calculated as index/divisor.
// So with divisor == 1 we get unique priorities, and with divisor > 1 multiple tasks could have
// the same priority.
void TestRandom(int divisor) {
  const int kTasks = 20;
  const int kMaxRunningTasks = 3;
  PriorityThreadPool thread_pool(kMaxRunningTasks);
  std::vector<std::unique_ptr<Task>> tasks;
  tasks.reserve(kTasks);
  Share share;
  for (int i = 0; i != kTasks; ++i) {
    tasks.emplace_back(std::make_unique<Task>(i, i / divisor, &share));
  }
  std::shuffle(tasks.begin(), tasks.end(), ThreadLocalRandom());
  std::set<int> scheduled;
  size_t schedule_idx = 0;
  std::set<int> stopped;
  std::vector<int> running_vector;
  std::vector<int> expected_running;

  BOOST_SCOPE_EXIT(&share, scheduled, &thread_pool) {
    thread_pool.StartShutdown();
    for (int idx = 0; idx != kTasks; ++idx) {
      share.Stop(idx);
    }
    thread_pool.CompleteShutdown();
  } BOOST_SCOPE_EXIT_END;

  while (stopped.size() != kTasks) {
    if (schedule_idx < kTasks && RandomUniformInt<int>(0, 2 + scheduled.size()) == 0) {
      auto& task = tasks[schedule_idx];
      auto index = task->Index();
      scheduled.insert(index);
      ++schedule_idx;
      auto submit_result = thread_pool.Submit(std::move(task));
      ASSERT_TRUE(submit_result == nullptr);
      LOG(INFO) << "Submitted: " << index << ", scheduled: " << yb::ToString(scheduled);
    } else if (!scheduled.empty() &&
               RandomUniformInt<int>(0, std::max<int>(0, 13 - scheduled.size())) == 0) {
      auto it = scheduled.end();
      std::advance(
          it, -RandomUniformInt<int>(1, std::min<int>(scheduled.size(), kMaxRunningTasks)));
      auto idx = *it;
      stopped.insert(idx);
      share.Stop(idx);
      scheduled.erase(it);
      LOG(INFO) << "Stopped: " << idx << ", scheduled: " << yb::ToString(scheduled);
    }
    std::this_thread::sleep_for(kWaitTime);
    share.ResetRunning();
    std::this_thread::sleep_for(kWaitTime);
    auto running = share.running();
    running_vector.clear();
    for (int i = 0; i != kTasks; ++i) {
      if (running & (1 << i)) {
        running_vector.push_back(i / divisor);
      }
    }
    expected_running.clear();
    auto it = scheduled.end();
    auto left = kMaxRunningTasks;
    while (it != scheduled.begin() && left > 0) {
      --it;
      expected_running.push_back(*it / divisor);
      --left;
    }
    std::reverse(expected_running.begin(), expected_running.end());
    ASSERT_EQ(expected_running, running_vector) << "Scheduled: " << yb::ToString(scheduled)
                                                << ", running: " << yb::ToString(running_vector)
                                                << ", state: " << thread_pool.StateToString();
  }
}

TEST(PriorityThreadPoolTest, RandomUnique) {
  TestRandom(1 /* divisor */);
}

TEST(PriorityThreadPoolTest, RandomNonUnique) {
  TestRandom(3 /* divisor */);
}

constexpr int kMaxRandomTaskTimeMs = 40;

class RandomTask : public PriorityThreadPoolTask {
 public:
  RandomTask() : priority_(RandomUniformInt(0, 4)) {}

  void Run(const Status& status, PriorityThreadPoolSuspender* suspender) override {
    if (!status.ok()) {
      return;
    }
    std::this_thread::sleep_for(1ms * RandomUniformInt(1, kMaxRandomTaskTimeMs));
  }

  // Returns true if the task belongs to specified key, which was passed to
  // PriorityThreadPool::Remove.
  bool BelongsTo(void* key) override {
    return false;
  }

  // Priority of this task.
  int Priority() const override {
    return priority_;
  }

  void AddToStringFields(std::string* out) const override {
  }

 private:
  int priority_;
};

TEST(PriorityThreadPoolTest, RandomTasks) {
  constexpr int kMaxRunningTasks = 10;
  PriorityThreadPool thread_pool(kMaxRunningTasks);
  TestThreadHolder holder;
  for (int i = 0; i != kMaxRunningTasks; ++i) {
    holder.AddThread([&stop = holder.stop_flag(), &thread_pool] {
      while (!stop.load()) {
        auto task = thread_pool.Submit(std::make_unique<RandomTask>());
        ASSERT_TRUE(task == nullptr);
        // Submit tasks slightly slower than they complete.
        // To frequently get case of empty thread pool.
        std::this_thread::sleep_for(1ms * RandomUniformInt(1, kMaxRandomTaskTimeMs * 5 / 4));
      }
    });
  }
  holder.WaitAndStop(60s);
  thread_pool.Shutdown();
}

} // namespace yb
