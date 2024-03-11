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

#pragma once

#include "yb/rpc/thread_pool.h"

#include "yb/util/lockfree.h"
#include "yb/util/status.h"

namespace yb {
namespace rpc {

class StrandTask : public MPSCQueueEntry<StrandTask>, public ThreadPoolTask {
 protected:
  ~StrandTask() = default;
};

template <class F>
FunctorThreadPoolTask<F, StrandTask>* MakeFunctorStrandTask(const F& f) {
  return new FunctorThreadPoolTask<F, StrandTask>(f);
}

template <class F>
FunctorThreadPoolTask<F, StrandTask>* MakeFunctorStrandTask(F&& f) {
  return new FunctorThreadPoolTask<F, StrandTask>(std::move(f));
}

template <class F, class ErrorFunc>
class StrandTaskWithErrorFunc : public StrandTask {
 public:
  explicit StrandTaskWithErrorFunc(
      const F& f,
      const ErrorFunc& error_handler) : f_(f), error_handler_(error_handler) {}
  explicit StrandTaskWithErrorFunc(
      F&& f, ErrorFunc&& error_handler)
      : f_(std::move(f)),
        error_handler_(std::move(error_handler)) {}

  virtual ~StrandTaskWithErrorFunc() = default;

 private:
  void Run() override {
    f_();
  }

  void Done(const Status& status) override {
    if (!status.ok()) {
      error_handler_(status);
    }
    delete this;
  }

  F f_;
  ErrorFunc error_handler_;
};

// Strand prevent concurrent execution of enqueued tasks.
// If task is submitted into strand and it already has enqueued tasks, new task will be executed
// after all previously enqueued tasks.
//
// Submitted task should inherit StrandTask or wrapped by class that provides such inheritance.
class Strand : public ThreadPoolTask {
 public:
  explicit Strand(ThreadPool* thread_pool);
  virtual ~Strand();

  void Enqueue(StrandTask* task);

  template <class F>
  void EnqueueFunctor(const F& f) {
    Enqueue(MakeFunctorStrandTask(f));
  }

  template <class F>
  void EnqueueFunctor(F&& f) {
    Enqueue(MakeFunctorStrandTask(std::move(f)));
  }

  // Shut down the strand and wait for running tasks to finish. Concurrent calls to this function
  // are OK, and each of them will wait for the tasks.
  void Shutdown();

  // Wait for running tasks to complete. This uses a "busy wait" approach of waiting for 1ms in a
  // loop. It is suitable for use during shutdown and in tests.
  void BusyWait();

 private:
  void Run() override;

  void Done(const Status& status) override;

  void ProcessTasks(const Status& status, bool allow_closing);

  ThreadPool& thread_pool_;
  std::atomic<size_t> active_tasks_{0};
  MPSCQueue<StrandTask> queue_;
  std::atomic<bool> running_{false};
  std::atomic<bool> closing_{false};
};

} // namespace rpc
} // namespace yb
