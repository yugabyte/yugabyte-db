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

#include "yb/util/priority_thread_pool.h"

#include <mutex>
#include <unordered_map>

#include <boost/container/stable_vector.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/ranked_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/compare_util.h"
#include "yb/util/locks.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/unique_lock.h"

using namespace std::placeholders;

namespace yb {

namespace {

const Status kRemoveTaskStatus = STATUS(Aborted, "Task removed from priority thread pool");
const Status kShutdownStatus = STATUS(ShutdownInProgress, "Priority thread pool shutdown");
const Status kNoWorkersStatus = STATUS(Aborted, "No workers to perform task");

class PriorityThreadPoolWorker;
using TaskPtr = std::unique_ptr<PriorityThreadPoolTask>;

constexpr int kEmptyQueueTaskPriority = -1;
constexpr int kEmptyQueueGroupNoPriority = -1;
constexpr int kNoGroupPriority = 0;

// ------------------------------------------------------------------------------------------------
// Compare Priorities
// ------------------------------------------------------------------------------------------------

// To be used in Priority Pool, and Comparators
// Outputs 1 if the LHS is greater, -1 if the RHS is greater
// Outputs 0 otherwise
int ComparePriorities(PriorityThreadPoolPriorities lhs, PriorityThreadPoolPriorities rhs) {
  if (lhs.group_no_priority != rhs.group_no_priority) {
    if (lhs.group_no_priority > rhs.group_no_priority) {
      return 1;
    } else {
      return -1;
    }
  }
  if (lhs.task_priority != rhs.task_priority) {
    if (lhs.task_priority > rhs.task_priority)
      return 1;
    else
      return -1;
  }
  return 0;
}

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolInternalTask
// ------------------------------------------------------------------------------------------------

class PriorityThreadPoolInternalTask {
 public:
  PriorityThreadPoolInternalTask(
      int task_priority, int group_no_priority, TaskPtr task, PriorityThreadPoolWorker* worker,
      const int group_no, std::mutex* thread_pool_mutex)
      : task_priority_(task_priority),
        group_no_priority_(group_no_priority),
        serial_no_(task->SerialNo()),
        group_no_(group_no),
        state_(worker != nullptr ? PriorityThreadPoolTaskState::kRunning
                                 : PriorityThreadPoolTaskState::kNotStarted),
        task_(std::move(task)),
        worker_(worker),
        thread_pool_mutex_(*thread_pool_mutex) {}

  std::unique_ptr<PriorityThreadPoolTask>& task() const {
    return task_;
  }

  // This is called get_worker_unsafe because in order to call this function, the caller needs
  // to turn off thread safety analysis, so we only do it inside the GetWorker wrapper function.
  PriorityThreadPoolWorker* get_worker_unsafe() const REQUIRES(thread_pool_mutex_) {
    return worker_.load(std::memory_order_relaxed);
  }

  // Another getter for worker_ but this time the mutex is not required. The value returned should
  // only be used for ToString.
  PriorityThreadPoolWorker* get_worker_relaxed() const NO_THREAD_SAFETY_ANALYSIS {
    return worker_.load(std::memory_order_relaxed);
  }

  size_t serial_no() const {
    return serial_no_;
  }

  int group_no() const {
    return group_no_;
  }

  // Changes to priority does not require immediate effect, so
  // relaxed could be used.
  int task_priority() const {
    return task_priority_.load(std::memory_order_relaxed);
  }

  int group_no_priority() const {
    std::lock_guard lock(group_no_priority_mutex_);
    return group_no_priority_;
  }

  bool group_no_priority_frozen() const {
    std::lock_guard lock(group_no_priority_mutex_);
    return group_no_priority_frozen_;
  }

  // Changes to state does not require immediate effect, so
  // relaxed could be used.
  PriorityThreadPoolTaskState state() const {
    return state_.load(std::memory_order_relaxed);
  }

  void SetWorker(PriorityThreadPoolWorker* worker) REQUIRES(thread_pool_mutex_) {
    // Task state could be only changed when thread pool lock is held.
    // So it is safe to avoid state caching for logging.
    LOG_IF(DFATAL, state() != PriorityThreadPoolTaskState::kNotStarted)
        << "Wrong task state " << state() << " in " << __PRETTY_FUNCTION__;
    worker_.store(worker, std::memory_order_release);
    SetState(PriorityThreadPoolTaskState::kRunning);
  }

  void SetTaskPriority(int value) {
    task_priority_.store(value, std::memory_order_relaxed);
  }

  // Returns true if the group no priority was successfully set.
  // Returns false otherwise.
  bool SetGroupNoPriority(int value) {
    std::lock_guard lock(group_no_priority_mutex_);
    if (!group_no_priority_frozen_) {
      group_no_priority_ = value;
      return true;
    }
    return false;
  }

  void SetState(PriorityThreadPoolTaskState value) {
    state_.store(value, std::memory_order_relaxed);
  }

  void SetGroupNoPriorityFrozen(bool freeze)  {
    std::lock_guard lock(group_no_priority_mutex_);
    group_no_priority_frozen_ = freeze;
  }

  PriorityThreadPoolPriorities GetPriorities() const {
    return PriorityThreadPoolPriorities{task_priority_.load(), group_no_priority()};
  }

  // Reading the value of worker_ requires thread_pool_mutex_. But since this is only used for
  // logging and we are just printing the pointer value we do not need to lock.
  std::string ToString() const {
    return Format(
        "{ task: $0 worker: $1 state: $2 task_priority: $3 group_no_priority: $4 serial_no: $5 }",
        TaskToString(), get_worker_relaxed(), state(), task_priority(), group_no_priority(),
        serial_no_);
  }

 private:
  const std::string& TaskToString() const NO_THREAD_SAFETY_ANALYSIS {
    if (!task_to_string_ready_.load(std::memory_order_acquire)) {
      std::lock_guard lock(task_to_string_mutex_);
      if (!task_to_string_ready_.load(std::memory_order_acquire)) {
        task_to_string_ = task_->ToString();
        task_to_string_ready_.store(true, std::memory_order_release);
      }
    }
    return task_to_string_;
  }
  std::atomic<int> task_priority_;
  int group_no_priority_ GUARDED_BY(group_no_priority_mutex_);
  bool group_no_priority_frozen_ GUARDED_BY(group_no_priority_mutex_) = false;
  mutable std::mutex group_no_priority_mutex_;
  const size_t serial_no_;
  const int group_no_;
  std::atomic<PriorityThreadPoolTaskState> state_{PriorityThreadPoolTaskState::kNotStarted};
  mutable TaskPtr task_;

