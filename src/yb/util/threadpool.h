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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include <gtest/gtest_prod.h>

#include "yb/gutil/callback_forward.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/condition_variable.h"
#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/status.h"
#include "yb/util/thread_pool.h"
#include "yb/util/unique_lock.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(StopWaitIfFailed);

class Runnable {
 public:
  virtual void Run() = 0;
  virtual ~Runnable() = default;
};

// THREAD_POOL_METRICS_DEFINE / THREAD_POOL_METRICS_INSTANCE are helpers which define the metrics
// required for a ThreadPoolMetrics object and instantiate said objects, respectively. Example
// usage:
// // At the top of the file:
// THREAD_POOL_METRICS_DEFINE(server, thread_pool_foo, "Thread pool for Foo jobs.")
// ...
// // Inline:
// ThreadPoolBuilder("foo")
//   .set_metrics(THREAD_POOL_METRICS_INSTANCE(server_->metric_entity(), thread_pool_foo))
//   ...
//   .Build(...);
#define THREAD_POOL_METRICS_DEFINE(entity, name, label) \
    METRIC_DEFINE_event_stats(entity, BOOST_PP_CAT(name, _queue_length), \
        label " Queue Length", yb::MetricUnit::kMicroseconds, \
        label " - queue length event stats."); \
    METRIC_DEFINE_event_stats(entity, BOOST_PP_CAT(name, _queue_time_us), \
        label " Queue Time", yb::MetricUnit::kMicroseconds, \
        label " - queue time event stats, microseconds."); \
    METRIC_DEFINE_event_stats(entity, BOOST_PP_CAT(name, _run_time_us), \
        label " Run Time", yb::MetricUnit::kMicroseconds, \
        label " - run time event stats, microseconds.")

#define THREAD_POOL_METRICS_INSTANCE(entity, name) { \
      BOOST_PP_CAT(METRIC_, BOOST_PP_CAT(name, _run_time_us)).Instantiate(entity), \
      BOOST_PP_CAT(METRIC_, BOOST_PP_CAT(name, _queue_time_us)).Instantiate(entity), \
      BOOST_PP_CAT(METRIC_, BOOST_PP_CAT(name, _run_time_us)).Instantiate(entity) \
    }

class ThreadPoolToken {
 public:
  virtual ~ThreadPoolToken() = default;

  virtual Status SubmitFunc(std::function<void()> f) = 0;
  virtual void Shutdown() = 0;

  Status SubmitClosure(const Closure& task);
  Status Submit(const std::shared_ptr<Runnable>& runnable);
};

class ThreadPool {
 public:
  explicit ThreadPool(const ThreadPoolOptions& options) : impl_(options) {}

  void Shutdown() {
    impl_.Shutdown();
  }

  Status SubmitFunc(const std::function<void()>& func);
  Status SubmitClosure(const Closure& task);
  Status Submit(const std::shared_ptr<Runnable>& r);

  bool WaitFor(MonoDelta delta) {
    return WaitUntil(MonoTime::NowPlus(delta));
  }

  bool WaitUntil(MonoTime until) {
    return impl_.BusyWait(until);
  }

  void Wait() {
    impl_.BusyWait(MonoTime::kUninitialized);
  }

  size_t NumWorkers() const {
    return impl_.NumWorkers();
  }

  bool Idle() const {
    return impl_.Idle();
  }

  // Allocates a new token for use in token-based task submission. All tokens
  // must be destroyed before their ThreadPool is destroyed.
  //
  // There is no limit on the number of tokens that may be allocated.
  enum class ExecutionMode {
    // Tasks submitted via this token will be executed serially.
    SERIAL,

    // Tasks submitted via this token may be executed concurrently.
    CONCURRENT,
  };

  std::unique_ptr<ThreadPoolToken> NewToken(ExecutionMode mode);

 private:
  template <class F>
  Status DoSubmit(const F& f);

  YBThreadPool impl_;
};

class ThreadPoolBuilder {
 public:
  explicit ThreadPoolBuilder(std::string name);

  // Note: We violate the style guide by returning mutable references here
  // in order to provide traditional Builder pattern conveniences.
  ThreadPoolBuilder& set_min_threads(int min_threads) {
    options_.min_workers = min_threads;
    return *this;
  }

  ThreadPoolBuilder& set_max_threads(int max_threads) {
    options_.max_workers = max_threads;
    return *this;
  }

  ThreadPoolBuilder& unlimited_threads() {
    options_.max_workers = ThreadPoolOptions::kUnlimitedWorkers;
    return *this;
  }

  ThreadPoolBuilder& set_max_queue_size(int max_queue_size) {
    // TODO(!!!)
    return *this;
  }

  ThreadPoolBuilder& set_idle_timeout(const MonoDelta& idle_timeout) {
    options_.idle_timeout = idle_timeout;
    return *this;
  }

  ThreadPoolBuilder& set_metrics(ThreadPoolMetrics metrics) {
    options_.metrics = std::move(metrics);
    return *this;
  }

  const std::string& name() const { return options_.name; }
  int min_threads() const { return 1; }
  int max_threads() const { return static_cast<int>(options_.max_workers); }
  int max_queue_size() const { return std::numeric_limits<int>::max(); }
  const MonoDelta& idle_timeout() const { return options_.idle_timeout; }

  // Instantiate a new ThreadPool with the existing builder arguments.
  Status Build(std::unique_ptr<ThreadPool>* pool) const {
    pool->reset(new ThreadPool(options_));
    return Status::OK();
  }

 private:
  ThreadPoolOptions options_;

  DISALLOW_COPY_AND_ASSIGN(ThreadPoolBuilder);
};

// Runs submitted tasks in created thread pool with specified concurrency.
class TaskRunner {
 public:
  TaskRunner() = default;

  Status Init(int concurrency);

  template <class F>
  void Submit(F&& f) {
    ++running_tasks_;
    auto status = thread_pool_->SubmitFunc([this, f = std::forward<F>(f)]() {
      auto s = f();
      CompleteTask(s);
    });
    if (!status.ok()) {
      CompleteTask(status);
    }
  }

  Status status() {
    std::lock_guard lock(mutex_);
    return first_failure_;
  }

  Status Wait(StopWaitIfFailed stop_wait_if_failed);

 private:
  void CompleteTask(const Status& status);

  std::unique_ptr<ThreadPool> thread_pool_;
  std::atomic<size_t> running_tasks_{0};
  std::atomic<bool> failed_{false};
  Status first_failure_ GUARDED_BY(mutex_);
  std::mutex mutex_;
  std::condition_variable cond_ GUARDED_BY(mutex_);
};

} // namespace yb
