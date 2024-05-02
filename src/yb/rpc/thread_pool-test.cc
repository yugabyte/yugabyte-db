//
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
//

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "yb/rpc/strand.h"
#include "yb/rpc/thread_pool.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/random_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

DECLARE_int32(TEST_strand_done_inject_delay_ms);

using namespace std::literals;

namespace yb {
namespace rpc {

class ThreadPoolTest : public YBTest {
};

enum class TestTaskState {
  IDLE,
  EXECUTED,
  COMPLETED,
  FAILED,
};

class TestTask final : public ThreadPoolTask {
 public:
  TestTask() {}

  ~TestTask() {}

  bool IsCompleted() const {
    return state_ == TestTaskState::COMPLETED;
  }

  bool IsFailed() const {
    return state_ == TestTaskState::FAILED;
  }

  bool IsDone() const {
    return state_ == TestTaskState::COMPLETED || state_ == TestTaskState::FAILED;
  }

  void SetLatch(CountDownLatch* latch) {
    latch_ = latch;
  }

  void SetSuccessCounter(std::atomic<size_t>* success_counter) {
    success_counter_ = success_counter;
  }

  void SetDelayMicros(size_t delay_micros) {
    delay_micros_ = delay_micros;
  }

 private:
  void Run() override {
    if (delay_micros_) {
      std::this_thread::sleep_for(1us * delay_micros_);
    }
    auto expected = TestTaskState::IDLE;
    ASSERT_TRUE(state_.compare_exchange_strong(expected, TestTaskState::EXECUTED));
    ASSERT_EQ(expected, TestTaskState::IDLE);
  }

  void Done(const Status& status) override {
    auto expected = status.ok() ? TestTaskState::EXECUTED : TestTaskState::IDLE;
    const auto target_state = status.ok() ? TestTaskState::COMPLETED : TestTaskState::FAILED;
    ASSERT_TRUE(state_.compare_exchange_strong(expected, target_state));
    if (latch_) {
      latch_->CountDown();
    }
    if (status.ok() && success_counter_) {
      success_counter_->fetch_add(1, std::memory_order_acq_rel);
    }
  }

  CountDownLatch* latch_ = nullptr;
  std::atomic<TestTaskState> state_ = { TestTaskState::IDLE };
  std::atomic<size_t>* success_counter_ = nullptr;
  size_t delay_micros_ = 0;
};

TEST_F(ThreadPoolTest, TestSingleThread) {
  constexpr size_t kTotalTasks = 100;
  constexpr size_t kTotalWorkers = 1;
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kTotalWorkers,
  });

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  for (auto& task : tasks) {
    task.SetLatch(&latch);
    ASSERT_TRUE(pool.Enqueue(&task));
  }
  latch.Wait();
  for (auto& task : tasks) {
    ASSERT_TRUE(task.IsCompleted());
  }
}

TEST_F(ThreadPoolTest, TestSingleProducer) {
  constexpr size_t kTotalTasks = 10000;
  constexpr size_t kTotalWorkers = 4;
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kTotalWorkers,
  });

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  for (auto& task : tasks) {
    task.SetLatch(&latch);
    ASSERT_TRUE(pool.Enqueue(&task));
  }
  latch.Wait();
  for (auto& task : tasks) {
    ASSERT_TRUE(task.IsCompleted());
  }
}

