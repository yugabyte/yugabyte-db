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

#include "yb/docdb/vector_lsm.h"

#include <queue>
#include <thread>

#include <boost/intrusive/list.hpp>

#include "yb/rocksdb/env.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/flags.h"
#include "yb/util/file_util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;

DEFINE_RUNTIME_uint64(vector_index_task_size, 16,
                      "Number of vectors in a single insert subtask for vector index. "
                      "When the number of elements in the batch is more than this times the task "
                      "size multiple tasks will be used to insert batch.");

DEFINE_RUNTIME_uint64(vector_index_max_insert_tasks, 1000,
                      "Limit for number of insert subtask for vector index. "
                      "When this limit is reached, new inserts are blocked until necessary amount "
                      "of tasks is released.");

DEFINE_RUNTIME_uint64(vector_index_task_pool_size, 1000,
                      "Pool size of insert subtasks for vector index. "
                      "Pool is just used to avoid memory allocations and does not limit the total "
                      "number of tasks.");

namespace yb::docdb {

using vectorindex::IndexableVectorType;
using vectorindex::ValidDistanceResultType;
using vectorindex::VertexId;
namespace bi = boost::intrusive;

namespace {

std::string GetIndexChunkPath(std::string storage_dir, size_t chunk_index) {
  return JoinPathSegments(storage_dir, Format("vectorindex_$0", chunk_index));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertTask :
    public rpc::ThreadPoolTask,
    public bi::list_base_hook<bi::link_mode<bi::normal_link>> {
 public:
  using Types = VectorLSMTypes<Vector, DistanceResult>;
  using InsertRegistry = typename Types::InsertRegistry;
  using Chunk = typename Types::Chunk;
  using VertexWithDistance = typename Types::VertexWithDistance;
  using SearchHeap = std::priority_queue<VertexWithDistance>;

  explicit VectorLSMInsertTask(InsertRegistry& registry) : registry_(registry) {
  }

  void Bind(Chunk& chunk) {
    chunk_ = &chunk;
  }

  void Add(VertexId vertex_id, Vector vector) {
    vectors_.emplace_back(vertex_id, std::move(vector));
  }

  void Run() override {
    for (const auto& [vertex_id, vector] : vectors_) {
      // TODO(lsm) handle failure
      CHECK_OK(chunk_->Insert(vertex_id, vector));
    }
  }

  void Search(SearchHeap& heap, const Vector& query_vector, size_t max_num_results) const {
    SharedLock lock(mutex_);
    for (const auto& [id, vector] : vectors_) {
      auto distance = chunk_->Distance(query_vector, vector);
      VertexWithDistance vertex(id, distance);
      if (heap.size() < max_num_results) {
        heap.push(vertex);
      } else if (heap.top() > vertex) {
        heap.pop();
        heap.push(vertex);
      }
    }
  }

  void Done(const Status& status) override;
 private:
  mutable rw_spinlock mutex_;
  InsertRegistry& registry_;
  Chunk* chunk_;
  std::vector<std::pair<VertexId, Vector>> vectors_;
};

} // namespace

// Registry for all active Vector LSM insert subtasks.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertRegistry {
 public:
  using Types = VectorLSMTypes<Vector, DistanceResult>;
  using Chunk = typename Types::Chunk;
  using VertexWithDistance = typename Types::VertexWithDistance;
  using InsertTask = VectorLSMInsertTask<Vector, DistanceResult>;
  using InsertTaskList = boost::intrusive::list<InsertTask>;
  using InsertTaskPtr = std::unique_ptr<InsertTask>;

  explicit VectorLSMInsertRegistry(rpc::ThreadPool& thread_pool)
      : thread_pool_(thread_pool) {}

  ~VectorLSMInsertRegistry() {
    for (;;) {
      {
        std::shared_lock lock(mutex_);
        if (active_tasks_.empty()) {
          break;
        }
      }
      YB_LOG_EVERY_N_SECS(INFO, 1) << "Waiting for vector insertion tasks to finish";
      std::this_thread::sleep_for(100ms);
    }
  }

  InsertTaskList AllocateTasks(Chunk& chunk, size_t num_tasks) EXCLUDES(mutex_) {
    InsertTaskList result;
    {
      UniqueLock lock(mutex_);
      while (allocated_tasks_ + num_tasks >= FLAGS_vector_index_max_insert_tasks) {
        // TODO(lsm) Pass timeout here.
        allocated_tasks_cond_.wait(GetLockForCondition(lock));
      }
      allocated_tasks_ += num_tasks;
      for (size_t left = num_tasks; left-- > 0;) {
        InsertTaskPtr task;
        if (task_pool_.empty()) {
          task = std::make_unique<InsertTask>(*this);
        } else {
          task = std::move(task_pool_.back());
          task_pool_.pop_back();
        }
        task->Bind(chunk);
        result.push_back(*task.release());
      }
    }
    return result;
  }

