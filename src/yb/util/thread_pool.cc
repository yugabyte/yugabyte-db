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

#include "yb/util/thread_pool.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <boost/intrusive/list.hpp>

#include "yb/util/cgroups.h"
#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/lockfree.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;

DEFINE_NON_RUNTIME_uint64(default_idle_timeout_ms, 15000,
    "Default RPC YBThreadPool idle timeout value in milliseconds");

static bool detailed_logging = true;
namespace yb {

bool TEST_fail_to_create_second_thread_in_thread_pool_without_queue = false;

void YBThreadPool::DisableDetailedLogging() { detailed_logging = false; }

namespace {

class Worker;
struct WorkerLink;

using TaskQueue = SemiFairQueue<ThreadPoolTask>;
using WaitingWorkers = LockFreeStack<WorkerLink>;

constexpr size_t kStopCreatingWorkersFlag = 1ul << 48;

struct ThreadPoolShare {
  const ThreadPoolOptions options;
  TaskQueue task_queue;
  std::atomic<size_t> task_queue_size{0};
  WaitingWorkers waiting_workers;
  std::atomic<size_t> num_workers{0};

  explicit ThreadPoolShare(ThreadPoolOptions o)
      : options(std::move(o)) {}

  void PushTask(ThreadPoolTask* task) {
    if (options.metrics.queue_time_us_stats) {
      task->set_submit_time(MonoTime::Now());
    }
    task_queue.Push(task);
    task_queue_size.fetch_add(1, std::memory_order_acq_rel);
  }

  ThreadPoolTask* PopTask() {
    auto result = task_queue.Pop();
    if (result) {
      task_queue_size.fetch_sub(1, std::memory_order_acq_rel);
      if (options.metrics.queue_time_us_stats) {
        options.metrics.queue_time_us_stats->Increment(
            (MonoTime::Now() - result->submit_time()).ToMicroseconds());
      }
    }
    return result;
  }
};

const auto kShuttingDownStatus = STATUS(Aborted, "Service is shutting down");

// There is difference between idle stop and external stop.
// In case of idle stop we don't perform join on thread, since it is already detached.
YB_DEFINE_ENUM(WorkerState, (kRunning)(kWaitingTask)(kIdleStop)(kExternalStop));

class Worker : public boost::intrusive::list_base_hook<> {
 public:
  explicit Worker(ThreadPoolShare& share, bool persistent);

  Status Start(size_t index, ThreadPoolTask* task) EXCLUDES(mutex_) {
    UniqueLock lock(mutex_);
    SCHECK_EQ(state_, WorkerState::kRunning, IllegalState, "Worker already stopped");
    auto name = strings::Substitute("$0_$1_worker", share_.options.name, index);
    return Thread::Create(share_.options.name, name, &Worker::Execute, this, task, &thread_);
  }

  ~Worker();

  Worker(const Worker& worker) = delete;
  void operator=(const Worker& worker) = delete;