  mutable std::atomic<PriorityThreadPoolWorker*> worker_ GUARDED_BY(thread_pool_mutex_);

  mutable std::atomic<bool> task_to_string_ready_{false};
  mutable simple_spinlock task_to_string_mutex_;
  mutable std::string task_to_string_ GUARDED_BY(task_to_string_mutex_);

  std::mutex& thread_pool_mutex_;
};

} // namespace

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolTaskExecutor
// ------------------------------------------------------------------------------------------------

// Used to access PriorityThreadPoolTask's internal Execute() method.
class PriorityThreadPoolTaskExecutor {
 public:
  static void Run(
      PriorityThreadPoolTask& task, const Status& status,
      PriorityThreadPoolSuspender* suspender = nullptr) {
    task.Execute(PriorityThreadPoolTask::ExecuteTag{}, status, suspender);
  }
};

namespace {

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolWorkerContext
// ------------------------------------------------------------------------------------------------

class PriorityThreadPoolWorkerContext {
 public:
  virtual ~PriorityThreadPoolWorkerContext() {}

  virtual void PauseIfNecessary(PriorityThreadPoolWorker* worker) = 0;
  virtual void NotifyTaskAborted(const PriorityThreadPoolInternalTask* task) = 0;

  // Returns true if the worker picked another task.
  virtual bool NotifyWorkerFinished(PriorityThreadPoolWorker* worker) = 0;
};

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolWorker
// ------------------------------------------------------------------------------------------------

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
  void Perform(const PriorityThreadPoolInternalTask* task) {
    {
      std::lock_guard lock(mutex_);
      LOG_IF(DFATAL, task_) << "Task is already set in call to " << __PRETTY_FUNCTION__;
      if (!stopped_) {
        std::swap(task_, task);
      }
    }
    YB_PROFILE(cond_.notify_one());
    if (task) {
      PriorityThreadPoolTaskExecutor::Run(*task->task(), kShutdownStatus);
      context_->NotifyTaskAborted(task);
    }
  }

  // It is invoked from NotifyWorkerFinished to directly assign a new task.
  void SetTask(const PriorityThreadPoolInternalTask* task) EXCLUDES(mutex_) {
    task_ = task;
  }

  void Run() {
    UniqueLock lock(mutex_);
    while (!stopped_) {
      if (!task_) {
        WaitOnConditionVariable(&cond_, &lock);
        continue;
      }
      running_task_ = true;

      // The thread safety analysis in Clang 11 does not understand that we are holding the lock
      // on all exit paths from this scope, so we use NO_THREAD_SAFETY_ANALYSIS here.
      auto se = ScopeExit([this]() NO_THREAD_SAFETY_ANALYSIS {
        running_task_ = false;
      });

      {
        yb::ReverseLock<decltype(lock)> rlock(lock);
        for (;;) {
          VLOG(4) << "Worker " << this << " running task: " << task_->ToString();
          PriorityThreadPoolTaskExecutor::Run(*task_->task(), Status::OK(), this);
          if (!context_->NotifyWorkerFinished(this)) {
            break;
          }
        }
      }
    }
    LOG_IF(DFATAL, task_) << "task_ is still set when exiting Run().";
  }

  void Stop() {
    const PriorityThreadPoolInternalTask* task = nullptr;
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
      if (!running_task_) {
        std::swap(task, task_);
      }
    }
    YB_PROFILE(cond_.notify_one());
    if (task) {
      PriorityThreadPoolTaskExecutor::Run(*task->task(), kShutdownStatus);
      context_->NotifyTaskAborted(task);
    }
  }

  void PauseIfNecessary() override {
    LOG_IF(DFATAL, yb::Thread::CurrentThreadId() != thread_->tid())
        << "PauseIfNecessary invoked not from worker thread";
    context_->PauseIfNecessary(this);
  }

  void Resumed() {
    YB_PROFILE(cond_.notify_one());
  }

  const PriorityThreadPoolInternalTask* task() const {
    return task_;
  }

  // We need to pass the mutex here and check that it is the same mutex that has been locked by
  // the lock, because thread safety analysis in Clang 11 would not understand
  // REQUIRES(*lock->mutex()) -- it would not know that we've locked that mutex.
  void WaitResume(UniqueLock<std::mutex>* lock, std::mutex* mutex) REQUIRES(*mutex) {
    CHECK_EQ(lock->mutex(), mutex);
    WaitOnConditionVariable(&cond_, lock, [this]() {
      return task_->state() != PriorityThreadPoolTaskState::kPaused;
    });
  }

  std::string ToString() const {
    std::lock_guard lock(mutex_);
    return Format("{ worker: $0 }", static_cast<const void*>(this));
  }

 private:
  PriorityThreadPoolWorkerContext* const context_;
  yb::ThreadPtr thread_;
  mutable std::mutex mutex_;
  std::condition_variable cond_;
  bool stopped_ GUARDED_BY(mutex_) = false;

  bool running_task_ GUARDED_BY(mutex_) = false;

  // TODO(mbautin): clarify locking semantics for this field.
  const PriorityThreadPoolInternalTask* task_ = nullptr;
};

// ------------------------------------------------------------------------------------------------
// PriorityTaskComparator
// ------------------------------------------------------------------------------------------------

class PriorityTaskComparator {
 public:
  bool operator()(const PriorityThreadPoolInternalTask& lhs,
                  const PriorityThreadPoolInternalTask& rhs) const {
    // The task with highest priority
    // is picked first, if priorities are equal the task that
    // was added earlier is picked.
    int compare = ComparePriorities(lhs.GetPriorities(), rhs.GetPriorities());
    if (compare != 0) {
      return compare > 0;
    }
    return lhs.serial_no() < rhs.serial_no();
  }
};

// ------------------------------------------------------------------------------------------------
// StateAndPriorityTaskComparator
// ------------------------------------------------------------------------------------------------
//
// Not running tasks (i.e. paused or not started) go first, ordered by priority, state and serial.
// After them running tasks, ordered by priority, state and serial.
// I.e. paused tasks goes before not started tasks with the same priority.
//
// For example, if we have the following tasks with two different priorities (ignoring serial
// numbers, which are only used for breaking ties):
//
// T1 {P1, kPaused}
// T2 {P1, kNotStarted}
// T3 {P1, kRunning}
// T4 {P2, kPaused}
// T5 {P2, kNotStarted}
// T6 {P2, kRunning}
//
// Then the order will be as follows:
//
// T4 {P2, kPaused}      /--- Non-running tasks
// T5 {P2, kNotStarted}  |
// T1 {P1, kPaused}      |
// T2 {P1, kNotStarted}  `---------------------
// T6 {P2, kRunning}     /--- Running tasks ---
// T3 {P1, kRunning}     `----------------------

