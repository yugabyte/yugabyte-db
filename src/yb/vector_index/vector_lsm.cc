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

#include "yb/vector_index/vector_lsm.h"

#include <queue>
#include <thread>

#include <boost/intrusive/list.hpp>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/metadata.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/unique_lock.h"

#include "yb/vector_index/vector_lsm_metadata.h"

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

DEFINE_RUNTIME_bool(vector_index_dump_stats, false,
                    "Whether to dump stats related to vector index search.");

DEFINE_test_flag(bool, vector_index_skip_update_metadata_during_shutdown, false,
                 "Whether VectorLSM metadata update should be skipped after shutdown has been "
                 "initiated");

DEFINE_test_flag(uint64, vector_index_delay_saving_first_chunk_ms, 0,
                 "Delay saving the first chunk in VectorLSM for specified amount of milliseconds");

DEFINE_NON_RUNTIME_uint32(vector_index_concurrent_reads, 0,
                          "Max number of concurrent reads on vector index chunk. "
                          "0 - use number of CPUs for it.");

namespace yb::vector_index {

namespace bi = boost::intrusive;

namespace {

YB_DEFINE_ENUM(ImmutableChunkState, (kInMemory)(kOnDisk)(kInManifest));

// While mutable chunk is running, this value is added to num_tasks.
// During stop, we decrease num_tasks by this value. So zero num_tasks means that mutable chunk
// is stopped.
constexpr size_t kRunningMark = 1e18;

std::string GetChunkPath(const std::string& storage_dir, size_t chunk_serial_no) {
  return JoinPathSegments(storage_dir, Format("vectorindex_$0", chunk_serial_no));
}

size_t MaxConcurrentReads() {
  auto max_concurrent_reads = FLAGS_vector_index_concurrent_reads;
  return max_concurrent_reads == 0 ? std::thread::hardware_concurrency() : max_concurrent_reads;
}

} // namespace

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertTask :
    public rpc::ThreadPoolTask,
    public bi::list_base_hook<bi::link_mode<bi::normal_link>> {
 public:
  using Types = VectorLSMTypes<Vector, DistanceResult>;
  using InsertRegistry = typename Types::InsertRegistry;
  using VectorWithDistance = typename Types::VectorWithDistance;
  using SearchHeap = std::priority_queue<VectorWithDistance>;
  using LSM = VectorLSM<Vector, DistanceResult>;
  using MutableChunk = typename LSM::MutableChunk;

  explicit VectorLSMInsertTask(LSM& lsm) : lsm_(lsm) {}

  void Bind(const std::shared_ptr<MutableChunk>& chunk) {
    DCHECK(!chunk_);
    DCHECK_ONLY_NOTNULL(chunk->index);
    chunk_ = chunk;
  }

  void Add(VectorId vertex_id, Vector&& vector) {
    vectors_.emplace_back(vertex_id, std::move(vector));
  }

  void Run() override {
    for (const auto& [vertex_id, vector] : vectors_) {
      lsm_.CheckFailure(chunk_->index->Insert(vertex_id, vector));
    }
    auto new_tasks = --chunk_->num_tasks;
    if (new_tasks == 0) {
      chunk_->save_callback();
      chunk_->save_callback = {};
    }
  }

  void Search(
      SearchHeap& heap, const Vector& query_vector, const SearchOptions& options) const {
    SharedLock lock(mutex_);
    for (const auto& [id, vector] : vectors_) {
      if (!options.filter(id)) {
        continue;
      }
      auto distance = chunk_->index->Distance(query_vector, vector);
      VectorWithDistance vertex(id, distance);
      if (heap.size() < options.max_num_results) {
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
  LSM& lsm_;
  std::shared_ptr<MutableChunk> chunk_;
  std::vector<std::pair<VectorId, Vector>> vectors_;
};

// Registry for all active Vector LSM insert subtasks.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertRegistry {
 public:
  using Types = VectorLSMTypes<Vector, DistanceResult>;
  using LSM = VectorLSM<Vector, DistanceResult>;
  using VectorIndex = typename Types::VectorIndex;
  using VectorWithDistance = typename Types::VectorWithDistance;
  using InsertTask = VectorLSMInsertTask<Vector, DistanceResult>;
  using InsertTaskList = boost::intrusive::list<InsertTask>;
  using InsertTaskPtr = std::unique_ptr<InsertTask>;

  using MutableChunk = typename VectorLSM<Vector, DistanceResult>::MutableChunk;

  VectorLSMInsertRegistry(const std::string& log_prefix, rpc::ThreadPool& thread_pool)
      : log_prefix_(log_prefix), thread_pool_(thread_pool) {}

  void Shutdown() {
    for (;;) {
      {
        SharedLock lock(mutex_);
        if (active_tasks_.empty()) {
          break;
        }
      }
      YB_LOG_EVERY_N_SECS(INFO, 1) << LogPrefix() << "Waiting for vector insertion tasks to finish";
      std::this_thread::sleep_for(100ms);
    }
  }

  InsertTaskList AllocateTasks(
      LSM& lsm, const std::shared_ptr<MutableChunk>& chunk, size_t num_tasks) EXCLUDES(mutex_) {
    InsertTaskList result;
    {
      UniqueLock lock(mutex_);
      while (allocated_tasks_ + num_tasks >= FLAGS_vector_index_max_insert_tasks) {
        // TODO(vector_index) Pass timeout here.
        allocated_tasks_cond_.wait(GetLockForCondition(lock));
      }
      allocated_tasks_ += num_tasks;
      for (size_t left = num_tasks; left-- > 0;) {
        InsertTaskPtr task;
        if (task_pool_.empty()) {
          task = std::make_unique<InsertTask>(lsm);
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

  typename VectorIndex::SearchResult Search(
      const Vector& query_vector, const SearchOptions& options) {
    typename InsertTask::SearchHeap heap;
    {
      SharedLock lock(mutex_);
      for (const auto& task : active_tasks_) {
        task.Search(heap, query_vector, options);
      }
    }
    return ReverseHeapToVector(heap);
  }

  bool HasRunningTasks() {
    SharedLock lock(mutex_);
    return !active_tasks_.empty();
  }

 private:
  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  const std::string log_prefix_;
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
    chunk_ = nullptr;
    vectors_.clear();
  }
  lsm_.insert_registry_->TaskDone(this);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
struct VectorLSM<Vector, DistanceResult>::MutableChunk {
  VectorIndexPtr index;
  size_t num_entries = 0;
  // See comments for kRunningMark.
  std::atomic<size_t> num_tasks = kRunningMark;
  std::function<void()> save_callback;
  rocksdb::UserFrontiersPtr user_frontiers;

  // Returns true if registration was successful. Otherwise, new mutable chunk should be allocated.
  // Invoked when owning VectorLSM holds the mutex.
  bool RegisterInsert(
      const std::vector<InsertEntry>& entries, const Options& options, size_t new_tasks,
      const rocksdb::UserFrontiers* frontiers) {
    if (num_entries && num_entries + entries.size() > index->MaxVectors()) {
      return false;
    }
    num_entries += entries.size();
    num_tasks += new_tasks;
    if (frontiers) {
      rocksdb::UpdateFrontiers(user_frontiers, *frontiers);
    }
    return true;
  }

  void Insert(VectorLSM& lsm, VectorId vertex_id, const Vector& vector) {
    lsm.CheckFailure(index->Insert(vertex_id, vector));
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(num_entries, num_tasks, user_frontiers);
  }
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
struct VectorLSM<Vector, DistanceResult>::ImmutableChunk {
  // Chunk serial no, used in file name and never changes.
  size_t serial_no = 0;

  // In memory order for chunk. All chunks in immutable chunks are ordered using it.
  // Could be changed between application runs.
  size_t order_no = 0;

  ImmutableChunkState state = ImmutableChunkState::kInMemory;
  VectorIndexPtr index;
  rocksdb::UserFrontiersPtr user_frontiers;
  std::promise<Status>* flush_promise = nullptr;

  void AddToUpdate(VectorLSMUpdatePB& update) {
    auto& added_chunk = *update.mutable_add_chunks()->Add();
    added_chunk.set_serial_no(serial_no);
    added_chunk.set_order_no(order_no);
    user_frontiers->Smallest().ToPB(added_chunk.mutable_smallest()->mutable_user_frontier());
    user_frontiers->Largest().ToPB(added_chunk.mutable_largest()->mutable_user_frontier());
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(serial_no, order_no, state, user_frontiers);
  }
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::VectorLSM()
    // TODO(vector_index) Use correct env for encryption
    : env_(rocksdb::Env::Default()) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::~VectorLSM() {
  StartShutdown();
  CompleteShutdown();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::StartShutdown() {
  std::lock_guard lock(mutex_);
  stopping_ = true;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::CompleteShutdown() {
  if (!insert_registry_) {
    // Was not opened.
    return;
  }
  insert_registry_->Shutdown();
  auto start_time = CoarseMonoClock::now();
  auto last_warning_time = start_time;
  MonoDelta report_interval = 1s;
  for (;;) {
    size_t chunks_left;
    {
      std::lock_guard lock(mutex_);
      chunks_left = updates_queue_.size();
      if (chunks_left == 0) {
        break;
      }
    }
    auto now = CoarseMonoClock::now();
    if (now > last_warning_time + report_interval) {
      LOG_WITH_PREFIX(WARNING) << "Long wait to save chunks: " << MonoDelta(now - start_time)
                               << ", chunks left: " << chunks_left;
      last_warning_time = now;
      report_interval = std::min<MonoDelta>(report_interval * 2, 30s);
    }
    std::this_thread::sleep_for(10ms);
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
inline void VectorLSM<Vector, DistanceResult>::CheckFailure(const Status& status) {
  if (status.ok()) {
    return;
  }
  Status existing_status;
  {
    std::lock_guard lock(mutex_);
    if (failed_status_.ok()) {
      failed_status_ = status;
      return;
    }
    existing_status = failed_status_;
  }
  YB_LOG_EVERY_N_SECS(WARNING, 1)
      << LogPrefix() << "Vector LSM already in failed state: " << existing_status
      << ", while trying to set new failed state: " << status;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Open(Options options) {
  std::lock_guard lock(mutex_);

  options_ = std::move(options);
  insert_registry_ = std::make_unique<InsertRegistry>(options_.log_prefix, *options_.thread_pool);

  RETURN_NOT_OK(env_->CreateDirs(options_.storage_dir));
  auto load_result = VERIFY_RESULT(VectorLSMMetadataLoad(env_, options_.storage_dir));
  metadata_file_no_ = load_result.next_free_file_no;
  std::unordered_map<size_t, const VectorLSMChunkPB*> chunks;
  for (const auto& update : load_result.updates) {
    for (const auto& chunk : update.add_chunks()) {
      chunks.emplace(chunk.serial_no(), &chunk);
    }
    for (const auto& chunk_no : update.remove_chunks()) {
      if (!chunks.erase(chunk_no)) {
        return STATUS_FORMAT(Corruption, "Attempt to remove non existing chunk: $0", chunk_no);
      }
    }
  }

  for (const auto& [_, chunk_pb] : chunks) {
    auto index = chunk_pb->serial_no() ? options_.vector_index_factory() : nullptr;
    auto user_frontiers = options_.frontiers_factory();
    RETURN_NOT_OK(user_frontiers->Smallest().FromPB(chunk_pb->smallest().user_frontier()));
    RETURN_NOT_OK(user_frontiers->Largest().FromPB(chunk_pb->largest().user_frontier()));
    immutable_chunks_.push_back(std::make_shared<ImmutableChunk>(ImmutableChunk {
      .serial_no = chunk_pb->serial_no(),
      .order_no = chunk_pb->order_no(),
      .state = ImmutableChunkState::kInManifest,
      .index = index,
      .user_frontiers = std::move(user_frontiers),
      .flush_promise = nullptr,
    }));

    current_chunk_serial_no_ = std::max<size_t>(current_chunk_serial_no_, chunk_pb->serial_no());
  }

  CountDownLatch latch(immutable_chunks_.size());
  for (size_t i = 0; i != immutable_chunks_.size(); ++i) {
    auto& chunk = immutable_chunks_[i];
    if (!chunk->serial_no) {
      latch.CountDown();
      continue;
    }
    options_.thread_pool->EnqueueFunctor([this, &latch, &chunk] {
      CheckFailure(chunk->index->LoadFromFile(
          GetChunkPath(options_.storage_dir, chunk->serial_no),
          MaxConcurrentReads()));
      latch.CountDown();
    });
  }
  latch.Wait();

  std::sort(
      immutable_chunks_.begin(), immutable_chunks_.end(), [](const auto& lhs, const auto& rhs) {
    return lhs->order_no < rhs->order_no;
  });
  VLOG_WITH_PREFIX(1) << "Loaded " << immutable_chunks_.size() << " chunks";

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::CreateCheckpoint(const std::string& out) {
  decltype(immutable_chunks_) chunks;
  {
    std::lock_guard lock(mutex_);
    // TODO(vector_index) Prevent chunk from being deleted from the filesystem.
    chunks.reserve(immutable_chunks_.size());
    for (const auto& chunk : immutable_chunks_) {
      if (chunk->state == ImmutableChunkState::kInManifest) {
        chunks.push_back(chunk);
      }
    }
  }
  RETURN_NOT_OK(env_->CreateDirs(out));
  VectorLSMUpdatePB update;
  for (const auto& chunk : chunks) {
    if (chunk->serial_no) {
      RETURN_NOT_OK(env_->LinkFile(
          GetChunkPath(options_.storage_dir, chunk->serial_no),
          GetChunkPath(out, chunk->serial_no)));
    }
    chunk->AddToUpdate(update);
  }
  auto metadata_file = VERIFY_RESULT(VectorLSMMetadataOpenFile(env_, out, 0));
  return VectorLSMMetadataAppendUpdate(*metadata_file, update);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Insert(
    std::vector<InsertEntry> entries, const VectorLSMInsertContext& context) {
  VLOG_WITH_PREFIX_AND_FUNC(5)
      << "entries: " << entries.size() << ", frontier: " << AsString(context.frontiers);

  std::shared_ptr<MutableChunk> chunk;
  size_t num_tasks = ceil_div<size_t>(entries.size(), FLAGS_vector_index_task_size);
  {
    std::lock_guard lock(mutex_);
    RETURN_NOT_OK(failed_status_);

    if (!mutable_chunk_) {
      RETURN_NOT_OK(CreateNewMutableChunk(entries.size()));
    }
    if (!mutable_chunk_->RegisterInsert(entries, options_, num_tasks, context.frontiers)) {
      RETURN_NOT_OK(RollChunk(entries.size()));
      RSTATUS_DCHECK(
          mutable_chunk_->RegisterInsert(entries, options_, num_tasks, context.frontiers),
          RuntimeError, "Failed to register insert into a new mutable chunk");
    }
    chunk = mutable_chunk_;
  }

  if (!num_tasks) {
    // Empty insert could be used to update frontiers.
    return Status::OK();
  }

  size_t entries_per_task = ceil_div(entries.size(), num_tasks);

  auto tasks = insert_registry_->AllocateTasks(*this, chunk, num_tasks);
  auto tasks_it = tasks.begin();
  size_t index_in_task = 0;
  for (auto& [vertex_id, v] : entries) {
    if (index_in_task++ >= entries_per_task) {
      ++tasks_it;
      index_in_task = 0;
    }
    tasks_it->Add(vertex_id, std::move(v));
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
    std::vector<VectorWithDistance<DistanceResult>>& combined_results,
    std::vector<VectorWithDistance<DistanceResult>>& chunk_results,
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
        } else if (entry.vector_id == it->vector_id) {
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
auto VectorLSM<Vector, DistanceResult>::AllIndexes() const -> Result<std::vector<VectorIndexPtr>> {
  // TODO(vector_index) Optimize memory allocation.
  std::vector<VectorIndexPtr> result;
  {
    SharedLock lock(mutex_);
    RETURN_NOT_OK(failed_status_);

    result.reserve(1 + immutable_chunks_.size());
    if (mutable_chunk_) {
      result.push_back(mutable_chunk_->index);
    }
    for (const auto& chunk : immutable_chunks_) {
      if (chunk->index) {
        result.push_back(chunk->index);
      }
    }
  }
  return result;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
auto VectorLSM<Vector, DistanceResult>::Search(
    const Vector& query_vector, const SearchOptions& options) const ->
    Result<typename VectorLSM<Vector, DistanceResult>::SearchResults> {
  if (options.max_num_results == 0) {
    return SearchResults();
  }

  auto indexes = VERIFY_RESULT(AllIndexes());
  bool dump_stats = FLAGS_vector_index_dump_stats;

  auto start_registry_search = dump_stats ? MonoTime::Now() : MonoTime();
  auto intermediate_results = insert_registry_->Search(query_vector, options);
  VLOG_WITH_PREFIX_AND_FUNC(4)
      << "Results from registry: " << AsString(intermediate_results);
  size_t num_results_from_insert_registry = intermediate_results.size();

  size_t sum_num_found_entries = 0;
  auto start_chunks_search = dump_stats ? MonoTime::Now() : MonoTime();
  for (const auto& index : indexes) {
    auto chunk_results = VERIFY_RESULT(index->Search(query_vector, options));
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Chunk results: " << AsString(chunk_results);
    sum_num_found_entries += chunk_results.size();
    MergeChunkResults(intermediate_results, chunk_results, options.max_num_results);
  }
  auto stop_search = dump_stats ? MonoTime::Now() : MonoTime();

  LOG_IF_WITH_PREFIX_AND_FUNC(INFO, dump_stats)
      << "VI_STATS: Number of chunks: " << indexes.size() << ", entries found in all chunks: "
      << sum_num_found_entries << ", entries found in insert registry: "
      << num_results_from_insert_registry << ", time to search insert registry: "
      << (start_chunks_search - start_registry_search).ToMicroseconds()
      << "us, time to search index chunks: " << (stop_search - start_chunks_search).ToMicroseconds()
      << "us";

  return intermediate_results;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<bool> VectorLSM<Vector, DistanceResult>::HasVectorId(
    const vector_index::VectorId& vector_id) const {
  // Search in insert registry is not implemented, so just wait until all entries are inserted.
  while (insert_registry_->HasRunningTasks()) {
    std::this_thread::sleep_for(10ms);
  }
  auto indexes = VERIFY_RESULT(AllIndexes());
  for (const auto& index : indexes) {
    auto vector_res = index->GetVector(vector_id);
    if (vector_res.ok()) {
      return true;
    }
    if (!vector_res.status().IsNotFound()) {
      return vector_res.status();
    }
  }
  return false;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::DoSaveChunk(const ImmutableChunkPtr& chunk) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << AsString(*chunk);

  if (chunk->index) {
    const auto chunk_path = GetChunkPath(options_.storage_dir, chunk->serial_no);
    if (chunk->serial_no == 1 && FLAGS_TEST_vector_index_delay_saving_first_chunk_ms != 0) {
      std::this_thread::sleep_for(FLAGS_TEST_vector_index_delay_saving_first_chunk_ms * 1ms);
    }
    RETURN_NOT_OK(chunk->index->SaveToFile(chunk_path));
  }

  rocksdb::WritableFile* metadata_file = nullptr;
  ImmutableChunkPtr writing_chunk;
  {
    std::lock_guard lock(mutex_);
    chunk->state = ImmutableChunkState::kOnDisk;
    if (stopping_ && FLAGS_TEST_vector_index_skip_update_metadata_during_shutdown) {
      for (auto it = updates_queue_.begin(); it != updates_queue_.end();) {
        if (it->second->state == ImmutableChunkState::kOnDisk) {
          it = updates_queue_.erase(it);
        } else {
          ++it;
        }
      }
      if (updates_queue_.empty()) {
        updates_queue_empty_.notify_all();
      }
      return Status::OK();
    }
    if (writing_update_) {
      return Status::OK();
    }
    auto it = updates_queue_.find(chunk->order_no);
    RSTATUS_DCHECK(
        it != updates_queue_.end(), RuntimeError,
        "Missing chunk in updates queue: $0", chunk->order_no);
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Updated queue: " << AsString(updates_queue_);
    for (;;) {
      if (it->second->state != ImmutableChunkState::kOnDisk) {
        return Status::OK();
      }
      if (it == updates_queue_.begin()) {
        break;
      }
      --it;
    }
    writing_update_ = true;
    if (!metadata_file_) {
      metadata_file_ = VERIFY_RESULT(VectorLSMMetadataOpenFile(
         env_, options_.storage_dir, metadata_file_no_));
    }
    metadata_file = metadata_file_.get();
    writing_chunk = updates_queue_.begin()->second;
  }

  return UpdateManifest(metadata_file, std::move(writing_chunk));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::UpdateManifest(
    rocksdb::WritableFile* metadata_file, ImmutableChunkPtr chunk) {
  for (;;) {
    VectorLSMUpdatePB update;
    chunk->AddToUpdate(update);
    VLOG_WITH_PREFIX_AND_FUNC(4) << update.ShortDebugString();
    RETURN_NOT_OK(VectorLSMMetadataAppendUpdate(*metadata_file, update));

    std::lock_guard lock(mutex_);
    chunk->state = ImmutableChunkState::kInManifest;
    if (chunk->flush_promise) {
      chunk->flush_promise->set_value(Status::OK());
      chunk->flush_promise = nullptr;
    }

    RETURN_NOT_OK(RemoveUpdateQueueEntry(chunk->order_no));
    if (updates_queue_.empty() ||
        updates_queue_.begin()->second->state != ImmutableChunkState::kOnDisk) {
      writing_update_ = false;
      break;
    }
    chunk = updates_queue_.begin()->second;
  }

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::SaveChunk(const ImmutableChunkPtr& chunk) {
  auto status = DoSaveChunk(chunk);
  if (!status.ok()) {
    if (chunk->flush_promise) {
      chunk->flush_promise->set_value(status);
      chunk->flush_promise = nullptr;
    }
    LOG_WITH_PREFIX(DFATAL) << "Save chunk failed: " << status;
    std::lock_guard lock(mutex_);
    auto remove_status = RemoveUpdateQueueEntry(chunk->order_no);
    LOG_IF_WITH_PREFIX(DFATAL, !remove_status.ok()) << remove_status;
    if (failed_status_.ok()) {
      failed_status_ = status;
    }
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::DoFlush(std::promise<Status>* promise) {
  size_t chunk_serial_no = 0;;

  VectorIndexPtr index;
  if (mutable_chunk_->num_entries) {
    chunk_serial_no = ++current_chunk_serial_no_;
    index = mutable_chunk_->index;
  }
  immutable_chunks_.push_back(std::make_shared<ImmutableChunk>(ImmutableChunk {
    .serial_no = chunk_serial_no,
    .order_no = immutable_chunks_.empty() ? 0 : immutable_chunks_.back()->order_no + 1,
    .state = ImmutableChunkState::kInMemory,
    .index = std::move(index),
    .user_frontiers = std::move(mutable_chunk_->user_frontiers),
    .flush_promise = promise,
  }));
  auto chunk = immutable_chunks_.back();
  updates_queue_.emplace(chunk->order_no, chunk);

  mutable_chunk_->save_callback = [this, chunk]() {
    SaveChunk(chunk);
  };
  auto tasks = mutable_chunk_->num_tasks -= kRunningMark;
  RSTATUS_DCHECK_LT(tasks, kRunningMark, RuntimeError, "Wrong value for num_tasks");
  if (tasks == 0) {
    options_.thread_pool->EnqueueFunctor(mutable_chunk_->save_callback);
    // TODO(vector_index) Optimize memory allocation related to save callback
    mutable_chunk_->save_callback = {};
  }
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::RollChunk(size_t min_points) {
  RETURN_NOT_OK(DoFlush(/* promise=*/ nullptr));
  return CreateNewMutableChunk(min_points);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::CreateNewMutableChunk(size_t min_vectors) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << min_vectors;
  VectorIndexPtr index;
  if (mutable_chunk_ && mutable_chunk_->num_entries == 0 &&
      mutable_chunk_->index->MaxVectors() >= min_vectors) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Reuse index of " << AsString(*mutable_chunk_);
    index = std::move(mutable_chunk_->index);
  } else {
    index = options_.vector_index_factory();
    RETURN_NOT_OK(index->Reserve(
        std::max(min_vectors, options_.points_per_chunk),
        options_.thread_pool->options().max_workers, MaxConcurrentReads()));
  }

  mutable_chunk_ = std::make_shared<MutableChunk>();
  mutable_chunk_->index = std::move(index);
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Flush(bool wait) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << "wait: " << wait;
  std::promise<Status> promise;
  {
    std::lock_guard lock(mutex_);
    if (!mutable_chunk_) {
      return Status::OK();
    }
    RETURN_NOT_OK(DoFlush(wait ? &promise : nullptr));
    mutable_chunk_ = nullptr;
  }

  return wait ? promise.get_future().get() : Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
rocksdb::UserFrontierPtr VectorLSM<Vector, DistanceResult>::GetFlushedFrontier() {
  rocksdb::UserFrontierPtr result;
  std::lock_guard lock(mutex_);
  VLOG_WITH_PREFIX_AND_FUNC(4) << "immutable_chunks: " << AsString(immutable_chunks_);

  for (const auto& chunk : immutable_chunks_) {
    if (chunk->state != ImmutableChunkState::kInManifest) {
      continue;
    }
    rocksdb::UserFrontier::Update(
        &chunk->user_frontiers->Largest(), rocksdb::UpdateUserValueType::kLargest, &result);
  }
  return result;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
rocksdb::FlushAbility VectorLSM<Vector, DistanceResult>::GetFlushAbility() {
  std::lock_guard lock(mutex_);
  for (const auto& chunk : immutable_chunks_) {
    if (chunk->state != ImmutableChunkState::kInManifest) {
      return rocksdb::FlushAbility::kAlreadyFlushing;
    }
  }
  if (mutable_chunk_ && mutable_chunk_->num_entries) {
    return rocksdb::FlushAbility::kHasNewData;
  }
  return rocksdb::FlushAbility::kNoNewData;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
auto VectorLSM<Vector, DistanceResult>::options() const ->
    const typename VectorLSM<Vector, DistanceResult>::Options& {
  return options_;
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

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
DistanceResult VectorLSM<Vector, DistanceResult>::Distance(
    const Vector& lhs, const Vector& rhs) const {
  VectorIndexPtr index;
  // TODO(vector_index) Should improve scenario when there is no active chunk.
  {
    SharedLock lock(mutex_);
    if (mutable_chunk_) {
      index = mutable_chunk_->index;
    } else {
      for (const auto& chunk : immutable_chunks_) {
        index = chunk->index;
        if (index) {
          break;
        }
      }
    }
    if (!index) {
      index = options_.vector_index_factory();
    }
  }
  return index->Distance(lhs, rhs);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::WaitForFlush() {
  std::unique_lock lock(mutex_);
  // TODO(vector-index) Don't wait flushes that started after this call.
  updates_queue_empty_.wait(
      lock, [this]() NO_THREAD_SAFETY_ANALYSIS { return updates_queue_.empty(); });
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::RemoveUpdateQueueEntry(size_t order_no) {
  auto num_erased = updates_queue_.erase(order_no);
  if (updates_queue_.empty()) {
    updates_queue_empty_.notify_all();
  }
  RSTATUS_DCHECK_EQ(
      num_erased, 1, RuntimeError, "Failed to remove written chunk from updates queue");
  return Status::OK();
}

YB_INSTANTIATE_TEMPLATE_FOR_ALL_VECTOR_AND_DISTANCE_RESULT_TYPES(VectorLSM);

template void MergeChunkResults<float>(
    std::vector<VectorWithDistance<float>>& combined_results,
    std::vector<VectorWithDistance<float>>& chunk_results,
    size_t max_num_results);

}  // namespace yb::vector_index
