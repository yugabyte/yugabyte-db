// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/sysinfo.h"

#include "yb/util/barrier.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/locks.h"
#include "yb/util/metrics.h"
#include "yb/util/promise.h"
#include "yb/util/random.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"

using std::atomic;
using std::shared_ptr;
using std::string;
using std::thread;
using std::unique_ptr;
using std::vector;

using strings::Substitute;
DECLARE_bool(enable_tracing);

using std::shared_ptr;

namespace yb {

namespace {

static Status BuildMinMaxTestPool(
    int min_threads, int max_threads, std::unique_ptr<ThreadPool>* pool) {
  return ThreadPoolBuilder("test").set_min_threads(min_threads)
                                  .set_max_threads(max_threads)
                                  .Build(pool);
}

} // anonymous namespace

class TestThreadPool : public ::testing::Test {
 public:
  TestThreadPool() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tracing) = true;
  }
};

TEST_F(TestThreadPool, TestNoTaskOpenClose) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(4, 4, &thread_pool));
  thread_pool->Shutdown();
}

static void SimpleTaskMethod(int n, Atomic32 *counter) {
  while (n--) {
    base::subtle::NoBarrier_AtomicIncrement(counter, 1);
    boost::detail::yield(n);
  }
}

class SimpleTask : public Runnable {
 public:
  SimpleTask(int n, Atomic32 *counter)
    : n_(n), counter_(counter) {
  }

  void Run() override {
    SimpleTaskMethod(n_, counter_);
  }

 private:
  int n_;
  Atomic32 *counter_;
};

TEST_F(TestThreadPool, TestSimpleTasks) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(4, 4, &thread_pool));

  Atomic32 counter(0);
  std::shared_ptr<Runnable> task(new SimpleTask(15, &counter));

  ASSERT_OK(thread_pool->SubmitFunc(std::bind(&SimpleTaskMethod, 10, &counter)));
  ASSERT_OK(thread_pool->Submit(task));
  ASSERT_OK(thread_pool->SubmitFunc(std::bind(&SimpleTaskMethod, 20, &counter)));
  ASSERT_OK(thread_pool->Submit(task));
  ASSERT_OK(thread_pool->SubmitClosure(Bind(&SimpleTaskMethod, 123, &counter)));
  thread_pool->Wait();
  ASSERT_EQ(10 + 15 + 20 + 15 + 123, base::subtle::NoBarrier_Load(&counter));
  thread_pool->Shutdown();
}

static void IssueTraceStatement() {
  TRACE("hello from task");
}

// Test that the thread-local trace is propagated to tasks
// submitted to the threadpool.
TEST_F(TestThreadPool, TestTracePropagation) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(1, 1, &thread_pool));

  scoped_refptr<Trace> t(new Trace);
  {
    ADOPT_TRACE(t.get());
    ASSERT_OK(thread_pool->SubmitFunc(&IssueTraceStatement));
  }
  thread_pool->Wait();
  ASSERT_STR_CONTAINS(t->DumpToString(true), "hello from task");
}

TEST_F(TestThreadPool, TestSubmitAfterShutdown) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(1, 1, &thread_pool));
  thread_pool->Shutdown();
  Status s = thread_pool->SubmitFunc(&IssueTraceStatement);
  ASSERT_EQ("Service unavailable: The pool has been shut down.",
            s.ToString(/* no file/line */ false));
}

class SlowTask : public Runnable {
 public:
  explicit SlowTask(CountDownLatch* latch)
    : latch_(latch) {
  }

  void Run() override {
    latch_->Wait();
  }

  static shared_ptr<Runnable> NewSlowTask(CountDownLatch* latch) {
    return std::make_shared<SlowTask>(latch);
  }
 private:
  CountDownLatch* latch_;
};

