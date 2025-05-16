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

DEFINE_RUNTIME_uint64(vector_index_task_pool_size, 1000,
    "Pool size of insert subtasks for vector index. "
    "Pool is just used to avoid memory allocations and does not limit the total "
    "number of tasks.");

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

DEFINE_RUNTIME_bool(vector_index_dump_stats, false,
    "Whether to dump stats related to vector index search.");

DEFINE_RUNTIME_bool(vector_index_disable_compactions, true,
    "Disable Vector LSM backgorund compactions.");

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
YB_DEFINE_ENUM(CompactionType, (kBackground)(kManual));
YB_DEFINE_ENUM(ManifestUpdateType, (kFull)(kActual));

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

int32_t CompactionPriorityStartBound() {
  const auto start_bound = FLAGS_vector_index_compaction_priority_start_bound;
  return start_bound >= 0 ? start_bound : FLAGS_compaction_priority_start_bound;
}

int32_t CompactionPriorityStepSize() {
  const auto step_size = FLAGS_vector_index_compaction_priority_step_size;
  return step_size >= 0 ? step_size : FLAGS_compaction_priority_step_size;
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
  explicit VectorLSMFileMetaData(size_t serial_no) : serial_no_(serial_no) {}

  size_t serial_no() const {
    return serial_no_;
  }

  bool IsObsolete() const {
    return obsolete_.load(std::memory_order::acquire);
  }

  void MarkObsolete() {
    obsolete_.store(true, std::memory_order::release);
  }

  std::string ToString() const {
    return YB_CLASS_TO_STRING(serial_no);
  }

 private:
  const size_t serial_no_;
  std::atomic<bool> obsolete_ = { false };
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertTask :
    public rpc::ThreadPoolTask,
    public bi::list_base_hook<bi::link_mode<bi::normal_link>> {
 public:
  using LSM = VectorLSM<Vector, DistanceResult>;
  using InsertRegistry = typename LSM::InsertRegistry;
  using VectorWithDistance = typename LSM::VectorWithDistance;
  using SearchHeap = std::priority_queue<VectorWithDistance>;
  using MutableChunkPtr = typename LSM::MutableChunkPtr;

  explicit VectorLSMInsertTask(LSM& lsm) : lsm_(lsm) {}

  void Bind(const MutableChunkPtr& chunk) {
    DCHECK(!chunk_);
    DCHECK_ONLY_NOTNULL(chunk->index);
    chunk_ = chunk;
  }

  void Add(VectorId vector_id, Vector&& vector) {
    vectors_.emplace_back(vector_id, std::move(vector));
  }

  void Run() override {
    DCHECK(chunk_.get());
    DCHECK(chunk_->index.get());
    for (const auto& [vector_id, vector] : vectors_) {
      lsm_.CheckFailure(chunk_->index->Insert(vector_id, vector));
    }
    auto new_tasks = --chunk_->num_tasks;
    if (new_tasks == 0) {
      DCHECK(chunk_->save_callback);
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
  MutableChunkPtr chunk_;
  std::vector<std::pair<VectorId, Vector>> vectors_;
};

// Registry for all active Vector LSM insert subtasks.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSMInsertRegistry {
 public:
  using LSM = VectorLSM<Vector, DistanceResult>;
  using VectorIndex = typename LSM::VectorIndex;
  using VectorWithDistance = typename LSM::VectorWithDistance;
  using InsertTask = VectorLSMInsertTask<Vector, DistanceResult>;
  using InsertTaskList = boost::intrusive::list<InsertTask>;
  using InsertTaskPtr = std::unique_ptr<InsertTask>;
  using MutableChunkPtr = typename VectorLSM<Vector, DistanceResult>::MutableChunkPtr;

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
      LSM& lsm, const MutableChunkPtr& chunk, size_t num_tasks) EXCLUDES(mutex_) {
    InsertTaskList result;
    {
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

  // An access to this variable is synchronized by num_tasks: it is guaranteed that the variable
  // is set and it is safe to trigger save_callback only when num_tasks < kRunningMark.
  std::function<void()> save_callback;

  rocksdb::UserFrontiersPtr user_frontiers;

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

  void Insert(VectorLSM& lsm, VectorId vector_id, const Vector& vector) {
    lsm.CheckFailure(index->Insert(vector_id, vector));
  }

  ImmutableChunkPtr Immutate(size_t order_no, std::promise<Status>* flush_promise) {
    // Move should not be used for index as the mutable chunk could still be used by other
    // entities, for exampe by VectorLSMInsertTask.
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
  // Chunk serial no, used in file name and never changes.
  VectorLSMFileMetaDataPtr file;

  // In memory order for chunk. All chunks in immutable chunks are ordered using it.
  // Could be changed between application runs.
  size_t order_no = 0;

  ImmutableChunkState state = ImmutableChunkState::kInMemory;
  VectorIndexPtr index;
  rocksdb::UserFrontiersPtr user_frontiers;

 private:
  std::promise<Status>* flush_promise = nullptr;
  std::atomic<bool> under_compaction = { false };

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

  size_t serial_no() const {
    return file ? file->serial_no() : 0;
  }

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
    DCHECK(!IsUnderCompaction());
    if (file) {
      file->MarkObsolete();
    }
  }

  bool IsUnderCompaction() const {
    return under_compaction.load(std::memory_order::acquire);
  }

  bool TryLockForCompaction() {
    return TrySetUnderCompation(true);
  }

  void UnlockForCompaction() {
    [[maybe_unused]] bool success = TrySetUnderCompation(false);
    DCHECK(success);
  }

  void Compacted() {
    UnlockForCompaction();
    MarkObsolete();
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        order_no, state, file, user_frontiers,
        (under_compaction, std::to_string(IsUnderCompaction())));
  }

 private:
  bool TrySetUnderCompation(bool value) {
    return under_compaction.exchange(value) != value;
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
    if (chunk->state != ImmutableChunkState::kInManifest || !chunk->TryLockForCompaction()) {
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
      chunk->UnlockForCompaction();
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
      DCHECK_ONLY_NOTNULL(chunk->file.get());
      update.add_remove_chunks(chunk->file->serial_no());
    }
  }

  std::string ToString() const {
    static auto chunks_formatter = [](const auto& chunk) {
      return Format("{order_no: $0, serial_no: $1}", chunk->order_no, chunk->serial_no());
    };
    return YB_CLASS_TO_STRING(index, (chunks, AsString(chunks_, chunks_formatter)));
  }

 private:
  // Index of the front chunk from chunks in the LSM's immutable chunks collection.
  size_t index_ = 0;

  // Continuous interval of chunks from LSM's immutable chunks collection.
  ImmutableChunkPtrs chunks_;

  // Exclusive upper bound.
  size_t end_index() const {
    return index_ + size();
  }
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
class VectorLSM<Vector, DistanceResult>::CompactionTask : public PriorityThreadPoolTask {
  using LSM = VectorLSM<Vector, DistanceResult>;

 public:
  CompactionTask(
      LSM& lsm,
      CompactionType compaction_type = CompactionType::kBackground,
      CompactionScope&& compaction_scope = {},
      StdStatusCallback&& callback = {})
      : lsm_(lsm),
        log_prefix_(Format("$0[TASK $1] ", lsm.LogPrefix(), SerialNo())),
        compaction_type_(compaction_type),
        compaction_scope_(std::move(compaction_scope)),
        callback_(std::move(callback)),
        priority_(CalculatePriority()) {
  }

  CompactionType compaction_type() const {
    return compaction_type_;
  }

  int Priority() const {
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
      LOG_WITH_PREFIX(INFO) << "not ready: " << ready;
      return Completed(ready);
    }

    // TODO(vector_index): leverage the suspender.
    auto status = DoRun();
    LOG_WITH_PREFIX(INFO) << "done: " << status;
    Completed(status);
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

    // TODO(vector_index): maybe should not account immutable chunks which are not yet manifested.
    const int num_chunks = narrow_cast<int>(lsm_.num_immutable_chunks());
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

    // Maybe schedule pending background compaction.
    lsm_.ScheduleBackgroundCompaction();
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
  {
    SharedLock lock(mutex_);
    if (!stopping_) {
      return Status::OK();
    }
  }

  auto status = Status(Status::kShutdownInProgress,
                       file_name, line_number, "Vector LSM is shutting down");
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
  std::lock_guard lock(mutex_);
  stopping_ = true;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void VectorLSM<Vector, DistanceResult>::CompleteShutdown() {
  LOG_IF_WITH_PREFIX(DFATAL, RUNNING_STATUS().ok()) << "Vector LSM is not shutting down";

  if (!insert_registry_) {
    return; // Was not opened.
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

  // Remove not started compaction tasks from the thread pool.
  options_.compaction_thread_pool->Remove(this);

  // Prioritize remaining compaction tasks and wait for compactions to be completed.
  {
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
  insert_registry_ = std::make_unique<InsertRegistry>(
      options_.log_prefix, *options_.insert_thread_pool);

  RETURN_NOT_OK(env_->CreateDirs(options_.storage_dir));
  auto load_result = VERIFY_RESULT(VectorLSMMetadataLoad(env_, options_.storage_dir));
  next_manifest_file_no_ = load_result.next_free_file_no;
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
    VectorLSMFileMetaDataPtr file;
    VectorIndexPtr index;
    if (chunk_pb->serial_no()) {
      index = options_.vector_index_factory();
      file  = CreateVectorLSMFileMetaData(*index, chunk_pb->serial_no());
    }

    auto user_frontiers = options_.frontiers_factory();
    RETURN_NOT_OK(user_frontiers->Smallest().FromPB(chunk_pb->smallest().user_frontier()));
    RETURN_NOT_OK(user_frontiers->Largest().FromPB(chunk_pb->largest().user_frontier()));
    immutable_chunks_.push_back(std::make_shared<ImmutableChunk>(
        chunk_pb->order_no(), std::move(file), std::move(index),
        std::move(user_frontiers), ImmutableChunkState::kInManifest
    ));

    last_serial_no_ = std::max<size_t>(last_serial_no_, chunk_pb->serial_no());
  }

  CountDownLatch latch(immutable_chunks_.size());
  for (size_t i = 0; i != immutable_chunks_.size(); ++i) {
    auto& chunk = immutable_chunks_[i];
    if (!chunk->file) {
      latch.CountDown();
      continue;
    }
    options_.insert_thread_pool->EnqueueFunctor([this, &latch, &chunk] {
      CheckFailure(chunk->index->LoadFromFile(
          GetChunkPath(options_.storage_dir, chunk->file->serial_no()),
          MaxConcurrentReads()));
      latch.CountDown();
    });
  }
  latch.Wait();

  std::sort(
      immutable_chunks_.begin(), immutable_chunks_.end(), [](const auto& lhs, const auto& rhs) {
    return lhs->order_no < rhs->order_no;
  });
  LOG_WITH_PREFIX(INFO) << "Loaded " << immutable_chunks_.size() << " chunks, "
                        << "last serial no: " << last_serial_no_ << ", "
                        << "next manifest no: " << next_manifest_file_no_;
  VLOG_WITH_PREFIX(2)   << "Loaded " << AsString(immutable_chunks_);

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
      if (chunk->state == ImmutableChunkState::kInManifest) {
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
          GetChunkPath(options_.storage_dir, serial_no),
          GetChunkPath(out, serial_no)));
    }
    chunk->AddToUpdate(update);
  }

  auto manifest_file = VERIFY_RESULT(VectorLSMMetadataOpenFile(env_, out, 0));
  RETURN_NOT_OK(VectorLSMMetadataAppendUpdate(*manifest_file, update));

  // TODO(vector_index): merge the cleanup logic with the same from DoCompactChunks().
  // Some chunks could become obsolete, let's explicitely unreference them and trigger obsolete
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

  auto tasks = insert_registry_->AllocateTasks(*this, chunk, num_tasks);
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

  auto start_registry_search = MonoTime::NowIf(dump_stats);
  auto intermediate_results = insert_registry_->Search(query_vector, options);
  VLOG_WITH_PREFIX_AND_FUNC(4) << "Results from registry: " << AsString(intermediate_results);
  size_t num_results_from_insert_registry = intermediate_results.size();

  size_t sum_num_found_entries = 0;
  auto start_chunks_search = MonoTime::NowIf(dump_stats);
  for (const auto& index : indexes) {
    auto chunk_results = VERIFY_RESULT(index->Search(query_vector, options));
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Chunk results: " << AsString(chunk_results);
    sum_num_found_entries += chunk_results.size();
    MergeChunkResults(intermediate_results, chunk_results, options.max_num_results);
  }
  auto stop_search = MonoTime::NowIf(dump_stats);

  LOG_IF_WITH_PREFIX_AND_FUNC(INFO, dump_stats)
      << "VI_STATS: Number of chunks: " << indexes.size() << ", entries found in all chunks: "
      << sum_num_found_entries << ", entries found in insert registry: "
      << num_results_from_insert_registry << ", time to search insert registry: "
      << (start_chunks_search - start_registry_search).ToPrettyString()
      << ", time to search index chunks: " << (stop_search - start_chunks_search).ToPrettyString();

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
VectorLSMFileMetaDataPtr VectorLSM<Vector, DistanceResult>::CreateVectorLSMFileMetaData(
    VectorIndex& index, size_t serial_no) {
  auto* raw_ptr = new VectorLSMFileMetaData(serial_no);
  auto ptr = VectorLSMFileMetaDataPtr(raw_ptr, [this](VectorLSMFileMetaData* raw_ptr) {
    std::unique_ptr<VectorLSMFileMetaData> ptr { raw_ptr };
    if (!ptr->IsObsolete()) {
      return; // Non-obsolete files should be kept on disk, just relase the memory.
    }

    ObsoleteFile(std::move(ptr));
  });

  index.Attach(ptr);
  return ptr;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::NextSerialNo() {
  std::lock_guard lock(mutex_);
  return ++last_serial_no_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::TEST_SkipManifestUpdateDuringShutdown() {
  LOG_WITH_PREFIX_AND_FUNC(INFO) << "Hit test condition";
  for (auto it = updates_queue_.begin(); it != updates_queue_.end();) {
    if (it->second->state == ImmutableChunkState::kOnDisk) {
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
Status VectorLSM<Vector, DistanceResult>::DoSaveChunk(const ImmutableChunkPtr& chunk) {
  VLOG_WITH_PREFIX_AND_FUNC(3) << AsString(*chunk);

  if (chunk->index) {
    LOG_IF(DFATAL, chunk->file.get())
        << "Chunk is already saved to "
        << GetChunkPath(options_.storage_dir, chunk->file->serial_no());

    const auto serial_no = NextSerialNo();
    if (serial_no == 1 && FLAGS_TEST_vector_index_delay_saving_first_chunk_ms != 0) {
      std::this_thread::sleep_for(FLAGS_TEST_vector_index_delay_saving_first_chunk_ms * 1ms);
    }

    chunk->file = CreateVectorLSMFileMetaData(*chunk->index, serial_no);
    const auto chunk_path = GetChunkPath(options_.storage_dir, serial_no);
    RETURN_NOT_OK(chunk->index->SaveToFile(chunk_path));
  }

  WritableFile* manifest_file = nullptr;
  ImmutableChunkPtr writing_chunk;
  {
    std::lock_guard lock(mutex_);
    chunk->state = ImmutableChunkState::kOnDisk;
    if (stopping_ && FLAGS_TEST_vector_index_skip_manifest_update_during_shutdown) {
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
    // or `updates_queue_.begin()->second->state == ImmutableChunkState::kOnDisk`.
    // to identify if we need to capture writing_manifest_.
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Updates queue: " << AsString(updates_queue_);
    for (;;) {
      if (it->second->state != ImmutableChunkState::kOnDisk) {
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

  return UpdateManifest(manifest_file, std::move(writing_chunk));
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::UpdateManifest(
    WritableFile* manifest_file, ImmutableChunkPtr chunk) {
  DCHECK_ONLY_NOTNULL(chunk.get());
  DCHECK_EQ(chunk->state, ImmutableChunkState::kOnDisk);

  while (chunk) {
    VectorLSMUpdatePB update;
    chunk->AddToUpdate(update);
    VLOG_WITH_PREFIX_AND_FUNC(3) << update.ShortDebugString();
    RETURN_NOT_OK(VectorLSMMetadataAppendUpdate(*manifest_file, update));

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

      if (updates_queue_.size() &&
          updates_queue_.begin()->second->state == ImmutableChunkState::kOnDisk) {
        chunk = updates_queue_.begin()->second;
      } else {
        chunk = nullptr;
        ReleaseManifestUnlocked();
      }
    }

    ScheduleBackgroundCompaction();
  }

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
  {
    std::lock_guard lock(mutex_);
    if (failed_status_.ok()) {
      failed_status_ = status;
    }
  }
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
    options_.insert_thread_pool->EnqueueFunctor(mutable_chunk_->save_callback);
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
Result<typename VectorLSM<Vector, DistanceResult>::VectorIndexPtr>
VectorLSM<Vector, DistanceResult>::CreateVectorIndex(size_t min_vectors) const {
  auto capacity = std::max(min_vectors, options_.vectors_per_chunk);
  VLOG_WITH_PREFIX_AND_FUNC(1) << "requested capacity: " << capacity;

  auto index = options_.vector_index_factory();
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
    VLOG_WITH_PREFIX_AND_FUNC(1) << "Flushing " << mutable_chunk_->num_entries << " entries";
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
size_t VectorLSM<Vector, DistanceResult>::num_immutable_chunks() const {
  SharedLock lock(mutex_);
  return immutable_chunks_.size();
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
bool VectorLSM<Vector, DistanceResult>::TEST_ObsoleteFilesCleanupInProgress() const {
  return obsolete_files_cleanup_in_progress_;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::TEST_NextManifestFileNo() const {
  SharedLock lock(mutex_);
  return next_manifest_file_no_;
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

  auto path = GetChunkPath(options_.storage_dir, file.serial_no());
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
  scope.reserve(num_immutable_chunks());

  // It is expected the collection of immutable chunks is sorted by order_no, so it is safe
  // to start from the very first item and grab a continuous interaval of manifested chunks.
  {
    SharedLock lock(mutex_);
    for (size_t i = 0; i < immutable_chunks_.size(); ++i) {
      if (!scope.TryLock(i, immutable_chunks_[i])) {
        break;
      }
    }
  }

  return scope;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::CompactionScope
VectorLSM<Vector, DistanceResult>::PickChunksForCompaction() const {
  // TODO(vector_index): to be implemented by https://github.com/yugabyte/yugabyte-db/issues/27089.
  return {};
}

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<typename VectorLSM<Vector, DistanceResult>::ImmutableChunkPtr>
VectorLSM<Vector, DistanceResult>::DoCompactChunks(const ImmutableChunkPtrs& input_chunks) {
  // Input chunks collection must be sorted by order_no and each chunk's state must be set
  // to ImmutableChunkState::kInManifest.
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
    auto merge_filter = options_.vector_merge_filter_factory();

    // Let's be more conservative and don't check shutdown status on every inserted vector.
    const size_t min_iterations_to_check_shutdown =
        std::min<size_t>(2, 200000 / merged_index->Dimensions());
    size_t num_iterations_to_check_shutdown = min_iterations_to_check_shutdown;

    // The only available way to merge vector indexes at the moment is to add all the vectors
    // to a new vector index, filtering outdated vectors out.
    for (const auto& index : indexes) {
      for (const auto& [vector_id, vector] : *index) {
        if (merge_filter->Filter(vector_id) == rocksdb::FilterDecision::kKeep) {
          RETURN_NOT_OK(merged_index->Insert(vector_id, vector));
        }

        if (--num_iterations_to_check_shutdown == 0) {
          RETURN_NOT_OK(RUNNING_STATUS());
          num_iterations_to_check_shutdown = min_iterations_to_check_shutdown;
        }
      }
    }

    if (TEST_sleep_on_merged_chunk_populated) {
      SleepFor(TEST_sleep_on_merged_chunk_populated);
      num_iterations_to_check_shutdown = 1; // To force checking shutting down status.
    }

    // Check shutting down in progress before saving new vector index on disk.
    if (num_iterations_to_check_shutdown) {
      RETURN_NOT_OK(RUNNING_STATUS());
    }

    LOG_WITH_PREFIX(INFO) << "Chunks merge done [vectors: " << merged_index->Size() << "]";

    // Save new index to disk.
    merged_index_file = CreateVectorLSMFileMetaData(*merged_index, NextSerialNo());
    const auto chunk_path = GetChunkPath(options_.storage_dir, merged_index_file->serial_no());
    RETURN_NOT_OK(merged_index->SaveToFile(chunk_path));
    LOG_WITH_PREFIX(INFO)
        << "Compaction done, new chunk " << merged_index_file->ToString() << " saved to disk";
  }

  // Create new immutable chunk for further processing.
  return std::make_shared<ImmutableChunk>(
      input_chunks.front()->order_no, std::move(merged_index_file), std::move(merged_index),
      std::move(merged_frontiers), ImmutableChunkState::kOnDisk);
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status VectorLSM<Vector, DistanceResult>::DoCompact(
    const CompactionContext& context, CompactionScope&& scope) {
  RETURN_NOT_OK(RUNNING_STATUS());
  RSTATUS_DCHECK(!scope.empty(), InvalidArgument, "Compaction scope must be specified");
  LOG_WITH_PREFIX(INFO) << "Picked chunks: " << AsString(scope);

  auto merged_chunk = VERIFY_RESULT(DoCompactChunks(scope.chunks()));

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
        if (chunk->state != ImmutableChunkState::kInManifest || scope.contains(i)) {
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

  // Check running compacitons, only one task for manual compaction is allowed at a time.
  {
    UniqueLock lock(compaction_tasks_mutex_);
    if (has_pending_manual_compaction_ ||
        ContainsTask(compaction_tasks_, CompactionType::kManual)) {
      return STATUS(ServiceUnavailable, "Vector index manual compaction already in progress");
    }
    has_pending_manual_compaction_ = true; // Prevents adding background compaction tasks.

    // Let's wait for ongoning background compactions completion to be able to pick all the
    // chunks for the manual compaction. No other tasks are expected in compaction_tasks_.
    if (!compaction_tasks_.empty()) {
      WaitForCompactionTasksDone(lock);
    }
  }

  // TODO(vector_index): do we need to trigger sync flush?

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
  if (FLAGS_vector_index_disable_compactions) {
    VLOG_WITH_PREFIX(2) << "Background compactions disabled";
    return;
  }

  // TODO(vector_index): this logic should be replaced with compaction_score calculation with
  // triggerring on the following conditions: 1) immutable chunk's state becomes kInManifest, 2)
  // a chunk is being deleted from immutable_chunks_ collection. The following formula should be
  // used: score = [num chunks in manifest] / file_number_compaction_trigger. And the compaction
  // should be triggered if score >= 1.
  size_t num_manifested_chunks = 0;
  {
    SharedLock lock(mutex_);
    num_manifested_chunks = std::ranges::count_if(
        immutable_chunks_,
        [](auto&& chunk){ return chunk->state == ImmutableChunkState::kInManifest; });
  }
  if (num_manifested_chunks < FilesNumberCompactionTrigger()) {
    VLOG_WITH_PREFIX(2)
        << "Skipping background compaction as number of manifested files < compaction trigger "
        << "(" << num_manifested_chunks << " < " << FilesNumberCompactionTrigger() << ")";
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

    task = std::make_unique<CompactionTask>(*this);
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
  auto submitted = options_.compaction_thread_pool->Submit(task->Priority(), &task);
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
  compaction_tasks_cv_.wait(
      lock, [this]() NO_THREAD_SAFETY_ANALYSIS { return compaction_tasks_.empty(); });
}

YB_INSTANTIATE_TEMPLATE_FOR_ALL_VECTOR_AND_DISTANCE_RESULT_TYPES(VectorLSM);

template void MergeChunkResults<float>(
    std::vector<VectorWithDistance<float>>& combined_results,
    std::vector<VectorWithDistance<float>>& chunk_results,
    size_t max_num_results);

}  // namespace yb::vector_index
