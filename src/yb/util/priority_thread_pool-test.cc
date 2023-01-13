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

#include <gtest/gtest.h>

#include "yb/util/metrics.h"
#include "yb/util/priority_thread_pool.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tostring.h"

using namespace std::literals;

namespace yb {

// In our jenkins environment test macs switch threads rare, so have to use higher step time.
#if defined(__APPLE__)
const auto kStepTime = 100ms;
#else
const auto kStepTime = 25ms;
#endif
const auto kWaitTime = kStepTime * 3;
const int kNoDrive = 0;
const int kDrive1 = 1;
const int kDrive2 = 2;
const int kTopDiskCompactionPriority = 100;
const int kTopDiskFlushPriority = 200;

class Share {
 public:
  bool Step(int index) {
    running_.fetch_or(1ULL << index, std::memory_order_acq_rel);
    return (stop_.load(std::memory_order_acquire) & (1ULL << index)) == 0;
  }

  void Stop(int index) {
    stop_.fetch_or(1ULL << index, std::memory_order_acq_rel);
  }

  void StopAll() {
    stop_.store(std::numeric_limits<uint64_t>::max(), std::memory_order_release);
  }

  void ResetRunning() {
    running_.store(0, std::memory_order_release);
  }

  uint64_t running() {
    return running_.load(std::memory_order_acquire);
  }

  // Fill `out` with priorities of running tasks.
  // `divisor` is used to convert index to priority.
  void FillRunningTaskPriorities(std::vector<int>* out, int divisor = 1) {
    std::this_thread::sleep_for(kWaitTime);
    ResetRunning();
    std::this_thread::sleep_for(kWaitTime);
    auto running_mask = running();
    out->clear();
    int i = 0;
    while (running_mask != 0) {
      if (running_mask & 1) {
        out->push_back(i / divisor);
      }
      running_mask >>= 1;
      ++i;
    }
  }

 private:
  // ith bit is set to 1 when task i was reported as running after last reset.
  std::atomic<uint64_t> running_{0};
  // ith bit is set to 1 when we should stop task i.
  std::atomic<uint64_t> stop_{0};
};

struct TestTaskMetrics {
  std::atomic<uint64_t> queued_added;
  std::atomic<uint64_t> queued_removed;
  std::atomic<uint64_t> paused_added;
  std::atomic<uint64_t> paused_removed;
  std::atomic<uint64_t> active_added;
  std::atomic<uint64_t> active_removed;
};

class Task : public PriorityThreadPoolTask {
 public:
  Task(int index, Share* share, uint64_t drive, bool pause = true, bool compaction = false,
      TestTaskMetrics* metrics = nullptr)
      : index_(index),
        share_(share),
        pause_(pause),
        compaction_(compaction),
        metrics_(metrics) {
  }

  void Run(const Status& status, PriorityThreadPoolSuspender* suspender) override {
    if (!status.ok()) {
      return;
    }
    while (share_->Step(index_)) {
      if (pause_) {
        suspender->PauseIfNecessary();
      }
      std::this_thread::sleep_for(kStepTime);
    }
  }

  bool ShouldRemoveWithKey(void* key) override {
    return false;
  }

  int Index() const {
    return index_;
  }

  std::string ToString() const override {
    return YB_CLASS_TO_STRING(index);
  }

  void UpdateStatsStateChangedTo(PriorityThreadPoolTaskState state) override {
    if (!metrics_) {
      return;
    }
    switch (state) {
      case yb::PriorityThreadPoolTaskState::kNotStarted:
        metrics_->queued_added++;
        return;
      case yb::PriorityThreadPoolTaskState::kPaused:
        metrics_->paused_added++;
        return;
      case yb::PriorityThreadPoolTaskState::kRunning:
        metrics_->active_added++;
        return;
    }
    FATAL_INVALID_ENUM_VALUE(yb::PriorityThreadPoolTaskState, state);
  }