TEST_F(TestThreadPool, TestThreadPoolWithNoMinimum) {
  MonoDelta idle_timeout = MonoDelta::FromMilliseconds(1);
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(0).set_max_threads(3)
      .set_idle_timeout(idle_timeout).Build(&thread_pool));
  // There are no threads to start with.
  ASSERT_EQ(0, thread_pool->num_threads_);
  // We get up to 3 threads when submitting work.
  CountDownLatch latch(1);
  auto se = ScopeExit([&latch] {
      latch.CountDown();
  });
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(2, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(3, thread_pool->num_threads_);
  // The 4th piece of work gets queued.
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(3, thread_pool->num_threads_);
  // Finish all work
  latch.CountDown();
  thread_pool->Wait();
  ASSERT_EQ(0, thread_pool->active_threads_);
  thread_pool->Shutdown();
  ASSERT_EQ(0, thread_pool->num_threads_);
}

TEST_F(TestThreadPool, TestThreadPoolWithNoMaxThreads) {
  // By default a threadpool's max_threads is set to the number of CPUs, so
  // this test submits more tasks than that to ensure that the number of CPUs
  // isn't some kind of upper bound.
  const int kNumCPUs = base::NumCPUs();
  std::unique_ptr<ThreadPool> thread_pool;

  // Build a threadpool with no limit on the maximum number of threads.
  ASSERT_OK(ThreadPoolBuilder("test")
                .set_max_threads(std::numeric_limits<int>::max())
                .Build(&thread_pool));
  CountDownLatch latch(1);
  auto se = ScopeExit([&latch] {
    latch.CountDown();
    });

  // Submit tokenless tasks. Each should create a new thread.
  for (int i = 0; i < kNumCPUs * 2; i++) {
    ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  }
  ASSERT_EQ(kNumCPUs * 2, thread_pool->num_threads_);

  // Submit tasks on two tokens. Only two threads should be created.
  unique_ptr<ThreadPoolToken> t1 = thread_pool->NewToken(ThreadPool::ExecutionMode::SERIAL);
  unique_ptr<ThreadPoolToken> t2 = thread_pool->NewToken(ThreadPool::ExecutionMode::SERIAL);
  for (int i = 0; i < kNumCPUs * 2; i++) {
    ThreadPoolToken* t = (i % 2 == 0) ? t1.get() : t2.get();
    ASSERT_OK(t->Submit(SlowTask::NewSlowTask(&latch)));
  }
  ASSERT_EQ((kNumCPUs * 2) + 2, thread_pool->num_threads_);

  // Submit more tokenless tasks. Each should create a new thread.
  for (int i = 0; i < kNumCPUs; i++) {
    ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  }
  ASSERT_EQ((kNumCPUs * 3) + 2, thread_pool->num_threads_);

  latch.CountDown();
  thread_pool->Wait();
  thread_pool->Shutdown();
}

// Regression test for a bug where a task is submitted exactly
// as a thread is about to exit. Previously this could hang forever.
TEST_F(TestThreadPool, TestRace) {
  alarm(10);
  MonoDelta idle_timeout = MonoDelta::FromMicroseconds(1);
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(0).set_max_threads(1)
      .set_idle_timeout(idle_timeout).Build(&thread_pool));

  for (int i = 0; i < 500; i++) {
    CountDownLatch l(1);
    ASSERT_OK(thread_pool->SubmitFunc([&l]() { l.CountDown(); }));
    l.Wait();
    // Sleeping a different amount in each iteration makes it more likely to hit
    // the bug.
    SleepFor(MonoDelta::FromMicroseconds(i));
  }
}