  void Stop() {
    ThreadPoolTask* task;
    {
      std::lock_guard lock(mutex_);
      if (state_ != WorkerState::kIdleStop) {
        state_ = WorkerState::kExternalStop;
        --share_.num_workers;
      }
      task = std::exchange(task_, nullptr);
      cond_.notify_one();
    }
    if (task) {
      task->Done(kShuttingDownStatus);
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

  bool Idle() const {
    std::lock_guard lock(mutex_);
    return added_to_waiting_workers_;
  }

 private:
  // Our main invariant is empty task queue or empty worker queue.
  // In other words, one of those queues should be empty.
  // Meaning that we do not have work (task queue empty) or
  // does not have free hands (worker queue empty)
  void Execute(ThreadPoolTask* task) {
#ifdef __linux__
    if (auto cgroup = share_.options.cgroup ? share_.options.cgroup : DefaultThreadCgroup()) {
      auto status = cgroup->MoveCurrentThreadToGroup();
      if (!status.ok()) {
        LOG(DFATAL) << "Failed to move thread to cgroup: " << status;
      }
    }
#endif

    Thread::current_thread()->SetUserData(&share_);
    bool has_run_metrics = share_.options.metrics.run_time_us_stats != nullptr;
    if (!task) {
      task = PopTask();
    }
    while (task) {
      auto start = MonoTime::NowIf(has_run_metrics);
      if (!task->run_token()) {
        task->Run();
        task->Done(Status::OK());
      } else {
        auto run_token = task->run_token()->lock();
        if (run_token) {
          task->Run();
          task->Done(Status::OK());
        } else {
          task->Done(kShuttingDownStatus);
        }
      }
      if (has_run_metrics) {
        share_.options.metrics.run_time_us_stats->Increment(
            (MonoTime::Now() - start).ToMicroseconds());
      }
      task = PopTask();
    }
  }

  ThreadPoolTask* PopTask() {
    // First of all we try to get already queued task, w/o locking.
    // If there is no task, so we could go to waiting state.
    if (auto* task = share_.PopTask()) {
      return task;
    }

    UniqueLock lock(mutex_);
    if (auto task_opt = DoPopTask()) {
      return *task_opt;
    }

    state_ = WorkerState::kWaitingTask;

    for (;;) {
      AddToWaitingWorkers();

      if (auto task = share_.PopTask()) {
        state_ = WorkerState::kRunning;
        return task;
      }

      bool timeout;
      if (!persistent_ && share_.options.idle_timeout) {
        CHECK(!task_);
        auto duration = share_.options.idle_timeout.ToSteadyDuration();
        timeout = cond_.wait_for(GetLockForCondition(lock), duration) == std::cv_status::timeout;
      } else {
        cond_.wait(GetLockForCondition(lock));
        timeout = false;
      }

      if (state_ == WorkerState::kExternalStop) {
        return nullptr;
      }
      if (task_) {
        state_ = WorkerState::kRunning;
        return std::exchange(task_, nullptr);
      }

      if (timeout && added_to_waiting_workers_) {
        if (auto task = share_.PopTask()) {
          state_ = WorkerState::kRunning;
          return task;
        }
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
    if (auto task = share_.PopTask()) {
      return task;
    }
    return std::nullopt;
  }

  void AddToWaitingWorkers() REQUIRES(mutex_) {
    if (!added_to_waiting_workers_) {
      share_.waiting_workers.Push(link_);
      added_to_waiting_workers_ = true;
    }
  }

  ThreadPoolShare& share_;
  const bool persistent_;
  WorkerLink* link_ = nullptr;
  scoped_refptr<Thread> thread_;
  mutable std::mutex mutex_;
  std::condition_variable cond_;
  WorkerState state_ GUARDED_BY(mutex_) = WorkerState::kRunning;
  bool added_to_waiting_workers_ GUARDED_BY(mutex_) = false;
  ThreadPoolTask* task_ GUARDED_BY(mutex_) = nullptr;
};

struct WorkerLink {
  Worker* worker = nullptr;
  std::atomic<WorkerLink*> next_link{nullptr};
};

using Workers = boost::intrusive::list<Worker>;

void SetNext(WorkerLink& worker, WorkerLink* next) {
  worker.next_link.store(next, std::memory_order_release);
}

WorkerLink* GetNext(WorkerLink& worker) {
  return worker.next_link.load(std::memory_order_acquire);
}

class FreeWorkerLinks {
 public:
  ~FreeWorkerLinks() {
    while (auto link = free_links_.Pop()) {
      delete link;
    }
  }

  WorkerLink* Acquire() {
    auto link = free_links_.Pop();
    if (!link) {
      link = new WorkerLink{};
    }
    return link;
  }

  void Release(WorkerLink* link) {
    link->worker = nullptr;
    free_links_.Push(link);
  }

 private:
  LockFreeStack<WorkerLink> free_links_;
};

FreeWorkerLinks free_worker_links_;

Worker::Worker(ThreadPoolShare& share, bool persistent)
    : share_(share), persistent_(persistent),
      link_(free_worker_links_.Acquire()) {
  link_->worker = this;
}

Worker::~Worker() {
  {
    std::lock_guard lock(mutex_);
    DCHECK(!task_);
    if (state_ == WorkerState::kIdleStop) {
      free_worker_links_.Release(link_);
      return;
    }
  }
  if (thread_) {
    thread_->Join();
  }
  free_worker_links_.Release(link_);
}

} // namespace

class YBThreadPool::Impl {
 public:
  explicit Impl(ThreadPoolOptions options)
      : share_(std::move(options)) {
    if (detailed_logging) {
      LOG(INFO) << "Starting thread pool " << share_.options.ToString();
    } else {
      VLOG(1) << "Starting thread pool " << share_.options.ToString();
    }

    for (size_t index = 0; index != options.min_workers; ++index) {
      if (!TryStartNewWorker(nullptr, /* persistent = */ true).ok()) {
        break;
      }
    }
  }

  const ThreadPoolOptions& options() const {
    return share_.options;
  }

  bool Enqueue(ThreadPoolTask* task) EXCLUDES(mutex_) {
    ++adding_;
    if (closing_) {
      --adding_;
      task->Done(kShuttingDownStatus);
      return false;
    }

    if (share_.options.metrics.queue_length_stats) {
      share_.options.metrics.queue_length_stats->Increment(
          share_.task_queue_size.load(std::memory_order_relaxed));
    }

    if (NotifyWorker(task)) {
      --adding_;
      return true;
    }

    {
      auto start_worker_status = TryStartNewWorker(task, /* persistent= */ false);
      if (start_worker_status.ok()) {
        --adding_;
        return true;
      } else if (share_.options.max_workers == ThreadPoolOptions::kUnlimitedWorkersWithoutQueue) {
        task->Done(start_worker_status);
        --adding_;
        return false;
      }
    }

    // We can get here in 3 cases:
    // 1) Reached number of workers and all workers are busy.
    // 2) Failed to start a new thread for a worker.
    // 3) We are shutting down. In this case we add task to the queue, so it will be processed by
    //    Shutdown.
    share_.PushTask(task);
    --adding_;
    NotifyWorker(nullptr);
    return true;
  }

  Status TryStartNewWorker(ThreadPoolTask* task, bool persistent) EXCLUDES(mutex_) {
    Worker* worker = nullptr;
    if (share_.num_workers++ < share_.options.max_workers) {
      std::lock_guard lock(mutex_);
      if (!closing_) {
        auto new_worker = std::make_unique<Worker>(share_, persistent);
        workers_.push_back(*(worker = new_worker.release()));
      }
    }
    Status status;
    if (worker) {
      if (TEST_fail_to_create_second_thread_in_thread_pool_without_queue &&
          share_.options.max_workers == ThreadPoolOptions::kUnlimitedWorkersWithoutQueue &&
          worker_counter_ != 0) {
        status = STATUS_FORMAT(RuntimeError, "TEST: Artificial start thread failure");
      } else {
        status = worker->Start(++worker_counter_, task);
      }
      if (status.ok()) {
        if (task && share_.options.metrics.queue_time_us_stats) {
          share_.options.metrics.queue_time_us_stats->Increment(0);
        }
        return Status::OK();
      }
      bool empty;
      {
        std::lock_guard lock(mutex_);
        if (!closing_) {
          workers_.erase_and_dispose(workers_.iterator_to(*worker), std::default_delete<Worker>());
          empty = workers_.empty();
        } else {
          empty = false;
        }
      }
      if (empty) {
        LOG_WITH_PREFIX(FATAL) << "Unable to start first worker: " << status;
      } else {
        LOG_WITH_PREFIX(WARNING) << "Unable to start worker: " << status;
      }
    } else {
      static const Status kReachedWorkersLimitStatus = STATUS(
          RuntimeError, "Reached workers limit");
      status = kReachedWorkersLimitStatus;
    }
    --share_.num_workers;
    return status;
  }

  // Returns true if we found worker that will pick up this task, false otherwise.
  bool NotifyWorker(ThreadPoolTask* task) {
    while (auto link = share_.waiting_workers.Pop()) {
      auto state = link->worker->Notify(task);
      switch (state) {
        case WorkerState::kWaitingTask:
          if (task && share_.options.metrics.queue_time_us_stats) {
            share_.options.metrics.queue_time_us_stats->Increment(0);
          }
          return true;
        case WorkerState::kExternalStop: [[fallthrough]];
        case WorkerState::kRunning:
          break;
        case WorkerState::kIdleStop: {
          std::lock_guard lock(mutex_);
          if (!closing_) {
            workers_.erase_and_dispose(
                workers_.iterator_to(*link->worker), std::default_delete<Worker>());
          }
        } break;
      }
    }
    return false;
  }

  std::string LogPrefix() const {
    return share_.options.name + ": ";
  }

  void Shutdown() EXCLUDES(mutex_) {
    // Prevent new worker threads from being created by pretending a large number of workers have
    // already been created.
    share_.num_workers ^= kStopCreatingWorkersFlag;
    decltype(workers_) workers;
    {
      std::lock_guard lock(mutex_);
      if (closing_) {
        CHECK(share_.task_queue.Empty());
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
    while (auto* task = share_.PopTask()) {
      task->Done(kShuttingDownStatus);
    }

    workers.clear_and_dispose(std::default_delete<Worker>());
  }

  bool Owns(Thread* thread) {
    return thread && thread->user_data() == &share_;
  }

  bool BusyWait(MonoTime deadline) {
    while (!Idle()) {
      if (deadline && MonoTime::Now() > deadline) {
        return false;
      }
      std::this_thread::sleep_for(1ms);
    }
    return true;
  }

  bool Idle() {
    if (adding_ || !share_.task_queue.Empty()) {
      return false;
    }
    std::lock_guard lock(mutex_);
    for (const auto& worker : workers_) {
      if (!worker.Idle()) {
        return false;
      }
    }
    return true;
  }

  size_t NumWorkers() const {
    return share_.num_workers.load(std::memory_order_relaxed) & ~kStopCreatingWorkersFlag;
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

YBThreadPool::YBThreadPool(ThreadPoolOptions options)
    : impl_(new Impl(std::move(options))) {
}

YBThreadPool::YBThreadPool(YBThreadPool&& rhs) noexcept
    : impl_(std::move(rhs.impl_)) {}

YBThreadPool& YBThreadPool::operator=(YBThreadPool&& rhs) noexcept {
  impl_->Shutdown();
  impl_ = std::move(rhs.impl_);
  return *this;
}

YBThreadPool::~YBThreadPool() {
  if (impl_) {
    impl_->Shutdown();
  }
}

bool YBThreadPool::Enqueue(ThreadPoolTask* task) {
  return impl_->Enqueue(task);
}

void YBThreadPool::Shutdown() {
  impl_->Shutdown();
}

const ThreadPoolOptions& YBThreadPool::options() const {
  return impl_->options();
}

bool YBThreadPool::Owns(Thread* thread) {
  return impl_->Owns(thread);
}

bool YBThreadPool::OwnsThisThread() {
  return Owns(Thread::current_thread());
}

bool YBThreadPool::BusyWait(MonoTime deadline) {
  return impl_->BusyWait(deadline);
}

size_t YBThreadPool::NumWorkers() const {
  return impl_->NumWorkers();
}

bool YBThreadPool::Idle() const {
  return impl_->Idle();
}

// ------------------------------------------------------------------------------------------------
// ThreadSubPoolBase
// ------------------------------------------------------------------------------------------------

void ThreadSubPoolBase::Shutdown() {
  auto active_enqueues = active_enqueues_.fetch_add(kStopMark, std::memory_order_acq_rel);
  if (active_enqueues >= kStopMark) {
    while (!(active_enqueues & kStoppedMark)) {
      std::this_thread::sleep_for(1ms);
      active_enqueues = active_enqueues_.load(std::memory_order_acquire);
    }
    return;
  }
  while (active_enqueues & (kStopMark - 1)) {
    std::this_thread::sleep_for(1ms);
    active_enqueues = active_enqueues_.load(std::memory_order_acquire);
  }
  std::weak_ptr<void> run_token = std::exchange(run_token_, nullptr);
  // We expected shutdown to happen rarely, so just use busy wait here.
  while (!run_token.expired()) {
    std::this_thread::sleep_for(1ms);
  }
  AbortTasks();
  active_enqueues_.fetch_add(kStoppedMark, std::memory_order_acq_rel);
}

void ThreadSubPoolBase::AbortTasks() {
}

// ------------------------------------------------------------------------------------------------
// ThreadSubPool
// ------------------------------------------------------------------------------------------------

ThreadSubPool::ThreadSubPool(YBThreadPool* thread_pool) : ThreadSubPoolBase(thread_pool) {
}

ThreadSubPool::~ThreadSubPool() {
}

bool ThreadSubPool::Enqueue(ThreadPoolTask* task) {
  return EnqueueHelper([this, task](bool ok) {
    if (ok) {
      task->set_run_token(run_token_);
      return thread_pool_.Enqueue(task);
    }
    task->Done(STATUS(Aborted, "Thread sub-pool closing"));
    return false;
  });
}

MonoDelta DefaultIdleTimeout() {
  return MonoDelta::FromMilliseconds(FLAGS_default_idle_timeout_ms);
}


// ------------------------------------------------------------------------------------------------
// YBTaggedThreadPools
// ------------------------------------------------------------------------------------------------

YBTaggedThreadPools::YBTaggedThreadPools(YBTaggedThreadPools::OptionsGenerator options_generator)
    : options_generator_{std::move(options_generator)} {}

YBTaggedThreadPools::~YBTaggedThreadPools() {
  Shutdown();
}

void YBTaggedThreadPools::Shutdown() {
  PoolMap pools;
  {
    std::lock_guard lock(mutex_);
    shutting_down_ = true;
    pools_by_tag_.Emplace();
    std::swap(pools, pools_holder_);
  }
  for (auto& pool : pools | std::views::values) {
    pool->Shutdown();
  }
}

YBThreadPoolScopedPtr YBTaggedThreadPools::LookupPool(Tag tag) {
  auto pools_by_tag = pools_by_tag_.get();
  auto itr = pools_by_tag->find(tag);
  if (itr != pools_by_tag->end()) {
    return itr->second;
  }
  return nullptr;
}

Result<YBThreadPoolScopedPtr> YBTaggedThreadPools::Pool(Tag tag) {
  if (auto pool = LookupPool(tag)) {
    return pool;
  }

  UniqueLock<std::mutex> lock(mutex_);
  if (auto pool = LookupPool(tag)) {
    return pool;
  }
  if (shutting_down_) {
    return STATUS(ShutdownInProgress, "thread pools shutting down");
  }

  CleanupIdlePools(lock);

  auto pool = pools_holder_.try_emplace(
      tag, make_scoped_refptr<RefCountedData<YBThreadPool>>(options_generator_(tag))).first->second;
  pools_by_tag_.Emplace(pools_holder_);
  return pool;
}

void YBTaggedThreadPools::CleanupIdlePools(UniqueLock<std::mutex>& lock) {
  // To avoid the case where someone calls Pool(tag) on an idle pool, then CleanupIdlePools()
  // shuts down the pool before they are able to queue on the pool, we do the following:
  // 1. prevent new callers of Pool(tag) from accessing the pool without mutex, by updating
  //    pools_by_tag_ with idle pools removed.
  // 2. shutdown and delete any pool with ref_count == 2 and no workers, since this means no one
  //    else has access to it to queue new tasks, and nothing is running.
  // 3. add any other removed pools back to pools_by_tag_, since someone else holds a reference and
  //    may queue new tasks.
  // This depends on the ref_count increment having acquire memory order, which is true for
  // RefCountedThreadSafe + scoped_refptr but not for std::shared_ptr.
  std::vector<YBThreadPoolScopedPtr> shutdown;

  PoolMap temp_pools_by_tag;
  PoolMap shutdown_candidates;
  for (auto& [tag, pool] : pools_holder_) {
    // Two references: pools_holder_ and pools_by_tag_.
    if (pool.HasTwoRef() && pool->NumWorkers() == 0) {
      shutdown_candidates.try_emplace(tag, pool);
    } else {
      temp_pools_by_tag.try_emplace(tag, pool);
    }
  }
  pools_by_tag_.Emplace(std::move(temp_pools_by_tag));
  for (auto& [tag, pool] : shutdown_candidates) {
    // Two references: pools_holder_ and to_remove.
    if (pool.HasTwoRef() && pool->NumWorkers() == 0) {
      shutdown.emplace_back(std::move(pool));
      pools_holder_.erase(tag);
    }
  }
  pools_by_tag_.Emplace(pools_holder_);

  [&] NO_THREAD_SAFETY_ANALYSIS {
    lock.unlock();
    for (auto& pool : shutdown) {
      pool->Shutdown();
    }
    lock.lock();
  }();
}

} // namespace yb
