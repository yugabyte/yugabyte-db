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

#pragma once

#include <condition_variable>
#include <future>
#include <map>

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/rocksdb_fwd.h"
#include "yb/rocksdb/metadata.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/env.h"
#include "yb/util/kv_util.h"
#include "yb/util/locks.h"

#include "yb/util/status_callback.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

class VectorLSMFileMetaData;
using VectorLSMFileMetaDataPtr = std::shared_ptr<VectorLSMFileMetaData>;

template<IndexableVectorType Vector>
struct VectorLSMInsertEntry {
  VectorId vector_id;
  Vector   vector;
};

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
struct VectorLSMOptions;

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
class VectorLSMInsertRegistry;

struct VectorLSMInsertContext {
  const rocksdb::UserFrontiers* frontiers = nullptr;
};

class VectorLSMMergeFilter {
 public:
  virtual ~VectorLSMMergeFilter() = default;
  virtual rocksdb::FilterDecision Filter(VectorId vector_id) = 0;
};
using VectorLSMMergeFilterPtr = std::unique_ptr<VectorLSMMergeFilter>;

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
struct VectorLSMOptions {
  using VectorIndexFactory = vector_index::VectorIndexFactory<Vector, DistanceResult>;
  using MergeFilterFactory = std::function<VectorLSMMergeFilterPtr()>;
  using FrontiersFactory   = std::function<rocksdb::UserFrontiersPtr()>;

  std::string log_prefix;
  std::string storage_dir;
  VectorIndexFactory vector_index_factory;
  size_t vectors_per_chunk;
  rpc::ThreadPool* thread_pool;
  rpc::ThreadPool* insert_thread_pool;
  PriorityThreadPool* compaction_thread_pool;
  FrontiersFactory frontiers_factory;
  MergeFilterFactory vector_merge_filter_factory;
};

template<IndexableVectorType VectorType,
         ValidDistanceResultType DistanceResultType>
class VectorLSMInsertTask;

template<IndexableVectorType VectorType,
         ValidDistanceResultType DistanceResultType>
class VectorLSM {
 public:
  using DistanceResult = DistanceResultType;
  using Vector = VectorType;
  using VectorWithDistance = vector_index::VectorWithDistance<DistanceResult>;
  using Options = VectorLSMOptions<Vector, DistanceResult>;
  using VectorIndex = VectorIndexIf<Vector, DistanceResult>;
  using VectorIndexPtr = VectorIndexIfPtr<Vector, DistanceResult>;
  using SearchResults = typename VectorIndex::SearchResult;
  using InsertEntry = VectorLSMInsertEntry<Vector>;
  using InsertEntries = std::vector<InsertEntry>;
  using InsertRegistry = VectorLSMInsertRegistry<Vector, DistanceResult>;

  VectorLSM();
  ~VectorLSM();

  Status Open(Options options);
  Status Destroy();
  Status CreateCheckpoint(const std::string& out);

  rocksdb::UserFrontierPtr GetFlushedFrontier();
  rocksdb::FlushAbility GetFlushAbility();

  Status Insert(std::vector<InsertEntry> entries, const VectorLSMInsertContext& context);

  Result<SearchResults> Search(const Vector& query_vector, const SearchOptions& options) const;

  Result<bool> HasVectorId(const vector_index::VectorId& vector_id) const;

  Status Flush(bool wait);
  Status WaitForFlush();

  // Force chunks compaction. Flush does not happen.
  Status Compact(bool wait = false);

  void StartShutdown();
  void CompleteShutdown();
  bool IsShuttingDown() const;

  size_t NumImmutableChunks() const EXCLUDES(mutex_);
  size_t NumSavedImmutableChunks() const EXCLUDES(mutex_);

  Env* TEST_GetEnv() const;
  bool TEST_HasBackgroundInserts() const;
  bool TEST_HasCompactions() const EXCLUDES(mutex_);
  bool TEST_ObsoleteFilesCleanupInProgress() const;
  size_t TEST_NextManifestFileNo() const EXCLUDES(mutex_);

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const;

  const Options& options() const;

  struct MutableChunk;
  using  MutableChunkPtr = std::shared_ptr<MutableChunk>;