TEST_F(TestThreadPool, TestVariableSizeThreadPool) {
  MonoDelta idle_timeout = MonoDelta::FromMilliseconds(1);
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(1).set_max_threads(4).set_max_queue_size(1)
      .set_idle_timeout(idle_timeout).Build(&thread_pool));
  // There is 1 thread to start with.
  ASSERT_EQ(1, thread_pool->num_threads_);
  // We get up to 4 threads when submitting work.
  CountDownLatch latch(1);
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(1, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(2, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(3, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(4, thread_pool->num_threads_);
  // The 5th piece of work gets queued.
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(4, thread_pool->num_threads_);
  // The 6th piece of work gets rejected.
  ASSERT_NOK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_EQ(4, thread_pool->num_threads_);
  // Finish all work
  latch.CountDown();
  thread_pool->Wait();
  ASSERT_EQ(0, thread_pool->active_threads_);
  thread_pool->Shutdown();
  ASSERT_EQ(0, thread_pool->num_threads_);
}

TEST_F(TestThreadPool, TestMaxQueueSize) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(1).set_max_threads(1)
      .set_max_queue_size(1).Build(&thread_pool));

  CountDownLatch latch(1);
  ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  Status s = thread_pool->Submit(SlowTask::NewSlowTask(&latch));
  // We race against the worker thread to re-enqueue.
  // If we get there first, we fail on the 2nd Submit().
  // If the worker dequeues first, we fail on the 3rd.
  if (s.ok()) {
    s = thread_pool->Submit(SlowTask::NewSlowTask(&latch));
  }
  CHECK(s.IsServiceUnavailable()) << "Expected failure due to queue blowout:" << s.ToString();
  latch.CountDown();
  thread_pool->Wait();
  thread_pool->Shutdown();
}

void TestQueueSizeZero(int max_threads) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .set_min_threads(0).set_max_threads(max_threads)
                .set_max_queue_size(0).Build(&thread_pool));

  CountDownLatch latch(1);
  for (int i = 0; i < max_threads; i++) {
    ASSERT_OK(thread_pool->Submit(SlowTask::NewSlowTask(&latch)));
  }
  Status s = thread_pool->Submit(SlowTask::NewSlowTask(&latch));
  ASSERT_TRUE(s.IsServiceUnavailable()) << "Expected failure due to queue blowout:" << s.ToString();
  latch.CountDown();
  thread_pool->Wait();
  thread_pool->Shutdown();
}

TEST_F(TestThreadPool, TestMaxQueueZero) {
  TestQueueSizeZero(1);
  TestQueueSizeZero(5);
}


TEST_F(TestThreadPool, TestMaxQueueZeroNoThreads) {
  TestQueueSizeZero(0);
}

// Test that setting a promise from another thread yields
// a value on the current thread.
TEST_F(TestThreadPool, TestPromises) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(1).set_max_threads(1)
      .set_max_queue_size(1).Build(&thread_pool));

  Promise<int> my_promise;
  ASSERT_OK(thread_pool->SubmitClosure(
                     Bind(&Promise<int>::Set, Unretained(&my_promise), 5)));
  ASSERT_EQ(5, my_promise.Get());
  thread_pool->Shutdown();
}


METRIC_DEFINE_entity(test_entity);
METRIC_DEFINE_event_stats(test_entity, queue_length, "queue length",
                        MetricUnit::kTasks, "queue length");

METRIC_DEFINE_event_stats(test_entity, queue_time, "queue time",
                        MetricUnit::kMicroseconds, "queue time");

METRIC_DEFINE_event_stats(test_entity, run_time, "run time",
                        MetricUnit::kMicroseconds, "run time");