TEST_F(ThreadPoolTest, TestMultiProducers) {
  constexpr size_t kTotalTasks = 10000;
  constexpr size_t kTotalWorkers = 4;
  constexpr size_t kProducers = 4;
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kTotalWorkers,
  });

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  std::vector<std::thread> threads;
  size_t begin = 0;
  for (size_t i = 0; i != kProducers; ++i) {
    size_t end = kTotalTasks * (i + 1) / kProducers;
    threads.emplace_back([&pool, &latch, &tasks, begin, end] {
      CDSAttacher attacher;
      for (size_t i = begin; i != end; ++i) {
        tasks[i].SetLatch(&latch);
        ASSERT_TRUE(pool.Enqueue(&tasks[i]));
      }
    });
    begin = end;
  }
  latch.Wait();
  for (auto& task : tasks) {
    ASSERT_TRUE(task.IsCompleted());
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(ThreadPoolTest, TestQueueOverflow) {
  constexpr size_t kTotalTasks = 10000;
  constexpr size_t kTotalWorkers = 4;
  constexpr size_t kProducers = 4;
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kTotalWorkers,
  });

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  std::vector<std::thread> threads;
  size_t begin = 0;
  std::atomic<size_t> enqueue_failed(0);
  for (size_t i = 0; i != kProducers; ++i) {
    size_t end = kTotalTasks * (i + 1) / kProducers;
    threads.emplace_back([&pool, &latch, &tasks, &enqueue_failed, begin, end] {
      CDSAttacher attacher;
      for (size_t i = begin; i != end; ++i) {
        tasks[i].SetLatch(&latch);
        if(!pool.Enqueue(&tasks[i])) {
          ++enqueue_failed;
        }
      }
    });
    begin = end;
  }
  latch.Wait();
  size_t failed = 0;
  for (auto& task : tasks) {
    if(!task.IsCompleted()) {
      ASSERT_TRUE(task.IsFailed());
      ++failed;
    }
  }
  ASSERT_EQ(enqueue_failed, failed);
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(ThreadPoolTest, TestShutdown) {
  constexpr size_t kTotalTasks = 10000;
  constexpr size_t kTotalWorkers = 4;
  constexpr size_t kProducers = 4;
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kTotalWorkers,
  });

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  std::vector<std::thread> threads;
  size_t begin = 0;
  for (size_t i = 0; i != kProducers; ++i) {
    size_t end = kTotalTasks * (i + 1) / kProducers;
    threads.emplace_back([&pool, &latch, &tasks, begin, end] {
      CDSAttacher attacher;
      for (size_t i = begin; i != end; ++i) {
        tasks[i].SetLatch(&latch);
        pool.Enqueue(&tasks[i]);
      }
    });
    begin = end;
  }
  pool.Shutdown();
  latch.Wait();
  for (auto& task : tasks) {
    ASSERT_TRUE(task.IsDone());
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(ThreadPoolTest, TestOwns) {
  class TestTask : public ThreadPoolTask {
   public:
    explicit TestTask(ThreadPool* thread_pool) : thread_pool_(thread_pool) {}

    void Run() {
      thread_ = Thread::current_thread();
      ASSERT_TRUE(thread_pool_->OwnsThisThread());
    }

    void Done(const Status& status) {
      latch_.CountDown();
    }

    Thread* thread() {
      return thread_;
    }

    void Wait() {
      return latch_.Wait();
    }

    virtual ~TestTask() {}

   private:
    ThreadPool* const thread_pool_;
    Thread* thread_ = nullptr;
    CountDownLatch latch_{1};
  };

  constexpr size_t kTotalWorkers = 1;

  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kTotalWorkers,
  });
  ASSERT_FALSE(pool.OwnsThisThread());
  TestTask task(&pool);
  pool.Enqueue(&task);
  task.Wait();
  ASSERT_TRUE(pool.Owns(task.thread()));
}

namespace strand {

constexpr size_t kPoolMaxTasks = 100;
constexpr size_t kPoolTotalWorkers = 4;

TEST_F(ThreadPoolTest, Strand) {
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kPoolTotalWorkers,
  });
  Strand strand(&pool);

  CountDownLatch latch(kPoolMaxTasks);
  std::atomic<int> counter(0);
  for (auto i = 0; i != kPoolMaxTasks; ++i) {
    strand.EnqueueFunctor([&counter, &latch] {
      ASSERT_EQ(++counter, 1);
      std::this_thread::sleep_for(1ms);
      ASSERT_EQ(--counter, 0);
      latch.CountDown();
    });
  }

  latch.Wait();
  strand.Shutdown();
}

TEST_F(ThreadPoolTest, StrandShutdown) {
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kPoolTotalWorkers,
  });
  Strand strand(&pool);

  CountDownLatch latch1(1);
  strand.EnqueueFunctor([&latch1] {
    latch1.CountDown();
    std::this_thread::sleep_for(500ms);
  });
  class AbortedTask : public StrandTask {
   public:
    void Run() override {
      ASSERT_TRUE(false);
    }

    void Done(const Status& status) override {
      ASSERT_TRUE(status.IsAborted());
    }

    virtual ~AbortedTask() = default;
  };
  AbortedTask aborted_task;
  strand.Enqueue(&aborted_task);
  latch1.Wait();
  strand.Shutdown();
}

TEST_F(ThreadPoolTest, NotUsedStrandShutdown) {
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kPoolTotalWorkers,
  });

  Strand strand(&pool);

  std::atomic<bool> shutdown_completed{false};
  std::thread shutdown_thread([strand = &strand, &shutdown_completed]{
    strand->Shutdown();
    shutdown_completed = true;
  });

  ASSERT_OK(LoggedWaitFor(
      [&shutdown_completed] {
        return shutdown_completed.load();
      },
      5s * kTimeMultiplier, "Waiting for strand shutdown"));

  shutdown_thread.join();
}

