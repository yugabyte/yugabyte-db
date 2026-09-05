// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include <thread>

#include <boost/function.hpp>
#include <boost/intrusive/list.hpp>

#include "yb/rpc/thread_pool.h"

#include "yb/util/flags.h"
#include "yb/util/shared_lock.h"
#include "yb/util/sync_point.h"
#include "yb/util/unique_lock.h"

#include "yb/vector_index/vector_lsm.h"

DECLARE_uint64(vector_index_max_insert_tasks);
DECLARE_uint64(vector_index_max_merge_tasks);
DECLARE_uint64(vector_index_task_pool_size);

namespace yb::vector_index {

namespace bi = boost::intrusive;

using namespace std::literals;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertRegistryBase;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertTask :
    public rpc::ThreadPoolTask,
    public bi::list_base_hook<bi::link_mode<bi::normal_link>> {
 public:
  using InsertRegistry = VectorLSMInsertRegistryBase<Vector, DistanceResult>;
  using InsertCallback = boost::function<void(const Status&)>;
  using VectorIndexPtr = typename VectorLSM<Vector, DistanceResult>::VectorIndexPtr;
  using VectorWithDistance = typename VectorLSM<Vector, DistanceResult>::VectorWithDistance;
  using SearchHeap = std::priority_queue<VectorWithDistance>;

  void Bind(const VectorIndexPtr& index, std::shared_ptr<InsertRegistry> registry,
            InsertCallback insert_callback) {
    DCHECK(index);
    DCHECK(insert_callback);
    DCHECK(!index_);
    DCHECK(!insert_callback_);
    DCHECK(vectors_.empty());

    index_ = index;
    registry_ = std::move(registry);
    insert_callback_ = std::move(insert_callback);
  }

  void Add(VectorId vector_id, Vector&& vector) {
    vectors_.emplace_back(vector_id, std::move(vector));
  }

  void Run() override {
    insert_callback_(DoInsert());
    insert_callback_ = {};
  }

  void Done(const Status&) override {
    std::shared_ptr<InsertRegistry> registry;
    {
      std::lock_guard lock(mutex_);
      index_ = nullptr;
      registry = std::move(registry_);
      vectors_.clear();
    }

    // We are not really interested in the status as it could indicate shutting down
    // or abortion due to shutting down only. Make sure to unset done_callback_ before calling it.
    DCHECK(registry);
    registry->TaskDone(this);
  }

  void Search(SearchHeap& heap, const Vector& query_vector, const SearchOptions& options) const {
    SharedLock lock(mutex_);
    for (const auto& [id, vector] : vectors_) {
      if (!options.filter(id)) {
        continue;
      }
      auto distance = index_->Distance(query_vector, vector);
      VectorWithDistance vertex(id, distance);
      if (heap.size() < options.max_num_results) {
        heap.push(vertex);
      } else if (heap.top() > vertex) {
        heap.pop();
        heap.push(vertex);
      }
    }
  }

 protected:
  Status DoInsert() {
    DCHECK(index_);
    for (const auto& [vector_id, vector] : vectors_) {
      RETURN_NOT_OK(index_->Insert(vector_id, vector));
    }
    return Status::OK();
  }

  mutable rw_spinlock mutex_;
  std::shared_ptr<InsertRegistry> registry_;
  VectorIndexPtr index_;
  InsertCallback insert_callback_;
  std::vector<std::pair<VectorId, Vector>> vectors_;
};

// Registry for all active Vector LSM insert subtasks.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertRegistryBase
    : public std::enable_shared_from_this<VectorLSMInsertRegistryBase<Vector, DistanceResult>> {
 public:
  using InsertTask = VectorLSMInsertTask<Vector, DistanceResult>;
  using InsertTaskList = boost::intrusive::list<InsertTask>;
  using InsertTaskPtr = std::unique_ptr<InsertTask>;

  virtual ~VectorLSMInsertRegistryBase() = default;

  void Shutdown() {
    for (;;) {
      {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        if (allocated_tasks_ == 0) {
          break;
        }
      }
      YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 1) << "Waiting for vector insertion tasks to finish";
      std::this_thread::sleep_for(100ms);
    }
  }

  void ExecuteTasks(InsertTaskList& list) EXCLUDES(mutex_) {
    DCHECK(!list.empty());
    auto last = --list.end();
    auto it = list.begin();
    {
      std::lock_guard lock(mutex_);
      // splice does not invalidate iterators, so `it` and `last` stay usable below.
      active_tasks_.splice(active_tasks_.end(), list);
    }
    // ++it reads the visited task's successor link without the mutex. This is safe up to `last`:
    // the link is only rewritten when the successor is unlinked, which cannot happen before the
    // successor is enqueued, i.e. after the read. The link of `last` could be rewritten by a
    // concurrent splice at any moment, so iteration stops at `last` and never reads it.
    while (it != last) {
      auto& task = *it++;
      thread_pool_.Enqueue(&task);
    }
    thread_pool_.Enqueue(&*last);
    TEST_SYNC_POINT("VectorLSMInsertRegistryBase::ExecuteTasks:Enqueued");
  }

  void TaskDone(InsertTask* raw_task) EXCLUDES(mutex_) {
    DCHECK_ONLY_NOTNULL(raw_task);

    InsertTaskPtr task(raw_task);
    {
      std::lock_guard lock(mutex_);
      --allocated_tasks_;
      // Catches a task that completed before ExecuteTasks moved it to active_tasks_.
      DCHECK(!active_tasks_.empty());
      active_tasks_.erase(active_tasks_.iterator_to(*raw_task));
      if (task_pool_.size() < FLAGS_vector_index_task_pool_size) {
        task_pool_.push_back(std::move(task));
      }
      DoTaskDoneUnlocked();
    }
  }

