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

#include "yb/rpc/thread_pool.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <boost/intrusive/list.hpp>

#include <cds/container/basket_queue.h>
#include <cds/gc/dhp.h>

#include "yb/util/flags.h"
#include "yb/util/lockfree.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;

DEFINE_NON_RUNTIME_uint64(default_idle_timeout_ms, 15000,
    "Default RPC ThreadPool idle timeout value in milliseconds");

namespace yb::rpc {

namespace {

class Worker;

using TaskQueue = cds::container::BasketQueue<cds::gc::DHP, ThreadPoolTask*>;
using WaitingWorkers = LockFreeStack<Worker>;

struct ThreadPoolShare {
  const ThreadPoolOptions options;
  TaskQueue task_queue;
  WaitingWorkers waiting_workers;
  std::atomic<size_t> num_workers{0};

  explicit ThreadPoolShare(ThreadPoolOptions o)
      : options(std::move(o)) {}
};

const auto kShuttingDownStatus = STATUS(Aborted, "Service is shutting down");

void TaskDone(ThreadPoolTask* task, const Status& status) {
  // We have to retrieve the subpool pointer from the task before calling Done, because that call
  // may destroy the task.
  auto* sub_pool = task->sub_pool();
  task->Done(status);
  if (sub_pool) {
    sub_pool->OnTaskDone(task, status);
  }
}

// There is difference between idle stop and external stop.
// In case of idle stop we don't perform join on thread, since it is already detached.
YB_DEFINE_ENUM(WorkerState, (kRunning)(kWaitingTask)(kIdleStop)(kExternalStop));

class Worker : public boost::intrusive::list_base_hook<> {
 public:
  explicit Worker(ThreadPoolShare& share)
      : share_(share) {
  }

  Status Start(size_t index, ThreadPoolTask* task) {
    auto name = strings::Substitute("$0_$1_worker", share_.options.name, index);
    return Thread::Create(share_.options.name, name, &Worker::Execute, this, task, &thread_);
  }

  ~Worker() {
    {
      std::lock_guard lock(mutex_);
      DCHECK(!task_);
      if (state_ == WorkerState::kIdleStop) {
        return;
      }
    }
    if (thread_) {
      thread_->Join();
    }
  }

  Worker(const Worker& worker) = delete;
  void operator=(const Worker& worker) = delete;

  void Stop() {
    ThreadPoolTask* task;
    {
      std::lock_guard lock(mutex_);
      if (state_ != WorkerState::kIdleStop) {
        state_ = WorkerState::kExternalStop;
      }
      task = std::exchange(task_, nullptr);
      cond_.notify_one();
    }
    if (task) {
      TaskDone(task, kShuttingDownStatus);
    }
  }

  WorkerState Notify(ThreadPoolTask* task) {
    std::lock_guard lock(mutex_);
    added_to_waiting_workers_ = false;
    // There could be cases when we popped task after adding ourselves to worker queue (see below).
    // So we are already processing task, but reside in worker queue.
    if (state_ == WorkerState::kWaitingTask) {
      DCHECK(!task_);
      task_ = task;
      cond_.notify_one();
    }
    return state_;
  }

 private:
  // Our main invariant is empty task queue or empty worker queue.
  // In other words, one of those queues should be empty.
  // Meaning that we do not have work (task queue empty) or
  // does not have free hands (worker queue empty)
  void Execute(ThreadPoolTask* task) {
    Thread::current_thread()->SetUserData(&share_);
    while (task) {
      task->Run();
      TaskDone(task, Status::OK());
      task = PopTask();
    }
  }

  ThreadPoolTask* PopTask() {
    // First of all we try to get already queued task, w/o locking.
    // If there is no task, so we could go to waiting state.
    ThreadPoolTask* task;
    if (share_.task_queue.pop(task)) {
      return task;
    }

    UniqueLock lock(mutex_);
    if (auto task_opt = DoPopTask()) {
      return *task_opt;
    }

    state_ = WorkerState::kWaitingTask;

    for (;;) {
      AddToWaitingWorkers();

      bool timeout;
      if (share_.options.idle_timeout) {
        CHECK(!task_);
        auto duration = share_.options.idle_timeout.ToSteadyDuration();
        timeout = cond_.wait_for(GetLockForCondition(lock), duration) == std::cv_status::timeout;
      } else {
        cond_.wait(GetLockForCondition(lock));
        timeout = false;
      }

      if (auto task_opt = DoPopTask()) {
        state_ = WorkerState::kRunning;
        return *task_opt;
      }

      if (timeout && added_to_waiting_workers_) {
        --share_.num_workers;
        state_ = WorkerState::kIdleStop;
        auto thread = std::move(thread_);
        return nullptr;
      }
    }
  }

