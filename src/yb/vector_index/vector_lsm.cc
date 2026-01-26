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

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/metadata.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/priority_thread_pool.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/unique_lock.h"

#include "yb/vector_index/vector_lsm_metadata.h"

using namespace std::literals;

DEFINE_NON_RUNTIME_uint32(vector_index_concurrent_reads, 0,
    "Max number of concurrent reads on vector index chunk. 0 - use number of CPUs for it");

DEFINE_RUNTIME_uint64(vector_index_task_size, 16,
    "Number of vectors in a single insert subtask for vector index. "
    "When the number of elements in the batch is more than this times the task "
    "size multiple tasks will be used to insert batch.");

DEFINE_RUNTIME_uint64(vector_index_max_insert_tasks, 1000,
    "Limit for number of insert subtask for vector index. "
    "When this limit is reached, new inserts are blocked until necessary amount "
    "of tasks is released.");

DEFINE_RUNTIME_uint64(vector_index_max_merge_tasks, 100,
    "Limit for number of merge subtasks for vector index compaction. "
    "The same thread pool as for inserts is used to run merge tasks. "
    "Set to 0 to disable usage of insert thread pool for merge tasks.");

DEFINE_RUNTIME_uint64(vector_index_task_pool_size, 1000,
    "Pool size of insert subtasks for vector index. "
    "Pool is just used to avoid memory allocations and does not limit the total "
    "number of tasks.");

DEFINE_RUNTIME_uint64(vector_index_compaction_always_include_size_threshold, 64_MB,
    "Always include chunks of smaller or equal size in a compaction by size ratio.");

DEFINE_RUNTIME_int32(vector_index_compaction_priority_start_bound, 0,
    "Compaction task of Vector LSM that has number of chunk files less than specified will have "
    "priority 0. Set to -1 to use the value of 'compaction_priority_start_bound' flag.");

DEFINE_RUNTIME_int32(vector_index_compaction_priority_step_size, -1,
    "Compaction task of Vector LSM that has number of chunk files greater than "
    "vector_index_compaction_priority_start_bound will get 1 extra priority per every "
    "vector_index_compaction_priority_step_size files. Set to -1 to use the value of "
    "'compaction_priority_step_size' flag.");

DEFINE_RUNTIME_int32(vector_index_files_number_compaction_trigger, 5,
    "Number of files to trigger Vector LSM background compaction.");

DEFINE_RUNTIME_int32(vector_index_compaction_size_amp_max_percent, 200,
    "The max size amplification for vector index background compaction. "
    "Set to -1 to disable compactions by size amplification.");

DEFINE_RUNTIME_uint32(vector_index_compaction_size_amp_max_merge_width, 0,
    "The maximum number of chunks in a single background compaction by size amplification. "
    "0 - no limit.");

DEFINE_RUNTIME_int32(vector_index_compaction_size_ratio_percent, 20,
    "The percentage upto which chunks that are larger are include in vector index "
    "background compaction. Set to -100 to disable compactions by size ratio.");

DEFINE_RUNTIME_int32(vector_index_compaction_size_ratio_min_merge_width, 4,
    "The minimum number of chunks in a single background compaction by size ratio.");

DEFINE_RUNTIME_uint32(vector_index_compaction_size_ratio_max_merge_width, 0,
    "The maximum number of chunks in a single background compaction by size ratio. 0 - no limit.");

DEFINE_RUNTIME_bool(vector_index_dump_stats, false,
    "Whether to dump stats related to vector index search.");

DEFINE_RUNTIME_bool(vector_index_enable_compactions, true,
    "Enable Vector LSM background compactions.");

DEFINE_test_flag(bool, vector_index_skip_manifest_update_during_shutdown, false,
    "Whether VectorLSM manifest update should be skipped after shutdown has been initiated");

DEFINE_test_flag(uint64, vector_index_delay_saving_first_chunk_ms, 0,
    "Delay saving the first chunk in VectorLSM for specified amount of milliseconds");

DECLARE_int32(compaction_priority_start_bound);
DECLARE_int32(compaction_priority_step_size);

namespace yb::vector_index {

namespace bi = boost::intrusive;

namespace {

YB_DEFINE_ENUM(ImmutableChunkState, (kInMemory)(kOnDisk)(kInManifest));
YB_DEFINE_ENUM(CompactionState, (kNone)(kCompacting)(kCompacted));
YB_DEFINE_ENUM(CompactionType, (kBackground)(kManual));
YB_DEFINE_ENUM(ManifestUpdateType, (kFull)(kActual));

// While mutable chunk is running, this value is added to num_tasks.
// During stop, we decrease num_tasks by this value. So zero num_tasks means that mutable chunk
// is stopped.
constexpr size_t kRunningMark = 1e18;

std::string GetChunkPath(
    const std::string& storage_dir, const std::string& extension, uint64_t chunk_serial_no) {
  return JoinPathSegments(storage_dir, Format("vectorindex_$0$1", chunk_serial_no, extension));
}

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
std::string GetChunkPath(
    const VectorLSMOptions<Vector, DistanceResult>& options, uint64_t chunk_serial_no) {
  return GetChunkPath(options.storage_dir, options.file_extension, chunk_serial_no);
}

size_t MaxConcurrentReads() {
  auto max_concurrent_reads = FLAGS_vector_index_concurrent_reads;
  return max_concurrent_reads == 0 ? std::thread::hardware_concurrency() : max_concurrent_reads;
}

int32_t CompactionPriorityStartBound() {
  const auto start_bound = FLAGS_vector_index_compaction_priority_start_bound;
  return start_bound >= 0 ? start_bound : FLAGS_compaction_priority_start_bound;
}

int32_t CompactionPriorityStepSize() {
  const auto step_size = FLAGS_vector_index_compaction_priority_step_size;
  return step_size >= 0 ? step_size : FLAGS_compaction_priority_step_size;
}

size_t SizeRatioCompactionMinMergeWidth() {
  const auto width = make_unsigned(FLAGS_vector_index_compaction_size_ratio_min_merge_width);
  return std::max<size_t>(width, 2); // Must be at least two chunks.
}

size_t SizeRatioCompactionMaxMergeWidth() {
  auto width = FLAGS_vector_index_compaction_size_ratio_max_merge_width;
  if (width == 0) {
    width = std::numeric_limits<decltype(width)>::max();
  }

  // Should not be lower than min merge width.
  return std::max<size_t>(width, SizeRatioCompactionMinMergeWidth());
}

size_t SizeAmpCompactionMinMergeWidth() {
  return 2; // Must be at least two chunks.
}

size_t SizeAmpCompactionMaxMergeWidth() {
  auto width = FLAGS_vector_index_compaction_size_amp_max_merge_width;
  if (width == 0) {
    width = std::numeric_limits<decltype(width)>::max();
  }

  // Should not be lower than min merge width.
  return std::max<size_t>(width, SizeAmpCompactionMinMergeWidth());
}

size_t FilesNumberCompactionTrigger() {
  return make_unsigned(FLAGS_vector_index_files_number_compaction_trigger);
}

template<typename Tasks>
bool ContainsTask(const Tasks& tasks, CompactionType type) {
  return std::ranges::any_of(tasks, [type](auto&& task){ return task->compaction_type() == type; });
}

} // namespace

MonoDelta TEST_sleep_during_flush;
MonoDelta TEST_sleep_on_merged_chunk_populated;

class VectorLSMFileMetaData final {
 public:
  explicit VectorLSMFileMetaData(uint64_t serial_no, uint64_t size_on_disk)
      : serial_no_(serial_no), size_on_disk_(size_on_disk)
  {}

  uint64_t serial_no() const {
    return serial_no_;
  }

  uint64_t size_on_disk() const {
    return size_on_disk_;
  }

  bool IsObsolete() const {
    return obsolete_.load(std::memory_order::acquire);
  }

  void MarkObsolete() {
    obsolete_.store(true, std::memory_order::release);
  }

  std::string ToString() const {
    return YB_CLASS_TO_STRING(serial_no, size_on_disk);
  }