  bool HasRunningTasks() {
    SharedLock lock(mutex_);
    return !active_tasks_.empty();
  }

 protected:
  using VectorIndexPtr = typename InsertTask::VectorIndexPtr;
  using InsertCallback = typename InsertTask::InsertCallback;

  VectorLSMInsertRegistryBase(std::string log_prefix, rpc::ThreadPool& thread_pool)
      : log_prefix_(std::move(log_prefix)), thread_pool_(thread_pool) {}

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  Result<InsertTaskList> DoAllocateTasks(
      size_t num_tasks, const VectorIndexPtr& index,
      InsertCallback&& insert_callback) REQUIRES(mutex_) {
    if (stopping_) {
      return STATUS_FORMAT(ShutdownInProgress, "VectorLSM registry is shutting down");
    }
    InsertTaskList result;
    allocated_tasks_ += num_tasks;
    for (size_t left = num_tasks; left-- > 0;) {
      InsertTaskPtr task;
      if (task_pool_.empty()) {
        task = std::make_unique<InsertTask>();
      } else {
        task = std::move(task_pool_.back());
        task_pool_.pop_back();
      }

      // Make sure insert_callback is not moved but copied as it is used in several tasks.
      task->Bind(index, this->shared_from_this(), insert_callback);

      result.push_back(*task.release());
    }
    return result;
  }

  virtual void DoTaskDoneUnlocked() REQUIRES(mutex_) {
    // Nothing to do, could be used in derived classes.
  }

  const std::string log_prefix_;
  rpc::ThreadPool& thread_pool_;
  std::shared_mutex mutex_;
  bool stopping_ GUARDED_BY(mutex_) = false;
  size_t allocated_tasks_ GUARDED_BY(mutex_) = 0;
  InsertTaskList active_tasks_ GUARDED_BY(mutex_);
  std::vector<InsertTaskPtr> task_pool_ GUARDED_BY(mutex_);
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertRegistry : public VectorLSMInsertRegistryBase<Vector, DistanceResult> {
 public:
  using Base = VectorLSMInsertRegistryBase<Vector, DistanceResult>;
  using InsertTask = typename Base::InsertTask;
  using InsertTaskList = typename Base::InsertTaskList;
  using SearchResults  = typename VectorLSM<Vector, DistanceResult>::SearchResults;

  VectorLSMInsertRegistry(const std::string& log_prefix, rpc::ThreadPool& thread_pool)
      : Base(Format("$0[I] ", log_prefix), thread_pool) {}

  template <typename... Args>
  Result<InsertTaskList> AllocateTasks(size_t num_tasks, Args&&... args) EXCLUDES(mutex_) {
    UniqueLock lock(mutex_);
    while (allocated_tasks_ &&
            allocated_tasks_ + num_tasks >= FLAGS_vector_index_max_insert_tasks) {
      // TODO(vector_index) Pass timeout here.
      if (allocated_tasks_cond_.wait_for(GetLockForCondition(lock), 1s) ==
              std::cv_status::timeout) {
        auto allocated_tasks = allocated_tasks_;
        lock.unlock();
        LOG_WITH_FUNC(WARNING)
            << "Long wait to allocate " << num_tasks << " tasks, allocated: " << allocated_tasks
            << ", allowed: " << FLAGS_vector_index_max_insert_tasks;
        lock.lock();
      }
    }

    return DoAllocateTasks(num_tasks, std::forward<Args>(args)...);
  }

  void DoTaskDoneUnlocked() override REQUIRES(mutex_) {
    allocated_tasks_cond_.notify_all();
  }

  SearchResults Search(const Vector& query_vector, const SearchOptions& options) {
    typename InsertTask::SearchHeap heap;
    {
      SharedLock lock(mutex_);
      for (const auto& task : active_tasks_) {
        task.Search(heap, query_vector, options);
      }
    }
    return ReverseHeapToVector(heap);
  }

 private:
  using Base::LogPrefix;
  using Base::DoAllocateTasks;
  using Base::mutex_;
  using Base::active_tasks_;
  using Base::allocated_tasks_;

  std::condition_variable_any allocated_tasks_cond_;
};

// Registry for all active Vector LSM insert subtasks.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMMergeRegistry : public VectorLSMInsertRegistryBase<Vector, DistanceResult> {
 public:
  using Base = VectorLSMInsertRegistryBase<Vector, DistanceResult>;
  using InsertTaskList = typename Base::InsertTaskList;

  VectorLSMMergeRegistry(const std::string& log_prefix, rpc::ThreadPool& thread_pool)
      : Base(Format("$0[M] ", log_prefix), thread_pool) {}

  template <typename... Args>
  Result<InsertTaskList> AllocateTasks(size_t num_tasks, Args&&... args) EXCLUDES(mutex_) {
    // Sanity check for the case the flag has been set to 0 right before calling this method.
    size_t max_tasks = MaxCapacity();
    if (max_tasks == 0) {
      max_tasks = 1;
      LOG_WITH_PREFIX(INFO) << "Max merge tasks flag is 0, using 1 instead";
    }

    {
      UniqueLock lock(mutex_);
      if (allocated_tasks_ >= max_tasks) {
        return InsertTaskList{};
      }

      num_tasks = std::min(num_tasks, max_tasks - allocated_tasks_);
      return DoAllocateTasks(num_tasks, std::forward<Args>(args)...);
    }
  }

  size_t MaxCapacity() const {
    return FLAGS_vector_index_max_merge_tasks;
  }

 protected:
  using Base::LogPrefix;
  using Base::DoAllocateTasks;
  using Base::mutex_;
  using Base::allocated_tasks_;
};

}  // namespace yb::vector_index