TEST_F(TestThreadPool, TestMetrics) {
  MetricRegistry registry;
  vector<ThreadPoolMetrics> all_metrics;
  for (int i = 0; i < 3; i++) {
    scoped_refptr<MetricEntity> entity = METRIC_ENTITY_test_entity.Instantiate(
        &registry, Substitute("test $0", i));
    all_metrics.emplace_back(ThreadPoolMetrics{
        METRIC_queue_length.Instantiate(entity),
        METRIC_queue_time.Instantiate(entity),
        METRIC_run_time.Instantiate(entity)
    });
  }

  // Enable metrics for the thread pool.
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
            .set_min_threads(1).set_max_threads(1)
            .set_metrics(all_metrics[0])
            .Build(&thread_pool));

  unique_ptr<ThreadPoolToken> t1 = thread_pool->NewTokenWithMetrics(
      ThreadPool::ExecutionMode::SERIAL, all_metrics[1]);
  unique_ptr<ThreadPoolToken> t2 = thread_pool->NewTokenWithMetrics(
      ThreadPool::ExecutionMode::SERIAL, all_metrics[2]);

  // Submit once to t1, twice to t2, and three times without a token.
  ASSERT_OK(t1->SubmitFunc([](){}));
  ASSERT_OK(t2->SubmitFunc([](){}));
  ASSERT_OK(t2->SubmitFunc([](){}));
  ASSERT_OK(thread_pool->SubmitFunc([](){}));
  ASSERT_OK(thread_pool->SubmitFunc([](){}));
  ASSERT_OK(thread_pool->SubmitFunc([](){}));
  thread_pool->Wait();

  // The total counts should reflect the number of submissions to each token.
  ASSERT_EQ(1, all_metrics[1].queue_length_stats->TotalCount());
  ASSERT_EQ(1, all_metrics[1].queue_time_us_stats->TotalCount());
  ASSERT_EQ(1, all_metrics[1].run_time_us_stats->TotalCount());
  ASSERT_EQ(2, all_metrics[2].queue_length_stats->TotalCount());
  ASSERT_EQ(2, all_metrics[2].queue_time_us_stats->TotalCount());
  ASSERT_EQ(2, all_metrics[2].run_time_us_stats->TotalCount());

  // And the counts on the pool-wide metrics should reflect all submissions.
  ASSERT_EQ(6, all_metrics[0].queue_length_stats->TotalCount());
  ASSERT_EQ(6, all_metrics[0].queue_time_us_stats->TotalCount());
  ASSERT_EQ(6, all_metrics[0].run_time_us_stats->TotalCount());
}

// For test cases that should run with both kinds of tokens.
class TestThreadPoolTokenTypes : public TestThreadPool,
                                 public testing::WithParamInterface<ThreadPool::ExecutionMode> {};

INSTANTIATE_TEST_CASE_P(Tokens, TestThreadPoolTokenTypes,
                        ::testing::Values(ThreadPool::ExecutionMode::SERIAL,
                                          ThreadPool::ExecutionMode::CONCURRENT));


TEST_P(TestThreadPoolTokenTypes, TestTokenSubmitAndWait) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .Build(&thread_pool));
  unique_ptr<ThreadPoolToken> t = thread_pool->NewToken(GetParam());
  int i = 0;
  ASSERT_OK(t->SubmitFunc([&]() {
    SleepFor(MonoDelta::FromMilliseconds(1));
    i++;
  }));
  t->Wait();
  ASSERT_EQ(1, i);
}

TEST_F(TestThreadPool, TestTokenSubmitsProcessedSerially) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .Build(&thread_pool));
  unique_ptr<ThreadPoolToken> t = thread_pool->NewToken(ThreadPool::ExecutionMode::SERIAL);
  Random r(SeedRandom());
  string result;
  for (char c = 'a'; c < 'f'; c++) {
    // Sleep a little first so that there's a higher chance of out-of-order
    // appends if the submissions did execute in parallel.
    int sleep_ms = r.Next() % 5;
    ASSERT_OK(t->SubmitFunc([&result, c, sleep_ms]() {
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      result += c;
    }));
  }
  t->Wait();
  ASSERT_EQ("abcde", result);
}

TEST_P(TestThreadPoolTokenTypes, TestTokenSubmitsProcessedConcurrently) {
  const int kNumTokens = 5;
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .set_max_threads(kNumTokens)
                .Build(&thread_pool));
  vector<unique_ptr<ThreadPoolToken>> tokens;

  // A violation to the tested invariant would yield a deadlock, so let's set
  // up an alarm to bail us out.
  alarm(60);
  auto se = ScopeExit([] {
    alarm(0); // Disable alarm on test exit.
  });
  shared_ptr<Barrier> b = std::make_shared<Barrier>(kNumTokens + 1);
  for (int i = 0; i < kNumTokens; i++) {
    tokens.emplace_back(thread_pool->NewToken(GetParam()));
    ASSERT_OK(tokens.back()->SubmitFunc([b]() {
      b->Wait();
    }));
  }

  // This will deadlock if the above tasks weren't all running concurrently.
  b->Wait();
}

