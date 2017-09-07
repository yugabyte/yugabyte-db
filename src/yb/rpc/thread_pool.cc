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

#include <condition_variable>
#include <mutex>
#include <thread>

#include <boost/lockfree/queue.hpp>
#include <boost/scope_exit.hpp>

#include "yb/util/thread.h"

namespace yb {
namespace rpc {

namespace {

class Worker;

typedef boost::lockfree::queue<ThreadPoolTask*> TaskQueue;
typedef boost::lockfree::queue<Worker*> WaitingWorkers;

struct ThreadPoolShare {
  ThreadPoolOptions options;
  TaskQueue task_queue;
  WaitingWorkers waiting_workers;

  explicit ThreadPoolShare(ThreadPoolOptions o)
      : options(std::move(o)),
        task_queue(options.queue_limit),
        waiting_workers(options.max_workers) {
  }
};

class Worker {
 public:
  explicit Worker(ThreadPoolShare* share, size_t index)
      : share_(share) {
    auto name = strings::Substitute("rpc_tp_$0_$1", share_->options.name, index);
    CHECK_OK(yb::Thread::Create("rpc_thread_pool", name, &Worker::Execute, this, &thread_));
  }

  ~Worker() {
    thread_->Join();
  }

  Worker(const Worker& worker) = delete;
  void operator=(const Worker& worker) = delete;

  void Stop() {
    stop_requested_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    cond_.notify_one();
  }

  bool Notify() {
    std::lock_guard<std::mutex> lock(mutex_);
    added_to_waiting_workers_ = false;
    // There could be cases when we popped task after adding ourselves to worker queue (see below).
    // So we are already processing task, but reside in worker queue.
    // To handle this case we use waiting_task_ flag.
    // If we don't wait task, we return false here, and next worker would be popped from queue
    // and notified.
    if (!waiting_task_) {
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
    while (!stop_requested_) {
      ThreadPoolTask* task = nullptr;
      if (PopTask(&task)) {
        task->Run();
        task->Done(Status::OK());
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
    waiting_task_ = true;
    BOOST_SCOPE_EXIT(&waiting_task_) {
        waiting_task_ = false;
    } BOOST_SCOPE_EXIT_END;

    while (!stop_requested_) {
      AddToWaitingWorkers();

      // There could be situation, when task was queued before we added ourselves to
      // the worker queue. So worker queue could be empty in this case, and nobody was notified
      // about new task. So we check there for this case. This technique is similar to
      // double check.
      if (share_->task_queue.pop(*task)) {
        return true;
      }

      cond_.wait(lock);

      // Sometimes another worker could steal task before we wake up. In this case we will
      // just enqueue ourselves back.
      if (share_->task_queue.pop(*task)) {
        return true;
      }
    }
    return false;
  }

  void AddToWaitingWorkers() {
    if (!added_to_waiting_workers_) {
      auto pushed = share_->waiting_workers.bounded_push(this);
      CHECK(pushed);
      added_to_waiting_workers_ = true;
    }
  }

  ThreadPoolShare* share_;
  scoped_refptr<yb::Thread> thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<bool> stop_requested_ = {false};
  bool waiting_task_ = false;
  bool added_to_waiting_workers_ = false;
};

} // namespace

class ThreadPool::Impl {
 public:
  explicit Impl(ThreadPoolOptions options)
      : share_(std::move(options)),
        queue_full_status_(STATUS_SUBSTITUTE(ServiceUnavailable,
                                             "Queue is full, max items: $0",
                                             share_.options.queue_limit)) {
    workers_.reserve(share_.options.max_workers);
    while (workers_.size() != share_.options.max_workers) {
      workers_.emplace_back(nullptr);
    }
  }

  const ThreadPoolOptions& options() const {
    return share_.options;
  }

  bool Enqueue(ThreadPoolTask* task) {
    ++adding_;
    if (closing_) {
      --adding_;
      task->Done(shutdown_status_);
      return false;
    }
    bool added = share_.task_queue.bounded_push(task);
    --adding_;
    if (!added) {
      task->Done(queue_full_status_);
      return false;
    }
    Worker* worker = nullptr;
    while (share_.waiting_workers.pop(worker)) {
      if (worker->Notify()) {
        return true;
      }
    }

    // We increment created_workers_ every time, the first max_worker increments would produce
    // a new worker. And after that, we will just increment it doing nothing after that.
    // So we could be lock free here.
    auto index = created_workers_++;
    if (index < share_.options.max_workers) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!closing_) {
        workers_[index].reset(new Worker(&share_, index));
      }
    } else {
      --created_workers_;
    }
    return true;
  }

  void Shutdown() {
    // Block creating new workers.
    created_workers_ += workers_.size();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closing_) {
        CHECK(share_.task_queue.empty());
        CHECK(workers_.empty());
        return;
      }
      closing_ = true;
    }
    for (auto& worker : workers_) {
      if (worker) {
        worker->Stop();
      }
    }
    workers_.clear();
    // Shutdown is quite rare situation otherwise enqueue is quite frequent.
    // Because of this we use "atomic lock" in enqueue and busy wait in shutdown.
    // So we could process enqueue quickly, and stuck in shutdown for sometime.
    while(adding_ != 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ThreadPoolTask* task = nullptr;
    while (share_.task_queue.pop(task)) {
      task->Done(shutdown_status_);
    }
  }

 private:
  ThreadPoolShare share_;
  std::vector<std::unique_ptr<Worker>> workers_;
  std::atomic<size_t> created_workers_ = {0};
  std::mutex mutex_;
  std::atomic<bool> closing_ = {false};
  std::atomic<size_t> adding_ = {0};
  const Status shutdown_status_ = STATUS(Aborted, "Service is shutting down");
  const Status queue_full_status_;
};

ThreadPool::ThreadPool(ThreadPoolOptions options)
    : impl_(new Impl(std::move(options))) {
}

ThreadPool::ThreadPool(ThreadPool&& rhs)
    : impl_(std::move(rhs.impl_)) {}

ThreadPool& ThreadPool::operator=(ThreadPool&& rhs) {
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

} // namespace rpc
} // namespace yb