  void ExecuteTasks(InsertTaskList& list) EXCLUDES(mutex_) {
    for (auto& task : list) {
      thread_pool_.Enqueue(&task);
    }
    std::lock_guard lock(mutex_);
    active_tasks_.splice(active_tasks_.end(), list);
  }

  void TaskDone(InsertTask* raw_task) EXCLUDES(mutex_) {
    InsertTaskPtr task(raw_task);
    std::lock_guard lock(mutex_);
    --allocated_tasks_;
    allocated_tasks_cond_.notify_all();
    active_tasks_.erase(active_tasks_.iterator_to(*raw_task));
    if (task_pool_.size() < FLAGS_vector_index_task_pool_size) {
      task_pool_.push_back(std::move(task));
    }
  }

  typename Chunk::SearchResult Search(const Vector& query_vector, size_t max_num_results) {
    typename InsertTask::SearchHeap heap;
    {
      SharedLock lock(mutex_);
      for (const auto& task : active_tasks_) {
        task.Search(heap, query_vector, max_num_results);
      }
    }
    return ReverseHeapToVector(heap);
  }

  bool HasRunningTasks() {
    SharedLock lock(mutex_);
    return !active_tasks_.empty();
  }

 private:
  rpc::ThreadPool& thread_pool_;
  std::shared_mutex mutex_;
  std::condition_variable_any allocated_tasks_cond_;
  size_t allocated_tasks_ GUARDED_BY(mutex_) = 0;
  InsertTaskList active_tasks_ GUARDED_BY(mutex_);
  std::vector<InsertTaskPtr> task_pool_ GUARDED_BY(mutex_);
};


template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSMInsertTask<Vector, DistanceResult>::Done(const Status& status) {
  {
    std::lock_guard lock(mutex_);
    vectors_.clear();
  }
  registry_.TaskDone(this);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::VectorLSM()
    : env_(rocksdb::Env::Default()) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::~VectorLSM() = default;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Open(Options options) {
  options_ = std::move(options);
  insert_registry_ = std::make_unique<InsertRegistry>(*options_.insert_thread_pool);

  return env_->CreateDirIfMissing(options_.storage_dir);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Insert(
    std::vector<InsertEntry> entries, HybridTime write_time) {
  ChunkPtr chunk;
  {
    std::lock_guard lock(mutex_);
    if (!mutable_chunk_) {
      RETURN_NOT_OK(CreateNewMutableChunk());
    } else if (entries_in_mutable_chunks_ &&
               entries_in_mutable_chunks_ + entries.size() > options_.points_per_chunk) {
      // TODO(lsm) Handle size of entries greater than points_per_chunk.
      RETURN_NOT_OK(RollChunk());
    }
    chunk = mutable_chunk_;
    entries_in_mutable_chunks_ += entries.size();
  }

  size_t num_tasks = ceil_div<size_t>(entries.size(), FLAGS_vector_index_task_size);
  size_t entries_per_task = ceil_div(entries.size(), num_tasks);

  auto tasks = insert_registry_->AllocateTasks(*chunk, num_tasks);
  auto tasks_it = tasks.begin();
  size_t index_in_task = 0;
  BaseTableKeysBatch keys_batch;
  for (auto& [vertex_id, base_table_key, v] : entries) {
    if (index_in_task++ >= entries_per_task) {
      ++tasks_it;
      index_in_task = 0;
    }
    tasks_it->Add(vertex_id, std::move(v));
    keys_batch.emplace_back(vertex_id, base_table_key.AsSlice());
  }
  RETURN_NOT_OK(options_.key_value_storage->StoreBaseTableKeys(keys_batch, write_time));
  insert_registry_->ExecuteTasks(tasks);

  return Status::OK();
}

// Merges results from multiple chunks.
// results_with_chunk - already merged results from chunks on previous steps.
// chunk_results - results from a new chunk - chunk_index.
// max_num_results - limit for number of results.
// Expects that results_with_chunk and chunk_results already ordered by distance.
template<ValidDistanceResultType DistanceResult>
void MergeChunkResults(
    std::vector<vectorindex::VertexWithDistance<DistanceResult>>& combined_results,
    std::vector<vectorindex::VertexWithDistance<DistanceResult>>& chunk_results,
    size_t max_num_results) {
  // Store the current size of the existing results.
  auto old_size = std::min(combined_results.size(), max_num_results);


  // Because of concurrency we could get the same vertex from different sources.
  // So remove duplicates from chunk_results before merging.
  {
    // It could happen that results were not sorted by vertex id, when distances are equal.
    std::sort(chunk_results.begin(), chunk_results.end());
    auto it = combined_results.begin();
    auto end = combined_results.end();
    std::erase_if(chunk_results, [&it, end](const auto& entry) {
      while (it != end) {
        if (entry > *it) {
          ++it;
        } else if (entry.vertex_id == it->vertex_id) {
          return true;
        } else {
          break;
        }
      }
      return false;
    });
  }

  // Calculate the combined size of the existing and new chunk results.
  auto sum_size = old_size + chunk_results.size();

  // Resize the results vector to hold the merged results, but cap it at max_num_results.
  combined_results.resize(std::min(sum_size, max_num_results));

  // Initialize iterators for traversing both results:
  // lhi: left-hand iterator for the existing results, starting from the end.
  // rhi: right-hand iterator for the new chunk results, starting from the end.
  auto lhi = make_signed(old_size) - 1;
  auto rhi = make_signed(chunk_results.size()) - 1;

  // out_index: keeps track of where to insert the next result in the merged vector.
  auto out_index = sum_size;

  // Merge the results from both vectors while both have elements left.
  while (lhi >= 0 && rhi >= 0) {
    --out_index;

    // Compare elements from both sides, insert the larger one at the correct position.
    if (combined_results[lhi] <= chunk_results[rhi]) {
      if (out_index < combined_results.size()) {
        // Insert the new chunk result and associate it with the current chunk index.
        combined_results[out_index] = chunk_results[rhi];
      }
      --rhi;  // Move to the next element in the new chunk results.
    } else {
      if (out_index < combined_results.size()) {
        // Otherwise, move the element from the existing results.
        combined_results[out_index] = combined_results[lhi];
      }
      --lhi;  // Move to the next element in the existing results.
    }
  }

  // If any elements remain in the new chunk results, insert them.
  while (rhi >= 0) {
    --out_index;
    combined_results[out_index] = chunk_results[rhi];
    --rhi;
  }

  // At this point, the existing results don't need further processing
  // because they were already sorted in the correct position or ignored.
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
auto VectorLSM<Vector, DistanceResult>::Search(
    const Vector& query_vector, const SearchOptions& options) const ->
    Result<typename VectorLSM<Vector, DistanceResult>::SearchResults> {
  if (options.max_num_results == 0) {
    return SearchResults();
  }

  ChunkPtr mutable_chunk;
  // TODO(lsm) Optimize memory allocation.
  decltype(immutable_chunks_) immutable_chunks;
  {
    SharedLock lock(mutex_);
    mutable_chunk = mutable_chunk_;
    immutable_chunks = immutable_chunks_;
  }

  auto intermediate_results = insert_registry_->Search(query_vector, options.max_num_results);

  if (mutable_chunk) {
    auto chunk_results = mutable_chunk->Search(query_vector, options.max_num_results);
    MergeChunkResults(intermediate_results, chunk_results, options.max_num_results);
  }

  for (const auto& chunk : immutable_chunks) {
    auto chunk_results = chunk->Search(query_vector, options.max_num_results);
    MergeChunkResults(intermediate_results, chunk_results, options.max_num_results);
  }

  SearchResults final_results;
  final_results.reserve(intermediate_results.size());
  for (const auto& [vertex_id, distance] : intermediate_results) {
    auto base_table_key = VERIFY_RESULT(options_.key_value_storage->ReadBaseTableKey(vertex_id));
    final_results.push_back({
      .distance = distance,
      .base_table_key = std::move(base_table_key)
    });
  }

  return final_results;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::RollChunk() {
  auto chunk_index = current_chunk_serial_no_++;
  const auto chunk_path = GetIndexChunkPath(options_.storage_dir, chunk_index);
  // TODO(lsm) Implement storing and loading data to file.
  // RETURN_NOT_OK(mutable_chunk_->AttachToFile(chunk_path));

  immutable_chunks_.push_back(std::move(mutable_chunk_));

  return CreateNewMutableChunk();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::CreateNewMutableChunk() {
  mutable_chunk_ = options_.chunk_factory();
  RETURN_NOT_OK(mutable_chunk_->Reserve(options_.points_per_chunk));
  entries_in_mutable_chunks_ = 0;
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::TEST_num_immutable_chunks() const {
  SharedLock lock(mutex_);
  return immutable_chunks_.size();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::TEST_HasBackgroundInserts() const {
  return insert_registry_->HasRunningTasks();
}

YB_INSTANTIATE_TEMPLATE_FOR_ALL_VECTOR_AND_DISTANCE_RESULT_TYPES(VectorLSM);

template void MergeChunkResults<float>(
    std::vector<vectorindex::VertexWithDistance<float>>& combined_results,
    std::vector<vectorindex::VertexWithDistance<float>>& chunk_results,
    size_t max_num_results);

}  // namespace yb::docdb