class StateAndPriorityTaskComparator {
 public:
  bool operator()(const PriorityThreadPoolInternalTask& lhs,
                  const PriorityThreadPoolInternalTask& rhs) const {
    auto lhs_running = lhs.state() == PriorityThreadPoolTaskState::kRunning;
    if (lhs_running != (rhs.state() == PriorityThreadPoolTaskState::kRunning)) {
      // If lhs is not running and rhs is running, lhs goes first.
      return !lhs_running;
    }

    int compare = ComparePriorities(lhs.GetPriorities(), rhs.GetPriorities());
    if (compare != 0) {
      return compare > 0;
    }
    if (lhs.state() != rhs.state()) {
      return lhs.state() < rhs.state();
    }
    return lhs.serial_no() < rhs.serial_no();
  }
};

std::atomic<size_t> task_serial_no_(1);

} // namespace

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolToken::Impl
// ------------------------------------------------------------------------------------------------

class PriorityThreadPoolTokenContext {
 public:
  virtual ~PriorityThreadPoolTokenContext() = default;
  virtual void NotifyTaskCompleted(size_t serial_no) = 0;
  virtual void NotifyTaskDestroyed(size_t serial_no) = 0;
};

class PriorityThreadPoolToken::Impl : public PriorityThreadPoolTokenContext {
 public:
  ~Impl();
  Impl(PriorityThreadPool& pool, size_t max_concurrency);

  bool UpdateTaskPriority(size_t serial_no);
  bool PrioritizeTask(size_t serial_no);

  void Remove(void* task_key);
  void SetMaxConcurrency(size_t new_limit);
  void ScheduleTasks();

  Status Submit(PriorityThreadPoolTokenTaskPtr* task);

  void StartShutdown();
  void CompleteShutdown();

  std::string ToString() const;

 private:
  void SanitizeTask(size_t serial_no);
  void NotifyTaskCompleted(size_t serial_no) override;
  void NotifyTaskDestroyed(size_t serial_no) override;

  bool EraseActiveTask(size_t serial_no) EXCLUDES(mutex_);
  void EraseDeferredTasks(
      const Status& status, std::optional<void*> task_key = std::nullopt) EXCLUDES(mutex_);

  std::string ToStringUnlocked() const REQUIRES_SHARED(mutex_);
  static std::string TaskToString(const PriorityThreadPoolTokenTask& task);

  PriorityThreadPool& pool_;
  const std::shared_ptr<void> pool_ticket_;

  mutable rw_spinlock mutex_;
  size_t max_concurency_ GUARDED_BY(mutex_) = 0;

  using DeferredTaskMap = std::map<size_t, PriorityThreadPoolTokenTaskPtr>;
  DeferredTaskMap deferred_tasks_ GUARDED_BY(mutex_);

  using ActiveTaskMap = std::unordered_map<size_t, PriorityThreadPoolTokenTask*>;
  ActiveTaskMap active_tasks_ GUARDED_BY(mutex_);
  std::condition_variable_any active_tasks_erase_cv_;
};

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolTask
// ------------------------------------------------------------------------------------------------

PriorityThreadPoolTask::PriorityThreadPoolTask()
    : serial_no_(task_serial_no_.fetch_add(1, std::memory_order_acq_rel)) {
}

void PriorityThreadPoolTask::Execute(
    ExecuteTag, const Status& status, PriorityThreadPoolSuspender* suspender) {
  return Run(status, suspender);
}