TEST_F(TestThreadPool, TestTokenSubmitsNonSequential) {
  const int kNumSubmissions = 5;
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .set_max_threads(kNumSubmissions)
                .Build(&thread_pool));

  // A violation to the tested invariant would yield a deadlock, so let's set
  // up an alarm to bail us out.
  alarm(60);
  auto se = ScopeExit([] {
    alarm(0); // Disable alarm on test exit.
  });
  shared_ptr<Barrier> b = std::make_shared<Barrier>(kNumSubmissions + 1);
  unique_ptr<ThreadPoolToken> t = thread_pool->NewToken(ThreadPool::ExecutionMode::CONCURRENT);
  for (int i = 0; i < kNumSubmissions; i++) {
    ASSERT_OK(t->SubmitFunc([b]() {
      b->Wait();
    }));
  }

  // This will deadlock if the above tasks weren't all running concurrently.
  b->Wait();
}

TEST_P(TestThreadPoolTokenTypes, TestTokenShutdown) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .set_max_threads(4)
                .Build(&thread_pool));

  unique_ptr<ThreadPoolToken> t1(thread_pool->NewToken(GetParam()));
  unique_ptr<ThreadPoolToken> t2(thread_pool->NewToken(GetParam()));
  CountDownLatch l1(1);
  CountDownLatch l2(1);

  // A violation to the tested invariant would yield a deadlock, so let's set
  // up an alarm to bail us out.
  alarm(60);
  auto se = ScopeExit([] {
    alarm(0); // Disable alarm on test exit.
  });

  for (int i = 0; i < 3; i++) {
    ASSERT_OK(t1->SubmitFunc([&]() {
      l1.Wait();
    }));
  }
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(t2->SubmitFunc([&]() {
      l2.Wait();
    }));
  }

  // Unblock all of t1's tasks, but not t2's tasks.
  l1.CountDown();

  // If this also waited for t2's tasks, it would deadlock.
  t1->Shutdown();

  // We can no longer submit to t1 but we can still submit to t2.
  ASSERT_TRUE(t1->SubmitFunc([](){}).IsServiceUnavailable());
  ASSERT_OK(t2->SubmitFunc([](){}));

  // Unblock t2's tasks.
  l2.CountDown();
  t2->Shutdown();
}

TEST_P(TestThreadPoolTokenTypes, TestTokenWaitForAll) {
  const int kNumTokens = 3;
  const int kNumSubmissions = 20;
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .Build(&thread_pool));
  Random r(SeedRandom());
  vector<unique_ptr<ThreadPoolToken>> tokens;
  for (int i = 0; i < kNumTokens; i++) {
    tokens.emplace_back(thread_pool->NewToken(GetParam()));
  }

  atomic<int32_t> v(0);
  for (int i = 0; i < kNumSubmissions; i++) {
    // Sleep a little first to raise the likelihood of the test thread
    // reaching Wait() before the submissions finish.
    int sleep_ms = r.Next() % 5;

    auto task = [&v, sleep_ms]() {
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      v++;
    };

    // Half of the submissions will be token-less, and half will use a token.
    if (i % 2 == 0) {
      ASSERT_OK(thread_pool->SubmitFunc(task));
    } else {
      int token_idx = r.Next() % tokens.size();
      ASSERT_OK(tokens[token_idx]->SubmitFunc(task));
    }
  }
  thread_pool->Wait();
  ASSERT_EQ(kNumSubmissions, v);
}

