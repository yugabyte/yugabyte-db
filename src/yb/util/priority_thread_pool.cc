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
#include <queue>
#include <set>

#include <boost/container/stable_vector.hpp>
#include <boost/scope_exit.hpp>
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

static constexpr int kEmptyQueuePriority = -1;

YB_STRONGLY_TYPED_BOOL(PickTask);

class PriorityThreadPoolWorkerContext {
 public:
  virtual void PauseIfNecessary(PriorityThreadPoolWorker* worker) = 0;
  virtual bool WorkerFinished(PriorityThreadPoolWorker* worker) = 0;
  virtual ~PriorityThreadPoolWorkerContext() {}
};

class PriorityThreadPoolWorker : public PriorityThreadPoolSuspender {
 public:
  explicit PriorityThreadPoolWorker(PriorityThreadPoolWorkerContext* context)
      : context_(context) {
  }

  void SetThread(ThreadPtr thread) {
    thread_ = std::move(thread);
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
      task->Run(abort_status, nullptr /* suspender */);
    }
  }

  // It is invoked from WorkerFinished to directly assign a new task.
  void SetTask(TaskPtr task) {
    task_ = std::move(task);
  }

  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopped_) {
      if (!task_) {
        cond_.wait(lock);
        continue;
      }
      running_task_ = true;
      BOOST_SCOPE_EXIT(&running_task_) {
        running_task_ = false;
      } BOOST_SCOPE_EXIT_END;
      {
        yb::ReverseLock<decltype(lock)> rlock(lock);
        for (;;) {
          task_->Run(Status::OK(), this);
          if (!context_->WorkerFinished(this)) {
            break;
          }
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
      if (!running_task_) {
        task = std::move(task_);
      }
    }
    cond_.notify_one();
    if (task) {
      task->Run(abort_status, nullptr /* suspender */);
    }
  }

  void PauseIfNecessary() override {
    LOG_IF(DFATAL, yb::Thread::CurrentThreadId() != thread_->tid())
        << "PauseIfNecessary invoked not from worker thread";
    context_->PauseIfNecessary(this);
  }

  const TaskPtr& task() const {
    return task_;
  }

  // The following function is protected by the priority thread pool's lock.
  void SetPausedFlag() {
    paused_ = true;
  }

  // The following function is protected by the priority thread pool's lock.
  void Resume() {
    DCHECK(paused_);
    paused_ = false;
    cond_.notify_one();
  }

  void WaitResume(std::unique_lock<std::mutex>* lock) {
    cond_.wait(*lock, [this]() {
      return !paused_;
    });
  }

  std::string ToString() const {
    return Format("{ paused: $0 }", paused_);
  }

 private:
  PriorityThreadPoolWorkerContext* const context_;
  yb::ThreadPtr thread_;
  // Cannot use thread safety annotations, because std::unique_lock is used with this mutex.
  std::mutex mutex_;
  std::condition_variable cond_;
  bool stopped_ = false;
  bool running_task_ = false;
  TaskPtr task_;

  bool paused_ = false;
};

class PriorityTaskComparator {
 public:
  bool operator()(const TaskPtr& lhs, const TaskPtr& rhs) const {
    return (*this)(lhs.get(), rhs.get());
  }

  bool operator()(PriorityThreadPoolTask* lhs, PriorityThreadPoolTask* rhs) const {
    auto lhs_priority = lhs->Priority();
    auto rhs_priority = rhs->Priority();
    // The task with highest priority is picked first, if priorities are equal the task that
    // was added earlier is picked.
    return lhs_priority < rhs_priority ||
           (lhs_priority == rhs_priority && lhs->SerialNo() > rhs->SerialNo());
  }
};

class PriorityThreadPoolWorkerComparator {
 public:
  // Used only for paused workers, so each of them has non null task.
  bool operator()(PriorityThreadPoolWorker* lhs, PriorityThreadPoolWorker* rhs) const {
    return task_comparator_(lhs->task(), rhs->task());
  }
 private:
  PriorityTaskComparator task_comparator_;
};

std::atomic<size_t> task_serial_no_(0);

} // namespace

PriorityThreadPoolTask::PriorityThreadPoolTask()
    : serial_no_(task_serial_no_.fetch_add(1, std::memory_order_acq_rel)) {
}