 private:
  struct ImmutableChunk;
  using  ImmutableChunkPtr  = std::shared_ptr<ImmutableChunk>;
  using  ImmutableChunkPtrs = std::vector<ImmutableChunkPtr>;

  class  CompactionScope;
  struct CompactionContext;
  class  CompactionTask;
  using  CompactionTaskPtr = std::unique_ptr<CompactionTask>;

  friend class  VectorLSMInsertTask<Vector, DistanceResult>;
  friend struct MutableChunk;

  const std::string& LogPrefix() const {
    return options_.log_prefix;
  }

  // Utility method to correctly prepare Status instance in case of shutting down.
  Status DoCheckRunning(const char* file_name, int line_number) const EXCLUDES(mutex_);

  // Saves the current mutable chunk to disk and creates a new one.
  Status RollChunk(size_t min_vectors) REQUIRES(mutex_);
  Status DoFlush(std::promise<Status>* promise) REQUIRES(mutex_);

  // Use var arg to avoid specifying arguments twice in SaveChunk and DoSaveChunk.
  void SaveChunk(const ImmutableChunkPtr& chunk) EXCLUDES(mutex_);
  void CheckFailure(const Status& status) EXCLUDES(mutex_);

  // Actual implementation for SaveChunk, to have ability simply return Status in case of failure.
  Status DoSaveChunk(const ImmutableChunkPtr& chunk) EXCLUDES(mutex_);

  Result<VectorLSMFileMetaDataPtr> SaveIndexToFile(VectorIndex& index, uint64_t serial_no);

  // The argument `chunk` must be the very first chunk from `updates_queue_`.
  Status UpdateManifest(WritableFile* manifest_file, ImmutableChunkPtr chunk) EXCLUDES(mutex_);

  void AcquireManifest() EXCLUDES(mutex_);
  void ReleaseManifest() EXCLUDES(mutex_);
  void ReleaseManifestUnlocked() REQUIRES(mutex_);
  Result<WritableFile*> RollManifest() REQUIRES(mutex_);

  Result<uint64_t> GetChunkFileSize(uint64_t serial_no) const;

  // Creates vector index and reserve at least for `min_vectors` entries.
  Result<VectorIndexPtr> CreateVectorIndex(size_t min_vectors) const;

  Status CreateNewMutableChunk(size_t min_vectors) REQUIRES(mutex_);

  Result<std::vector<VectorIndexPtr>> AllIndexes() const EXCLUDES(mutex_);

  // Creates new file metadata for the vector index file and attaches to the one.
  VectorLSMFileMetaDataPtr CreateVectorLSMFileMetaData(
      VectorIndex& index, uint64_t serial_no, uint64_t size_on_disk);

  uint64_t NextSerialNo() EXCLUDES(mutex_);
  uint64_t LastSerialNo() const EXCLUDES(mutex_);

  void DoDeleteObsoleteChunks() EXCLUDES(cleanup_mutex_);
  void DeleteObsoleteChunks() EXCLUDES(cleanup_mutex_);
  void DeleteFile(const VectorLSMFileMetaData& file);
  void ObsoleteFile(std::unique_ptr<VectorLSMFileMetaData>&& file) EXCLUDES(cleanup_mutex_);
  void TriggerObsoleteChunksCleanup(bool async);

  // Returns compaction scope with a continuos subset of immutable chunks, which consists of
  // first N manifested chunks starting from the very first one (chunk N+1 is not manifested).
  // The flushes and the current manifest updates are not stopped, which means other newer chunks
  // could become manifested while the full compaction is happening, which means it is not allowed
  // to keep iterators to the selected range as they could become invalidated.
  CompactionScope PickChunksForFullCompaction() const EXCLUDES(mutex_);

  // Return the scope for [begin_idx, end_idx), the chunks must be ready for the compaction.
  CompactionScope PickChunksReadyForCompaction(
      size_t begin_idx, size_t end_idx, const std::string& reason) const REQUIRES_SHARED(mutex_);

  // Looks at overall size amplification. If size amplification exceeds the configured value, then
  // does a compaction on the longest span of candidate chunks  ending at the earliest chunk.
  CompactionScope PickChunksBySizeAmplification() const REQUIRES_SHARED(mutex_);
  CompactionScope PickChunksForCompaction() const EXCLUDES(mutex_);

