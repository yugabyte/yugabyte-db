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

#include <boost/intrusive/list.hpp>

#include "yb/rocksdb/env.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/flags.h"
#include "yb/util/file_util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/unique_lock.h"

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
using vectorindex::VertexWithDistance;
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

  void Done(const Status& status) override;
 private:
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
  using InsertTask = VectorLSMInsertTask<Vector, DistanceResult>;
  using InsertTaskList = boost::intrusive::list<InsertTask>;
  using InsertTaskPtr = std::unique_ptr<InsertTask>;

  explicit VectorLSMInsertRegistry(rpc::ThreadPool& thread_pool)
      : thread_pool_(thread_pool) {}

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
 private:
  rpc::ThreadPool& thread_pool_;
  std::mutex mutex_;
  std::condition_variable allocated_tasks_cond_;
  size_t allocated_tasks_ GUARDED_BY(mutex_);
  InsertTaskList active_tasks_ GUARDED_BY(mutex_);
  std::vector<InsertTaskPtr> task_pool_ GUARDED_BY(mutex_);
};


template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSMInsertTask<Vector, DistanceResult>::Done(const Status& status) {
  vectors_.clear();
  registry_.TaskDone(this);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::VectorLSM(Options options)
    : options_(std::move(options)), env_(rocksdb::Env::Default()),
      insert_registry_(new InsertRegistry(*options_.insert_thread_pool)) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Open() {
  return env_->CreateDirIfMissing(options_.storage_dir);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Insert(std::vector<InsertEntry> entries) {
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
  }

  size_t num_tasks = ceil_div<size_t>(entries.size(), FLAGS_vector_index_task_size);
  size_t entries_per_task = ceil_div(entries.size(), num_tasks);

  auto tasks = insert_registry_->AllocateTasks(*chunk, num_tasks);
  auto tasks_it = tasks.begin();
  size_t index_in_task = std::numeric_limits<size_t>::max();
  for (auto& [vertex_id, base_table_key, v] : entries) {
    if (index_in_task++ >= entries_per_task) {
      ++tasks_it;
      index_in_task = 0;
    }
    tasks_it->Add(vertex_id, std::move(v));
    options_.key_value_storage->StoreBaseTableKey(vertex_id, base_table_key);
  }
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
    std::vector<std::pair<VertexWithDistance<DistanceResult>, int>>& results_with_chunk,
    const std::vector<VertexWithDistance<DistanceResult>>& chunk_results,
    int chunk_index,
    size_t max_num_results) {
  // Store the current size of the existing results.
  auto old_size = results_with_chunk.size();

  // Calculate the combined size of the existing and new chunk results.
  auto sum_size = old_size + chunk_results.size();

  // Resize the results vector to hold the merged results, but cap it at max_num_results.
  results_with_chunk.resize(std::min(sum_size, max_num_results));

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
    if (results_with_chunk[lhi].first <= chunk_results[rhi]) {
      if (out_index < results_with_chunk.size()) {
        // Insert the new chunk result and associate it with the current chunk index.
        results_with_chunk[out_index] = {chunk_results[rhi], chunk_index};
      }
      --rhi;  // Move to the next element in the new chunk results.
    } else {
      if (out_index < results_with_chunk.size()) {
        // Otherwise, move the element from the existing results.
        results_with_chunk[out_index] = results_with_chunk[lhi];
      }
      --lhi;  // Move to the next element in the existing results.
    }
  }

  // If any elements remain in the new chunk results, insert them.
  while (rhi >= 0) {
    --out_index;
    results_with_chunk[out_index] = {chunk_results[rhi], chunk_index};
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
  const int kMutableChunkIndex = -1;
  using IntermediateResult = std::pair<vectorindex::VertexWithDistance<DistanceResult>, int>;
  std::vector<IntermediateResult> results_with_chunk;

  ChunkPtr mutable_chunk;
  // TODO(lsm) Optimize memory allocation.
  decltype(immutable_chunks_) immutable_chunks;
  {
    SharedLock lock(mutex_);
    mutable_chunk = mutable_chunk_;
    immutable_chunks = immutable_chunks_;
  }

  if (mutable_chunk) {
    auto mutable_results = mutable_chunk->Search(query_vector, options.max_num_results);
    for (auto& result : mutable_results) {
      results_with_chunk.push_back({
          result,
          kMutableChunkIndex
      });
    }
  }

  {
    int chunk_index = 0;
    for (const auto& chunk : immutable_chunks) {
      auto chunk_results = chunk->Search(query_vector, options.max_num_results);
      MergeChunkResults(results_with_chunk, chunk_results, chunk_index, options.max_num_results);
      ++chunk_index;
    }
  }

  SearchResults final_results;
  final_results.reserve(results_with_chunk.size());
  for (const auto& item : results_with_chunk) {
    auto [vertex_id, distance] = item.first;
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

YB_INSTANTIATE_TEMPLATE_FOR_ALL_VECTOR_AND_DISTANCE_RESULT_TYPES(VectorLSM);

}  // namespace yb::docdb
