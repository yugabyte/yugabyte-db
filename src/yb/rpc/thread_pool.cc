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

#include <cds/container/basket_queue.h>
#include <cds/gc/dhp.h>

#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"

using namespace std::literals;

namespace yb {
namespace rpc {

namespace {

class Worker;

typedef cds::container::BasketQueue<cds::gc::DHP, ThreadPoolTask*> TaskQueue;
typedef cds::container::BasketQueue<cds::gc::DHP, Worker*> WaitingWorkers;

struct ThreadPoolShare {
  const ThreadPoolOptions options;
  TaskQueue task_queue;
  WaitingWorkers waiting_workers;

  explicit ThreadPoolShare(ThreadPoolOptions o)
      : options(std::move(o)) {}
};

const char* kRpcThreadCategory = "rpc_thread_pool";

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

class Worker {
 public:
  explicit Worker(ThreadPoolShare* share)
      : share_(share) {
  }

  Status Start(size_t index) {
    auto name = strings::Substitute("rpc_tp_$0_$1", share_->options.name, index);
    return yb::Thread::Create(kRpcThreadCategory, name, &Worker::Execute, this, &thread_);
  }

  ~Worker() {
    if (thread_) {
      thread_->Join();
    }
  }

  Worker(const Worker& worker) = delete;
  void operator=(const Worker& worker) = delete;

  void Stop() {
    stop_requested_ = true;
    std::lock_guard lock(mutex_);
    cond_.notify_one();
  }

  bool Notify() {
    std::lock_guard lock(mutex_);
    added_to_waiting_workers_ = false;
    // There could be cases when we popped task after adding ourselves to worker queue (see below).
    // So we are already processing task, but reside in worker queue.
    // To handle this case we use waiting_for_task_ flag.
    // If we are not waiting for a task, we return false here, and next worker would be popped from
    // queue and notified.
    if (!waiting_for_task_) {
      return false;
    }
    cond_.notify_one();
    return true;
  }

 private:
  // Our main invariant is empty task queue or empty worker queue.
  // In other words, one of those queues should be empty.
  // Meaning that we does not have work (task queue empty) or
  // does not have free hands (worker queue empty)
  void Execute() {
    Thread::current_thread()->SetUserData(share_);
    while (!stop_requested_) {
      ThreadPoolTask* task = nullptr;
      if (PopTask(&task)) {
        task->Run();
        TaskDone(task, Status::OK());
      }
    }
  }

  bool PopTask(ThreadPoolTask** task) {
    // First of all we try to get already queued task, w/o locking.
    // If there is no task, so we could go to waiting state.
    if (share_->task_queue.pop(*task)) {
      return true;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    waiting_for_task_ = true;
    auto se = ScopeExit([this] {
      waiting_for_task_ = false;
    });

    while (!stop_requested_) {
      AddToWaitingWorkers();

      // There could be a situation when a task was queued before we added ourselves to the worker
      // queue. So the worker queue could be empty in this case, while nobody was notified about
      // the new task. So we check there for this case. This technique is similar to
      // double-checked locking.
      if (share_->task_queue.pop(*task)) {
        return true;
      }

      cond_.wait(lock);

      // Sometimes another worker could steal a task before we wake up. In this case we will
      // just enqueue ourselves back.
      if (share_->task_queue.pop(*task)) {
        return true;
      }
    }
    return false;
  }

  void AddToWaitingWorkers() {
    if (!added_to_waiting_workers_) {
      auto pushed = share_->waiting_workers.push(this);
      DCHECK(pushed); // BasketQueue always succeed.
      added_to_waiting_workers_ = true;
    }
  }

  ThreadPoolShare* share_;
  scoped_refptr<yb::Thread> thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<bool> stop_requested_ = {false};
  bool waiting_for_task_ = false;
  bool added_to_waiting_workers_ = false;
};

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
    bool added = share_.task_queue.push(task);
    DCHECK(added); // BasketQueue always succeed.
    Worker* worker = nullptr;
    while (share_.waiting_workers.pop(worker)) {
      if (worker->Notify()) {
        --adding_;
        return true;
      }
    }
    --adding_;

    auto index = num_workers_++;
    if (index < share_.options.max_workers) {
      std::lock_guard lock(mutex_);
      if (!closing_) {
        auto new_worker = std::make_unique<Worker>(&share_);
        auto status = new_worker->Start(workers_.size());
        if (status.ok()) {
          workers_.push_back(std::move(new_worker));
        } else {
          if (workers_.empty()) {
            LOG(FATAL) << "Unable to start first worker: " << status;
          } else {
            LOG(WARNING) << "Unable to start worker: " << status;
          }
          --num_workers_;
        }
      }
    } else {
      --num_workers_;
    }
    return true;
  }

  void Shutdown() {
    // Prevent new worker threads from being created by pretending a large number of workers have
    // already been created.
    num_workers_ = 1ul << 48;
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
      if (worker) {
        worker->Stop();
      }
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
        LOG(DFATAL) << "Workers were added while closing: " << workers_.size();
        workers_.clear();
      }
    }
    ThreadPoolTask* task = nullptr;
    while (share_.task_queue.pop(task)) {
      TaskDone(task, kShuttingDownStatus);
    }
  }

  bool Owns(Thread* thread) {
    return thread && thread->user_data() == &share_;
  }

 private:
  ThreadPoolShare share_;
  std::vector<std::unique_ptr<Worker>> workers_ GUARDED_BY(mutex_);
  // An atomic counterpart of workers_.size() during normal operation. During shutdown, this is set
  // to an unrealistically high number to prevent new workers from being created.
  std::atomic<size_t> num_workers_ = {0};
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

bool ThreadPool::IsCurrentThreadRpcWorker() {
  const Thread* thread = Thread::current_thread();
  return thread != nullptr && thread->category() == kRpcThreadCategory;
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

} // namespace rpc
} // namespace yb
