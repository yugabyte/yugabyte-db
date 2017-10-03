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

#include <boost/bind.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>

#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/bind.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/metrics.h"
#include "kudu/util/promise.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/trace.h"

using std::shared_ptr;

namespace kudu {

namespace {
static Status BuildMinMaxTestPool(int min_threads, int max_threads, gscoped_ptr<ThreadPool>* pool) {
  return ThreadPoolBuilder("test").set_min_threads(min_threads)
                                  .set_max_threads(max_threads)
                                  .Build(pool);
}
} // anonymous namespace

TEST(TestThreadPool, TestNoTaskOpenClose) {
  gscoped_ptr<ThreadPool> thread_pool;
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

  void Run() OVERRIDE {
    SimpleTaskMethod(n_, counter_);
  }

 private:
  int n_;
  Atomic32 *counter_;
};

TEST(TestThreadPool, TestSimpleTasks) {
  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(4, 4, &thread_pool));

  Atomic32 counter(0);
  std::shared_ptr<Runnable> task(new SimpleTask(15, &counter));

  ASSERT_OK(thread_pool->SubmitFunc(boost::bind(&SimpleTaskMethod, 10, &counter)));
  ASSERT_OK(thread_pool->Submit(task));
  ASSERT_OK(thread_pool->SubmitFunc(boost::bind(&SimpleTaskMethod, 20, &counter)));
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
TEST(TestThreadPool, TestTracePropagation) {
  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(1, 1, &thread_pool));

  scoped_refptr<Trace> t(new Trace);
  {
    ADOPT_TRACE(t.get());
    ASSERT_OK(thread_pool->SubmitFunc(&IssueTraceStatement));
  }
  thread_pool->Wait();
  ASSERT_STR_CONTAINS(t->DumpToString(true), "hello from task");
}

TEST(TestThreadPool, TestSubmitAfterShutdown) {
  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(BuildMinMaxTestPool(1, 1, &thread_pool));
  thread_pool->Shutdown();
  Status s = thread_pool->SubmitFunc(&IssueTraceStatement);
  ASSERT_EQ("Service unavailable: The pool has been shut down.",
            s.ToString());
}

class SlowTask : public Runnable {
 public:
  explicit SlowTask(CountDownLatch* latch)
    : latch_(latch) {
  }

  void Run() OVERRIDE {
    latch_->Wait();
  }

 private:
  CountDownLatch* latch_;
};

TEST(TestThreadPool, TestThreadPoolWithNoMinimum) {
  MonoDelta idle_timeout = MonoDelta::FromMilliseconds(1);
  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(0).set_max_threads(3)
      .set_idle_timeout(idle_timeout).Build(&thread_pool));
  // There are no threads to start with.
  ASSERT_TRUE(thread_pool->num_threads_ == 0);
  // We get up to 3 threads when submitting work.
  CountDownLatch latch(1);
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(2, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(3, thread_pool->num_threads_);
  // The 4th piece of work gets queued.
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(3, thread_pool->num_threads_);
  // Finish all work
  latch.CountDown();
  thread_pool->Wait();
  ASSERT_EQ(0, thread_pool->active_threads_);
  thread_pool->Shutdown();
  ASSERT_EQ(0, thread_pool->num_threads_);
}

// Regression test for a bug where a task is submitted exactly
// as a thread is about to exit. Previously this could hang forever.
TEST(TestThreadPool, TestRace) {
  alarm(10);
  MonoDelta idle_timeout = MonoDelta::FromMicroseconds(1);
  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(0).set_max_threads(1)
      .set_idle_timeout(idle_timeout).Build(&thread_pool));

  for (int i = 0; i < 500; i++) {
    CountDownLatch l(1);
    ASSERT_OK(thread_pool->SubmitFunc(boost::bind(&CountDownLatch::CountDown, &l)));
    l.Wait();
    // Sleeping a different amount in each iteration makes it more likely to hit
    // the bug.
    SleepFor(MonoDelta::FromMicroseconds(i));
  }
}

