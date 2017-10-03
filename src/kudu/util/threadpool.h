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
#ifndef KUDU_UTIL_THREAD_POOL_H
#define KUDU_UTIL_THREAD_POOL_H

#include <boost/function.hpp>
#include <gtest/gtest_prod.h>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "kudu/gutil/callback_forward.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/condition_variable.h"
#include "kudu/util/monotime.h"
#include "kudu/util/mutex.h"
#include "kudu/util/status.h"

namespace kudu {

class Histogram;
class ThreadPool;
class Trace;

class Runnable {
 public:
  virtual void Run() = 0;
  virtual ~Runnable() {}
};

// ThreadPool takes a lot of arguments. We provide sane defaults with a builder.
//
// name: Used for debugging output and default names of the worker threads.
//    Since thread names are limited to 16 characters on Linux, it's good to
//    choose a short name here.
//    Required.
//
// min_threads: Minimum number of threads we'll have at any time.
//    Default: 0.
//
// max_threads: Maximum number of threads we'll have at any time.
//    Default: Number of CPUs detected on the system.
//
// max_queue_size: Maximum number of items to enqueue before returning a
//    Status::ServiceUnavailable message from Submit().
//    Default: INT_MAX.
//
// timeout: How long we'll keep around an idle thread before timing it out.
//    We always keep at least min_threads.
//    Default: 500 milliseconds.
//
class ThreadPoolBuilder {
 public:
  explicit ThreadPoolBuilder(std::string name);

  // Note: We violate the style guide by returning mutable references here
  // in order to provide traditional Builder pattern conveniences.
  ThreadPoolBuilder& set_min_threads(int min_threads);
  ThreadPoolBuilder& set_max_threads(int max_threads);
  ThreadPoolBuilder& set_max_queue_size(int max_queue_size);
  ThreadPoolBuilder& set_idle_timeout(const MonoDelta& idle_timeout);

  const std::string& name() const { return name_; }
  int min_threads() const { return min_threads_; }
  int max_threads() const { return max_threads_; }
  int max_queue_size() const { return max_queue_size_; }
  const MonoDelta& idle_timeout() const { return idle_timeout_; }

  // Instantiate a new ThreadPool with the existing builder arguments.
  Status Build(gscoped_ptr<ThreadPool>* pool) const;

 private:
  friend class ThreadPool;
  const std::string name_;
  int min_threads_;
  int max_threads_;
  int max_queue_size_;
  MonoDelta idle_timeout_;

  DISALLOW_COPY_AND_ASSIGN(ThreadPoolBuilder);
};

// Thread pool with a variable number of threads.
// The pool can execute a class that implements the Runnable interface, or a
// boost::function, which can be obtained via boost::bind().
//
// Usage Example:
//    static void Func(int n) { ... }
//    class Task : public Runnable { ... }
//
//    gscoped_ptr<ThreadPool> thread_pool;
//    CHECK_OK(
//        ThreadPoolBuilder("my_pool")
//            .set_min_threads(0)
//            .set_max_threads(5)
//            .set_max_queue_size(10)
//            .set_timeout(MonoDelta::FromMilliseconds(2000))
//            .Build(&thread_pool));
//    thread_pool->Submit(shared_ptr<Runnable>(new Task()));
//    thread_pool->Submit(boost::bind(&Func, 10));
class ThreadPool {
 public:
  ~ThreadPool();

  // Wait for the running tasks to complete and then shutdown the threads.
  // All the other pending tasks in the queue will be removed.
  // NOTE: That the user may implement an external abort logic for the
  //       runnables, that must be called before Shutdown(), if the system
  //       should know about the non-execution of these tasks, or the runnable
  //       require an explicit "abort" notification to exit from the run loop.
  void Shutdown();

  // Submit a function using the kudu Closure system.
  Status SubmitClosure(const Closure& task) WARN_UNUSED_RESULT;

  // Submit a function binded using boost::bind(&FuncName, args...)
  Status SubmitFunc(const boost::function<void()>& func)
      WARN_UNUSED_RESULT;

  // Submit a Runnable class
  Status Submit(const std::shared_ptr<Runnable>& task)
      WARN_UNUSED_RESULT;

  // Wait until all the tasks are completed.
  void Wait();

  // Waits for the pool to reach the idle state, or until 'until' time is reached.
  // Returns true if the pool reached the idle state, false otherwise.
  bool WaitUntil(const MonoTime& until);

  // Waits for the pool to reach the idle state, or until 'delta' time elapses.
  // Returns true if the pool reached the idle state, false otherwise.
  bool WaitFor(const MonoDelta& delta);

  // Return the current number of tasks waiting in the queue.
  // Typically used for metrics.
  int queue_length() const {
    return ANNOTATE_UNPROTECTED_READ(queue_size_);
  }

  // Attach a histogram which measures the queue length seen by tasks when they enter
  // the thread pool's queue.
  void SetQueueLengthHistogram(const scoped_refptr<Histogram>& hist);

  // Attach a histogram which measures the amount of time that tasks spend waiting in
  // the queue.
  void SetQueueTimeMicrosHistogram(const scoped_refptr<Histogram>& hist);

  // Attach a histogram which measures the amount of time that tasks spend running.
  void SetRunTimeMicrosHistogram(const scoped_refptr<Histogram>& hist);

 private:
  friend class ThreadPoolBuilder;

  // Create a new thread pool using a builder.
  explicit ThreadPool(const ThreadPoolBuilder& builder);

  // Initialize the thread pool by starting the minimum number of threads.
  Status Init();

  // Clear all entries from queue_. Requires that lock_ is held.
  void ClearQueue();

  // Dispatcher responsible for dequeueing and executing the tasks
  void DispatchThread(bool permanent);

  // Create new thread. Required that lock_ is held.
  Status CreateThreadUnlocked();

 private:
  FRIEND_TEST(TestThreadPool, TestThreadPoolWithNoMinimum);
  FRIEND_TEST(TestThreadPool, TestVariableSizeThreadPool);

  struct QueueEntry {
    std::shared_ptr<Runnable> runnable;
    Trace* trace;

    // Time at which the entry was submitted to the pool.
    MonoTime submit_time;
  };

  const std::string name_;
  const int min_threads_;
  const int max_threads_;
  const int max_queue_size_;
  const MonoDelta idle_timeout_;

  Status pool_status_;
  Mutex lock_;
  ConditionVariable idle_cond_;
  ConditionVariable no_threads_cond_;
  ConditionVariable not_empty_;
  int num_threads_;
  int active_threads_;
  int queue_size_;
  std::list<QueueEntry> queue_;

  scoped_refptr<Histogram> queue_length_histogram_;
  scoped_refptr<Histogram> queue_time_us_histogram_;
  scoped_refptr<Histogram> run_time_us_histogram_;

  DISALLOW_COPY_AND_ASSIGN(ThreadPool);
};

} // namespace kudu
#endif
