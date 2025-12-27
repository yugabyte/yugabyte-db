//
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
//

#pragma once

#include <concepts>
#include <memory>
#include <string>

#include "yb/gutil/port.h"

#include "yb/util/debug/leakcheck_disabler.h"
#include "yb/util/metrics.h"
#include "yb/util/lockfree.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

namespace yb {

class Status;
class Thread;

class ThreadSubPoolBase;

class ThreadPoolTask {
 public:
  // Invoked in thread pool
  virtual void Run() = 0;

  // When the thread pool done with a task, i.e. it completed or failed, it invokes Done.
  virtual void Done(const Status& status) = 0;

  const std::optional<std::weak_ptr<void>>& run_token() const {
    return run_token_;
  }

  void set_run_token(std::weak_ptr<void> run_token) {
    run_token_ = std::move(run_token);
  }

  void set_submit_time(MonoTime value) {
    submit_time_ = value;
  }

  MonoTime submit_time() {
    return submit_time_;
  }

 protected:
  virtual ~ThreadPoolTask() {}

  friend inline void SetNext(ThreadPoolTask& task, ThreadPoolTask* next) {
    task.next_ = next;
  }

  DISABLE_ASAN friend inline ThreadPoolTask* GetNext(ThreadPoolTask& task) {
    return task.next_;
  }

  // If there is a sub-pool set on the task, the thread pool will also notify the sub-pool about
  // the task completion.
  ThreadPoolTask* next_ = nullptr;
  std::optional<std::weak_ptr<void>> run_token_;
  MonoTime submit_time_;
};

template <typename T>
concept ThreadPoolTaskOrSubclass = requires (T* t) {
  std::is_base_of_v<ThreadPoolTask, T>;
};

template <class F, ThreadPoolTaskOrSubclass Base>
class FunctorThreadPoolTask : public Base {
 public:
  explicit FunctorThreadPoolTask(const F& f) : f_(f) {}
  explicit FunctorThreadPoolTask(F&& f) : f_(std::move(f)) {}

  virtual ~FunctorThreadPoolTask() = default;

 private:
  void Run() override {
    f_();
  }

  void Done(const Status& status) override {
    delete this;
  }

  F f_;
};

template <class F, ThreadPoolTaskOrSubclass TaskBase>
FunctorThreadPoolTask<F, TaskBase>* MakeFunctorThreadPoolTask(const F& f) {
  return new FunctorThreadPoolTask<F, TaskBase>(f);
}

template <class F, ThreadPoolTaskOrSubclass TaskBase>
FunctorThreadPoolTask<F, TaskBase>* MakeFunctorThreadPoolTask(F&& f) {
  return new FunctorThreadPoolTask<F, TaskBase>(std::move(f));
}

MonoDelta DefaultIdleTimeout();

// Interesting thread pool metrics. Can be applied to the entire pool (see
// ThreadPoolBuilder) or to individual tokens.
struct ThreadPoolMetrics {
  // Measures the queue length seen by tasks when they enter the queue.
  scoped_refptr<EventStats> queue_length_stats;

  // Measures the amount of time that tasks spend waiting in a queue.
  scoped_refptr<EventStats> queue_time_us_stats;

  // Measures the amount of time that tasks spend running.
  scoped_refptr<EventStats> run_time_us_stats;
};

struct ThreadPoolOptions {
  std::string name;

  size_t max_workers;
  size_t min_workers = 0;
  MonoDelta idle_timeout = DefaultIdleTimeout();

  ThreadPoolMetrics metrics = {};

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(name, max_workers, min_workers, idle_timeout);
  }

  static constexpr auto kUnlimitedWorkers = std::numeric_limits<decltype(max_workers)>::max();
  static constexpr auto kUnlimitedWorkersWithoutQueue = kUnlimitedWorkers - 1;
};

