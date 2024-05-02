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

#pragma once

#include <concepts>
#include <memory>
#include <string>

#include "yb/gutil/port.h"

#include "yb/util/status.h"
#include "yb/util/tostring.h"
#include "yb/util/type_traits.h"

namespace yb {

class Status;
class Thread;

namespace rpc {

class ThreadSubPoolBase;

class ThreadPoolTask {
 public:
  // Invoked in thread pool
  virtual void Run() = 0;

  // When the thread pool done with a task, i.e. it completed or failed, it invokes Done.
  virtual void Done(const Status& status) = 0;

  ThreadSubPoolBase* sub_pool() const { return sub_pool_; }
  void set_sub_pool(ThreadSubPoolBase* sub_pool) { sub_pool_ = sub_pool; }

 protected:
  virtual ~ThreadPoolTask() {}

  // If there is a sub-pool set on the task, the thread pool will also notify the sub-pool about
  // the task completion.
  ThreadSubPoolBase *sub_pool_ = nullptr;
};

template <typename T>
concept ThreadPoolTaskOrSubclass = requires (T* t) {
  std::is_base_of_v<ThreadPoolTask, T>;
};  // NOLINT

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

struct ThreadPoolOptions {
  std::string name;

  size_t max_workers;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(name, max_workers);
  }

  static constexpr auto kUnlimitedWorkers = std::numeric_limits<decltype(max_workers)>::max();
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

class ThreadPool : public TaskRecipient<ThreadPoolTask> {
 public:
  explicit ThreadPool(ThreadPoolOptions options);

  template <class... Args>
  explicit ThreadPool(Args&&... args)
      : ThreadPool(ThreadPoolOptions{std::forward<Args>(args)...}) {
  }

  virtual ~ThreadPool();

  ThreadPool(ThreadPool&& rhs) noexcept;
  ThreadPool& operator=(ThreadPool&& rhs) noexcept;

  const ThreadPoolOptions& options() const;

  virtual bool Enqueue(ThreadPoolTask* task);

  void Shutdown();

  static bool IsCurrentThreadRpcWorker();

  bool Owns(Thread* thread);
  bool OwnsThisThread();

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

// Base class for our two equivalents of ThreadPoolToken of yb::ThreadPool (Strand for serial token
// and ThreadSubPool for concurrent token). Has common logic for maintaining the closing state and
// waiting for shutdown.
class ThreadSubPoolBase {
 public:
  explicit ThreadSubPoolBase(ThreadPool* thread_pool) : thread_pool_(*thread_pool) {}
  virtual ~ThreadSubPoolBase() {}

  // Shut down the strand and wait for running tasks to finish. Concurrent calls to this function
  // are OK, and each of them will wait for all tasks.
  void Shutdown();

  // Wait for running tasks to complete. This uses a "busy wait" approach of waiting for 1ms in a
  // loop. It is suitable for use during shutdown and in tests.
  void BusyWait();

  // This is called by the thread pool when a task finishes executing.
  virtual void OnTaskDone(ThreadPoolTask* task, const Status& status) = 0;

  size_t num_active_tasks() { return active_tasks_; }

 protected:
  // Used by BusyWait to determine if all the tasks have completed.
  virtual bool IsIdle();

  ThreadPool& thread_pool_;

  // Number of active tasks. This field is managed differently in the two subclasses.
  std::atomic<size_t> active_tasks_{0};

  std::atomic<bool> closing_{false};
};

class ThreadSubPool : public ThreadSubPoolBase, public TaskRecipient<ThreadPoolTask> {
 public:
  explicit ThreadSubPool(ThreadPool* thread_pool);
  virtual ~ThreadSubPool();

  bool Enqueue(ThreadPoolTask* task) override;
  void OnTaskDone(ThreadPoolTask* task, const Status& status) override;
};

} // namespace rpc
} // namespace yb