  std::optional<ThreadPoolTask*> DoPopTask() REQUIRES(mutex_) {
    if (state_ == WorkerState::kExternalStop) {
      return nullptr;
    }
    if (task_) {
      return std::exchange(task_, nullptr);
    }
    ThreadPoolTask* task;
    if (share_.task_queue.pop(task)) {
      return task;
    }
    return std::nullopt;
  }

  void AddToWaitingWorkers() REQUIRES(mutex_) {
    if (!added_to_waiting_workers_) {
      share_.waiting_workers.Push(this);
      added_to_waiting_workers_ = true;
    }
  }

  friend void SetNext(Worker* worker, Worker* next) {
    worker->next_waiting_worker_ = next;
  }

  friend Worker* GetNext(Worker* worker) {
    return worker->next_waiting_worker_;
  }

  ThreadPoolShare& share_;
  scoped_refptr<Thread> thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
  WorkerState state_ GUARDED_BY(mutex_) = WorkerState::kRunning;
  bool added_to_waiting_workers_ GUARDED_BY(mutex_) = false;
  ThreadPoolTask* task_ GUARDED_BY(mutex_) = nullptr;
  Worker* next_waiting_worker_ = nullptr;
};

using Workers = boost::intrusive::list<Worker>;

} // namespace

class ThreadPool::Impl {
 public:
  explicit Impl(ThreadPoolOptions options)
      : share_(std::move(options)) {
    LOG(INFO) << "Starting thread pool " << share_.options.ToString();
  }

  const ThreadPoolOptions& options() const {
    return share_.options;
  }

  bool Enqueue(ThreadPoolTask* task) {
    ++adding_;
    if (closing_) {
      --adding_;
      TaskDone(task, kShuttingDownStatus);
      return false;
    }
    if (NotifyWorker(task)) {
      --adding_;
      return true;
    }

    if (share_.num_workers++ < share_.options.max_workers) {
      std::lock_guard lock(mutex_);
      if (!closing_) {
        auto new_worker = std::make_unique<Worker>(share_);
        auto status = new_worker->Start(++worker_counter_, task);
        if (status.ok()) {
          workers_.push_back(*new_worker.release());
          --adding_;
          return true;
        } else {
          if (workers_.empty()) {
            LOG_WITH_PREFIX(FATAL) << "Unable to start first worker: " << status;
          } else {
            LOG_WITH_PREFIX(WARNING) << "Unable to start worker: " << status;
          }
        }
      }
    }
    --share_.num_workers;

    bool added = share_.task_queue.push(task);
    DCHECK(added); // BasketQueue always succeed.
    --adding_;
    NotifyWorker(nullptr);
    return true;
  }

  // Returns true if we found worker that will pick up this task, false otherwise.
  bool NotifyWorker(ThreadPoolTask* task) {
    while (auto worker = share_.waiting_workers.Pop()) {
      auto state = worker->Notify(task);
      switch (state) {
        case WorkerState::kWaitingTask:
          return true;
        case WorkerState::kExternalStop: [[fallthrough]];
        case WorkerState::kRunning:
          break;
        case WorkerState::kIdleStop: {
          std::lock_guard lock(mutex_);
          if (!closing_) {
            workers_.erase_and_dispose(
                workers_.iterator_to(*worker), std::default_delete<Worker>());
          }
        } break;
      }
    }
    return false;
  }

  std::string LogPrefix() const {
    return share_.options.name + ": ";
  }