// An object that can enqueue/submit tasks, e.g. a thread pool, a sub-pool, or a strand.
template<ThreadPoolTaskOrSubclass TaskType>
class TaskRecipient {
 public:
  TaskRecipient() {}
  virtual ~TaskRecipient() {}

  // Returns true if the task has been successfully enqueued. In case of failure to enqueue, the
  // Done method of the task should also be called with an error status.
  virtual bool Enqueue(TaskType* task) = 0;

  Status EnqueueWithStatus(TaskType* task) {
    if (!Enqueue(task)) {
      static const auto kFailedToEnqueueStatus =
          STATUS(Aborted, "Failed to enqueue thread pool task");
      return kFailedToEnqueueStatus;
    }
    return Status::OK();
  }

  template <NonReferenceType F>
  bool EnqueueFunctor(const F& f) {
    return Enqueue(MakeFunctorThreadPoolTask<F, TaskType>(f));
  }

  template <NonReferenceType F>
  bool EnqueueFunctor(F&& f) {
    return Enqueue(MakeFunctorThreadPoolTask<F, TaskType>(std::move(f)));
  }

  // Matches the interface of yb::ThreadPool::Submit.
  template <NonReferenceType F>
  Status Submit(const F& f) {
    return EnqueueWithStatus(MakeFunctorThreadPoolTask<F, TaskType>(f));
  }
};

class YBThreadPool : public TaskRecipient<ThreadPoolTask> {
 public:
  explicit YBThreadPool(ThreadPoolOptions options);

  template <class... Args>
  explicit YBThreadPool(Args&&... args)
      : YBThreadPool(ThreadPoolOptions{std::forward<Args>(args)...}) {
  }

  virtual ~YBThreadPool();

  YBThreadPool(YBThreadPool&& rhs) noexcept;
  YBThreadPool& operator=(YBThreadPool&& rhs) noexcept;

  const ThreadPoolOptions& options() const;

  bool Enqueue(ThreadPoolTask* task) override;

  void Shutdown();

  bool Owns(Thread* thread);
  bool OwnsThisThread();
  bool BusyWait(MonoTime deadline);

  size_t NumWorkers() const;
  bool Idle() const;

  // Used to disable detailed logging in pggate thread pools.
  static void DisableDetailedLogging();

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

// Base class for our two equivalents of ThreadPoolToken of yb::ThreadPool (Strand for serial token
// and ThreadSubPool for concurrent token). Has common logic for maintaining the closing state and
// waiting for shutdown.
class ThreadSubPoolBase {
 public:
  explicit ThreadSubPoolBase(YBThreadPool* thread_pool) : thread_pool_(*thread_pool) {}
  virtual ~ThreadSubPoolBase() {}

  // Shut down the strand and wait for running tasks to finish. Concurrent calls to this function
  // are OK, and each of them will wait for all tasks.
  void Shutdown();

 protected:
  static constexpr size_t kStopMark = 1ULL << 48;
  static constexpr size_t kStoppedMark = 1ULL << 63;

  bool Closing() const {
    return active_enqueues_.load(std::memory_order_relaxed) >= kStopMark;
  }

  virtual void AbortTasks();

  template <class Functor>
  bool EnqueueHelper(const Functor& functor) {
    if (Closing()) {
      return functor(false);
    }
    auto se = ScopeExit([this] {
      --active_enqueues_;
    });
    if (++active_enqueues_ >= kStopMark) {
      return functor(false);
    }
    return functor(true);
  }

  YBThreadPool& thread_pool_;

  // Number of active tasks. This field is managed differently in the two subclasses.
  std::shared_ptr<void> run_token_ = std::make_shared<int>();

  std::atomic<size_t> active_enqueues_{0};
};

class ThreadSubPool : public ThreadSubPoolBase, public TaskRecipient<ThreadPoolTask> {
 public:
  explicit ThreadSubPool(YBThreadPool* thread_pool);
  virtual ~ThreadSubPool();

  bool Enqueue(ThreadPoolTask* task) override;
};

} // namespace yb