TEST(TestThreadPool, TestVariableSizeThreadPool) {
  MonoDelta idle_timeout = MonoDelta::FromMilliseconds(1);
  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(1).set_max_threads(4)
      .set_idle_timeout(idle_timeout).Build(&thread_pool));
  // There is 1 thread to start with.
  ASSERT_EQ(1, thread_pool->num_threads_);
  // We get up to 4 threads when submitting work.
  CountDownLatch latch(1);
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(1, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(2, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(3, thread_pool->num_threads_);
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(4, thread_pool->num_threads_);
  // The 5th piece of work gets queued.
  ASSERT_OK(thread_pool->Submit(
        shared_ptr<Runnable>(new SlowTask(&latch))));
  ASSERT_EQ(4, thread_pool->num_threads_);
  // Finish all work
  latch.CountDown();
  thread_pool->Wait();
  ASSERT_EQ(0, thread_pool->active_threads_);
  thread_pool->Shutdown();
  ASSERT_EQ(0, thread_pool->num_threads_);
}

TEST(TestThreadPool, TestMaxQueueSize) {
  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
      .set_min_threads(1).set_max_threads(1)
      .set_max_queue_size(1).Build(&thread_pool));

  CountDownLatch latch(1);
  ASSERT_OK(thread_pool->Submit(shared_ptr<Runnable>(new SlowTask(&latch))));
  Status s = thread_pool->Submit(shared_ptr<Runnable>(new SlowTask(&latch)));
  // We race against the worker thread to re-enqueue.
  // If we get there first, we fail on the 2nd Submit().
  // If the worker dequeues first, we fail on the 3rd.
  if (s.ok()) {
    s = thread_pool->Submit(shared_ptr<Runnable>(new SlowTask(&latch)));
  }
  CHECK(s.IsServiceUnavailable()) << "Expected failure due to queue blowout:" << s.ToString();
  latch.CountDown();
  thread_pool->Wait();
  thread_pool->Shutdown();
}

// Test that setting a promise from another thread yields
// a value on the current thread.
TEST(TestThreadPool, TestPromises) {
  gscoped_ptr<ThreadPool> thread_pool;
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
METRIC_DEFINE_histogram(test_entity, queue_length, "queue length",
                        MetricUnit::kTasks, "queue length", 1000, 1);

METRIC_DEFINE_histogram(test_entity, queue_time, "queue time",
                        MetricUnit::kMicroseconds, "queue time", 1000000, 1);

METRIC_DEFINE_histogram(test_entity, run_time, "run time",
                        MetricUnit::kMicroseconds, "run time", 1000, 1);

TEST(TestThreadPool, TestMetrics) {
  MetricRegistry registry;
  scoped_refptr<MetricEntity> entity = METRIC_ENTITY_test_entity.Instantiate(
      &registry, "test entity");

  gscoped_ptr<ThreadPool> thread_pool;
  ASSERT_OK(ThreadPoolBuilder("test")
            .set_min_threads(1).set_max_threads(1)
            .Build(&thread_pool));

  // Enable metrics for the thread pool.
  scoped_refptr<Histogram> queue_length = METRIC_queue_length.Instantiate(entity);
  scoped_refptr<Histogram> queue_time = METRIC_queue_time.Instantiate(entity);
  scoped_refptr<Histogram> run_time = METRIC_run_time.Instantiate(entity);
  thread_pool->SetQueueLengthHistogram(queue_length);
  thread_pool->SetQueueTimeMicrosHistogram(queue_time);
  thread_pool->SetRunTimeMicrosHistogram(run_time);

  int kNumItems = 500;
  for (int i = 0; i < kNumItems; i++) {
    ASSERT_OK(thread_pool->SubmitFunc(boost::bind(&usleep, i)));
  }

  thread_pool->Wait();

  // Check that all histograms were incremented once per submitted item.
  ASSERT_EQ(kNumItems, queue_length->TotalCount());
  ASSERT_EQ(kNumItems, queue_time->TotalCount());
  ASSERT_EQ(kNumItems, run_time->TotalCount());
}

} // namespace kudu