  void UpdateStatsStateChangedFrom(PriorityThreadPoolTaskState state) override {
    if (!metrics_) {
      return;
    }
    switch (state) {
      case yb::PriorityThreadPoolTaskState::kNotStarted:
        metrics_->queued_removed++;
        return;
      case yb::PriorityThreadPoolTaskState::kPaused:
        metrics_->paused_removed++;
        return;
      case yb::PriorityThreadPoolTaskState::kRunning:
        metrics_->active_removed++;
        return;
    }
    FATAL_INVALID_ENUM_VALUE(yb::PriorityThreadPoolTaskState, state);
  }

  int CalculateGroupNoPriority(int active_tasks) const override {
    return compaction_
        ? kTopDiskCompactionPriority - active_tasks
        : kTopDiskFlushPriority - active_tasks;
  }

 private:
  const int index_;
  Share* const share_;
  const bool pause_;
  const bool compaction_;
  TestTaskMetrics* metrics_;
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
    tasks.emplace_back(std::make_unique<Task>(i, &share, kNoDrive));
  }
  std::shuffle(tasks.begin(), tasks.end(), ThreadLocalRandom());
  std::set<int> scheduled;
  size_t schedule_idx = 0;
  std::set<int> stopped;
  std::vector<int> running_vector;
  std::vector<int> expected_running;

  auto se = ScopeExit([&share, &thread_pool] {
    thread_pool.StartShutdown();
    share.StopAll();
    thread_pool.CompleteShutdown();
  });

  while (stopped.size() != kTasks) {
    if (schedule_idx < kTasks && RandomUniformInt<size_t>(0, 2 + scheduled.size()) == 0) {
      auto& task = tasks[schedule_idx];
      auto index = task->Index();
      scheduled.insert(index);
      ++schedule_idx;
      auto priority = task->Index() / divisor;
      ASSERT_OK(thread_pool.Submit(priority, &task));
      ASSERT_TRUE(task == nullptr);
      LOG(INFO) << "Submitted: " << index << ", scheduled: " << yb::ToString(scheduled);
    } else if (!scheduled.empty() &&
               RandomUniformInt<size_t>(0, std::max<size_t>(0, 13 - scheduled.size())) == 0) {
      auto it = scheduled.end();
      std::advance(
          it, -RandomUniformInt<ssize_t>(1, std::min<ssize_t>(scheduled.size(), kMaxRunningTasks)));
      auto idx = *it;
      stopped.insert(idx);
      share.Stop(idx);
      scheduled.erase(it);
      LOG(INFO) << "Stopped: " << idx << ", scheduled: " << yb::ToString(scheduled);
    }
    share.FillRunningTaskPriorities(&running_vector, divisor);
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
  RandomTask() = default;

  void Run(const Status& status, PriorityThreadPoolSuspender* suspender) override {
    if (!status.ok()) {
      return;
    }
    std::this_thread::sleep_for(1ms * RandomUniformInt(1, kMaxRandomTaskTimeMs));
  }

  // Returns true if the task belongs to specified key, which was passed to
  // PriorityThreadPool::Remove and should be removed.
  bool ShouldRemoveWithKey(void* key) override {
    return false;
  }

  std::string ToString() const override {
    return "RandomTask";
  }

  int CalculateGroupNoPriority(int active_tasks) const override {
    return kTopDiskFlushPriority - active_tasks;
  }
};

TEST(PriorityThreadPoolTest, RandomTasks) {
  constexpr int kMaxRunningTasks = 10;
  PriorityThreadPool thread_pool(kMaxRunningTasks);
  TestThreadHolder holder;
  for (int i = 0; i != kMaxRunningTasks; ++i) {
    holder.AddThread([&stop = holder.stop_flag(), &thread_pool] {
      while (!stop.load()) {
        auto priority = RandomUniformInt(0, 4);
        auto temp_task = std::make_unique<RandomTask>();
        ASSERT_OK(thread_pool.Submit(priority, &temp_task));
        ASSERT_TRUE(temp_task == nullptr);
        // Submit tasks slightly slower than they complete.
        // To frequently get case of empty thread pool.
        std::this_thread::sleep_for(1ms * RandomUniformInt(1, kMaxRandomTaskTimeMs * 5 / 4));
      }
    });
  }
  holder.WaitAndStop(60s);
  thread_pool.Shutdown();
}

namespace {

size_t SubmitTask(
    int index, Share* share, PriorityThreadPool* thread_pool, int drive = kNoDrive,
    bool pause = true, bool compaction = false, TestTaskMetrics* metrics = nullptr) {
  auto task = std::make_unique<Task>(index, share, drive, pause, compaction, metrics);
  size_t serial_no = task->SerialNo();
  EXPECT_OK(thread_pool->Submit(index /* priority */, &task, drive));
  EXPECT_TRUE(task == nullptr);
  LOG(INFO) << "Started " << index << ", serial no: " << serial_no;
  return serial_no;
}

} // namespace

TEST(PriorityThreadPoolTest, ChangePriority) {
  const int kMaxRunningTasks = 3;
  PriorityThreadPool thread_pool(kMaxRunningTasks);
  Share share;
  std::vector<int> running;

  auto se = ScopeExit([&share, &thread_pool] {
    thread_pool.StartShutdown();
    share.StopAll();
    thread_pool.CompleteShutdown();
  });

  auto task5 = SubmitTask(5, &share, &thread_pool);
  SubmitTask(6, &share, &thread_pool);
  SubmitTask(7, &share, &thread_pool);
  auto task1 = SubmitTask(1, &share, &thread_pool);
  auto task2 = SubmitTask(2, &share, &thread_pool);
  SubmitTask(3, &share, &thread_pool);

  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({5, 6, 7}));