TEST_F(ThreadPoolTest, StrandShutdownAndDestroyRace) {
  constexpr size_t kNumIters = 10;

  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kPoolTotalWorkers,
  });

  auto task = []{};

  for (size_t iter = 0; iter < kNumIters; ++iter) {
    Strand strand(&pool);

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_strand_done_inject_delay_ms) = 0;
    strand.EnqueueFunctor(task);
    // Give enough time for Strand::Done to be finished.
    std::this_thread::sleep_for(10ms);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_strand_done_inject_delay_ms) = 10;
    strand.EnqueueFunctor(task);

    strand.Shutdown();
  }
}

// Create multiple subpools in a thread pool and round-robin tasks between subpools. Shut down the
// subpools sequentially. We may fail to submit some tasks to some subpools. Shutting down a
// subpool should wait for all tasks in that subpool to complete.
TEST_F(ThreadPoolTest, SubPool) {
  constexpr size_t kTotalWorkers = 4;
  constexpr size_t kTasksPerSubPool = RegularBuildVsSanitizers(5000, 1000);
  constexpr size_t kNumSubPools = 5;
  constexpr size_t kTotalTasks = kNumSubPools * kTasksPerSubPool;
  ThreadPool pool(ThreadPoolOptions {
    .name = "test",
    .max_workers = kTotalWorkers,
  });
  struct SubPoolData {
    size_t index;
    std::unique_ptr<ThreadSubPool> subpool;
    std::unique_ptr<CountDownLatch> latch;
    std::unique_ptr<std::atomic<size_t>> num_successes;
    std::unique_ptr<std::atomic<size_t>> num_unsubmitted_tasks;

    SubPoolData(size_t index_, ThreadPool* pool, size_t num_tasks)
        : index(index_),
          subpool(std::make_unique<ThreadSubPool>(pool)),
          latch(std::make_unique<CountDownLatch>(num_tasks)),
          num_successes(std::make_unique<std::atomic<size_t>>(0)) {
    }
  };

  std::vector<SubPoolData> subpool_data_vec;
  for (size_t i = 0; i < kNumSubPools; ++i) {
    subpool_data_vec.emplace_back(i, &pool, kTasksPerSubPool);
  }
  std::vector<TestTask> tasks(kTotalTasks);
  TestThreadHolder holder;
  holder.AddThread(std::thread([&tasks, &subpool_data_vec] {
    CDSAttacher attacher;
    std::vector<bool> subpool_operational(kNumSubPools, true);
    auto start_time = MonoTime::Now();
    for (size_t task_index = 0; task_index < kTotalTasks; ++task_index) {
      auto subpool_index = task_index % kNumSubPools;
      auto& subpool_data = subpool_data_vec[subpool_index];
      if (!subpool_operational[subpool_index]) {
        subpool_data.latch->CountDown();
        continue;
      }
      auto& task = tasks[task_index];
      task.SetLatch(subpool_data.latch.get());
      task.SetSuccessCounter(subpool_data.num_successes.get());
      task.SetDelayMicros(RandomUniformInt(0, 1000));
      ThreadSubPool& subpool = *subpool_data.subpool;
      if (!subpool.Enqueue(&task)) {
        LOG(INFO) << "Subpool " << subpool_index << " was shut down after we managed to submit "
                  << task_index << " tasks out of " << kTasksPerSubPool;
        subpool_operational[subpool_index] = false;
        continue;
      }
      if (task_index % 10 == 0) {
        std::this_thread::sleep_for(1ms);
      }
    }
    LOG(INFO) << "Added all " << kTotalTasks << " tasks to all subpools in "
              << (MonoTime::Now() - start_time).ToMicroseconds() << " usec";
  }));
  LOG(INFO) << "Waiting for all subpools to complete all submitted tasks";
  for (auto& subpool_data : subpool_data_vec) {
    std::this_thread::sleep_for(200ms);
    auto& subpool = *subpool_data.subpool;
    subpool.Shutdown();
    ASSERT_EQ(subpool.num_active_tasks(), 0);
  }
  for (auto& subpool_data : subpool_data_vec) {
    LOG(INFO) << "Subpool " << subpool_data.index
              << " successfully completed " << *subpool_data.num_successes
              << " tasks out of " << kTasksPerSubPool;
  }
  LOG(INFO) << "All subpools should be clear of tasks. Checking completed task counts.";
  std::vector<size_t> completed_by_subpool(kNumSubPools, 0);
  for (size_t task_index = 0; task_index < kTotalTasks; ++task_index) {
    auto subpool_index = task_index % kNumSubPools;
    if (tasks[task_index].IsCompleted()) {
      completed_by_subpool[subpool_index]++;
    }
  }
  for (size_t subpool_index = 0; subpool_index < kNumSubPools; ++subpool_index) {
    ASSERT_EQ(completed_by_subpool[subpool_index], *subpool_data_vec[subpool_index].num_successes);
  }
}

} // namespace strand

} // namespace rpc
} // namespace yb