const std::string& PriorityThreadPoolTask::ToString() const {
  if (!string_representation_ready_.load(std::memory_order_acquire)) {
    std::lock_guard<simple_spinlock> lock(mutex_);
    if (!string_representation_ready_.load(std::memory_order_acquire)) {
      string_representation_ = Format("Task { priority: $0 serial: $1 ", Priority(), SerialNo());
      AddToStringFields(&string_representation_);
      string_representation_ += "}";
      string_representation_ready_.store(true, std::memory_order_release);
    }
  }
  return string_representation_;
}


// Maintains priority queue of added tasks and vector of free workers.
// One of them is always empty.
//
// When a task is added it tries to pick a free worker or launch a new one.
// If it is impossible the task is added to the task priority queue.
//
// When worker finishes it tries to pick a task from the priority queue.
// If the queue is empty, the worker is added to the vector of free workers.
class PriorityThreadPool::Impl : public PriorityThreadPoolWorkerContext {
 public:
  explicit Impl(size_t max_running_tasks) : max_running_tasks_(max_running_tasks) {
    CHECK_GE(max_running_tasks, 1);
  }

  ~Impl() {
    StartShutdown();
    CompleteShutdown();

#ifndef NDEBUG
    std::lock_guard<std::mutex> lock(mutex_);
    DCHECK(tasks_to_run_.empty());
    DCHECK(tasks_to_defer_.empty());
#endif
  }