  // Check that we could pause running task when priority increased.
  ASSERT_TRUE(thread_pool.ChangeTaskPriority(task1, 8));
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({1, 6, 7}));

  // Check that priority of queued task could be updated.
  ASSERT_TRUE(thread_pool.ChangeTaskPriority(task2, 4));
  share.Stop(1);
  share.Stop(7);

  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({2, 5, 6}));

  // Check decrease of priority.
  ASSERT_TRUE(thread_pool.ChangeTaskPriority(task5, 1));
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({2, 3, 6}));

  // Check same priority.
  ASSERT_TRUE(thread_pool.ChangeTaskPriority(task5, 6));
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({2, 5, 6}));
}

// Verify that the queued, paused, and active task metrics are updated as expected.
TEST(PriorityThreadPoolTest, TaskMetrics) {

  const int kMaxRunningTasks = 3;
  auto metrics = TestTaskMetrics();

  PriorityThreadPool thread_pool(kMaxRunningTasks);

  Share share;
  std::vector<int> running;

  auto se = ScopeExit([&share, &thread_pool] {
    thread_pool.StartShutdown();
    share.StopAll();
    thread_pool.CompleteShutdown();
  });

  // Add 4 tasks. First 3 run, 4th is in queue.
  SubmitTask(6, &share, &thread_pool, kNoDrive, /* pause */ true,
      /* compaction */ false, &metrics);
  SubmitTask(5, &share, &thread_pool, kNoDrive, /* pause */ true,
      /* compaction */ false, &metrics);
  SubmitTask(4, &share, &thread_pool, kNoDrive, /* pause */ true,
      /* compaction */ false, &metrics);
  SubmitTask(3, &share, &thread_pool, kNoDrive, /* pause */ true,
      /* compaction */ false, &metrics);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(metrics.active_added.load(), 3);
  ASSERT_EQ(metrics.active_removed.load(), 0);
  ASSERT_EQ(metrics.queued_added.load(), 1);
  ASSERT_EQ(metrics.queued_removed.load(), 0);
  ASSERT_EQ(metrics.paused_added.load(), 0);
  ASSERT_EQ(metrics.paused_removed.load(), 0);

  // Add a higher priority task + a lower priority task.
  // Task 4 should pause, lower task is queued.
  SubmitTask(7, &share, &thread_pool, kNoDrive, /* pause */ true,
      /* compaction */ false, &metrics);
  SubmitTask(2, &share, &thread_pool, kNoDrive, /* pause */ true,
      /* compaction */ false, &metrics);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(metrics.active_added.load(), 4);
  ASSERT_EQ(metrics.active_removed.load(), 1);
  ASSERT_EQ(metrics.queued_added.load(), 3);
  ASSERT_EQ(metrics.queued_removed.load(), 1);
  ASSERT_EQ(metrics.paused_added.load(), 1);
  ASSERT_EQ(metrics.paused_removed.load(), 0);

  // Finish one of the running tasks.
  // This should take the previously paused task and make it active.
  share.Stop(7);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(metrics.active_added.load(), 5);
  ASSERT_EQ(metrics.active_removed.load(), 2);
  ASSERT_EQ(metrics.queued_added.load(), 3);
  ASSERT_EQ(metrics.queued_removed.load(), 1);
  ASSERT_EQ(metrics.paused_added.load(), 1);
  ASSERT_EQ(metrics.paused_removed.load(), 1);

  // After shutdown, the number of tasks added and removed from each catagory should be equivalent.
  thread_pool.StartShutdown();
  share.StopAll();
  thread_pool.CompleteShutdown();
  ASSERT_EQ(metrics.active_added.load(), metrics.active_removed.load());
  ASSERT_EQ(metrics.queued_added.load(), metrics.queued_removed.load());
  ASSERT_EQ(metrics.paused_added.load(), metrics.paused_removed.load());
}

