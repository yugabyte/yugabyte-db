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

#include "yb/util/priority_thread_pool.h"

#include <mutex>

#include <boost/container/stable_vector.hpp>
#include <boost/thread/reverse_lock.hpp>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/locks.h"
#include "yb/util/priority_queue.h"
#include "yb/util/thread.h"

using namespace std::placeholders;

namespace yb {

namespace {

Status abort_status = STATUS(Aborted, "Priority thread pool shutdown");

typedef std::unique_ptr<PriorityThreadPoolTask> TaskPtr;
class PriorityThreadPoolWorker;
typedef std::function<TaskPtr(PriorityThreadPoolWorker*)> TaskPicker;

class PriorityThreadPoolWorker {
 public:
  explicit PriorityThreadPoolWorker(TaskPicker pick_task)
      : pick_task_(std::move(pick_task)) {
  }

  // Perform provided task in this worker. Called on a fresh worker, or worker that was just picked
  // from the free workers list.
  void Perform(TaskPtr task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      DCHECK(!task_);
      if (!stopped_) {
        task_.swap(task);
      }
    }
    cond_.notify_one();
    if (task) {
      task->Run(abort_status);
    }
  }

  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopped_) {
      if (!task_) {
        cond_.wait(lock);
        continue;
      }
      auto task = std::move(task_);
      yb::ReverseLock<decltype(lock)> rlock(lock);
      for (;;) {
        task->Run(Status::OK());
        task = pick_task_(this);
        if (!task) {
          break;
        }
      }
    }
    DCHECK(!task_);
  }

  void Stop() {
    TaskPtr task;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
      task = std::move(task_);
    }
    cond_.notify_one();
    if (task) {
      task->Run(abort_status);
    }
  }

 private:
  TaskPicker pick_task_;
  yb::ThreadPtr thread_;
  std::mutex mutex_;
  std::condition_variable cond_;
  bool stopped_ = false;
  TaskPtr task_;
};

class PriorityTaskComparator {
 public:
  bool operator()(const TaskPtr& lhs, const TaskPtr& rhs) const {
    auto lhs_priority = lhs->Priority();
    auto rhs_priority = rhs->Priority();
    // The task with highest priority is picked first, if priorities are equal the task that
    // was added earlier is picked.
    return lhs_priority < rhs_priority ||
           (lhs_priority == rhs_priority && lhs->SerialNo() > rhs->SerialNo());
  }
};

std::atomic<size_t> task_serial_no_(0);

} // namespace

PriorityThreadPoolTask::PriorityThreadPoolTask()
    : serial_no_(task_serial_no_.fetch_add(1, std::memory_order_acq_rel)) {
}

// Maintains priority queue of added tasks and vector of free workers.
// One of them is always empty.
//
// When a task is added it tries to pick free worker or launch a new one.
// If it is impossible the task is added to task priority queue.
//
// When worker finishes it tries to pick task from priority queue.
// If queue is empty worker is added to vector of free workers.
class PriorityThreadPool::Impl {
 public:
  explicit Impl(size_t max_threads) : max_threads_(max_threads) {
    CHECK_GE(max_threads, 1);
  }

  ~Impl() {
    StartShutdown();
    CompleteShutdown();
  }

  std::unique_ptr<PriorityThreadPoolTask> Submit(TaskPtr task) {
    PriorityThreadPoolWorker* worker = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!stopping_.load(std::memory_order_acquire)) {
        if (!free_workers_.empty()) {
          worker = free_workers_.back();
          free_workers_.pop_back();
          VLOG(4) << "Has free worker: " << worker;
        } else if (workers_.size() < max_threads_) {
          workers_.emplace_back(std::bind(&Impl::PickTask, this, _1));
          worker = &workers_.back();
          auto thread = yb::Thread::Make(
              "priority_thread_pool", "worker",
              std::bind(&PriorityThreadPoolWorker::Run, worker));
          if (!thread.ok()) {
            LOG(WARNING) << "Failed to launch new worker: " << thread.status();
            workers_.pop_back();
            worker = nullptr;
            if (!workers_.empty()) {
              queue_.Push(std::move(task));
              return nullptr;
            }
          } else {
            threads_.push_back(std::move(*thread));
            VLOG(3) << "Created new worker: " << worker;
          }
        } else {
          queue_.Push(std::move(task));
          VLOG(4) << "Added to queue";
          return nullptr;
        }
      }
    }

    if (worker) {
      VLOG(4) << "Passing task to worker: " << worker;
      worker->Perform(std::move(task));
    } else {
      VLOG(3) << "Task rejected";
      DCHECK(task);
    }

    return task;
  }

  void Remove(void* key) {
    std::vector<TaskPtr> removed_tasks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.RemoveIf([key, &removed_tasks](TaskPtr* task) {
        if ((**task).BelongsTo(key)) {
          removed_tasks.push_back(std::move(*task));
          return true;
        }
        return false;
      });
    }
    for (const auto& task : removed_tasks) {
      task->Run(abort_status);
    }
  }

  void StartShutdown() {
    if (!stopping_.exchange(true, std::memory_order_acq_rel)) {
      std::vector<TaskPtr> queue;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.Swap(&queue);
      }
      for (auto& task : queue) {
        task->Run(abort_status);
      }
    }
  }

  void CompleteShutdown() {
    decltype(threads_) threads;
    decltype(workers_) workers;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      threads.swap(threads_);
      workers.swap(workers_);
    }
    for (auto& worker : workers) {
      worker.Stop();
    }
    for (auto& thread : threads) {
      thread->Join();
    }
  }

 private:
  TaskPtr PickTask(PriorityThreadPoolWorker* worker) {
    TaskPtr result;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) {
        VLOG(4) << "No task for " << worker;
        free_workers_.push_back(worker);
        return nullptr;
      }
      result = queue_.Pop();
      VLOG(4) << "Picked task for " << worker;
    }
    return result;
  }

  const size_t max_threads_;
  std::mutex mutex_;
  PriorityQueue<TaskPtr, std::vector<TaskPtr>, PriorityTaskComparator> queue_ GUARDED_BY(mutex_);
  std::vector<yb::ThreadPtr> threads_ GUARDED_BY(mutex_);
  boost::container::stable_vector<PriorityThreadPoolWorker> workers_ GUARDED_BY(mutex_);
  std::vector<PriorityThreadPoolWorker*> free_workers_ GUARDED_BY(mutex_);
  std::atomic<bool> stopping_{false};
};

PriorityThreadPool::PriorityThreadPool(size_t max_threads) : impl_(new Impl(max_threads)) {
}

PriorityThreadPool::~PriorityThreadPool() {
}

std::unique_ptr<PriorityThreadPoolTask> PriorityThreadPool::Submit(
    std::unique_ptr<PriorityThreadPoolTask> task) {
  return impl_->Submit(std::move(task));
}

void PriorityThreadPool::Remove(void* key) {
  impl_->Remove(key);
}

void PriorityThreadPool::StartShutdown() {
  impl_->StartShutdown();
}

void PriorityThreadPool::CompleteShutdown() {
  impl_->CompleteShutdown();
}

} // namespace yb