TEST_F(TestThreadPool, TestFuzz) {
  const int kNumOperations = 1000;
  Random r(SeedRandom());
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .Build(&thread_pool));
  vector<unique_ptr<ThreadPoolToken>> tokens;

  for (int i = 0; i < kNumOperations; i++) {
    // Operation distribution:
    //
    // - Submit without a token: 40%
    // - Submit with a randomly selected token: 35%
    // - Allocate a new token: 10%
    // - Wait on a randomly selected token: 7%
    // - Shutdown a randomly selected token: 4%
    // - Deallocate a randomly selected token: 2%
    // - Wait for all submissions: 2%
    int op = r.Next() % 100;
    if (op < 40) {
      // Submit without a token.
      int sleep_ms = r.Next() % 5;
      ASSERT_OK(thread_pool->SubmitFunc([sleep_ms]() {
        // Sleep a little first to increase task overlap.
        SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      }));
    } else if (op < 75) {
      // Submit with a randomly selected token.
      if (tokens.empty()) {
        continue;
      }
      int sleep_ms = r.Next() % 5;
      int token_idx = r.Next() % tokens.size();
      Status s = tokens[token_idx]->SubmitFunc([sleep_ms]() {
        // Sleep a little first to increase task overlap.
        SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      });
      ASSERT_TRUE(s.ok() || s.IsServiceUnavailable());
    } else if (op < 85) {
      // Allocate a token with a randomly selected policy.
      ThreadPool::ExecutionMode mode = r.Next() % 2 ?
          ThreadPool::ExecutionMode::SERIAL :
          ThreadPool::ExecutionMode::CONCURRENT;
      tokens.emplace_back(thread_pool->NewToken(mode));
    } else if (op < 92) {
      // Wait on a randomly selected token.
      if (tokens.empty()) {
        continue;
      }
      int token_idx = r.Next() % tokens.size();
      tokens[token_idx]->Wait();
    } else if (op < 96) {
      // Shutdown a randomly selected token.
      if (tokens.empty()) {
        continue;
      }
      int token_idx = r.Next() % tokens.size();
      tokens[token_idx]->Shutdown();
    } else if (op < 98) {
      // Deallocate a randomly selected token.
      if (tokens.empty()) {
        continue;
      }
      auto it = tokens.begin();
      int token_idx = r.Next() % tokens.size();
      std::advance(it, token_idx);
      tokens.erase(it);
    } else {
      // Wait on everything.
      ASSERT_LT(op, 100);
      ASSERT_GE(op, 98);
      thread_pool->Wait();
    }
  }

  // Some test runs will shut down the pool before the tokens, and some won't.
  // Either way should be safe.
  if (r.Next() % 2 == 0) {
    thread_pool->Shutdown();
  }
}

TEST_P(TestThreadPoolTokenTypes, TestTokenSubmissionsAdhereToMaxQueueSize) {
  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .set_min_threads(1)
                .set_max_threads(1)
                .set_max_queue_size(1)
                .Build(&thread_pool));

  CountDownLatch latch(1);
  unique_ptr<ThreadPoolToken> t = thread_pool->NewToken(GetParam());
  auto se = ScopeExit([&latch] {
                     latch.CountDown();
    });
  // We will be able to submit two tasks: one for max_threads == 1 and one for
  // max_queue_size == 1.
  ASSERT_OK(t->Submit(SlowTask::NewSlowTask(&latch)));
  ASSERT_OK(t->Submit(SlowTask::NewSlowTask(&latch)));
  Status s = t->Submit(SlowTask::NewSlowTask(&latch));
  ASSERT_TRUE(s.IsServiceUnavailable());
}