TEST(PriorityThreadPoolTest, GroupNoActiveIORebalancing) {
  const int kMaxRunningTasks = 5;
  PriorityThreadPool thread_pool(kMaxRunningTasks, true /* prioritize_by_group_no */);
  Share share;
  std::vector<int> running;

  auto se = ScopeExit([&share, &thread_pool] {
    thread_pool.StartShutdown();
    share.StopAll();
    thread_pool.CompleteShutdown();
  });
  // Intially the integer passed into SubmitTask becomes the priority.
  // This number also serves as an index.
  // All submitted tasks in this set, have pause_ set to true, and have compaction_ set to true
  // indicating pausing is enabled, and they are simulating compaction tasks.
  SubmitTask(
    10 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, true /* compaction */);
  SubmitTask(
    9 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, true /* compaction */);
  SubmitTask(
    8 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, true /* compaction */);
  SubmitTask(
    7 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, true /* compaction */);
  SubmitTask(
    6 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, true /* compaction */);
  SubmitTask(
    3 /* priority */, &share, &thread_pool, kDrive2, true /* pause */, true /* compaction */);
  SubmitTask(
    2 /* priority */, &share, &thread_pool, kDrive2, true /* pause */, true /* compaction */);

  // We are submitting 9 tasks that belong to different drives.
  // Since the unit tests check for pausing repeatedly, we should see
  // the running tasks include the 2 tasks belonging to Drive2,
  // even though they were submitted later, with a lower size priority.
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({2, 3, 8, 9, 10}));
}

TEST(PriorityThreadPoolTest, GroupNoPicksTaskOnLessBusyDrive) {
  const int kMaxRunningTasks = 2;
  PriorityThreadPool thread_pool(kMaxRunningTasks, true /* prioritize_by_group_no */);
  Share share;
  std::vector<int> running;

  auto se = ScopeExit([&share, &thread_pool] {
    thread_pool.StartShutdown();
    share.StopAll();
    thread_pool.CompleteShutdown();
  });
  // Intially the integer passed into SubmitTask becomes the priority.
  // This number also serves as an index.
  // All submitted tasks in this set, have pause_ set to false, and have compaction_ set to true
  // indicating pausing is disabled, and they are simulating compaction tasks.
  SubmitTask(
    10 /* priority */, &share, &thread_pool, kDrive1, false /* pause */, true /* compaction */);
  SubmitTask(
    9 /* priority */, &share, &thread_pool, kDrive1, false /* pause */, true /* compaction */);
  SubmitTask(
    8 /* priority */, &share, &thread_pool, kDrive1, false /* pause */, true /* compaction */);
  SubmitTask(
    7 /* priority */, &share, &thread_pool, kDrive2, false /* pause */, true /* compaction */);
  SubmitTask(
    6 /* priority */, &share, &thread_pool, kDrive2, false /* pause */, true /* compaction */);

  // Since we turned the ability to pause off,
  // we should see tasks 10 and 9 running, as they were submitted first.
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({9, 10}));
  share.Stop(10);

  // Although task 8 has a higher size priority than task 7, since task 7 is alone on disk 2,
  // it should run first.
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({7, 9}));
}