// ------------------------------------------------------------------------------------------------
// PriorityThreadPool::Impl
// ------------------------------------------------------------------------------------------------

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
  explicit Impl(size_t max_running_tasks, bool prioritize_by_group_no)
      : max_running_tasks_(max_running_tasks),
        prioritize_by_group_no_(prioritize_by_group_no) {
    CHECK_GE(max_running_tasks, 1);
  }

  ~Impl() {
    StartShutdown();
    CompleteShutdown();

    // All tasks should have been removed by this point.
    std::lock_guard lock(mutex_);
    LOG_IF(DFATAL, !tasks_.empty())
        << "Shutting down a non-empty priority thread pool: " << StateToStringUnlocked();

    // All tickets should have been returned by this point.
    // TODO: a busy wait can be used on ticket.expired() instead.
    std::weak_ptr<void> ticket = std::exchange(ticket_, nullptr);
    LOG_IF(DFATAL, !ticket.expired()) <<
        "Shutting down a priority thread pool with num of active tickets = " << ticket.use_count();
  }

  std::shared_ptr<void> ticket() const {
    return ticket_;
  }

  Status Submit(int task_priority, TaskPtr* task, const uint64_t group_no) {
    if (!*task) {
      return STATUS(InvalidArgument, "Task is null");
    }
    PriorityThreadPoolWorker* worker = nullptr;
    const PriorityThreadPoolInternalTask* internal_task = nullptr;
    {
      std::lock_guard lock(mutex_);
      if (stopping_.load(std::memory_order_acquire)) {
        VLOG(3) << (**task).ToString() << " rejected because of shutdown";
        return kShutdownStatus;
      }
      worker = PickWorker();
      if (worker == nullptr) {
        if (workers_.empty()) {
          // Empty workers here means that we are unable to start even one worker thread.
          // So have to abort task in this case.
          VLOG(3) << (**task).ToString() << " rejected because cannot start even one worker";
          return kNoWorkersStatus;
        }
        VLOG(4) << "Added " << (**task).ToString() << " to queue";
      }

      internal_task = AddTask(task_priority, std::move(*task), worker, group_no);
      NotifyStateChangedTo(*internal_task, internal_task->state());
    }

    if (worker) {
      VLOG(4) << "Passing " << internal_task->ToString() << " to worker: " << worker;
      worker->Perform(internal_task);
    }

    return Status::OK();
  }

  void Remove(void* key) {
    std::vector<TaskPtr> abort_tasks;
    {
      std::lock_guard lock(mutex_);
      for (auto it = tasks_.begin(); it != tasks_.end();) {
        if (it->state() == PriorityThreadPoolTaskState::kNotStarted &&
            it->task()->ShouldRemoveWithKey(key)) {
          NotifyStateChangedFrom(*it, PriorityThreadPoolTaskState::kNotStarted);
          abort_tasks.push_back(std::move(it->task()));
          it = tasks_.erase(it);
        } else {
          ++it;
        }
      }
      UpdateMaxPriorityToDefer();
    }
    AbortTasks(abort_tasks, kRemoveTaskStatus);
  }

  void StartShutdown() {
    if (stopping_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    VLOG(1) << "Starting shutdown";
    std::vector<TaskPtr> abort_tasks;
    std::vector<PriorityThreadPoolWorker*> workers;
    {
      std::lock_guard lock(mutex_);
      for (auto it = tasks_.begin(); it != tasks_.end();) {
        VLOG(2) << "Task: " << it->ToString();
        auto state = it->state();
        if (state == PriorityThreadPoolTaskState::kNotStarted) {
          NotifyStateChangedFrom(*it, PriorityThreadPoolTaskState::kNotStarted);
          abort_tasks.push_back(std::move(it->task()));
          it = tasks_.erase(it);
        } else {
          if (state == PriorityThreadPoolTaskState::kPaused) {
            // On shutdown we should resume all workers, so they could complete their current tasks.
            ResumeWorker(it);
          }
          ++it;
        }
      }
      workers.reserve(workers_.size());
      for (auto& worker : workers_) {
        workers.push_back(&worker);
      }
      UpdateMaxPriorityToDefer();
    }
    for (auto* worker : workers) {
      worker->Stop();
    }
    AbortTasks(abort_tasks, kShutdownStatus);
  }

  void CompleteShutdown() {
    decltype(threads_) threads;
    decltype(workers_) workers;
    {
      std::lock_guard lock(mutex_);
      threads.swap(threads_);
      workers.swap(workers_);
    }
    for (auto& thread : threads) {
      thread->Join();
    }
  }

  // We use NO_THREAD_SAFETY_ANALYSIS here because thread safety analysis does not understand that
  // the mutex required by PriorityThreadPoolInternalTask::get_worker_unsafe() is the same mutex we
  // are already holding here.
  PriorityThreadPoolWorker* GetWorker(const PriorityThreadPoolInternalTask& task)
      REQUIRES(mutex_) NO_THREAD_SAFETY_ANALYSIS {
    return task.get_worker_unsafe();
  }

  void PauseIfNecessary(PriorityThreadPoolWorker* worker) override {
    const PriorityThreadPoolPriorities worker_task_priorities = worker->task()->GetPriorities();
    const PriorityThreadPoolPriorities defer_priorities{
        max_task_priority_to_defer_.load(std::memory_order_acquire),
        max_group_no_priority_to_defer()
    };
    // If worker_task has higher priority than the defer priorities, no need to pause
    if (ComparePriorities(defer_priorities, worker_task_priorities) < 0) {
      return;
    }

    PriorityThreadPoolWorker* higher_pri_worker = nullptr;
    const PriorityThreadPoolInternalTask* task = nullptr;
    {
      std::lock_guard lock(mutex_);

      // Check defer priorities again now that we're holding the lock (double-checked locking).
      const PriorityThreadPoolPriorities defer_priorities{
        max_task_priority_to_defer_.load(std::memory_order_acquire),
        max_group_no_priority_to_defer()
      };
      if (
          ComparePriorities(defer_priorities, worker_task_priorities) < 0 ||
          stopping_.load(std::memory_order_acquire)) {
        return;
      }

      // Check the task with the highest priority.
      // If this task is on the same group_no, we simply check priorities.
      // If the task is on a different group_no, we must check priorities, but
      // we must subtract 1 from the group_no priority, as if we were to run the task,
      // it would reduce it's own group_no priority by 1, and so we must account for this.
      // If worker task is a better fit, we can return.
      auto it = tasks_.get<StateAndPriorityTag>().begin();

      if (prioritize_by_group_no_) {
        if (it->group_no() == worker->task()->group_no() &&
            ComparePriorities(it->GetPriorities(), worker_task_priorities) <= 0) {
          return;
        } else if (it->group_no() != worker->task()->group_no() &&
            ComparePriorities(
                PriorityThreadPoolPriorities{it->task_priority(), it->group_no_priority() - 1},
                worker_task_priorities) <= 0) {
          return;
        }
      } else if (ComparePriorities(it->GetPriorities(), worker_task_priorities) <= 0) {
        return;
      }

      LOG(INFO) << "Pausing " << worker->task()->ToString()
                << " in favor of " << it->ToString() << ", max task priority of a task to defer: "
                << max_task_priority_to_defer_.load(std::memory_order_acquire) <<
                ", max group_no priority of a task to defer: "
                << max_group_no_priority_to_defer();

      // We need to increment the number of paused workers here even though we may decrease it very
      // soon as we un-pause a different worker, because there is logic inside PickWorker() that
      // takes paused_workers_ into account in order to allow a worker to run.
      ++paused_workers_;

      switch (it->state()) {
        case PriorityThreadPoolTaskState::kPaused:
          VLOG(4) << "Resuming " << GetWorker(*it);
          ResumeWorker(tasks_.project<PriorityTag>(it));
          break;
        case PriorityThreadPoolTaskState::kNotStarted:
          higher_pri_worker = PickWorker();
          if (!higher_pri_worker) {
            LOG(WARNING) << Format(
                "Unable to pick a worker for a higher priority task when trying to pause a lower "
                "priority task. We will resume the original task now. "
                "Number of workers: $0, "
                "paused workers: $1, "
                "free workers: $2, "
                "max running tasks: $3.",
                workers_.size(), paused_workers_, free_workers_.size(), max_running_tasks_);
            // We cannot use ResumeWorker here because we have not finished pausing this worker, so
            // we have to manually do part of what ResumeWorker does.
            --paused_workers_;

            // TODO: why is this needed here? This function is already supposed to be running on
            // the worker's thread.
            worker->Resumed();

            return;
          }
          // Change State From Queued Tasks
          NotifyStateChangedFrom(
              *tasks_.project<PriorityTag>(it), PriorityThreadPoolTaskState::kNotStarted);
          SetWorker(tasks_.project<PriorityTag>(it), higher_pri_worker);
          // Change State To Active Tasks
          NotifyStateChangedTo(
              *tasks_.project<PriorityTag>(it), PriorityThreadPoolTaskState::kRunning);
          task = &*it;
          break;
        case PriorityThreadPoolTaskState::kRunning:
          --paused_workers_;
          // TODO: if we call worker->Resumed() above, why not call it here?
          LOG(DFATAL) << "Pausing in favor of already running task: " << it->ToString()
                      << ", state: " << StateToStringUnlocked();
          return;
      }

      auto worker_task_it = tasks_.iterator_to(*worker->task());
      ModifyState(worker_task_it, PriorityThreadPoolTaskState::kPaused);
    }
    // Released the mutex.

    if (higher_pri_worker) {
      VLOG(4) << "Passing " << task->ToString() << " to " << higher_pri_worker;
      higher_pri_worker->Perform(task);
    }

    // This will block until this worker's task is resumed.
    {
      UniqueLock<std::mutex> lock(mutex_);
      worker->WaitResume(&lock, &mutex_);
      LOG(INFO) << "Resumed worker " << worker << " with " << worker->task()->ToString();
    }
  }

  bool ChangeTaskPriority(size_t serial_no, int priority) {
    std::lock_guard lock(mutex_);
    auto& index = tasks_.get<SerialNoTag>();
    auto it = index.find(serial_no);
    if (it == index.end()) {
      return false;
    }

    index.modify(it, [priority](PriorityThreadPoolInternalTask& task) {
      task.SetTaskPriority(priority);
    });
    UpdateMaxPriorityToDefer();

    VLOG(4) << "Changed task priority " << serial_no << ": " << it->ToString();

    return true;
  }

  bool PrioritizeTask(size_t serial_no) {
    if (prioritize_by_group_no_) {
      std::lock_guard lock(mutex_);
      auto& index = tasks_.get<SerialNoTag>();
      auto it = index.find(serial_no);
      if (it == index.end() || it->group_no_priority_frozen()) {
        return false;
      }

      int priority = kHighPriority;
      bool freeze = true;
      // Change Task Priority
      index.modify(it, [priority, freeze](PriorityThreadPoolInternalTask& task) {
        task.SetTaskPriority(priority);
        task.SetGroupNoPriority(priority);
        task.SetGroupNoPriorityFrozen(freeze);
      });

      UpdateMaxPriorityToDefer();

      return true;
    }
    return ChangeTaskPriority(serial_no, kHighPriority);
  }


  std::string StateToString() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return StateToStringUnlocked();
  }

  size_t TEST_num_tasks_pending() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return tasks_.size();
  }

  void TEST_SetThreadCreationFailureProbability(double probability) {
    thread_creation_failure_probability_ = probability;
  }

  std::mutex* TEST_mutex() {
    return &mutex_;
  }

 private:
  std::string StateToStringUnlocked() REQUIRES(mutex_) {
    return Format(
        "{ max_running_tasks: $0 tasks: $1 workers: $2 paused_workers: $3 free_workers: $4 "
            "stopping: $5 max_task_priority_to_defer: $6 max_group_no_priority_to_defer $7 }",
        max_running_tasks_, tasks_, workers_, paused_workers_, free_workers_,
        stopping_.load(), max_task_priority_to_defer_.load(),
        max_group_no_priority_to_defer());
  }

  void AbortTasks(const std::vector<TaskPtr>& tasks, const Status& abort_status) {
    VLOG(2) << "Aborting tasks: " << AsString(tasks) << " with status: " << abort_status;
    for (const auto& task : tasks) {
      PriorityThreadPoolTaskExecutor::Run(*task, abort_status);
    }
  }

  // It is expected to be triggered from the Worker::Run() thread.
  bool NotifyWorkerFinished(PriorityThreadPoolWorker* worker) EXCLUDES(mutex_) override {
    std::lock_guard lock(mutex_);
    TaskFinished(worker->task());
    if (!TryAssignTask(worker)) {
      free_workers_.push_back(worker);
      worker->SetTask(nullptr);
      return false;
    }

    return true;
  }

  bool TryAssignTask(PriorityThreadPoolWorker* worker) REQUIRES(mutex_) {
    if (tasks_.empty()) {
      VLOG(4) << "No tasks left for " << worker;
      return false;
    }

    auto it = tasks_.get<StateAndPriorityTag>().begin();
    switch (it->state()) {
      case PriorityThreadPoolTaskState::kPaused:
        VLOG(4) << "Resume other worker after " << worker << " finished";
        ResumeWorker(tasks_.project<PriorityTag>(it));
        return false;
      case PriorityThreadPoolTaskState::kNotStarted:
        VLOG(4) << "Picked task " << it->task()->ToString() << " for " << worker;
        // Change State From Queued
        NotifyStateChangedFrom(*it, PriorityThreadPoolTaskState::kNotStarted);
        worker->SetTask(&*it);
        SetWorker(tasks_.project<PriorityTag>(it), worker);
        // Change State To Active
        NotifyStateChangedTo(*it, PriorityThreadPoolTaskState::kRunning);
        return true;
      case PriorityThreadPoolTaskState::kRunning:
        VLOG(4) << "Only running tasks left, nothing for " << worker;
        return false;
    }

    FATAL_INVALID_ENUM_VALUE(PriorityThreadPoolTaskState, it->state());
  }

  // Task finished, adjust desired and unwanted tasks.
  void TaskFinished(const PriorityThreadPoolInternalTask* task) REQUIRES(mutex_) {
    VLOG(4) << "Finished " << task->ToString();
    NotifyStateChangedFrom(*task, task->state());
    tasks_.erase(tasks_.iterator_to(*task));
    UpdateMaxPriorityToDefer();
  }

  // Task finished, adjust desired and unwanted tasks.
  void NotifyTaskAborted(const PriorityThreadPoolInternalTask* task) override {
    VLOG(3) << "Aborted " << task->ToString();
    std::lock_guard lock(mutex_);
    TaskFinished(task);
  }

  template <class It>
  void ResumeWorker(It it) REQUIRES(mutex_) {
    auto task_state = it->state();
    LOG_IF(DFATAL, task_state != PriorityThreadPoolTaskState::kPaused)
        << "Resuming not paused worker, state: " << task_state;
    --paused_workers_;
    ModifyState(it, PriorityThreadPoolTaskState::kRunning);
    GetWorker(*it)->Resumed();
  }

  PriorityThreadPoolWorker* PickWorker() REQUIRES(mutex_) {
    if (workers_.size() - paused_workers_ - free_workers_.size() >= max_running_tasks_) {
      VLOG(1) << "We already have " << workers_.size() << " - " << paused_workers_ << " - "
              << free_workers_.size() << " >= " << max_running_tasks_
                          << " workers running, we could not run a new worker.";
      return nullptr;
    }

    if (!free_workers_.empty()) {
      auto worker = free_workers_.back();
      free_workers_.pop_back();
      VLOG(4) << "Has free worker: " << worker;
      return worker;
    }
    workers_.emplace_back(this);

    bool inject_failure = RandomActWithProbability(thread_creation_failure_probability_);

    auto worker = &workers_.back();
    auto thread = inject_failure ?
        STATUS(RuntimeError, "TEST: pretending we could not create a thread") :
        yb::Thread::Make(
            "priority_thread_pool", "priority-worker",
            std::bind(&PriorityThreadPoolWorker::Run, worker));

    if (!thread.ok()) {
      LOG(WARNING) << "Failed to launch new worker: " << thread.status();
      workers_.pop_back();
      return nullptr;
    }

    worker->SetThread(*thread);
    VLOG(3) << "Created new worker: " << worker << " thread id: " << (*thread)->tid();
    threads_.push_back(std::move(*thread));

    return worker;
  }

  const PriorityThreadPoolInternalTask* AddTask(
      int task_priority, TaskPtr task, PriorityThreadPoolWorker* worker, const uint64_t group_no)
      REQUIRES(mutex_) {
    int group_no_priority = kNoGroupPriority;
    if (prioritize_by_group_no_) {
      auto iter = active_tasks_per_group_.find(group_no);
      if (iter == active_tasks_per_group_.end()) {
          active_tasks_per_group_.insert(std::make_pair(group_no, 0));
      }
      group_no_priority = task->CalculateGroupNoPriority(active_tasks_per_group_[group_no]);
    }
    auto it = tasks_.emplace(
        task_priority, group_no_priority, std::move(task), worker, group_no, &mutex_).first;
    UpdateMaxPriorityToDefer();
    VLOG(4) << "New task added " << it->ToString() << ", max task to defer: "
            << max_task_priority_to_defer_.load(std::memory_order_acquire)
            << ", max group_no to defer: "
            << max_group_no_priority_to_defer();
    return &*it;
  }

  void UpdateMaxPriorityToDefer() REQUIRES(mutex_) {
    // We could pause a worker when both of the following conditions are met:
    // 1) Number of active tasks is greater than max_running_tasks_.
    // 2) Priority of the worker's current task is less than top max_running_tasks_ priorities.
    int task_priority = tasks_.size() <= max_running_tasks_
        ? kEmptyQueueTaskPriority
        : tasks_.nth(max_running_tasks_)->task_priority();
    max_task_priority_to_defer_.store(task_priority, std::memory_order_release);
    if (prioritize_by_group_no_) {
      int group_no_priority = tasks_.size() <= max_running_tasks_
          ? kEmptyQueueGroupNoPriority
          : tasks_.nth(max_running_tasks_)->group_no_priority();
      max_group_no_priority_to_defer_.store(group_no_priority);
    }
  }

  // This function is responsible for updating any tracking related to state changes to a state,
  // including updating active_tasks_per_group_ and metrics for the relevant queue.
  void NotifyStateChangedTo(
      const PriorityThreadPoolInternalTask& task,
      const PriorityThreadPoolTaskState state) REQUIRES(mutex_) {
    task.task()->StateChangedTo(state);
    if (prioritize_by_group_no_ && state == PriorityThreadPoolTaskState::kRunning) {
      ++active_tasks_per_group_[task.group_no()];
      UpdateGroupNoPriority(task.group_no());
    }
  }

  // This function is responsible for updating any tracking related to state changes from a state,
  // including updating active_tasks_per_group_ and metrics for the relevant queue.
  void NotifyStateChangedFrom(
      const PriorityThreadPoolInternalTask& task,
      const PriorityThreadPoolTaskState state) REQUIRES(mutex_) {
    task.task()->StateChangedFrom(state);
    if (prioritize_by_group_no_ && state == PriorityThreadPoolTaskState::kRunning) {
      auto& num_active_tasks = active_tasks_per_group_[task.group_no()];
      if (num_active_tasks > 0) {
        --num_active_tasks;
      } else {
        LOG(DFATAL) << "Possible corruption, unexpected num_active_tasks = " << num_active_tasks;
      }
      UpdateGroupNoPriority(task.group_no());
    }
  }

  void UpdateGroupNoPriority(int group_no) REQUIRES(mutex_) {
    auto& index_group = tasks_.get<GroupNoTag>();
    auto& index_serial = tasks_.get<SerialNoTag>();
    auto it_group_no = index_group.equal_range(group_no);
    int num_active_tasks = active_tasks_per_group_[group_no];
    for (auto it = it_group_no.first; it != it_group_no.second; ++it) {
      auto task_ptr = index_serial.find(it->serial_no());
      int priority = task_ptr->task()->CalculateGroupNoPriority(num_active_tasks);
      index_serial.modify(task_ptr, [priority](PriorityThreadPoolInternalTask& task) {
        task.SetGroupNoPriority(priority);
      });
    }
    UpdateMaxPriorityToDefer();
  }

  template <class It>
  void ModifyState(It it, PriorityThreadPoolTaskState new_state) REQUIRES(mutex_) {
    // Change State From Previous State
    const auto current_task = tasks_.project<PriorityTag>(it);
    NotifyStateChangedFrom(*current_task, current_task->state());
    tasks_.modify(it, [new_state](PriorityThreadPoolInternalTask& task) {
      task.SetState(new_state);
    });
    // Change State To New State
    NotifyStateChangedTo(*tasks_.project<PriorityTag>(it), new_state);
  }

  template <class It>
  void SetWorker(It it, PriorityThreadPoolWorker* worker) REQUIRES(mutex_) {
    // Thread safety analysis does not understand that the mutex that SetWorker requires is the
    // global thread pool worker we are already holding, so use NO_THREAD_SAFETY_ANALYSIS.
    tasks_.modify(it, [worker](PriorityThreadPoolInternalTask& task) NO_THREAD_SAFETY_ANALYSIS {
      task.SetWorker(worker);
    });
  }

  int max_group_no_priority_to_defer() const {
    return prioritize_by_group_no_ ? max_group_no_priority_to_defer_.load() : kNoGroupPriority;
  }

  const size_t max_running_tasks_;
  // If true, priorities will be evaluated in groups first, followed by task priority.
  const bool prioritize_by_group_no_;
  std::mutex mutex_;

  // Number of paused workers.
  size_t paused_workers_ GUARDED_BY(mutex_) = 0;

  std::vector<yb::ThreadPtr> threads_ GUARDED_BY(mutex_);
  boost::container::stable_vector<PriorityThreadPoolWorker> workers_ GUARDED_BY(mutex_);
  std::vector<PriorityThreadPoolWorker*> free_workers_ GUARDED_BY(mutex_);
  std::atomic<bool> stopping_{false};

  // Used for quick check, whether task with provided priority should be paused.
  std::atomic<int> max_task_priority_to_defer_{kEmptyQueueTaskPriority};
  std::atomic<int> max_group_no_priority_to_defer_{kEmptyQueueGroupNoPriority};

  // Map from group_no to number of active tasks in the group.
  std::unordered_map<size_t, int> active_tasks_per_group_ GUARDED_BY(mutex_);

  // Controls the number of active references to the pool.
  std::shared_ptr<void> ticket_ = std::make_shared<size_t>();

  // Tag for index ordered by original priority and serial no.
  // Used to determine desired tasks to run.
  class PriorityTag;

  // Tag for index ordered by state, priority and serial no.
  // Used to determine which task should be started/resumed when worker is paused.
  class StateAndPriorityTag;

  // Tag for index hashed by serial no.
  // Used to find task for priority change.
  class SerialNoTag;

  // Tag for index hashed by group no.
  // Used to find tasks for group no to change priority.
  class GroupNoTag;

  boost::multi_index_container<
    PriorityThreadPoolInternalTask,
    boost::multi_index::indexed_by<
      boost::multi_index::ranked_unique<
        boost::multi_index::tag<PriorityTag>,
        boost::multi_index::identity<PriorityThreadPoolInternalTask>,
        PriorityTaskComparator
      >,
      boost::multi_index::ordered_unique<
        boost::multi_index::tag<StateAndPriorityTag>,
        boost::multi_index::identity<PriorityThreadPoolInternalTask>,
        StateAndPriorityTaskComparator
      >,
      boost::multi_index::hashed_non_unique<
        boost::multi_index::tag<GroupNoTag>,
        boost::multi_index::const_mem_fun<
          PriorityThreadPoolInternalTask, int, &PriorityThreadPoolInternalTask::group_no>
      >,
      boost::multi_index::hashed_unique<
        boost::multi_index::tag<SerialNoTag>,
        boost::multi_index::const_mem_fun<
          PriorityThreadPoolInternalTask, size_t, &PriorityThreadPoolInternalTask::serial_no>
      >
    >
  > tasks_ GUARDED_BY(mutex_);

  std::atomic<double> thread_creation_failure_probability_{0};
};