TEST_F(TestThreadPool, TestTokenConcurrency) {
  const int kNumTokens = 20;
  const int kTestRuntimeSecs = 1;
  const int kCycleThreads = 2;
  const int kShutdownThreads = 2;
  const int kWaitThreads = 2;
  const int kSubmitThreads = 8;

  std::unique_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
                .Build(&thread_pool));
  vector<shared_ptr<ThreadPoolToken>> tokens;
  Random rng(SeedRandom());

  // Protects 'tokens' and 'rng'.
  simple_spinlock lock;

  // Fetch a token from 'tokens' at random.
  auto GetRandomToken = [&]() -> shared_ptr<ThreadPoolToken> {
    std::lock_guard l(lock);
    int idx = rng.Uniform(kNumTokens);
    return tokens[idx];
  };

  // Preallocate all of the tokens.
  for (int i = 0; i < kNumTokens; i++) {
    ThreadPool::ExecutionMode mode;
    {
      std::lock_guard l(lock);
      mode = rng.Next() % 2 ?
          ThreadPool::ExecutionMode::SERIAL :
          ThreadPool::ExecutionMode::CONCURRENT;
    }
    tokens.emplace_back(thread_pool->NewToken(mode).release());
  }

  atomic<int64_t> total_num_tokens_cycled(0);
  atomic<int64_t> total_num_tokens_shutdown(0);
  atomic<int64_t> total_num_tokens_waited(0);
  atomic<int64_t> total_num_tokens_submitted(0);

  CountDownLatch latch(1);
  vector<thread> threads;

  for (int i = 0; i < kCycleThreads; i++) {
    // Pick a token at random and replace it.
    //
    // The replaced token is only destroyed when the last ref is dropped,
    // possibly by another thread.
    threads.emplace_back([&]() {
      int num_tokens_cycled = 0;
      while (latch.count()) {
        {
          std::lock_guard l(lock);
          int idx = rng.Uniform(kNumTokens);
          ThreadPool::ExecutionMode mode = rng.Next() % 2 ?
              ThreadPool::ExecutionMode::SERIAL :
              ThreadPool::ExecutionMode::CONCURRENT;
          tokens[idx] = shared_ptr<ThreadPoolToken>(thread_pool->NewToken(mode).release());
        }
        num_tokens_cycled++;

        // Sleep a bit, otherwise this thread outpaces the other threads and
        // nothing interesting happens to most tokens.
        SleepFor(MonoDelta::FromMicroseconds(10));
      }
      total_num_tokens_cycled += num_tokens_cycled;
    });
  }

  for (int i = 0; i < kShutdownThreads; i++) {
    // Pick a token at random and shut it down. Submitting a task to a shut
    // down token will return a ServiceUnavailable error.
    threads.emplace_back([&]() {
      int num_tokens_shutdown = 0;
      while (latch.count()) {
        GetRandomToken()->Shutdown();
        num_tokens_shutdown++;
      }
      total_num_tokens_shutdown += num_tokens_shutdown;
    });
  }

  for (int i = 0; i < kWaitThreads; i++) {
    // Pick a token at random and wait for any outstanding tasks.
    threads.emplace_back([&]() {
      int num_tokens_waited  = 0;
      while (latch.count()) {
        GetRandomToken()->Wait();
        num_tokens_waited++;
      }
      total_num_tokens_waited += num_tokens_waited;
    });
  }

  for (int i = 0; i < kSubmitThreads; i++) {
    // Pick a token at random and submit a task to it.
    threads.emplace_back([&]() {
      int num_tokens_submitted = 0;
      Random rng(SeedRandom());
      while (latch.count()) {
        int sleep_ms = rng.Next() % 5;
        Status s = GetRandomToken()->SubmitFunc([sleep_ms]() {
          // Sleep a little first so that tasks are running during other events.
          SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
        });
        ASSERT_TRUE(s.ok() || s.IsServiceUnavailable());
        num_tokens_submitted++;
      }
      total_num_tokens_submitted += num_tokens_submitted;
    });
  }

  SleepFor(MonoDelta::FromSeconds(kTestRuntimeSecs));
  latch.CountDown();
  for (auto& t : threads) {
    t.join();
  }

  LOG(INFO) << Substitute("Tokens cycled ($0 threads): $1",
                          kCycleThreads, total_num_tokens_cycled.load());
  LOG(INFO) << Substitute("Tokens shutdown ($0 threads): $1",
                          kShutdownThreads, total_num_tokens_shutdown.load());
  LOG(INFO) << Substitute("Tokens waited ($0 threads): $1",
                          kWaitThreads, total_num_tokens_waited.load());
  LOG(INFO) << Substitute("Tokens submitted ($0 threads): $1",
                          kSubmitThreads, total_num_tokens_submitted.load());
}

} // namespace yb
