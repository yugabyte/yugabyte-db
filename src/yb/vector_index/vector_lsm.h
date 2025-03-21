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

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
struct VectorLSMOptions {
  using VectorIndexFactory = vector_index::VectorIndexFactory<Vector, DistanceResult>;
  using VectorIndexPtr     = vector_index::VectorIndexIfPtr<Vector, DistanceResult>;
  using VectorIndexPtrs    = std::vector<VectorIndexPtr>;
  using VectorIndexMerger  = std::function<Status(VectorIndexPtr&, const VectorIndexPtrs&)>;

  using FrontiersFactory = std::function<rocksdb::UserFrontiersPtr()>;

  std::string log_prefix;
  std::string storage_dir;
  VectorIndexFactory vector_index_factory;
  size_t vectors_per_chunk;
  rpc::ThreadPool* thread_pool;
  FrontiersFactory frontiers_factory;
  VectorIndexMerger vector_index_merger;
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

  size_t num_immutable_chunks() const;

  Env* TEST_GetEnv() const;
  bool TEST_HasBackgroundInserts() const;
  bool TEST_ObsoleteFilesCleanupInProgress() const;
  size_t TEST_NextManifestFileNo() const;

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const;

  const Options& options() const;

  struct MutableChunk;
  using  MutableChunkPtr = std::shared_ptr<MutableChunk>;

  struct ImmutableChunk;
  using  ImmutableChunkPtr  = std::shared_ptr<ImmutableChunk>;
  using  ImmutableChunkPtrs = std::vector<ImmutableChunkPtr>;

 private:
  friend class VectorLSMInsertTask<Vector, DistanceResult>;
  friend struct MutableChunk;

  const std::string& LogPrefix() const {
    return options_.log_prefix;
  }

  // Saves the current mutable chunk to disk and creates a new one.
  Status RollChunk(size_t min_vectors) REQUIRES(mutex_);
  Status DoFlush(std::promise<Status>* promise) REQUIRES(mutex_);

  // Use var arg to avoid specifying arguments twice in SaveChunk and DoSaveChunk.
  void SaveChunk(const ImmutableChunkPtr& chunk) EXCLUDES(mutex_);
  void CheckFailure(const Status& status) EXCLUDES(mutex_);

  // Actual implementation for SaveChunk, to have ability simply return Status in case of failure.
  Status DoSaveChunk(const ImmutableChunkPtr& chunk) EXCLUDES(mutex_);

  // The argument `chunk` must be the very first chunk from `updates_queue_`.
  Status UpdateManifest(WritableFile* manifest_file, ImmutableChunkPtr chunk) EXCLUDES(mutex_);

  // Creates vector index and reserve at least for `min_vectors` entries.
  Result<VectorIndexPtr> CreateVectorIndex(size_t min_vectors) const;

  Status CreateNewMutableChunk(size_t min_vectors) REQUIRES(mutex_);

  Status RemoveUpdateQueueEntry(size_t order_no) REQUIRES(mutex_);

  Result<std::vector<VectorIndexPtr>> AllIndexes() const EXCLUDES(mutex_);

  // Creates new file metadata for the vector index file and attaches to the one.
  VectorLSMFileMetaDataPtr CreateVectorLSMFileMetaData(VectorIndex& index, size_t serial_no);

  size_t NextSerialNo() EXCLUDES(mutex_);

  void DoDeleteObsoleteChunks() EXCLUDES(cleanup_mutex_);
  void DeleteObsoleteChunks() EXCLUDES(cleanup_mutex_);
  void DeleteFile(const VectorLSMFileMetaData& file);
  void ObsoleteFile(std::unique_ptr<VectorLSMFileMetaData>&& file) EXCLUDES(cleanup_mutex_);
  void ScheduleObsoleteChunksCleanup();

  // Updates compaction scope with a continuos subset of immutable chunks, which consists of
  // first N manifested chunks starting from the very first one (chunk N+1 is not manifested).
  // The flushes and the current manifest updates are not stopped, which means other newer chunks
  // could become manifested while the full compaction is happening, which means it is not allowed
  // to keep iterators to the selected range as they could become invalidated.
  ImmutableChunkPtrs PickChunksForFullCompaction() const EXCLUDES(mutex_);

  // Returns new chunk - a product of input chunks compaction; the new chunk is saved to a disk.
  Result<ImmutableChunkPtr> DoCompaction(const ImmutableChunkPtrs& input_chunks);

  Status DoFullCompaction() EXCLUDES(mutex_);
  Status ScheduleFullCompaction(StdStatusCallback callback = {});

  Status TEST_SkipManifestUpdateDuringShutdown() REQUIRES(mutex_);

  Options options_;
  Env* const env_;

  mutable rw_spinlock mutex_;
  size_t last_serial_no_ GUARDED_BY(mutex_) = 0;
  std::shared_ptr<MutableChunk> mutable_chunk_ GUARDED_BY(mutex_);

  // Immutable chunks are soreted by order_no and this order must be kept in case of collection
  // modifications (e.g. due to merging of chunks).
  ImmutableChunkPtrs immutable_chunks_ GUARDED_BY(mutex_);

  std::unique_ptr<InsertRegistry> insert_registry_;

  // May be changed if new manifest file is created (due to absence or compaction).
  size_t next_manifest_file_no_ = 0;
  std::unique_ptr<WritableFile> manifest_file_ GUARDED_BY(mutex_);
  bool writing_manifest_ GUARDED_BY(mutex_) = false;
  std::condition_variable_any writing_manifest_done_;

  std::atomic<bool> full_compaction_in_progress_ = false;

  bool stopping_ GUARDED_BY(mutex_) = false;

  // The map contains only chunks being saved, i.e. chunks in kInMemory and kOnDisk states -- this
  // invariant must be kept. The value of order_no is used as key in this map.
  std::map<size_t, ImmutableChunkPtr> updates_queue_ GUARDED_BY(mutex_);
  std::condition_variable_any updates_queue_empty_;

  // Currently this mutex is used only in DeleteObsoleteChunks, which are not allowed to run in
  // parallel, hence it is enough to use simple spin lock.
  simple_spinlock cleanup_mutex_;
  std::vector<std::unique_ptr<VectorLSMFileMetaData>> obsolete_files_;
  std::atomic<bool> obsolete_files_cleanup_in_progress_ = false;

  Status failed_status_ GUARDED_BY(mutex_);
};

template <template<class, class> class Factory, class VectorIndex>
using MakeVectorIndexFactory =
    Factory<typename VectorIndex::Vector, typename VectorIndex::DistanceResult>;

template<ValidDistanceResultType DistanceResult>
void MergeChunkResults(
    std::vector<VectorWithDistance<DistanceResult>>& combined_results,
    std::vector<VectorWithDistance<DistanceResult>>& chunk_results,
    size_t max_num_results);

}  // namespace yb::vector_index
