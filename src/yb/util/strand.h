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

#include "yb/util/lockfree.h"
#include "yb/util/status.h"
#include "yb/util/thread_pool.h"

namespace yb {

class StrandTask : public MPSCQueueEntry<StrandTask> {
 public:
  // Invoked in thread pool
  virtual void Run() = 0;

  // When the thread pool done with a task, i.e. it completed or failed, it invokes Done.
  virtual void Done(const Status& status) = 0;

  virtual ~StrandTask() = default;

 private:
  friend void SetNext(StrandTask& entry, StrandTask* next) {
    entry.next_ = next;
  }

  friend StrandTask* GetNext(const StrandTask& entry) {
    return entry.next_;
  }

  StrandTask* next_ = nullptr;
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
class Strand : public ThreadSubPoolBase,
               public TaskRecipient<StrandTask> {
 public:
  explicit Strand(YBThreadPool* thread_pool);
  virtual ~Strand();

  bool Enqueue(StrandTask* task) override;

  void TEST_BusyWait();

 private:
  void AbortTasks() override;

  class Task;
  friend class Task;

  std::unique_ptr<Task> task_;
};

} // namespace yb