 private:
  const uint64_t serial_no_;
  const uint64_t size_on_disk_;
  std::atomic<bool> obsolete_ = { false };
};

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

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertTaskSearchWrapper final : public VectorLSMInsertTask<Vector, DistanceResult> {
 public:
  using Base = VectorLSMInsertTask<Vector, DistanceResult>;
  using VectorWithDistance = typename VectorLSM<Vector, DistanceResult>::VectorWithDistance;
  using SearchHeap = std::priority_queue<VectorWithDistance>;

  void Search(
      SearchHeap& heap, const Vector& query_vector, const SearchOptions& options) const {
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

 private:
  // The class is just a wrapper variables must be defined.
  using Base::mutex_;
  using Base::index_;
  using Base::vectors_;
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
    for (auto& task : list) {
      thread_pool_.Enqueue(&task);
    }
    std::lock_guard lock(mutex_);
    active_tasks_.splice(active_tasks_.end(), list);
  }

  void TaskDone(InsertTask* raw_task) EXCLUDES(mutex_) {
    DCHECK_ONLY_NOTNULL(raw_task);

    InsertTaskPtr task(raw_task);
    {
      std::lock_guard lock(mutex_);
      --allocated_tasks_;
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

  VectorLSMInsertRegistryBase(const std::string& log_prefix, rpc::ThreadPool& thread_pool)
      : log_prefix_(log_prefix), thread_pool_(thread_pool) {}

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
      : Base(log_prefix, thread_pool) {}

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
    using SearchWrapper = VectorLSMInsertTaskSearchWrapper<Vector, DistanceResult>;
    using SearchHeap = typename SearchWrapper::SearchHeap;
    SearchHeap heap;
    {
      SharedLock lock(mutex_);
      for (const auto& task : active_tasks_) {
        static_cast<const SearchWrapper&>(task).Search(heap, query_vector, options);
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
      : Base(log_prefix, thread_pool) {}

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

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
struct VectorLSM<Vector, DistanceResult>::MutableChunk {
  VectorIndexPtr index;

  // Must be accessed under VectorLSM::mutex_.
  size_t num_entries = 0;

  // See comments for kRunningMark.
  std::atomic<size_t> num_tasks = kRunningMark;

  // An access to this variable is synchronized by num_tasks: it is guaranteed that the variable
  // is set and it is safe to trigger save_callback only when num_tasks < kRunningMark.
  std::function<void()> save_callback;

  rocksdb::UserFrontiersPtr user_frontiers;

  // Used to indicates this chunk insertion failed and hence save_callback should not be called.
  std::atomic<bool> insertion_failed { false };

  // Returns true if registration was successful. Otherwise, new mutable chunk should be allocated.
  // Invoked when owning VectorLSM holds the mutex.
  bool RegisterInsert(
      const std::vector<InsertEntry>& entries, const Options& options, size_t new_tasks,
      const rocksdb::UserFrontiers* frontiers) {
    if (num_entries && num_entries + entries.size() > index->Capacity()) {
      return false;
    }
    num_entries += entries.size();
    num_tasks += new_tasks;
    if (frontiers) {
      rocksdb::UpdateFrontiers(user_frontiers, *frontiers);
    }
    return true;
  }

  void InsertTaskDone() {
    auto new_tasks = --num_tasks;
    if (new_tasks == 0) {
      DCHECK(save_callback);
      if (!insertion_failed.load(std::memory_order::acquire)) {
        save_callback();
      }
      save_callback = {};
    }
  }

  // Must be triggered under VectorLSM::mutex_.
  ImmutableChunkPtr Immutate(size_t order_no, std::promise<Status>* flush_promise) {
    // Move should not be used for index as the mutable chunk could still be used by other
    // entities, for example by VectorLSMInsertTask.
    return std::make_shared<ImmutableChunk>(
        order_no, num_entries ? index : VectorIndexPtr{},
        std::move(user_frontiers), flush_promise);
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(num_entries, num_tasks, user_frontiers);
  }
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
struct VectorLSM<Vector, DistanceResult>::ImmutableChunk {
  // Chunk's associated file metadata. Can be null.
  VectorLSMFileMetaDataPtr file;

  // In memory order for chunk. All chunks in immutable chunks are ordered using it.
  // Could be changed between application runs.
  const size_t order_no = 0;

  // Must be accessed under LSM::mutex_ lock to guarantee thread-safety.
  ImmutableChunkState state = ImmutableChunkState::kInMemory;

  VectorIndexPtr index;

  // Must be accessed under LSM::mutex_ lock to guarantee thread-safety.
  const rocksdb::UserFrontiersPtr user_frontiers;

 private:
  // Must be accessed under LSM::mutex_ lock to guarantee thread-safety.
  std::promise<Status>* flush_promise = nullptr;

  std::atomic<CompactionState> compaction_state = { CompactionState::kNone };

 public:
  ImmutableChunk(
      size_t order_no_,
      VectorLSMFileMetaDataPtr&& file_,
      VectorIndexPtr&& index_,
      rocksdb::UserFrontiersPtr&& user_frontiers_,
      ImmutableChunkState state_)
      : file(std::move(file_)),
        order_no(order_no_),
        state(state_),
        index(std::move(index_)),
        user_frontiers(std::move(user_frontiers_))
  {}

  ImmutableChunk(
      size_t order_no_,
      VectorIndexPtr&& index_,
      rocksdb::UserFrontiersPtr&& user_frontiers_,
      std::promise<Status>* flush_promise_)
      : order_no(order_no_),
        state(ImmutableChunkState::kInMemory),
        index(std::move(index_)),
        user_frontiers(std::move(user_frontiers_)),
        flush_promise(flush_promise_)
  {}

  uint64_t serial_no() const {
    return file ? file->serial_no() : 0;
  }

  uint64_t file_size() const {
    return file ? file->size_on_disk() : 0;
  }

  // Must be triggered under LSM::mutex_ lock to guarantee thread-safety.
  bool IsOnDisk() const {
    return state == ImmutableChunkState::kOnDisk;
  }

  // Must be triggered under LSM::mutex_ lock to guarantee thread-safety.
  bool IsInManifest() const {
    return state == ImmutableChunkState::kInManifest;
  }

  // Must be triggered under LSM::mutex_ lock to guarantee thread-safety.
  void Flushed(const Status& status = Status::OK()) {
    if (flush_promise) {
      flush_promise->set_value(status);
      flush_promise = nullptr;
    }
  }

  void AddToUpdate(VectorLSMUpdatePB& update) {
    auto& added_chunk = *update.mutable_add_chunks()->Add();
    added_chunk.set_serial_no(serial_no());
    added_chunk.set_order_no(order_no);
    user_frontiers->Smallest().ToPB(added_chunk.mutable_smallest()->mutable_user_frontier());
    user_frontiers->Largest().ToPB(added_chunk.mutable_largest()->mutable_user_frontier());
  }

  void MarkObsolete() {
    // Sanity check. The expectation is to obsolete only compacted chunks.
    // However, obsoleting of non-compacted chunks could become a valid case in future.
    CHECK_EQ(CompactionState::kCompacted, compaction_state.load(std::memory_order::acquire));
    if (file) {
      file->MarkObsolete();
    }
  }

  bool TryLockForCompaction() {
    auto expected_state = CompactionState::kNone;
    return compaction_state.compare_exchange_strong(expected_state, CompactionState::kCompacting);
  }

  bool UnlockForCompaction() {
    // A chunk can be "unlocked for compaction" if it is being compacted now or still non-compacted.
    auto expected_state = CompactionState::kCompacting;
    return compaction_state.compare_exchange_strong(expected_state, CompactionState::kNone) ||
           expected_state == CompactionState::kNone;
  }

  void Compacted() {
    // A chunk must fall into all stages before being marked as compacted:
    // kNone => kCompaction => kCompacted.
    auto expected_state = CompactionState::kCompacting;
    CHECK(compaction_state.compare_exchange_strong(expected_state, CompactionState::kCompacted));
    MarkObsolete();
  }

  // Must be triggered under LSM::mutex_ lock to guarantee thread-safety.
  bool ReadyForCompaction() const {
    return compaction_state.load(std::memory_order::acquire) == CompactionState::kNone &&
           IsInManifest();
  }

  std::string ToString() const {
    // TODO(vector_index): access to state and user_frontiers should probably be under LSM::mutex_.
    return YB_STRUCT_TO_STRING(
        order_no, state, file, user_frontiers,
        (index, AsString(static_cast<void*>(index.get()))),
        compaction_state);
  }

  std::string ToShortString() const {
    return Format(
        "{order_no: $0, serial_no: $1, file_size: $2}",
        order_no, serial_no(), file_size());
  }
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSM<Vector, DistanceResult>::CompactionScope {
 public:
  ~CompactionScope() {
    Unlock();
  }

  CompactionScope() = default;
  CompactionScope(const CompactionScope&) = delete;
  CompactionScope(CompactionScope&&) = default;

  CompactionScope& operator=(const CompactionScope&) = delete;
  CompactionScope& operator=(CompactionScope&&) = default;

  const ImmutableChunkPtrs& chunks() const {
    return chunks_;
  }

  bool contains(size_t chunk_index) const {
    return index() <= chunk_index && chunk_index < end_index();
  }

  size_t index() const {
    return index_;
  }

  bool empty() const {
    return chunks_.empty();
  }

  void reserve(size_t size) {
    chunks_.reserve(size);
  }

  size_t size() const {
    return chunks_.size();
  }

  // Must be triggered under LSM::mutex_ to have thread safe access to chunk's state.
  bool TryLock(size_t chunk_index, const ImmutableChunkPtr& chunk) {
    DCHECK(chunk.get());
    if (!chunk->IsInManifest() || !chunk->TryLockForCompaction()) {
      return false;
    }

    if (empty()) {
      index_ = chunk_index;
    } else {
      DCHECK_EQ(chunk_index, end_index());
    }

    chunks_.emplace_back(chunk);
    return true;
  }

  void Unlock() {
    for (auto& chunk : chunks_) {
      // Something went wrong: compacted chunks must be excluded from chunks_ collection.
      CHECK(chunk->UnlockForCompaction());
    }
  }

  void Compacted() {
    for (auto& chunk : chunks_) {
      chunk->Compacted();
    }
    // Required to unreference immutable chunks and their members.
    chunks_.clear();
    index_ = 0;
  }

  void AddToUpdate(VectorLSMUpdatePB& update) {
    for (const auto& chunk : chunks_) {
      // Empty chunk is possible, for example it could be used to update frontiers.
      if (chunk->file) {
        update.add_remove_chunks(chunk->file->serial_no());
      }
    }
  }

  std::string ToString() const {
    static auto chunks_formatter = [](const auto& chunk) {
      return chunk->ToShortString();
    };
    return YB_CLASS_TO_STRING(index, (chunks, AsString(chunks_, chunks_formatter)));
  }

 private:
  // Exclusive upper bound.
  size_t end_index() const {
    return index_ + size();
  }

  // Index of the front chunk from chunks in the LSM's immutable chunks collection.
  size_t index_ = 0;

  // Continuous interval of chunks from LSM's immutable chunks collection.
  ImmutableChunkPtrs chunks_;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
struct VectorLSM<Vector, DistanceResult>::CompactionContext {
  size_t task_no;
  CompactionType type;

  ManifestUpdateType GetManifestUpdateType() const {
    return type == CompactionType::kManual ? ManifestUpdateType::kFull
                                           : ManifestUpdateType::kActual;
  }

  bool ForceManifestRoll() const {
    return type == CompactionType::kManual;
  }
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSM<Vector, DistanceResult>::CompactionTask : public PriorityThreadPoolTokenTask {
  using Base = PriorityThreadPoolTokenTask;
  using LSM = VectorLSM<Vector, DistanceResult>;

 public:
  CompactionTask(
      LSM& lsm,
      CompactionType compaction_type = CompactionType::kBackground,
      CompactionScope&& compaction_scope = {},
      StdStatusCallback&& callback = {})
      : Base(lsm.options_.compaction_token->context()),
        lsm_(lsm),
        log_prefix_(Format("$0[TASK $1] ", lsm.LogPrefix(), SerialNo())),
        compaction_type_(compaction_type),
        compaction_scope_(std::move(compaction_scope)),
        callback_(std::move(callback)),
        priority_(CalculatePriority()) {
  }

  CompactionType compaction_type() const {
    return compaction_type_;
  }

  uint64_t group_no() const override {
    return kDefaultGroupNo;
  }

  int priority() const override {
    return priority_.load(std::memory_order::acquire);
  }

  void UpdatePriority() {
    priority_.store(CalculatePriority(), std::memory_order::release);
  }

  std::string ToString() const override {
    return yb::Format("{ serial_no: $0, compaction_type: $1 }", SerialNo(), compaction_type_);
  }

 private:
  void Run(const Status& ready, PriorityThreadPoolSuspender* suspender) override {
    if (!ready.ok()) {
      LOG_WITH_PREFIX(INFO) << "Not ready: " << ready;
      return Completed(ready);
    }

    // Remember current serial_no to track possible changes in immutable chunks.
    const auto last_serial_no = lsm_.LastSerialNo();

    // TODO(vector_index): leverage the suspender.
    auto status = DoRun();
    if (status.ok() || status.IsShutdownInProgress()) {
      LOG_WITH_PREFIX(INFO) << "Done: " << status;
    } else {
      LOG_WITH_PREFIX(DFATAL) << "Failed: " << status;
    }
    Completed(status);

    if (last_serial_no == lsm_.LastSerialNo()) {
      VLOG_WITH_FUNC(2) << "VectorLSM not changed, no need to schedule next background compaction";
    } else {
      VLOG_WITH_FUNC(2) << "VectorLSM changed, scheduling next background compaction";
      lsm_.ScheduleBackgroundCompaction();
    }
  }

  bool ShouldRemoveWithKey(void* key) override {
    return key == &lsm_;
  }

  int CalculateGroupNoPriority(int active_tasks) const override {
    return rocksdb::internal::kTopDiskCompactionPriority - active_tasks;
  }

  int CalculatePriority() const {
    if (lsm_.IsShuttingDown()) {
      return rocksdb::internal::kShuttingDownPriority;
    }

    const int num_chunks  = narrow_cast<int>(lsm_.NumSavedImmutableChunks());
    const int start_bound = CompactionPriorityStartBound();
    int result = 0;
    if (num_chunks >= start_bound) {
      result = 1 + (num_chunks - start_bound) / CompactionPriorityStepSize();
    }
    return result;
  }

  void EnsureCompactionScope() {
    if (compaction_type_ == CompactionType::kBackground && compaction_scope_.empty()) {
      compaction_scope_ = lsm_.PickChunksForCompaction();
    }
  }

  Status DoRun() {
    EnsureCompactionScope();

    if (compaction_scope_.empty()) {
      LOG_WITH_PREFIX(INFO) << "Nothing to compact";
      return Status::OK();
    }

    CompactionContext context {
      .task_no = SerialNo(),
      .type = compaction_type(),
    };

    return lsm_.DoCompact(context, std::move(compaction_scope_));
  }

  void Completed(const Status& status) {
    lsm_.Deregister(*this);
    if (callback_) {
      callback_(status);
    }
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  LSM& lsm_;
  std::string log_prefix_;
  CompactionType    compaction_type_;
  CompactionScope   compaction_scope_;
  StdStatusCallback callback_;
  std::atomic<int>  priority_;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::VectorLSM()
    // TODO(vector_index) Use correct env for encryption
    : env_(Env::Default()) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::~VectorLSM() {
  StartShutdown();
  CompleteShutdown();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::DoCheckRunning(
    const char* file_name, int line_number) const {
  if (shutdown_controller_.IsRunning()) {
    return Status::OK();
  }

  Status status {
      Status::kShutdownInProgress, file_name, line_number, "VectorLSM is shutting down" };
  LOG_WITH_PREFIX(INFO) << status;
  return status;
}
#define RUNNING_STATUS() DoCheckRunning(__FILE__, __LINE__)

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::IsShuttingDown() const {
  auto status = RUNNING_STATUS();
  DCHECK(status.ok() || status.IsShutdownInProgress());
  return status.IsShutdownInProgress();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::StartShutdown() {
  auto scope = shutdown_controller_.StartShutdown();
  if (!scope) {
    LOG_IF_WITH_PREFIX(DFATAL, !scope.status().IsShutdownInProgress()) << scope.status();
    return;
  }

  LOG_WITH_PREFIX(INFO) << "VectorLSM shutdown started";
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::CompleteShutdown() {
  auto scope = shutdown_controller_.CompleteShutdown();
  if (!scope) {
    LOG_IF_WITH_PREFIX(DFATAL, !scope.status().IsShutdownInProgress()) << scope.status();
    return;
  }

  if (!insert_registry_) {
    return; // Was not opened.
  }
  insert_registry_->Shutdown();

  if (merge_registry_) {
    merge_registry_->Shutdown();
  }

  // Wait for all chunks to be saved.
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

  // Remove not started compaction tasks from the thread pool.
  options_.compaction_token->Remove(this);

  // Prioritize remaining compaction tasks and wait for compactions to be completed.
  {
    // TODO(vector_index): probably options_.compaction_token->PrioritizeTask() should
    // be triggered here.
    UniqueLock lock(compaction_tasks_mutex_);
    for (auto* task : compaction_tasks_) {
      task->UpdatePriority();
    }
    WaitForCompactionTasksDone(lock);
  }

  // Wait for obsolete files deletions if any.
  while (obsolete_files_cleanup_in_progress_) {
    std::this_thread::sleep_for(10ms);
  }

  // TODO(vector_index): schedule obsolete files deletion on startup.
  {
    std::lock_guard lock(cleanup_mutex_);
    obsolete_files_.clear();
  }

  LOG_WITH_PREFIX(INFO) << "VectorLSM shutdown completed";
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
  YB_LOG_WITH_PREFIX_EVERY_N_SECS(WARNING, 1)
      << "Vector LSM already in failed state: " << existing_status
      << ", while trying to set new failed state: " << status;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Open(Options options) {
  // Explicitly set the state of the controller for the cases when vector index can be reopened
  // after being shutdown.
  RETURN_NOT_OK(shutdown_controller_.Start());

  std::lock_guard lock(mutex_);

  options_ = std::move(options);
  if (options_.metric_entity) {
    metrics_ = std::make_unique<vector_index::VectorLSMMetrics>(options_.metric_entity);
  }
  insert_registry_ = std::make_shared<InsertRegistry>(
      options_.log_prefix, *options_.insert_thread_pool);
  merge_registry_ = std::make_shared<MergeRegistry>(
      options_.log_prefix, *options_.insert_thread_pool);

  RETURN_NOT_OK(env_->CreateDirs(options_.storage_dir));
  auto load_result = VERIFY_RESULT(VectorLSMMetadataLoad(env_, options_.storage_dir));
  next_manifest_file_no_ = load_result.next_free_file_no;
  std::unordered_map<uint64_t, const VectorLSMChunkPB*> chunks;
  for (const auto& update : load_result.updates) {
    for (const auto& chunk : update.add_chunks()) {
      chunks.emplace(chunk.serial_no(), &chunk);
    }
    for (const auto chunk_no : update.remove_chunks()) {
      if (!chunks.erase(chunk_no)) {
        return STATUS_FORMAT(Corruption, "Attempt to remove non existing chunk: $0", chunk_no);
      }
    }
  }

  for (const auto& [_, chunk_pb] : chunks) {
    VectorLSMFileMetaDataPtr file;
    VectorIndexPtr index;
    if (chunk_pb->serial_no()) {
      index = options_.vector_index_factory(FactoryMode::kLoad);

      const auto file_size = VERIFY_RESULT(GetChunkFileSize(chunk_pb->serial_no()));
      file = CreateVectorLSMFileMetaData(*index, chunk_pb->serial_no(), file_size);
    }

    auto user_frontiers = options_.frontiers_factory();
    RETURN_NOT_OK(user_frontiers->Smallest().FromPB(chunk_pb->smallest().user_frontier()));
    RETURN_NOT_OK(user_frontiers->Largest().FromPB(chunk_pb->largest().user_frontier()));
    immutable_chunks_.push_back(std::make_shared<ImmutableChunk>(
        chunk_pb->order_no(), std::move(file), std::move(index),
        std::move(user_frontiers), ImmutableChunkState::kInManifest
    ));

    last_serial_no_ = std::max<uint64_t>(last_serial_no_, chunk_pb->serial_no());
  }

  std::vector<std::promise<Status>> promises(immutable_chunks_.size());
  for (size_t i = 0; i != immutable_chunks_.size(); ++i) {
    auto& chunk = immutable_chunks_[i];
    if (!chunk->file) {
      promises[i].set_value(Status::OK());
      continue;
    }
    options_.insert_thread_pool->EnqueueFunctor([this, &promise = promises[i], &chunk] {
      auto status = chunk->index->LoadFromFile(
          GetChunkPath(options_, chunk->file->serial_no()),
          MaxConcurrentReads());
      if (!status.ok()) {
        status = status.CloneAndPrepend(Format("Failed to load $0", chunk->file->serial_no()));
        LOG_WITH_PREFIX(INFO) << status;
      }
      promise.set_value(status);
    });
  }
  for (auto& promise : promises) {
    auto status = promise.get_future().get();
    if (!status.ok() && failed_status_.ok()) {
      failed_status_ = status;
    }
  }

  std::sort(
      immutable_chunks_.begin(), immutable_chunks_.end(), [](const auto& lhs, const auto& rhs) {
    return lhs->order_no < rhs->order_no;
  });
  LOG_WITH_PREFIX(INFO) << "Loaded " << immutable_chunks_.size() << " chunks, "
                        << "last serial no: " << last_serial_no_ << ", "
                        << "next manifest no: " << next_manifest_file_no_;
  VLOG_WITH_PREFIX(2)   << "Loaded " << AsString(immutable_chunks_);

  if (auto_compactions_enabled_) {
    LOG_WITH_PREFIX(INFO) << "Background compactions enabled";
  }

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Destroy() {
  LOG_WITH_PREFIX(INFO) << __func__;
  StartShutdown();
  CompleteShutdown();
  return env_->DeleteRecursively(options_.storage_dir);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::CreateCheckpoint(const std::string& out) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << out;

  decltype(immutable_chunks_) chunks;
  {
    std::lock_guard lock(mutex_);
    chunks.reserve(immutable_chunks_.size());
    for (const auto& chunk : immutable_chunks_) {
      if (chunk->IsInManifest()) {
        chunks.push_back(chunk);
      }
    }
  }

  RETURN_NOT_OK(env_->CreateDirs(out));
  VectorLSMUpdatePB update;
  for (const auto& chunk : chunks) {
    if (chunk->file) {
      const auto serial_no = chunk->file->serial_no();
      RETURN_NOT_OK(env_->LinkFile(
          GetChunkPath(options_, serial_no),
          GetChunkPath(out, options_.file_extension, serial_no)));
    }
    chunk->AddToUpdate(update);
  }

  auto manifest_file = VERIFY_RESULT(VectorLSMMetadataOpenFile(env_, out, 0));
  RETURN_NOT_OK(VectorLSMMetadataAppendUpdate(*manifest_file, update));

  // TODO(vector_index): merge the cleanup logic with the same from DoCompactChunks().
  // Some chunks could become obsolete, let's explicitly unreference them and trigger obsolete
  // chunks cleanup.
  chunks.clear();
  TriggerObsoleteChunksCleanup(/* async = */ true);

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Insert(
    std::vector<InsertEntry> entries, const VectorLSMInsertContext& context) {
  VLOG_WITH_PREFIX_AND_FUNC(5)
      << "entries: " << entries.size() << ", frontier: " << AsString(context.frontiers);

  MutableChunkPtr chunk;
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

  auto tasks = VERIFY_RESULT(insert_registry_->AllocateTasks(
      num_tasks, chunk->index,
      [this, chunk](const Status& status) {
        if (!status.ok()) {
          auto failure = status.CloneAndPrepend("VectorLSM insertion failed");
          LOG(ERROR) << LogPrefix() << failure;
          CheckFailure(failure);
          chunk->insertion_failed.store(false, std::memory_order::release);
        }
        chunk->InsertTaskDone();
      }));
  DCHECK_EQ(num_tasks, tasks.size());

  auto tasks_it = tasks.begin();
  size_t index_in_task = 0;
  for (auto& [vector_id, v] : entries) {
    if (index_in_task++ >= entries_per_task) {
      ++tasks_it;
      index_in_task = 0;
    }
    tasks_it->Add(vector_id, std::move(v));
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

  // Because of concurrency we could get the same vector from different sources.
  // So remove duplicates from chunk_results before merging.
  {
    // It could happen that results were not sorted by vector id, when distances are equal.
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

  auto start_registry_search = MonoTime::Now();
  auto intermediate_results = insert_registry_->Search(query_vector, options);
  VLOG_WITH_PREFIX_AND_FUNC(4) << "Results from registry: " << AsString(intermediate_results);
  size_t num_results_from_insert_registry = intermediate_results.size();

  size_t sum_num_found_entries = 0;
  auto start_chunks_search = MonoTime::Now();
  for (const auto& index : indexes) {
    auto chunk_results = VERIFY_RESULT(index->Search(query_vector, options));
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Chunk " << index->IndexStatsStr() << " results: " << AsString(chunk_results);
    sum_num_found_entries += chunk_results.size();
    MergeChunkResults(intermediate_results, chunk_results, options.max_num_results);
  }
  auto stop_search = MonoTime::Now();
  auto search_insert_registry_time = start_chunks_search - start_registry_search;
  auto chunks_search_time = stop_search - start_chunks_search;

  if (metrics_) {
    metrics_->num_chunks->Increment(indexes.size());
    metrics_->total_found_entries->Increment(sum_num_found_entries);
    metrics_->insert_registry_entries->Increment(num_results_from_insert_registry);
    metrics_->insert_registry_search_us->Increment(search_insert_registry_time.ToMicroseconds());
    metrics_->chunks_search_us->Increment(chunks_search_time.ToMicroseconds());
  }

  LOG_IF_WITH_PREFIX_AND_FUNC(INFO, dump_stats)
      << "VI_STATS: Number of chunks: " << indexes.size() << ", entries found in all chunks: "
      << sum_num_found_entries << ", entries found in insert registry: "
      << num_results_from_insert_registry << ", time to search insert registry: "
      << search_insert_registry_time.ToPrettyString()
      << ", time to search index chunks: " << chunks_search_time.ToPrettyString();

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
Result<size_t> VectorLSM<Vector, DistanceResult>::TotalEntries() const {
  while (insert_registry_->HasRunningTasks()) {
    std::this_thread::sleep_for(10ms);
  }
  auto indexes = VERIFY_RESULT(AllIndexes());
  size_t result = 0;
  for (const auto& index : indexes) {
    result += index->Size();
  }
  return result;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSMFileMetaDataPtr VectorLSM<Vector, DistanceResult>::CreateVectorLSMFileMetaData(
    VectorIndex& index, uint64_t serial_no, uint64_t size_on_disk) {
  auto* raw_ptr = new VectorLSMFileMetaData(serial_no, size_on_disk);
  auto ptr = VectorLSMFileMetaDataPtr(raw_ptr, [this](VectorLSMFileMetaData* raw_ptr) {
    std::unique_ptr<VectorLSMFileMetaData> ptr { raw_ptr };
    if (!ptr->IsObsolete()) {
      return; // Non-obsolete files should be kept on disk, just release the memory.
    }

    ObsoleteFile(std::move(ptr));
  });

  index.Attach(ptr);
  return ptr;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
uint64_t VectorLSM<Vector, DistanceResult>::NextSerialNo() {
  std::lock_guard lock(mutex_);
  return ++last_serial_no_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
uint64_t VectorLSM<Vector, DistanceResult>::LastSerialNo() const {
  SharedLock lock(mutex_);
  return last_serial_no_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::TEST_SkipManifestUpdateDuringShutdown() {
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "Hit test condition";
  for (auto it = updates_queue_.begin(); it != updates_queue_.end();) {
    if (it->second->IsOnDisk()) {
      LOG_WITH_PREFIX_AND_FUNC(INFO) << "Erasing chunk: " << it->second->ToString();
      it = updates_queue_.erase(it);
    } else {
      ++it;
    }
  }
  if (updates_queue_.empty()) {
    updates_queue_empty_cv_.notify_all();
  }
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::ManifestAcquired() {
  SharedLock lock(mutex_);
  return writing_manifest_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::AcquireManifest() {
  UniqueLock lock(mutex_);

  // Wait for all current writes to manifest file are done.
  writing_manifest_done_cv_.wait(
      lock, [this]() NO_THREAD_SAFETY_ANALYSIS { return !writing_manifest_; });

  // Take the ownership of the writing state.
  writing_manifest_ = true;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::ReleaseManifest() {
  std::lock_guard lock(mutex_);
  ReleaseManifestUnlocked();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::ReleaseManifestUnlocked() {
  writing_manifest_ = false;
  writing_manifest_done_cv_.notify_all();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<WritableFile*> VectorLSM<Vector, DistanceResult>::RollManifest() {
  manifest_file_ = VERIFY_RESULT(VectorLSMMetadataOpenFile(
      env_, options_.storage_dir, next_manifest_file_no_++));
  return manifest_file_.get();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
auto VectorLSM<Vector, DistanceResult>::SaveIndexToFile(VectorIndex& index, uint64_t serial_no) ->
    Result<std::pair<VectorLSMFileMetaDataPtr, VectorIndexPtr>> {
  VLOG_WITH_PREFIX(1) << Format(
      "Saving vector index on disk, serial_no: $0, num entries: $1",
      serial_no, index.Size());

  const auto file_path = GetChunkPath(options_, serial_no);
  auto new_index = VERIFY_RESULT(index.SaveToFile(file_path));
  auto& actual_index = new_index ? *new_index : index;

  const auto file_size = VERIFY_RESULT(env_->GetFileSize(file_path));
  LOG_WITH_PREFIX(INFO) << Format(
      "Saved vector index on disk, serial_no: $0, num entries: $1, file size: $2, path: $3",
      serial_no, actual_index.Size(), file_size, file_path);

  return std::make_pair(CreateVectorLSMFileMetaData(actual_index, serial_no, file_size), new_index);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::DoSaveChunk(const ImmutableChunkPtr& chunk) {
  VLOG_WITH_PREFIX_AND_FUNC(3) << AsString(*chunk);

  VectorIndexPtr new_index;
  if (chunk->index) {
    LOG_IF(DFATAL, chunk->file.get())
        << "Chunk is already saved to "
        << GetChunkPath(options_, chunk->file->serial_no());

    const auto serial_no = NextSerialNo();
    if (serial_no == 1 && FLAGS_TEST_vector_index_delay_saving_first_chunk_ms != 0) {
      std::this_thread::sleep_for(FLAGS_TEST_vector_index_delay_saving_first_chunk_ms * 1ms);
    }

    std::tie(chunk->file, new_index) = VERIFY_RESULT(SaveIndexToFile(*chunk->index, serial_no));
  }

  WritableFile* manifest_file = nullptr;
  ImmutableChunkPtr writing_chunk;
  const bool stopping = !shutdown_controller_.IsRunning();
  {
    std::lock_guard lock(mutex_);
    if (new_index) {
      VLOG_WITH_PREFIX_AND_FUNC(3)
          << "Update index: " << AsString(*chunk) << " => " << AsString(new_index->IndexStatsStr());
      chunk->index = new_index;
    }
    chunk->state = ImmutableChunkState::kOnDisk;
    if (stopping && FLAGS_TEST_vector_index_skip_manifest_update_during_shutdown) {
      return TEST_SkipManifestUpdateDuringShutdown();
    }

    if (writing_manifest_) {
      return Status::OK();
    }

    auto it = updates_queue_.find(chunk->order_no);
    RSTATUS_DCHECK(
        it != updates_queue_.end(), RuntimeError,
        "Missing chunk in updates queue: $0", chunk->order_no);

    // It is required to preserve the order of chunks on disk. For that purpose we are iterating
    // back in the ordered flushing queue to make sure all the chunks, starting from the given
    // to the very first chunk, are on disk. The subsequent UpdateManifest() not necessary stop on
    // the given chunk, it takes all the on-disk chunks into account starting from the first one.
    // TODO(vector_index): probably the cycle could be replaced with `updates_queue_.begin() == it`
    // or `updates_queue_.begin()->second->IsOnDisk()`.
    // to identify if we need to capture writing_manifest_.
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Updates queue: " << AsString(updates_queue_);
    for (;;) {
      if (!it->second->IsOnDisk()) {
        return Status::OK();
      }
      if (it == updates_queue_.begin()) {
        break;
      }
      --it;
    }
    writing_manifest_ = true;
    manifest_file = manifest_file_ ? manifest_file_.get() : VERIFY_RESULT(RollManifest());
    writing_chunk = updates_queue_.begin()->second;
  }

  return UpdateManifest(*manifest_file, std::move(writing_chunk));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::AddChunkToManifest(
    WritableFile& manifest_file, ImmutableChunk& chunk) {
  DCHECK_EQ(chunk.state, ImmutableChunkState::kOnDisk);
  DCHECK(ManifestAcquired());

  VectorLSMUpdatePB update;
  chunk.AddToUpdate(update);
  VLOG_WITH_PREFIX_AND_FUNC(3) << update.ShortDebugString();

  RETURN_NOT_OK(VectorLSMMetadataAppendUpdate(manifest_file, update));

  // TODO(vector_index): print chunk number as well, could be covered within #27098.
  LOG_WITH_PREFIX(INFO) << Format("Added chunk to manifest: $0", chunk.ToShortString());
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::UpdateManifest(
    WritableFile& manifest_file, ImmutableChunkPtr chunk) {
  while (chunk) {
    DCHECK_ONLY_NOTNULL(chunk.get());
    RETURN_NOT_OK(AddChunkToManifest(manifest_file, *chunk));

    // Update chunks state and move to the next chunk in the flushing queue if any.
    {
      std::lock_guard lock(mutex_);
      chunk->state = ImmutableChunkState::kInManifest;
      chunk->Flushed(); // TODO(vector_index): maybe trigger outside the lock.

      auto num_erased = updates_queue_.erase(chunk->order_no);
      CHECK_EQ(num_erased, 1) << "Failed to remove written chunk from updates queue";
      if (updates_queue_.empty()) {
        updates_queue_empty_cv_.notify_all();
      }

      if (updates_queue_.size() && updates_queue_.begin()->second->IsOnDisk()) {
        chunk = updates_queue_.begin()->second;
      } else {
        chunk = nullptr;
        ReleaseManifestUnlocked();
      }
    }
  }

  // Scheduling a background compaction after the loop to maybe pick all flushed chunks,
  // rather than trying to schedule after each chunk got manifested.
  ScheduleBackgroundCompaction();

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::SaveChunk(const ImmutableChunkPtr& chunk) {
  if (TEST_sleep_during_flush && chunk->order_no) {
    SleepFor(TEST_sleep_during_flush);
  }

  auto status = DoSaveChunk(chunk);
  if (status.ok()) {
    return;
  }

  chunk->Flushed(status);
  LOG_WITH_PREFIX(DFATAL) << "Save chunk failed: " << status;
  CheckFailure(status);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::DoFlush(std::promise<Status>* promise) {
  size_t order_no = immutable_chunks_.empty() ? 0 : immutable_chunks_.back()->order_no + 1;
  immutable_chunks_.push_back(mutable_chunk_->Immutate(order_no, promise));
  auto chunk = immutable_chunks_.back();
  updates_queue_.emplace(chunk->order_no, chunk);

  mutable_chunk_->save_callback = [this, chunk]() {
    SaveChunk(chunk);
  };

  auto tasks = mutable_chunk_->num_tasks -= kRunningMark;
  RSTATUS_DCHECK_LT(tasks, kRunningMark, RuntimeError, "Wrong value for num_tasks");
  if (tasks == 0) {
    if (!mutable_chunk_->insertion_failed.load(std::memory_order::acquire)) {
      options_.insert_thread_pool->EnqueueFunctor(mutable_chunk_->save_callback);
    }
    // TODO(vector_index): Optimize memory allocation related to save callback
    mutable_chunk_->save_callback = {};
  }
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::RollChunk(size_t min_vectors) {
  VLOG_WITH_PREFIX_AND_FUNC(2) << "min_vectors: " << min_vectors;
  RETURN_NOT_OK(DoFlush(/* promise=*/ nullptr));
  return CreateNewMutableChunk(min_vectors);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<uint64_t> VectorLSM<Vector, DistanceResult>::GetChunkFileSize(uint64_t serial_no) const {
  return env_->GetFileSize(GetChunkPath(options_, serial_no));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<typename VectorLSM<Vector, DistanceResult>::VectorIndexPtr>
VectorLSM<Vector, DistanceResult>::CreateVectorIndex(size_t min_vectors) const {
  auto capacity = std::max(min_vectors, options_.vectors_per_chunk);
  VLOG_WITH_PREFIX_AND_FUNC(1) << "requested capacity: " << capacity;

  auto index = options_.vector_index_factory(FactoryMode::kCreate);
  RETURN_NOT_OK(index->Reserve(
      capacity, options_.insert_thread_pool->options().max_workers, MaxConcurrentReads()));

  VLOG_WITH_PREFIX_AND_FUNC(1) << "created index with capacity: " << index->Capacity();
  return index;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::CreateNewMutableChunk(size_t min_vectors) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << "min_vectors: " << min_vectors;
  VectorIndexPtr index;
  if (mutable_chunk_ && mutable_chunk_->num_entries == 0 &&
      mutable_chunk_->index->Capacity() >= min_vectors) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "reusing index of " << AsString(*mutable_chunk_);
    index = std::move(mutable_chunk_->index);
  } else {
    index = VERIFY_RESULT(CreateVectorIndex(min_vectors));
  }

  mutable_chunk_ = std::make_shared<MutableChunk>();
  mutable_chunk_->index = std::move(index);
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Flush(bool wait) {
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "wait: " << wait;

  if (TEST_sleep_during_flush) {
    SleepFor(TEST_sleep_during_flush);
  }

  RETURN_NOT_OK(RUNNING_STATUS());

  std::promise<Status> promise;
  {
    std::lock_guard lock(mutex_);
    if (!mutable_chunk_) {
      LOG_WITH_PREFIX_AND_FUNC(INFO) << "Noting to flush";
      return Status::OK();
    }
    VLOG_WITH_PREFIX_AND_FUNC(1) << mutable_chunk_->num_entries << " entries";
    RETURN_NOT_OK(DoFlush(wait ? &promise : nullptr));
    mutable_chunk_ = nullptr;
  }

  return wait ? promise.get_future().get() : Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
rocksdb::UserFrontierPtr VectorLSM<Vector, DistanceResult>::GetFlushedFrontier() {
  rocksdb::UserFrontierPtr result;
  std::lock_guard lock(mutex_);
  VLOG_WITH_PREFIX_AND_FUNC(5) << "immutable_chunks: " << AsString(immutable_chunks_);

  for (const auto& chunk : immutable_chunks_) {
    if (!chunk->IsInManifest()) {
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
    if (!chunk->IsInManifest()) {
      return rocksdb::FlushAbility::kAlreadyFlushing;
    }
  }
  if (mutable_chunk_ && mutable_chunk_->num_entries) {
    return rocksdb::FlushAbility::kHasNewData;
  }
  return rocksdb::FlushAbility::kNoNewData;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::NumImmutableChunks() const {
  SharedLock lock(mutex_);
  return immutable_chunks_.size();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::NumSavedImmutableChunks() const {
  SharedLock lock(mutex_);
  return std::ranges::count_if(
      immutable_chunks_,
      [](auto&& chunk) { return chunk->IsInManifest() || chunk->IsOnDisk(); });
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Env* VectorLSM<Vector, DistanceResult>::TEST_GetEnv() const {
  return env_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::TEST_HasBackgroundInserts() const {
  return insert_registry_->HasRunningTasks();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::TEST_HasCompactions() const {
  SharedLock lock(compaction_tasks_mutex_);
  return !compaction_tasks_.empty();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::TEST_ObsoleteFilesCleanupInProgress() const {
  return obsolete_files_cleanup_in_progress_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::TEST_NextManifestFileNo() const {
  SharedLock lock(mutex_);
  return next_manifest_file_no_;
}

// Get the file size of the chunk with the highest serial number.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
uint64_t VectorLSM<Vector, DistanceResult>::TEST_LatestChunkSize() const {
  SharedLock lock(mutex_);
  CHECK(!immutable_chunks_.empty());
  auto it_chunk = std::ranges::max_element(immutable_chunks_, {}, &ImmutableChunk::serial_no);
  return (*it_chunk)->file_size();
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
      index = options_.vector_index_factory(FactoryMode::kCreate);
    }
  }
  return index->Distance(lhs, rhs);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::WaitForFlush() {
  UniqueLock lock(mutex_);

  // TODO(vector_index) Don't wait flushes that started after this call.
  updates_queue_empty_cv_.wait(
      lock, [this]() NO_THREAD_SAFETY_ANALYSIS { return updates_queue_.empty(); });

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::TriggerObsoleteChunksCleanup(bool async) {
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "async = " << async;

  if (!RUNNING_STATUS().ok()) {
    return;
  }

  // The variable obsolete_files_cleanup_in_progress_ is unset in DeleteObsoleteChunks().
  bool in_progress = false;
  if (!obsolete_files_cleanup_in_progress_.compare_exchange_strong(in_progress, true)) {
    LOG_WITH_PREFIX_AND_FUNC(INFO) << "Obsolete chunks cleanup already in progress";
    return;
  }

  // Check if cleanup should be triggered in the same thread.
  if (!async) {
    return DeleteObsoleteChunks();
  }

  // TODO(vector_index): do we need to check for obsolete_files_ emptiness to prevent
  // adding a task when no cleaning up will probably happen?
  auto added = options_.thread_pool->EnqueueFunctor([this] { DeleteObsoleteChunks(); });
  if (!added) {
    obsolete_files_cleanup_in_progress_ = false;
    LOG_WITH_PREFIX_AND_FUNC(INFO)
        << "Failed to schedule obsolete chunks cleanup, probably shutting down";
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::DeleteObsoleteChunks() {
  CHECK(obsolete_files_cleanup_in_progress_);

  // TODO(vector-index): move this paradigm into a separate utility class (already have the same
  // approach somewhere, it is good to combine them).
  for (;;) {
    DoDeleteObsoleteChunks();
    obsolete_files_cleanup_in_progress_ = false;

    if (!RUNNING_STATUS().ok()) {
      return;
    }

    // Check if new obsolete files got added.
    {
      std::lock_guard lock(cleanup_mutex_);
      if (obsolete_files_.empty()) {
        return;
      }
    }

    // Let's try to move into an in-progress state again.
    bool in_progress = false;
    if (!obsolete_files_cleanup_in_progress_.compare_exchange_strong(in_progress, true)) {
      return; // Another task has already started obsolete files cleanup.
    }
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::DoDeleteObsoleteChunks() {
  // First stage: collect current obsolete files.
  decltype(obsolete_files_) files_to_delete;
  {
    std::lock_guard lock(cleanup_mutex_);
    std::swap(obsolete_files_, files_to_delete);
  }

  VLOG_WITH_PREFIX_AND_FUNC(1) << "obsolete files: " << AsString(files_to_delete);
  if (files_to_delete.empty()) {
    LOG_WITH_PREFIX_AND_FUNC(INFO) << "Nothing to cleanup";
    return;
  }

  // Second state: delete files.
  for (const auto& file : files_to_delete) {
    DeleteFile(*file);
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::DeleteFile(const VectorLSMFileMetaData& file) {
  // Sanity check, only obsolete files can be deleted.
  DCHECK(file.IsObsolete());

  auto path = GetChunkPath(options_, file.serial_no());
  if (!env_->FileExists(path)) {
    LOG_WITH_PREFIX(WARNING) << "File does not exist " << path;
    return;
  }

  auto status = env_->DeleteFile(path);
  if (status.ok()) {
    LOG_WITH_PREFIX(INFO) << "Deleted file " << path;
  } else {
    LOG_WITH_PREFIX(DFATAL) << "Failed to delete file " << path << ", status: " << status;
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::ObsoleteFile(
    std::unique_ptr<VectorLSMFileMetaData>&& file) {
  // Let's not queue a file for deletion if vector index is being shutdown.
  if (!RUNNING_STATUS().ok()) {
    return;
  }

  // Sanity check, only obsolete files should be passed.
  DCHECK(file->IsObsolete());

  std::lock_guard lock(cleanup_mutex_);
  obsolete_files_.push_back(std::move(file));

  // TODO(vector_index): maybe trigger ScheduleObsoleteFilesDeletion by some condition.
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::CompactionScope
VectorLSM<Vector, DistanceResult>::PickChunksForFullCompaction() const {
  CompactionScope scope;

  // The collection of immutable chunks is sorted by order_no, Let's start from the beginning
  // and grab a continuous interval of the manifested chunks.
  {
    SharedLock lock(mutex_);
    scope.reserve(immutable_chunks_.size());
    for (size_t i = 0; i < immutable_chunks_.size(); ++i) {
      if (!scope.TryLock(i, immutable_chunks_[i])) {
        break;
      }
      VLOG_WITH_PREFIX(1) << "Manual compaction picking " << immutable_chunks_[i]->ToShortString();
    }
  }

  return scope;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::CompactionScope
VectorLSM<Vector, DistanceResult>::PickChunksReadyForCompaction(
    size_t begin_idx, size_t end_idx, const std::string& reason) const {
  DCHECK_LE(end_idx, immutable_chunks_.size());
  DCHECK_LE(begin_idx, end_idx);

  CompactionScope scope;
  scope.reserve(end_idx - begin_idx);
  for (size_t idx = begin_idx; idx != end_idx; ++idx) {
    const auto& chunk = immutable_chunks_[idx];
    if (scope.TryLock(idx, chunk)) {
      LOG_WITH_PREFIX(INFO) << reason << " picking " << chunk->ToShortString();
    } else {
      // It is not expected to happen.
      LOG_WITH_PREFIX(DFATAL) << reason << " failed to pick " << AsString(chunk);
      break;
    }
  }

  return scope;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::CompactionScope
VectorLSM<Vector, DistanceResult>::PickChunksBySizeAmplification() const {
  // Percentage flexibility while reducing size amplification.
  const auto ratio = FLAGS_vector_index_compaction_size_amp_max_percent;
  if (ratio < 0) {
    VLOG_WITH_PREFIX(2) << "Size amplification compactions disabled by gflag";
    return {};
  }

  // Find base chunk: the very first chunk with data on disk.
  uint64_t base_chunk_size = 0;
  auto base_chunk_it = immutable_chunks_.begin();
  for (; base_chunk_it != immutable_chunks_.end(); ++base_chunk_it) {
    const auto& base_chunk = *base_chunk_it;
    if (!base_chunk->ReadyForCompaction()) {
      VLOG_WITH_PREFIX(2)
          << "Size amp not needed, stopping at base chunk not ready for compaction "
          << base_chunk->ToShortString();
      return {};
    }

    base_chunk_size = base_chunk->file_size();
    if (base_chunk_size) {
      VLOG_WITH_PREFIX(3) << "Size amp base candidate " << base_chunk->ToShortString();
      break;
    }
  }

  // Get the longest span of the candidates ready for the compaction.
  const auto max_width = SizeAmpCompactionMaxMergeWidth();
  size_t num_candidates_on_disk = 1; // Account base chunk.
  uint64_t candidates_size = 0;
  auto candidate_end = base_chunk_it + 1;
  for (; candidate_end != immutable_chunks_.end(); ++candidate_end) {
    if (num_candidates_on_disk >= max_width) {
        VLOG_WITH_PREFIX(3) << Format("Size amp reached max number of candidates ($0)", max_width);
        break;
    }

    const auto& candidate = *candidate_end;
    if (!candidate->ReadyForCompaction()) {
      VLOG_WITH_PREFIX(3)
          << "Size amp stopping at chunk not ready for compaction " << candidate->ToShortString();
      break;
    }

    const auto size = candidate->file_size();
    candidates_size += size;
    num_candidates_on_disk += size != 0;
    VLOG_WITH_PREFIX(3) << "Size amp candidate " << candidate->ToShortString();
  }

  if (num_candidates_on_disk < SizeAmpCompactionMinMergeWidth()) {
    VLOG_WITH_PREFIX(2) << Format("Size amp not needed due to not enough candidates on disk");
  }

  // Size amplification == percentage of additional size: [candidates size / base size] >= ratio.
  if (candidates_size * 100 < ratio * base_chunk_size) {
    VLOG_WITH_PREFIX(2) << Format(
        "Size amp not needed, newer chunks total size: $0 bytes, "
        "earliest chunk size: $1 bytes, size amplification percent: $2",
        candidates_size, base_chunk_size, ratio);
    return {};
  }

  LOG_WITH_PREFIX(INFO) << Format(
      "Size amp needed, newer chunks total size: $0 bytes, "
      "earliest chunk size: $1 bytes, size amplification percent: $2",
      candidates_size, base_chunk_size, ratio);
  return PickChunksReadyForCompaction(
      /* begin_idx = */ 0, std::distance(immutable_chunks_.begin(), candidate_end), "Size amp");
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::CompactionScope
VectorLSM<Vector, DistanceResult>::PickChunksBySizeRatio() const {
  const auto ratio = FLAGS_vector_index_compaction_size_ratio_percent;
  if (ratio <= -100) {
    VLOG_WITH_PREFIX(2) << "Size ratio compactions disabled by gflag";
    return {};
  }

  const auto min_width = SizeRatioCompactionMinMergeWidth();
  if (immutable_chunks_.size() < min_width) {
    VLOG_WITH_PREFIX(2) << "Size ratio compactions not needed, not enough candidates";
    return {};
  }

  const auto max_width = SizeRatioCompactionMaxMergeWidth();
  const uint64_t always_include_size_threshold =
      FLAGS_vector_index_compaction_always_include_size_threshold;

  auto candidate_end_idx = immutable_chunks_.size();
  auto candidate_begin_idx = candidate_end_idx;
  size_t num_candidates_on_disk = 0;

  // Adjust end iterator to stop if there's not enough chunks.
  auto rend = immutable_chunks_.rend() - min_width + 1;
  for (auto it = immutable_chunks_.rbegin(); it != rend; ++it) {
    if (!(*it)->ReadyForCompaction()) {
      continue;
    }

    auto candidate_size = (*it)->file_size();
    num_candidates_on_disk = candidate_size != 0;
    VLOG_WITH_PREFIX(3) << "Size ratio candidate " << (*it)->ToShortString();

    // Check if the succeeding chunks need compaction.
    auto succeeding_it = it + 1;
    for (; succeeding_it != immutable_chunks_.rend(); ++succeeding_it) {
      if (num_candidates_on_disk >= max_width) {
        VLOG_WITH_PREFIX(3)
            << Format("Size ratio reached max number of candidates ($0)", max_width);
        break;
      }

      if (!(*succeeding_it)->ReadyForCompaction()) {
        VLOG_WITH_PREFIX(3)
            << "Size ratio stopping at candidate not ready for compaction "
            << (*succeeding_it)->ToShortString();
        break;
      }

      const auto succeeding_size = (*succeeding_it)->file_size();

      // It is a valid case when a candidate has no file on disk, e.g. for metadata updates. Also
      // let's pick small chunks without checking the criteria is met.
      if (num_candidates_on_disk && (succeeding_size > always_include_size_threshold)) {
        // Pick files if the total/last candidate chunk size (increased by the specified ratio) is
        // still larger than the next candidate chunk.
        double max_expected_size = candidate_size * (100.0 + ratio) / 100.0;
        if (max_expected_size < static_cast<double>(succeeding_size)) {
          VLOG_WITH_PREFIX(3)
              << "Size ratio stopping at candidate not meet the size criteria "
              << (*succeeding_it)->ToShortString();
          break;
        }
      }

      candidate_size += succeeding_size;
      num_candidates_on_disk += succeeding_size != 0;
      VLOG_WITH_PREFIX(3) << "Size ratio candidate " << (*succeeding_it)->ToShortString();
    }

    // Found a series of consecutive chunks that need compaction.
    if (num_candidates_on_disk >= min_width) {
      candidate_end_idx = std::distance(immutable_chunks_.begin(), it.base());

      // We either reach the end or stop on the chunk that does not meet the criteria.
      candidate_begin_idx = std::distance(immutable_chunks_.begin(), succeeding_it.base());
      break;
    }
  }

  if (candidate_begin_idx == immutable_chunks_.size()) {
    VLOG_WITH_PREFIX(2) << Format("Size ratio not needed, size ratio percent: $0", ratio);
    return {};
  }

  LOG_WITH_PREFIX(INFO) << Format("Size ratio needed, size ratio percent: $0", ratio);
  return PickChunksReadyForCompaction(candidate_begin_idx, candidate_end_idx, "Size ratio");
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::CompactionScope
VectorLSM<Vector, DistanceResult>::PickChunksForCompaction() const {
  SharedLock lock(mutex_);
  if (auto chunks = PickChunksBySizeAmplification(); !chunks.empty()) {
    return chunks;
  }

  if (auto chunks = PickChunksBySizeRatio(); !chunks.empty()) {
    return chunks;
  }

  // TODO(vector_index): Size amplification and size ratio are within configured limits.
  // If max read amplification exceeds configured limits, then force compaction to reduce
  // the number of chunks without looking at file size ratio. Refer to original RocksDB:
  // https://github.com/facebook/rocksdb/blob/main/db/compaction/compaction_picker_universal.cc
  return {};
}

namespace {

// An iterator that merges multiple vector index iterators and filters out entries.
template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class FilteringIterator {
 public:
  using LSM = VectorLSM<Vector, DistanceResult>;
  using VectorIndex = typename LSM::VectorIndex;
  using VectorIndexPtr = typename LSM::VectorIndexPtr;
  using VectorIndexPtrs = std::vector<VectorIndexPtr>;
  using InnerIterator = typename VectorIndex::Iterator;
  using Iterator = typename VectorIndexPtrs::iterator;
  using ValueType = typename VectorIndex::IteratorValue;

  FilteringIterator(Iterator&& begin, Iterator&& end, VectorLSMMergeFilter& filter)
      : filter_(filter),
        outer_it_ (std::move(begin)),
        outer_end_(std::move(end)) {
  }

  FilteringIterator(VectorIndexPtrs& indexes, VectorLSMMergeFilter& filter)
      : FilteringIterator(indexes.begin(), indexes.end(), filter) {
  }

  bool Valid() const {
    return outer_it_ != outer_end_;
  }

  ValueType& operator*() {
    DCHECK(Valid());
    return value_;
  }

  ValueType* operator->() {
    DCHECK(Valid());
    return &value_;
  }

  bool Next() {
    if (!Valid()) {
      return false;
    }

    bool update_inner_iterator = false;
    if (inner_it_.Valid()) {
      DCHECK(inner_it_ != inner_end_);
      ++inner_it_;
    } else {
      // Iterator has been just created, that's a first call to Next().
      update_inner_iterator = true;
    }

    while (outer_it_ != outer_end_) {
      if (update_inner_iterator) {
        inner_it_  = (*outer_it_)->begin();
        inner_end_ = (*outer_it_)->end();
      }

      while (inner_it_ != inner_end_) {
        value_ = *inner_it_;
        if (filter_.Filter(value_.first) == rocksdb::FilterDecision::kKeep) {
          return true;
        }
        ++inner_it_;
      }

      ++outer_it_;
      update_inner_iterator = true;
    }

    return false;
  }

 private:
  VectorLSMMergeFilter& filter_;
  Iterator outer_it_;
  Iterator outer_end_;
  InnerIterator inner_it_ { nullptr };
  InnerIterator inner_end_ { nullptr };
  ValueType value_;
};

void PopulateMergeTasks(auto& tasks, size_t num_vectors_per_task, auto& source_iterator) {
  DCHECK(source_iterator.Valid());
  for (auto tasks_it = tasks.begin(); tasks_it != tasks.end(); ++tasks_it) {
    size_t vectors_in_task = 0;
    while (++vectors_in_task <= num_vectors_per_task) {
      if (!source_iterator.Next()) {
        return;
      }
      tasks_it->Add(source_iterator->first, std::move(source_iterator->second));
    }
  }
}

} // namespace

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class Merger {
 public:
  using LSM = VectorLSM<Vector, DistanceResult>;
  using VectorIndexPtr = typename LSM::VectorIndexPtr;
  using Iterator = FilteringIterator<Vector, DistanceResult>;
  using MergeRegistry = VectorLSMMergeRegistry<Vector, DistanceResult>;

  Merger(LSM& lsm, MergeRegistry& merge_registry)
      : lsm_(lsm), merge_registry_(merge_registry) {
  }

  Status Merge(size_t source_size, Iterator& source_iterator, VectorIndexPtr target_index) {
    if (merge_registry_.MaxCapacity() == 0) {
      return DoMerge(source_iterator, std::move(target_index));
    } else {
      return DoMergeWithThreadPool(source_size, source_iterator, std::move(target_index));
    }
  }

 private:
  const std::string& LogPrefix() const {
    return lsm_.LogPrefix();
  }

  Status DoMerge(Iterator& source_iterator, VectorIndexPtr target_index) {
    // Let's be more conservative and don't check shutdown status on every inserted vector.
    const size_t min_iterations_to_check_shutdown =
        std::min<size_t>(2, 200000 / target_index->Dimensions());
    size_t num_iterations_to_check_shutdown = min_iterations_to_check_shutdown;

    // The only available way to merge vector indexes at the moment is to add all the vectors
    // to a new vector index, filtering outdated vectors out.
    while (source_iterator.Next()) {
      RETURN_NOT_OK(target_index->Insert(source_iterator->first, source_iterator->second));

      if (--num_iterations_to_check_shutdown == 0) {
        RETURN_NOT_OK(lsm_.RUNNING_STATUS());
        num_iterations_to_check_shutdown = min_iterations_to_check_shutdown;
      }
    }

    return Status::OK();
  }

  Status DoMergeWithThreadPool(
      size_t source_size, Iterator& source_iterator, VectorIndexPtr target_index) {
    size_t num_total_tasks = ceil_div<size_t>(source_size, FLAGS_vector_index_task_size);
    size_t num_vectors_per_task = ceil_div(source_size, num_total_tasks);

    size_t num_scheduled_tasks = 0;
    std::atomic<size_t> num_completed_tasks = 0;

    while (source_iterator.Valid()) {
      RETURN_NOT_OK(lsm_.RUNNING_STATUS());

      auto tasks = VERIFY_RESULT(merge_registry_.AllocateTasks(
          num_total_tasks, target_index,
          [&num_completed_tasks](const Status&) {
            // TODO: Handle failure
            num_completed_tasks.fetch_add(1, std::memory_order::relaxed);
          }));

      VLOG_WITH_PREFIX(3) << "Allocated " << tasks.size() << " merge tasks";

      if (tasks.empty()) {
        std::this_thread::sleep_for(200ms);
        continue;
      }

      PopulateMergeTasks(tasks, num_vectors_per_task, source_iterator);

      RETURN_NOT_OK(lsm_.RUNNING_STATUS());

      num_scheduled_tasks += tasks.size();
      merge_registry_.ExecuteTasks(tasks);
    }

    // Wait for everything got merged.
    while (num_scheduled_tasks != num_completed_tasks.load(std::memory_order::relaxed)) {
      std::this_thread::sleep_for(200ms);
    }

    LOG_WITH_PREFIX(INFO) << "Chunks merge done via " << num_scheduled_tasks << " tasks";
    return Status::OK();
  }

  LSM& lsm_;
  MergeRegistry& merge_registry_;
};

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<typename VectorLSM<Vector, DistanceResult>::ImmutableChunkPtr>
VectorLSM<Vector, DistanceResult>::DoCompactChunks(const ImmutableChunkPtrs& input_chunks) {
  // Input chunks collection must be sorted by order_no and each chunk must be in manifest.
  DCHECK(!input_chunks.empty());

  std::vector<VectorIndexPtr> indexes;
  indexes.reserve(input_chunks.size());

  // Collect indexes and frontiers.
  size_t input_size = 0;
  rocksdb::UserFrontiersPtr merged_frontiers;
  for (const auto& chunk : input_chunks) {
    if (chunk->index) {
      indexes.push_back(chunk->index);
      input_size += indexes.back()->Size();
    }
    if (chunk->user_frontiers) {
      rocksdb::UpdateFrontiers(merged_frontiers, *(chunk->user_frontiers));
    }
  }

  LOG_WITH_PREFIX(INFO)
      << "Compaction input [chunks: " << input_chunks.size()
      << ", indexes: " << indexes.size() << ", vectors: " << input_size << "]";

  // Skip index merge section if no existing indexes are found across all input chunks. But
  // even in this case the chunks should be merged.
  VectorLSMFileMetaDataPtr merged_index_file;
  VectorIndexPtr merged_index;
  if (!indexes.empty()) {
    merged_index = VERIFY_RESULT(CreateVectorIndex(input_size));

    RSTATUS_DCHECK(options_.vector_merge_filter_factory,
                   IllegalState, "Vector merge filter factory must be specified");
    auto merge_filter = VERIFY_RESULT(options_.vector_merge_filter_factory());

    FilteringIterator<Vector, DistanceResult> iterator(indexes, *merge_filter);

    Merger<Vector, DistanceResult> merger(*this, *this->merge_registry_);
    RETURN_NOT_OK(merger.Merge(input_size, iterator, merged_index));

    if (TEST_sleep_on_merged_chunk_populated) {
      SleepFor(TEST_sleep_on_merged_chunk_populated);
    }

    // Check shutting down in progress before saving new vector index on disk.
    RETURN_NOT_OK(RUNNING_STATUS());

    LOG_WITH_PREFIX(INFO) << "Chunks merge done [vectors: " << merged_index->Size() << "]";

    // All vectors could be filtered out. Make sure there's data for saving to disk.
    if (!merged_index->Size()) {
      LOG_WITH_PREFIX(INFO) << "Compaction done, no chunk to save";
    } else {
      // Save new index to disk.
      VectorIndexPtr new_index;
      std::tie(merged_index_file, new_index) = VERIFY_RESULT(SaveIndexToFile(
          *merged_index, NextSerialNo()));
      if (new_index) {
        merged_index = new_index;
      }
      LOG_WITH_PREFIX(INFO)
          << "Compaction done, new chunk " << merged_index_file->ToString() << " saved to disk";
    }
  }

  // Create new immutable chunk for further processing. If nothing got merged
  // or merged chunk was empty, it would contain frontiers only.
  return std::make_shared<ImmutableChunk>(
      input_chunks.front()->order_no, std::move(merged_index_file), std::move(merged_index),
      std::move(merged_frontiers), ImmutableChunkState::kOnDisk);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::DoCompact(
    const CompactionContext& context, CompactionScope&& scope) {
  RETURN_NOT_OK(RUNNING_STATUS());
  RSTATUS_DCHECK(!scope.empty(), InvalidArgument, "Compaction scope must be specified");
  VLOG_WITH_PREFIX(2) << "Picked chunks: " << AsString(scope);

  auto merged_chunk = VERIFY_RESULT(DoCompactChunks(scope.chunks()));

  if (metrics_) {
    // TODO(vector_index): include metadata file update in write metrics.
    metrics_->compact_write_bytes->IncrementBy(merged_chunk->file_size());
  }

  // A new chunk must be in a manifested state to put into the immutable chunks collection to
  // keep the data consistence, as the old chunks are in manifested state. This means, manifest
  // file update should be done before in-memory structure update.

  // Lock manifest file for writes to be able to not miss any upcoming chunk.
  AcquireManifest();
  {
    // Allow manifest writes on scope exit -- once it got updated or error happened.
    ScopeExit scope_exit([this]{ ReleaseManifest(); });

    // Prepare manifest file update taking into account specified policy.
    VectorLSMUpdatePB update;
    merged_chunk->AddToUpdate(update);
    if (context.GetManifestUpdateType() == ManifestUpdateType::kActual) {
      scope.AddToUpdate(update);
    } else {
      update.set_reset(true); // Full update.

      SharedLock lock(mutex_);
      for (size_t i = 0; i < immutable_chunks_.size(); ++i) {
        const auto& chunk = immutable_chunks_[i];
        if (!chunk->IsInManifest() || scope.contains(i)) {
          continue;
        }
        chunk->AddToUpdate(update);
      }
    }

    // Update manifest file in accordance with the policy.
    WritableFile* manifest_file = nullptr;
    {
      std::lock_guard lock(mutex_);
      manifest_file = manifest_file_.get();
      if (!manifest_file || context.ForceManifestRoll()) {
        manifest_file = VERIFY_RESULT(RollManifest());
        VLOG_WITH_PREFIX_AND_FUNC(1) << "new manifest " << manifest_file->filename();
      }
    }

    VLOG_WITH_PREFIX_AND_FUNC(3) << update.ShortDebugString();
    RETURN_NOT_OK(VectorLSMMetadataAppendUpdate(*manifest_file, update));
  }

  // Update in-memory structure.
  {
    std::lock_guard lock(mutex_);
    merged_chunk->state = ImmutableChunkState::kInManifest;
    auto compacted_begin = immutable_chunks_.begin() + scope.index();
    auto compacted_end   = compacted_begin + scope.size();
    *compacted_begin = std::move(merged_chunk);
    immutable_chunks_.erase(++compacted_begin, compacted_end);
  }

  // TODO(vector_index): merge the cleanup logic with the same from CreateCheckpoint().
  // Mark input chunks as obsolete and maybe delete corresponding files.
  scope.Compacted();
  TriggerObsoleteChunksCleanup(/* async = */ false);

  LOG_WITH_PREFIX(INFO) << "Vector index compaction done";
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<std::unique_ptr<typename VectorLSM<Vector, DistanceResult>::CompactionTask>>
VectorLSM<Vector, DistanceResult>::RegisterManualCompaction(StdStatusCallback callback) {
  DCHECK(callback) << "Compaction callback must be specified";

  // Check running compactions, only one task for manual compaction is allowed at a time.
  {
    UniqueLock lock(compaction_tasks_mutex_);
    if (has_pending_manual_compaction_ ||
        ContainsTask(compaction_tasks_, CompactionType::kManual)) {
      return STATUS(ServiceUnavailable, "Vector index manual compaction already in progress");
    }
    has_pending_manual_compaction_ = true; // Prevents adding background compaction tasks.

    // Let's wait for ongoing background compactions completion to be able to pick all the
    // chunks for the manual compaction. No other tasks are expected in compaction_tasks_.
    if (!compaction_tasks_.empty()) {
      WaitForCompactionTasksDone(lock);
    }
  }

  // TODO(vector_index): do we need to trigger sync flush?
  // TODO(vector_index): do we need to wait for updates_queue_ is empty to grab more chunk
  // for a manual compaction, even without explicit flush being triggered here?

  // Pick files for manual compaction -- it's required to do while has_pending_manual_compaction_
  // is set, otherwise background compaction won't allow to pick all the manifested chunks.
  auto task = std::make_unique<CompactionTask>(
      *this, CompactionType::kManual, PickChunksForFullCompaction(), std::move(callback));
  Register(*task);

  // Release pending manual compaction state.
  {
    std::lock_guard lock(compaction_tasks_mutex_);
    has_pending_manual_compaction_ = false;
  }

  return std::move(task);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::ScheduleBackgroundCompaction() {
  if (!FLAGS_vector_index_enable_compactions) {
    VLOG_WITH_PREFIX(2) << "Background compactions disabled by gflag";
    return;
  }

  if (!auto_compactions_enabled_) {
    VLOG_WITH_PREFIX(2) << "Background compactions disabled";
    return;
  }

  if (!RUNNING_STATUS().ok()) {
    return;
  }

  // TODO(vector_index): this logic should be replaced with compaction_score calculation with
  // triggering on the following conditions: 1) immutable chunk's state becomes kInManifest, 2)
  // a chunk is being deleted from immutable_chunks_ collection. The following formula should be
  // used: score = [num chunks in manifest] / file_number_compaction_trigger. And the compaction
  // should be triggered if score >= 1.
  size_t num_manifested_chunks_on_disk = 0;
  {
    SharedLock lock(mutex_);

    num_manifested_chunks_on_disk = std::ranges::count_if(
        immutable_chunks_,
        [](auto&& chunk) { return chunk->ReadyForCompaction() && chunk->file; });
  }
  if (num_manifested_chunks_on_disk < FilesNumberCompactionTrigger()) {
    VLOG_WITH_PREFIX(2) << Format(
        "Skipping background compaction due to the number of manifested chunks "
        "on disk ($0) is less than the value of the compaction trigger ($1)",
        num_manifested_chunks_on_disk, FilesNumberCompactionTrigger());
    return;
  }

  // Check there's no running background compactions.
  CompactionTaskPtr task;
  {
    std::lock_guard lock(compaction_tasks_mutex_);
    if (has_pending_manual_compaction_ ||
        ContainsTask(compaction_tasks_, CompactionType::kBackground)) {
      VLOG_WITH_PREFIX(2) << "Skipping background compaction due to another compaction is running";
      return;
    }

    // TODO(vector_index): None of approaches for picking chunks is checked at this point, which
    // leads to a possibility of unnecessary compaction attempts by creating and submitting
    // a CompactionTask which will do nothing. Alternative way is to try to pick chunks for
    // compaction and in case of success create and submit the task -- but it is better to not
    // setup picked chunks for the compaction task letting it pick large scope on execution.
    task = std::make_unique<CompactionTask>(*this);
    VLOG_WITH_PREFIX(2) << "Created background compaction task: " << task->ToString();
    RegisterUnlocked(*task);
  }

  // TODO(vector_index): figure out what to do with bad status.
  [[maybe_unused]] auto status = SubmitTask(std::move(task));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::ScheduleManualCompaction(StdStatusCallback callback) {
  LOG_WITH_PREFIX(INFO) << "Vector index manual compaction requested";

  RETURN_NOT_OK(RUNNING_STATUS());

  auto task = VERIFY_RESULT(RegisterManualCompaction(std::move(callback)));
  if (!task) {
    return STATUS(IllegalState, "Failed to register manual compaction");
  }

  return SubmitTask(std::move(task));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::EnableAutoCompactions() {
  auto_compactions_enabled_ = true;
  LOG_WITH_PREFIX(INFO) << "Background compactions enabled";
  ScheduleBackgroundCompaction();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::Compact(bool wait) {
  if (!wait) {
    return ScheduleManualCompaction([this](const auto& status) {
      // TODO(vector_index): think on how to report failed async compactions.
      LOG_IF_WITH_PREFIX(ERROR, !status.ok()) << "Manual compaction failed: " << status;
    });
  }

  std::promise<Status> compaction_done;
  auto scheduled = ScheduleManualCompaction([&compaction_done](const Status& status){
    compaction_done.set_value(status);
  });
  if (!scheduled.ok()) {
    compaction_done.set_value(scheduled);
  }
  return compaction_done.get_future().get();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::WaitForCompaction() {
  // Wait for manual compaction task is completed. Returns immediately if there's no ongoing
  // manual compaction task.
  UniqueLock lock(compaction_tasks_mutex_);
  compaction_tasks_cv_.wait(
      lock,
      [this]() NO_THREAD_SAFETY_ANALYSIS {
        return !ContainsTask(compaction_tasks_, CompactionType::kManual);
      });

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::RegisterUnlocked(CompactionTask& task) {
  // Sanity check.
  DCHECK(!compaction_tasks_.contains(&task));

  compaction_tasks_.insert(&task);
  compaction_tasks_cv_.notify_all();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::Register(CompactionTask& task) {
  std::lock_guard lock(compaction_tasks_mutex_);
  return RegisterUnlocked(task);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::Deregister(CompactionTask& task) {
  std::lock_guard lock(compaction_tasks_mutex_);

  // Sanity check.
  DCHECK(compaction_tasks_.contains(&task));

  compaction_tasks_.erase(&task);
  compaction_tasks_cv_.notify_all();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::SubmitTask(CompactionTaskPtr task) {
  // TODO(vector-index): specify disk_group_no during submitting.
  auto submitted = options_.compaction_token->Submit(&task);
  if (!submitted.ok()) {
    LOG_WITH_PREFIX(ERROR) << "Failed to submit task " << task->ToString()
                           << ", status: " << submitted;
    Deregister(*task);
  }

  return submitted;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
template<typename Lock>
void VectorLSM<Vector, DistanceResult>::WaitForCompactionTasksDone(Lock& lock) {
  DCHECK_EQ(lock.mutex(), &compaction_tasks_mutex_);
  compaction_tasks_cv_.wait(
      lock, [this]() NO_THREAD_SAFETY_ANALYSIS { return compaction_tasks_.empty(); });
}

YB_INSTANTIATE_TEMPLATE_FOR_ALL_VECTOR_AND_DISTANCE_RESULT_TYPES(VectorLSM);

template void MergeChunkResults<float>(
    std::vector<VectorWithDistance<float>>& combined_results,
    std::vector<VectorWithDistance<float>>& chunk_results,
    size_t max_num_results);

}  // namespace yb::vector_index