TEST(PriorityThreadPoolTest, GroupNoOtherTasksRunBeforeCompactionTasks) {
  const int kMaxRunningTasks = 2;
  PriorityThreadPool thread_pool(kMaxRunningTasks, true /* prioritize_by_group_no */ );
  Share share;
  std::vector<int> running;

  auto se = ScopeExit([&share, &thread_pool] {
    thread_pool.StartShutdown();
    share.StopAll();
    thread_pool.CompleteShutdown();
  });

  // For this test, note that tasks 10 and 9 generates a dummy compaction task
  // while the other tasks represent other dummy tasks (in particular flushes)
  SubmitTask(
    10 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, true /* compaction */);
  SubmitTask(
    9 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, true /* compaction */);
  SubmitTask(
    7 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, false /* compaction */);
  SubmitTask(
    6 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, false /* compaction */);
  SubmitTask(
    5 /* priority */, &share, &thread_pool, kDrive1, true /* pause */, false /* compaction */);

  // Although tasks 10 and 9 have a higher size priority, the other_task's
  // should run first as they always have a higher disk priority
  // Since pausing is enabled, even though they were submitted later, they should run first.
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({6, 7}));

  share.Stop(7);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({5, 6}));

  share.Stop(5);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({6, 10}));
}

TEST(PriorityThreadPoolTest, FailureToCreateThread) {
  const int kMaxRunningTasks = 3;
  PriorityThreadPool thread_pool(kMaxRunningTasks);
  Share share;
  std::vector<int> running;

  auto se = ScopeExit([&share, &thread_pool] {
    LOG(INFO) << "Final state of the pool: " << thread_pool.StateToString();
    thread_pool.StartShutdown();
    share.StopAll();
    thread_pool.CompleteShutdown();
  });

  SubmitTask(5, &share, &thread_pool);
  SubmitTask(6, &share, &thread_pool);

  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({5, 6}));

  LOG(INFO) << "DISABLING CREATION OF NEW THREADS";

  // We fail to launch a new thread and just add the task to the list of waiting tasks.
  thread_pool.TEST_SetThreadCreationFailureProbability(1);

  // We cannot launch new threads now so we just add tasks as "not started" and the same two tasks
  // keep running.
  //
  // Prior to the #8348 fix, these failures also incorrectly increause the paused_workers_ counter
  // each time, which then causes an underflow in PickWorker and we would not be able to start
  // any new tasks at all.
  SubmitTask(7, &share, &thread_pool);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({5, 6}));

  SubmitTask(8, &share, &thread_pool);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({5, 6}));

  SubmitTask(9, &share, &thread_pool);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({5, 6}));

  LOG(INFO) << "ALLOWING CREATION OF NEW THREADS";

  // Now allow adding new threads and launch more tasks. Tasks 5 and 6 should immediately get
  // paused and replaced with 8 and 9, but because we pause and launch exactly one task, 7 does
  // not actually get started, even though we have enough threads and enough quota for it.
  // We might consider changing this in the future.
  thread_pool.TEST_SetThreadCreationFailureProbability(0);
  std::this_thread::sleep_for(kWaitTime);

  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({8, 9}));

  SubmitTask(10, &share, &thread_pool);
  share.FillRunningTaskPriorities(&running);
  ASSERT_EQ(running, std::vector<int>({8, 9, 10}));
}

} // namespace yb
