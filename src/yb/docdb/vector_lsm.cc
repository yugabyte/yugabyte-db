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

#include "yb/docdb/vector_lsm_metadata.h"

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

// While mutable chunk is running, this value is added to num_tasks.
// During stop, we decrease num_tasks by this value. So zero num_tasks means that mutable chunk
// is stopped.
constexpr size_t kRunningMark = 1e18;

std::string GetIndexChunkPath(const std::string& storage_dir, size_t chunk_index) {
  return JoinPathSegments(storage_dir, Format("vectorindex_$0", chunk_index));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertTask :
    public rpc::ThreadPoolTask,
    public bi::list_base_hook<bi::link_mode<bi::normal_link>> {
 public:
  using Types = VectorLSMTypes<Vector, DistanceResult>;
  using InsertRegistry = typename Types::InsertRegistry;
  using VertexWithDistance = typename Types::VertexWithDistance;
  using SearchHeap = std::priority_queue<VertexWithDistance>;

  using MutableChunk = typename VectorLSM<Vector, DistanceResult>::MutableChunk;

  explicit VectorLSMInsertTask(InsertRegistry& registry) : registry_(registry) {
  }

  void Bind(const std::shared_ptr<MutableChunk>& chunk) {
    DCHECK(!chunk_);
    chunk_ = chunk;
  }

  void Add(VertexId vertex_id, Vector vector) {
    vectors_.emplace_back(vertex_id, std::move(vector));
  }

  void Run() override {
    for (const auto& [vertex_id, vector] : vectors_) {
      // TODO(lsm) handle failure
      CHECK_OK(chunk_->chunk->Insert(vertex_id, vector));
    }
    auto new_tasks = --chunk_->num_tasks;
    if (new_tasks == 0) {
      chunk_->save_callback_();
      chunk_->save_callback_ = {};
    }
  }

  void Search(SearchHeap& heap, const Vector& query_vector, size_t max_num_results) const {
    SharedLock lock(mutex_);
    for (const auto& [id, vector] : vectors_) {
      auto distance = chunk_->chunk->Distance(query_vector, vector);
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
  std::shared_ptr<MutableChunk> chunk_;
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

  using MutableChunk = typename VectorLSM<Vector, DistanceResult>::MutableChunk;

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

  InsertTaskList AllocateTasks(
      const std::shared_ptr<MutableChunk>& chunk, size_t num_tasks) EXCLUDES(mutex_) {
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
  chunk_ = nullptr;
  {
    std::lock_guard lock(mutex_);
    vectors_.clear();
  }
  registry_.TaskDone(this);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
struct VectorLSM<Vector, DistanceResult>::MutableChunk {
  ChunkPtr chunk;
  size_t num_entries = 0;
  // See comments for kRunningMark.
  std::atomic<size_t> num_tasks = kRunningMark;
  std::function<void()> save_callback_;

  // Returns true if registration was successful. Otherwise, new mutable chunk should be allocated.
  bool RegisterInsert(
      const std::vector<InsertEntry>& entries, const Options& options, size_t new_tasks) {
    // TODO(lsm) Handle size of entries greater than points_per_chunk.
    if (num_entries && num_entries + entries.size() > options.points_per_chunk) {
      return false;
    }
    num_entries += entries.size();
    num_tasks += new_tasks;
    return true;
  }
};


template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::VectorLSM()
    : env_(rocksdb::Env::Default()) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::~VectorLSM() {
  auto start_time = CoarseMonoClock::now();
  auto last_warning_time = start_time;
  while (num_chunks_being_saved_ != 0) {
    auto now = CoarseMonoClock::now();
    if (now > last_warning_time + 30s) {
      LOG(WARNING) << "Long wait to save chunks: " << MonoDelta(now - start_time)
                   << ", chunks left: " << num_chunks_being_saved_;
      last_warning_time = now;
    }
    std::this_thread::sleep_for(10ms);
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Open(Options options) {
  std::lock_guard lock(mutex_);

  options_ = std::move(options);
  insert_registry_ = std::make_unique<InsertRegistry>(*options_.thread_pool);

  RETURN_NOT_OK(env_->CreateDirIfMissing(options_.storage_dir));
  auto load_result = VERIFY_RESULT(VectorLSMMetadataLoad(env_, options_.storage_dir));
  metadata_file_no_ = load_result.next_free_file_no;
  std::unordered_set<size_t> chunks;
  for (const auto& update : load_result.updates) {
    for (const auto& chunk : update.add_chunks()) {
      chunks.insert(chunk.serial_no());
    }
    for (const auto& chunk_no : update.remove_chunks()) {
      if (!chunks.erase(chunk_no)) {
        return STATUS_FORMAT(Corruption, "Attempt to remove non existing chunk: $0", chunk_no);
      }
    }
  }

  // TODO(lsm) Load via thread pool
  for (const auto& chunk_index : chunks) {
    auto chunk = options_.chunk_factory();
    RETURN_NOT_OK(chunk->LoadFromFile(GetIndexChunkPath(options_.storage_dir, chunk_index)));
    immutable_chunks_.push_back(chunk);
    current_chunk_serial_no_ = std::max(current_chunk_serial_no_, chunk_index);
  }

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Insert(
    std::vector<InsertEntry> entries, HybridTime write_time) {
  std::shared_ptr<MutableChunk> chunk;
  size_t num_tasks = ceil_div<size_t>(entries.size(), FLAGS_vector_index_task_size);
  {
    std::lock_guard lock(mutex_);
    if (!mutable_chunk_) {
      RETURN_NOT_OK(CreateNewMutableChunk());
    }
    if (!mutable_chunk_->RegisterInsert(entries, options_, num_tasks)) {
      RETURN_NOT_OK(RollChunk());
      RSTATUS_DCHECK(mutable_chunk_->RegisterInsert(entries, options_, num_tasks),
                     RuntimeError, "Failed to register insert into a new mutable chunk");
    }
    chunk = mutable_chunk_;
  }

  size_t entries_per_task = ceil_div(entries.size(), num_tasks);

  auto tasks = insert_registry_->AllocateTasks(chunk, num_tasks);
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
    if (mutable_chunk_) {
      mutable_chunk = mutable_chunk_->chunk;
    }
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
Status VectorLSM<Vector, DistanceResult>::DoSaveChunk(
    size_t chunk_index, const ChunkPtr& chunk) {
  const auto chunk_path = GetIndexChunkPath(options_.storage_dir, chunk_index);
  RETURN_NOT_OK(chunk->SaveToFile(chunk_path));

  std::lock_guard lock(mutex_);
  if (!metadata_file_) {
    metadata_file_ = VERIFY_RESULT(VectorLSMMetadataOpenFile(
       env_, options_.storage_dir, metadata_file_no_));
  }
  VectorLSMUpdate update;
  auto& added_chunk = *update.mutable_add_chunks()->Add();
  added_chunk.set_serial_no(chunk_index);
  return VectorLSMMetadataAppendUpdate(*metadata_file_, update);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
template<class... Args>
void VectorLSM<Vector, DistanceResult>::SaveChunk(Args&&... args) {
  auto status = DoSaveChunk(std::forward<Args>(args)...);
  // TODO(lsm) move vector lsm to failed state
  if (!status.ok()) {
    LOG(DFATAL) << "Save chunk failed: " << status;
  }
  --num_chunks_being_saved_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::RollChunk() {
  auto chunk_index = ++current_chunk_serial_no_;

  immutable_chunks_.push_back(mutable_chunk_->chunk);

  ++num_chunks_being_saved_;
  mutable_chunk_->save_callback_ = [this, chunk_index, chunk = mutable_chunk_->chunk]() {
    SaveChunk(chunk_index, chunk);
  };
  auto tasks = mutable_chunk_->num_tasks -= kRunningMark;
  RSTATUS_DCHECK_LT(tasks, kRunningMark, RuntimeError, "Wrong value for num_tasks");
  if (tasks == 0) {
    options_.thread_pool->EnqueueFunctor(mutable_chunk_->save_callback_);
    // TODO(lsm) Optimize memory allocation related to save callback
    mutable_chunk_->save_callback_ = {};
  }

  return CreateNewMutableChunk();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::CreateNewMutableChunk() {
  auto chunk = options_.chunk_factory();
  RETURN_NOT_OK(chunk->Reserve(options_.points_per_chunk));

  mutable_chunk_ = std::make_shared<MutableChunk>();
  mutable_chunk_->chunk = std::move(chunk);
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