  std::unique_ptr<PriorityThreadPoolTask> Submit(TaskPtr task) {
    PriorityThreadPoolWorker* worker = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_.load(std::memory_order_acquire)) {
        VLOG(3) << task->ToString() << " rejected because of shutdown";
        return task;
      }
      worker = PickWorker();
      auto task_ptr = task.get();
      if (worker == nullptr) {
        if (workers_.empty()) {
          // Empty workers here means that we are unable to start even one worker thread.
          // So have to abort task in this case.
          VLOG(3) << task->ToString() << " rejected because cannot start even one worker";
          return task;
        }
        VLOG(4) << "Added " << task->ToString() << " to queue";
        queue_.Push(std::move(task));
      }
      TaskAdded(task_ptr);
    }

    if (worker) {
      VLOG(4) << "Passing " << task->ToString() << " to worker: " << worker;
      worker->Perform(std::move(task));
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
      for (const auto& task : removed_tasks) {
        TaskFinished(task.get());
      }
    }
    for (const auto& task : removed_tasks) {
      task->Run(abort_status, nullptr /* suspender */);
    }
  }

  void StartShutdown() {
    if (stopping_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    std::vector<TaskPtr> queue;
    std::vector<PriorityThreadPoolWorker*> workers;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.Swap(&queue);
      for (const auto& task : queue) {
        if (!tasks_to_run_.erase(task.get())) {
          LOG_IF(DFATAL, tasks_to_defer_.erase(task.get()) == 0)
              << "Unexpected task queued: " << task->ToString();
        }
      }
      // On shutdown we should resume all workers, so they could complete their current tasks.
      while (!paused_workers_.empty()) {
        ResumeHighestPriorityWorker();
      }
      workers.reserve(workers_.size());
      for (auto& worker : workers_) {
        workers.push_back(&worker);
      }
    }
    for (auto* worker : workers) {
      worker->Stop();
    }
    for (auto& task : queue) {
      task->Run(abort_status, nullptr /* suspender */);
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
    for (auto& thread : threads) {
      thread->Join();
    }
  }

  void PauseIfNecessary(PriorityThreadPoolWorker* worker) override {
    auto worker_task_priority = worker->task()->Priority();
    if (max_priority_to_defer_.load(std::memory_order_acquire) < worker_task_priority) {
      return;
    }

    PriorityThreadPoolWorker* higher_pri_worker;
    TaskPtr task;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (max_priority_to_defer_.load(std::memory_order_acquire) < worker_task_priority ||
          stopping_.load(std::memory_order_acquire) ||
          queue_.empty() || queue_.top()->Priority() <= worker_task_priority) {
        return;
      }

      LOG(INFO) << "Pausing " << worker << " with " << worker->task()->ToString()
              << " in favor of " << queue_.top()->ToString() << ", max to defer: "
              << max_priority_to_defer_.load(std::memory_order_acquire)
              << ", queued: " << queue_.size();

      paused_workers_.push(worker);
      worker->SetPausedFlag();
      higher_pri_worker = PickWorker();
      if (!higher_pri_worker) {
        LOG(DFATAL) << Format(
            "Unable to pick a worker for a higher priority task when trying to pause a lower "
                "priority task, paused: $1, free: $2, max: $3",
            workers_.size(), paused_workers_.size(), free_workers_.size(), max_running_tasks_);
        worker->Resume();
        return;
      }
      task = queue_.Pop();
    }

    VLOG(4) << "Passing " << task->ToString() << " to " << higher_pri_worker;
    higher_pri_worker->Perform(std::move(task));
    {
      std::unique_lock<std::mutex> lock(mutex_);
      worker->WaitResume(&lock);
      LOG(INFO) << "Resumed " << worker << " with " << worker->task()->ToString();
    }
  }

  std::string StateToString() {
    std::lock_guard<std::mutex> lock(mutex_);
    return Format(
        "{ max_running_tasks: $0 tasks: $1 workers: $2 paused_workers: $3 free_workers: $4 "
            "stopping: $5 max_priority_to_defer: $6 tasks_to_run: $7 tasks_to_defer: $8 }",
        max_running_tasks_, queue_, workers_, paused_workers_.size(), free_workers_,
        stopping_.load(), max_priority_to_defer_.load(), tasks_to_run_, tasks_to_defer_);
  }

 private:
  bool WorkerFinished(PriorityThreadPoolWorker* worker) override {
    std::lock_guard<std::mutex> lock(mutex_);
    TaskFinished(worker->task().get());

    // This worker will be marked as free in any of three cases:
    // 1) worker does not want new tasks, i.e. it is stopped.
    // 2) task queue is empty.
    // 3) we have a paused worker whose task has a higher priority than the top-priority task in
    //    the queue.
    if (queue_.empty() ||
        (!paused_workers_.empty() &&
         queue_.top()->Priority() <= paused_workers_.top()->task()->Priority())) {
      VLOG(4) << "No task for " << worker;
      free_workers_.push_back(worker);
      ResumeHighestPriorityWorker();
      worker->SetTask(nullptr);
      return false;
    }
    worker->SetTask(queue_.Pop());
    VLOG(4) << "Picked task for " << worker;
    return true;
  }

  // Task finished, adjust desired and unwanted tasks.
  void TaskFinished(PriorityThreadPoolTask* task) REQUIRES(mutex_) {
    VLOG(4) << "Finished " << task->ToString();
    auto it = tasks_to_run_.find(task);
    if (it != tasks_to_run_.end()) {
      tasks_to_run_.erase(it);
      VLOG(4) << "Desired " << task->ToString() << " finished";
      // Desired task finished, so move first unwanted task to desired.
      if (!tasks_to_defer_.empty()) {
        auto last = --tasks_to_defer_.end();
        VLOG(4) << "Moving from unwanted to desired " << (**last).ToString();
        tasks_to_run_.insert(*last);
        tasks_to_defer_.erase(last);
      }
    } else {
      auto count = tasks_to_defer_.erase(task);
      LOG_IF(DFATAL, count != 1) << "Finished unexpected task: " << task->ToString();
      VLOG(4) << "Unwanted " << task->ToString() << " finished";
    }
  }

  // Resume paused worker with highest priority task.
  void ResumeHighestPriorityWorker() REQUIRES(mutex_) {
    if (paused_workers_.empty()) {
      return;
    }
    auto worker = paused_workers_.top();
    paused_workers_.pop();
    worker->Resume();
  }

  PriorityThreadPoolWorker* PickWorker() REQUIRES(mutex_) {
    if (workers_.size() - paused_workers_.size() - free_workers_.size() >= max_running_tasks_) {
      // If we already have max_running_tasks_ workers running, we could not run a new worker.
      return nullptr;
    }
    if (!free_workers_.empty()) {
      auto worker = free_workers_.back();
      free_workers_.pop_back();
      VLOG(4) << "Has free worker: " << worker;
      return worker;
    }
    workers_.emplace_back(this);
    auto worker = &workers_.back();
    auto thread = yb::Thread::Make(
        "priority_thread_pool", "worker",
        std::bind(&PriorityThreadPoolWorker::Run, worker));
    if (!thread.ok()) {
      LOG(WARNING) << "Failed to launch new worker: " << thread.status();
      workers_.pop_back();
      return nullptr;
    } else {
      worker->SetThread(*thread);
      threads_.push_back(std::move(*thread));
      VLOG(3) << "Created new worker: " << worker;
    }
    return worker;
  }

  void TaskAdded(PriorityThreadPoolTask* task) REQUIRES(mutex_) {
    if (tasks_to_run_.size() < max_running_tasks_) {
      tasks_to_run_.insert(task);
      UpdateDesiredPriority();
      VLOG(4) << "New desired " << task->ToString() << ", max to defer: "
              << max_priority_to_defer_.load(std::memory_order_acquire);
    } else if (task_comparator_(*tasks_to_run_.begin(), task)) {
      auto old_desired = *tasks_to_run_.begin();
      tasks_to_defer_.insert(old_desired);
      tasks_to_run_.erase(tasks_to_run_.begin());
      tasks_to_run_.insert(task);
      UpdateDesiredPriority();
      VLOG(4) << "Move from desired " << old_desired->ToString()
              << ", replaced by " << task->ToString() << ", max to defer: "
              << max_priority_to_defer_.load(std::memory_order_acquire);
    } else {
      tasks_to_defer_.insert(task);
      UpdateDesiredPriority();
      VLOG(4) << "New unwanted " << task->ToString() << ", max to defer: "
              << max_priority_to_defer_.load(std::memory_order_acquire);
    }

    // Sanity check.
    // Number of active tasks could be calculated using two ways:
    // Sum of desired and unwanted tasks.
    // Number of queued and currently running tasks (paused also counted).
    LOG_IF(DFATAL,
           tasks_to_run_.size() + tasks_to_defer_.size() !=
               queue_.size() + workers_.size() - free_workers_.size())
        << Format(
            "desired_tasks: $0 + unwanted_tasks: $1 != queue: $2 + workers: $3 - free_workers: $4",
            tasks_to_run_.size(), tasks_to_defer_.size(), queue_.size(), workers_.size(),
            free_workers_.size());
  }

  void UpdateDesiredPriority() REQUIRES(mutex_) {
    // We could pause a worker when both of the following conditions are met:
    // 1) tasks_to_defer_ is not empty, so the worker's current task could be unwanted.
    // 2) priority of the worker's current task is less than minimal priority of desired task.
    int priority = tasks_to_defer_.empty() ? kEmptyQueuePriority
                                           : (**--tasks_to_defer_.end()).Priority();
    max_priority_to_defer_.store(priority, std::memory_order_release);
  }

  const size_t max_running_tasks_;
  std::mutex mutex_;
  PriorityQueue<TaskPtr, std::vector<TaskPtr>, PriorityTaskComparator> queue_ GUARDED_BY(mutex_);

  // Priority queue of paused workers. Worker with task with the highest priority is at top.
  std::priority_queue<PriorityThreadPoolWorker*, std::vector<PriorityThreadPoolWorker*>,
                      PriorityThreadPoolWorkerComparator> paused_workers_ GUARDED_BY(mutex_);

  std::vector<yb::ThreadPtr> threads_ GUARDED_BY(mutex_);
  boost::container::stable_vector<PriorityThreadPoolWorker> workers_ GUARDED_BY(mutex_);
  std::vector<PriorityThreadPoolWorker*> free_workers_ GUARDED_BY(mutex_);
  std::atomic<bool> stopping_{false};

  // Used for quick check, whether task with provided priority should be paused.
  std::atomic<int> max_priority_to_defer_{kEmptyQueuePriority};

  // tasks_to_run_ + tasks_to_defer_ consists of all active tasks.
  // I.e. tasks performed by workers and queued.
  // The first max_running_tasks_ tasks with the highest priorities are placed in tasks_to_run_,
  // other tasks are placed in unwanted tasks.
  // Since we use task serial no when priorities are equal, we always have well-defined order.
  std::set<PriorityThreadPoolTask*, PriorityTaskComparator> tasks_to_run_;
  std::set<PriorityThreadPoolTask*, PriorityTaskComparator> tasks_to_defer_;

  PriorityTaskComparator task_comparator_;
};

PriorityThreadPool::PriorityThreadPool(size_t max_running_tasks)
    : impl_(new Impl(max_running_tasks)) {
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

std::string PriorityThreadPool::StateToString() {
  return impl_->StateToString();
}

} // namespace yb