  void Shutdown() {
    // Prevent new worker threads from being created by pretending a large number of workers have
    // already been created.
    share_.num_workers = 1ul << 48;
    decltype(workers_) workers;
    {
      std::lock_guard lock(mutex_);
      if (closing_) {
        CHECK(share_.task_queue.empty());
        CHECK(workers_.empty());
        return;
      }
      closing_ = true;
      workers = std::move(workers_);
    }
    for (auto& worker : workers) {
      worker.Stop();
    }
    // Shutdown is quite a rare situation otherwise, and enqueue is quite frequent.
    // Because of this we use "atomic lock" in enqueue and busy wait in shutdown.
    // So we could process enqueue quickly, and it is OK if we get stuck in shutdown for some time.
    while (adding_ != 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    {
      std::lock_guard lock(mutex_);
      if (!workers_.empty()) {
        LOG_WITH_PREFIX(DFATAL) << "Workers were added while closing: " << workers_.size();
        workers_.clear();
      }
    }
    ThreadPoolTask* task = nullptr;
    while (share_.task_queue.pop(task)) {
      TaskDone(task, kShuttingDownStatus);
    }

    workers.clear_and_dispose(std::default_delete<Worker>());
  }

  bool Owns(Thread* thread) {
    return thread && thread->user_data() == &share_;
  }

 private:
  ThreadPoolShare share_;
  Workers workers_ GUARDED_BY(mutex_);
  // An atomic counterpart of workers_.size() during normal operation. During shutdown, this is set
  // to an unrealistically high number to prevent new workers from being created.
  std::atomic<size_t> worker_counter_{0};
  std::mutex mutex_;
  std::atomic<bool> closing_ = {false};
  std::atomic<size_t> adding_ = {0};
};

// ------------------------------------------------------------------------------------------------

ThreadPool::ThreadPool(ThreadPoolOptions options)
    : impl_(new Impl(std::move(options))) {
}

ThreadPool::ThreadPool(ThreadPool&& rhs) noexcept
    : impl_(std::move(rhs.impl_)) {}

ThreadPool& ThreadPool::operator=(ThreadPool&& rhs) noexcept {
  impl_->Shutdown();
  impl_ = std::move(rhs.impl_);
  return *this;
}

ThreadPool::~ThreadPool() {
  if (impl_) {
    impl_->Shutdown();
  }
}

bool ThreadPool::Enqueue(ThreadPoolTask* task) {
  return impl_->Enqueue(task);
}

void ThreadPool::Shutdown() {
  impl_->Shutdown();
}

const ThreadPoolOptions& ThreadPool::options() const {
  return impl_->options();
}

bool ThreadPool::Owns(Thread* thread) {
  return impl_->Owns(thread);
}

bool ThreadPool::OwnsThisThread() {
  return Owns(Thread::current_thread());
}

// ------------------------------------------------------------------------------------------------
// ThreadSubPoolBase
// ------------------------------------------------------------------------------------------------

void ThreadSubPoolBase::Shutdown() {
  closing_ = true;
  // We expected shutdown to happen rarely, so just use busy wait here.
  BusyWait();
}

void ThreadSubPoolBase::BusyWait() {
  while (!IsIdle()) {
    std::this_thread::sleep_for(1ms);
  }
}

bool ThreadSubPoolBase::IsIdle() {
  // Use sequential consistency for safety. This is only used during shutdown.
  return active_tasks_ == 0;
}

// ------------------------------------------------------------------------------------------------
// ThreadSubPool
// ------------------------------------------------------------------------------------------------

ThreadSubPool::ThreadSubPool(ThreadPool* thread_pool) : ThreadSubPoolBase(thread_pool) {
}

ThreadSubPool::~ThreadSubPool() {
}

bool ThreadSubPool::Enqueue(ThreadPoolTask* task) {
  if (closing_.load(std::memory_order_acquire)) {
    task->Done(STATUS(Aborted, "Thread sub-pool closing"));
    return false;
  }
  active_tasks_.fetch_add(1, std::memory_order_acq_rel);
  task->set_sub_pool(this);
  return thread_pool_.Enqueue(task);
}

void ThreadSubPool::OnTaskDone(ThreadPoolTask* task, const Status& status) {
  active_tasks_.fetch_sub(1, std::memory_order_acq_rel);
}

MonoDelta DefaultIdleTimeout() {
  return MonoDelta::FromMilliseconds(FLAGS_default_idle_timeout_ms);
}

} // namespace yb::rpc