// ------------------------------------------------------------------------------------------------
// Forwarding method calls for the "pointer to impl" idiom
// ------------------------------------------------------------------------------------------------

PriorityThreadPool::PriorityThreadPool(size_t max_running_tasks, bool prioritize_by_group_no)
    : impl_(new Impl(max_running_tasks, prioritize_by_group_no)) {
}

PriorityThreadPool::~PriorityThreadPool() {
}

Status PriorityThreadPool::Submit(
    int task_priority, std::unique_ptr<PriorityThreadPoolTask>* task,
    const uint64_t group_no) {
  return impl_->Submit(task_priority, task, group_no);
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

bool PriorityThreadPool::ChangeTaskPriority(size_t serial_no, int priority) {
  return impl_->ChangeTaskPriority(serial_no, priority);
}

bool PriorityThreadPool::PrioritizeTask(size_t serial_no) {
  return impl_->PrioritizeTask(serial_no);
}

std::shared_ptr<void> PriorityThreadPool::ticket() const {
  return impl_->ticket();
}

void PriorityThreadPool::TEST_SetThreadCreationFailureProbability(double probability) {
  return impl_->TEST_SetThreadCreationFailureProbability(probability);
}

size_t PriorityThreadPool::TEST_num_tasks_pending() {
  return impl_->TEST_num_tasks_pending();
}

std::mutex* PriorityThreadPool::TEST_mutex() {
  return impl_->TEST_mutex();
}

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolToken::Impl
// ------------------------------------------------------------------------------------------------

PriorityThreadPoolToken::Impl::Impl(PriorityThreadPool& pool, size_t max_concurrency)
    : pool_(pool), pool_ticket_(pool_.ticket()), max_concurency_(max_concurrency) {
  LOG(INFO) << "PriorityThreadPoolToken created: " << AsString(this);
}

PriorityThreadPoolToken::Impl::~Impl() {
  StartShutdown();
  CompleteShutdown();

  // Sanity check to make sure all tasks were run/aborted.
  {
    SharedLock lock(mutex_);
    LOG_IF(DFATAL, active_tasks_.size())
        << "Shutting down a token with active tasks still running "
        << static_cast<void*>(this) << ToStringUnlocked();
    LOG_IF(DFATAL, deferred_tasks_.size())
        << "Shutting down a token with deferred tasks still queued "
        << static_cast<void*>(this) << ToStringUnlocked();
  }

  LOG(INFO) << "PriorityThreadPoolToken destroyed: " << AsString(this);
}

bool PriorityThreadPoolToken::Impl::EraseActiveTask(size_t serial_no) {
  bool erased = false;
  {
    std::lock_guard lock(mutex_);
    erased = active_tasks_.erase(serial_no);
  }
  if (erased) {
    YB_PROFILE(active_tasks_erase_cv_.notify_all());
  }
  return erased;
}

void PriorityThreadPoolToken::Impl::EraseDeferredTasks(
    const Status& status, std::optional<void*> task_key) {
  decltype(deferred_tasks_) tasks;
  {
    std::lock_guard lock(mutex_);
    if (!task_key.has_value()) {
      std::swap(tasks, deferred_tasks_);
    } else {
      auto it = deferred_tasks_.begin();
      while (it != deferred_tasks_.end()) {
        if (it->second->ShouldRemoveWithKey(*task_key)) {
          tasks.insert(std::move(*it));
          it = deferred_tasks_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  for (auto& task : tasks) {
    PriorityThreadPoolTaskExecutor::Run(*task.second, status);
  }
}

bool PriorityThreadPoolToken::Impl::UpdateTaskPriority(size_t serial_no) {
  // Try get active task priority.
  int priority;
  {
    SharedLock lock(mutex_);
    auto it = active_tasks_.find(serial_no);
    if (it == active_tasks_.end()) {
      return false;
    }
    priority = it->second->priority();
  }

  return pool_.ChangeTaskPriority(serial_no, priority);
}

bool PriorityThreadPoolToken::Impl::PrioritizeTask(size_t serial_no) {
  return pool_.PrioritizeTask(serial_no);
}

void PriorityThreadPoolToken::Impl::Remove(void* task_key) {
  EraseDeferredTasks(kRemoveTaskStatus, task_key);
  pool_.Remove(task_key);
}

void PriorityThreadPoolToken::Impl::SetMaxConcurrency(size_t new_limit) {
  {
    std::lock_guard lock(mutex_);
    max_concurency_ = new_limit;
    VLOG_WITH_FUNC(1) << "new max_concurency: " << max_concurency_;
  }
  ScheduleTasks();
}

void PriorityThreadPoolToken::Impl::StartShutdown() {
  EraseDeferredTasks(kShutdownStatus);
}

void PriorityThreadPoolToken::Impl::CompleteShutdown() {
  UniqueLock lock(mutex_);
  active_tasks_erase_cv_.wait(
      lock, [this]() NO_THREAD_SAFETY_ANALYSIS { return active_tasks_.empty(); });
}

void PriorityThreadPoolToken::Impl::ScheduleTasks() {
  VLOG_WITH_FUNC(4) << "Checking for next deferred tasks to schedule";

  // Get as many tasks as possible.
  std::vector<PriorityThreadPoolTokenTaskPtr> tasks_to_submit;
  {
    std::lock_guard lock(mutex_);
    size_t num_active_tasks = active_tasks_.size();
    if (max_concurency_ && num_active_tasks >= max_concurency_) {
      VLOG_WITH_FUNC(2) <<
          "Unable to schedule more tasks, num_active_tasks: " << num_active_tasks <<
          ", max_concurency: " << max_concurency_;
      return;
    }

    size_t num_tasks = deferred_tasks_.size();
    if (max_concurency_) {
      num_tasks = std::min(num_tasks, max_concurency_ - num_active_tasks);
    }

    if (num_tasks == 0) {
      return;
    }

    tasks_to_submit.reserve(num_tasks);
    auto it = deferred_tasks_.begin();
    auto it_end = std::next(it, num_tasks);
    while (it != it_end) {
      active_tasks_.emplace(it->first, it->second.get());
      tasks_to_submit.push_back(std::move(it->second));
      it = deferred_tasks_.erase(it);
    }
  }

  // Schedule picked tasks.
  VLOG_WITH_FUNC(2) << "Tasks to submit: " << AsString(tasks_to_submit);
  for (auto& task : tasks_to_submit) {
    const auto priority = task->priority();
    const auto group_no = task->group_no();
    auto status = pool_.Submit(priority, &task, group_no);
    if (!status.ok()) {
      // We can tolerate if task cannot be submitted, but pool shutdown is expected only.
      LOG_IF(DFATAL, !status.IsShutdownInProgress())
          << "Failed to start a deferred task, serial_no: " << task->SerialNo()
          << ", status: " << status;
      EraseActiveTask(task->SerialNo());
      PriorityThreadPoolTaskExecutor::Run(*task, status);
    }
  }
}

Status PriorityThreadPoolToken::Impl::Submit(PriorityThreadPoolTokenTaskPtr* task) {
  VLOG_WITH_FUNC(4) << "Adding deferred task: " << TaskToString(**task);

  // Add to a list of deferred tasks.
  const size_t serial_no = task->get()->SerialNo();
  {
    std::lock_guard lock(mutex_);
    deferred_tasks_.emplace(serial_no, std::move(*task));
  }

  ScheduleTasks();
  return Status::OK();
}

void PriorityThreadPoolToken::Impl::SanitizeTask(size_t serial_no) {
  SharedLock lock(mutex_);
  if (deferred_tasks_.contains(serial_no)) {
    // That is literally cannot happen and means some kind of a corruption, because a task
    // has been deleting from deferred queue before it is submitting to the pool.
    LOG_WITH_FUNC(FATAL) << Format(
        "Possible corruption, task {serial_no: $0} cannot be in deferred queue!", serial_no);
  }
}

void PriorityThreadPoolToken::Impl::NotifyTaskCompleted(size_t serial_no) {
  VLOG_WITH_FUNC(2) << Format("Task { serial_no: $0 }", serial_no);
  SanitizeTask(serial_no);

  // Remove task from the active tasks collection.
  if (EraseActiveTask(serial_no)) {
    VLOG_WITH_FUNC(4) << "Active task completed";
  } else {
    LOG_WITH_FUNC(DFATAL) << Format(
        "Task { serial_no: $0 } not found in the active tasks collection!", serial_no);
  }

  ScheduleTasks();
}

void PriorityThreadPoolToken::Impl::NotifyTaskDestroyed(size_t serial_no) {
  VLOG_WITH_FUNC(2) << Format("Task { serial_no: $0 }", serial_no);
  SanitizeTask(serial_no);

  // It is expected the task is being removed from the map in NotifyTaskCompleted.
  if (PREDICT_FALSE(EraseActiveTask(serial_no))) {
    LOG_WITH_FUNC(DFATAL) << Format(
        "Task { serial_no: $0 } should have been deleted on completion!", serial_no);
  }
}

std::string PriorityThreadPoolToken::Impl::TaskToString(const PriorityThreadPoolTokenTask& task) {
  return Format("$0, context: $1", AsString(&task), static_cast<const void*>(&task.context()));
}

std::string PriorityThreadPoolToken::Impl::ToString() const {
  SharedLock lock(mutex_);
  return ToStringUnlocked();
}

std::string PriorityThreadPoolToken::Impl::ToStringUnlocked() const {
  return YB_CLASS_TO_STRING(max_concurency, active_tasks, deferred_tasks);
}

// ------------------------------------------------------------------------------------------------
// PriorityThreadPoolToken::Task
// ------------------------------------------------------------------------------------------------

PriorityThreadPoolTokenTask::~PriorityThreadPoolTokenTask() {
  context_.NotifyTaskDestroyed(serial_no_);
}

void PriorityThreadPoolTokenTask::Execute(
    ExecuteTag tag, const Status& status, PriorityThreadPoolSuspender* suspender) {
  PriorityThreadPoolTask::Execute(std::move(tag), status, suspender);
  context_.NotifyTaskCompleted(serial_no_);
}

// ------------------------------------------------------------------------------------------------
// Forwarding method calls for the "pointer to impl" idiom
// ------------------------------------------------------------------------------------------------

PriorityThreadPoolToken::~PriorityThreadPoolToken() = default;

PriorityThreadPoolToken::PriorityThreadPoolToken(PriorityThreadPool& pool, size_t max_concurrency)
    : impl_(new Impl(pool, max_concurrency)) {
}

bool PriorityThreadPoolToken::UpdateTaskPriority(size_t serial_no) {
  return impl_->UpdateTaskPriority(serial_no);
}

bool PriorityThreadPoolToken::PrioritizeTask(size_t serial_no) {
  return impl_->PrioritizeTask(serial_no);
}

void PriorityThreadPoolToken::Remove(void* task_key) {
  return impl_->Remove(task_key);
}

void PriorityThreadPoolToken::SetMaxConcurrency(size_t new_limit) {
  return impl_->SetMaxConcurrency(new_limit);
}

Status PriorityThreadPoolToken::Submit(PriorityThreadPoolTokenTaskPtr* task) {
  return impl_->Submit(task);
}

void PriorityThreadPoolToken::StartShutdown() {
  return impl_->StartShutdown();
}

void PriorityThreadPoolToken::CompleteShutdown() {
  return impl_->CompleteShutdown();
}

PriorityThreadPoolTokenContext& PriorityThreadPoolToken::context() const {
  return *impl_;
}

std::string PriorityThreadPoolToken::ToString() const {
  return impl_->ToString();
}

} // namespace yb