  // Returns new chunk - a product of input chunks compaction; the new chunk is saved to a disk.
  Result<ImmutableChunkPtr> DoCompactChunks(const ImmutableChunkPtrs& input_chunks);

  Status DoCompact(const CompactionContext& context, CompactionScope&& scope) EXCLUDES(mutex_);

  void ScheduleBackgroundCompaction() EXCLUDES(mutex_);

  // Creates compaction task and tries to submit it to the thread pool. Triggres callback only if
  // compation task has been successfully submitted.
  Status ScheduleManualCompaction(StdStatusCallback callback) EXCLUDES(mutex_);

  Result<CompactionTaskPtr> RegisterManualCompaction(StdStatusCallback callback) EXCLUDES(mutex_);

  void Deregister(CompactionTask& task) EXCLUDES(compaction_tasks_mutex_);
  void Register(CompactionTask& task) EXCLUDES(compaction_tasks_mutex_);
  void RegisterUnlocked(CompactionTask& task) REQUIRES(compaction_tasks_mutex_);

  // Requirement: taks must be registered.
  Status SubmitTask(CompactionTaskPtr task);

  template<typename Lock>
  void WaitForCompactionTasksDone(Lock& lock) REQUIRES(compaction_tasks_mutex_);

  Status TEST_SkipManifestUpdateDuringShutdown() REQUIRES(mutex_);

  Options options_;
  Env* const env_;

  mutable rw_spinlock mutex_;
  uint64_t last_serial_no_ GUARDED_BY(mutex_) = 0;
  std::shared_ptr<MutableChunk> mutable_chunk_ GUARDED_BY(mutex_);

  // Immutable chunks are soreted by order_no and this order must be kept in case of collection
  // modifications (e.g. due to merging of chunks).
  ImmutableChunkPtrs immutable_chunks_ GUARDED_BY(mutex_);

  std::unique_ptr<InsertRegistry> insert_registry_;

  // May be changed if new manifest file is created (due to absence or compaction).
  size_t next_manifest_file_no_ = 0;
  std::unique_ptr<WritableFile> manifest_file_ GUARDED_BY(mutex_);
  // TODO(vector_index): maybe replace writing_manifest_ with a mutex-like object.
  bool writing_manifest_ GUARDED_BY(mutex_) = false;
  std::condition_variable_any writing_manifest_done_cv_;

  bool stopping_ GUARDED_BY(mutex_) = false;

  // The map contains only chunks being saved, i.e. chunks in kInMemory and kOnDisk states -- this
  // invariant must be kept. The value of order_no is used as key in this map.
  std::map<size_t, ImmutableChunkPtr> updates_queue_ GUARDED_BY(mutex_);
  std::condition_variable_any updates_queue_empty_cv_;

  mutable rw_spinlock compaction_tasks_mutex_;
  std::condition_variable_any compaction_tasks_cv_;
  std::unordered_set<CompactionTask*> compaction_tasks_ GUARDED_BY(compaction_tasks_mutex_);

  // Used to inform background compactions that there's a manual compaction task which is
  // waiting for background
  bool has_pending_manual_compaction_ GUARDED_BY(compaction_tasks_mutex_) = false;

  // Currently this mutex is used only in DeleteObsoleteChunks, which are not allowed to run in
  // parallel, hence it is enough to use simple spin lock.
  simple_spinlock cleanup_mutex_;
  std::vector<std::unique_ptr<VectorLSMFileMetaData>> obsolete_files_ GUARDED_BY(cleanup_mutex_);
  std::atomic<bool> obsolete_files_cleanup_in_progress_ = false;

  Status failed_status_ GUARDED_BY(mutex_);
};

template<template<class, class> class Factory, class VectorIndex>
using MakeVectorIndexFactory =
    Factory<typename VectorIndex::Vector, typename VectorIndex::DistanceResult>;

template<ValidDistanceResultType DistanceResult>
void MergeChunkResults(
    std::vector<VectorWithDistance<DistanceResult>>& combined_results,
    std::vector<VectorWithDistance<DistanceResult>>& chunk_results,
    size_t max_num_results);

}  // namespace yb::vector_index
