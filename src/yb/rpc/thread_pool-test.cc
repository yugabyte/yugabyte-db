//
// Copyright (c) YugaByte, Inc.
//

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "yb/rpc/thread_pool.h"

#include "yb/util/test_util.h"
#include "yb/util/countdown_latch.h"

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

 private:
  void Run() override {
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
  }

  CountDownLatch* latch_ = nullptr;
  std::atomic<TestTaskState> state_ = { TestTaskState::IDLE };
};

TEST_F(ThreadPoolTest, TestSingleThread) {
  constexpr size_t kTotalTasks = 100;
  constexpr size_t kTotalWorkers = 1;
  ThreadPool pool("test", kTotalTasks, kTotalWorkers);

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
  ThreadPool pool("test", kTotalTasks, kTotalWorkers);

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
  ThreadPool pool("test", kTotalTasks, kTotalWorkers);

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  std::vector<std::thread> threads;
  size_t begin = 0;
  for (size_t i = 0; i != kProducers; ++i) {
    size_t end = kTotalTasks * (i + 1) / kProducers;
    threads.emplace_back([&pool, &latch, &tasks, begin, end] {
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
  ThreadPool pool("test", kTotalTasks, kTotalWorkers);

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  std::vector<std::thread> threads;
  size_t begin = 0;
  std::atomic<size_t> enqueue_failed(0);
  for (size_t i = 0; i != kProducers; ++i) {
    size_t end = kTotalTasks * (i + 1) / kProducers;
    threads.emplace_back([&pool, &latch, &tasks, &enqueue_failed, begin, end] {
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
  ThreadPool pool("test", kTotalTasks, kTotalWorkers);

  CountDownLatch latch(kTotalTasks);
  std::vector<TestTask> tasks(kTotalTasks);
  std::vector<std::thread> threads;
  size_t begin = 0;
  for (size_t i = 0; i != kProducers; ++i) {
    size_t end = kTotalTasks * (i + 1) / kProducers;
    threads.emplace_back([&pool, &latch, &tasks, begin, end] {
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

} // namespace rpc
} // namespace yb
