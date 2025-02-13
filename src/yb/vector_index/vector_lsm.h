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

#include "yb/util/kv_util.h"
#include "yb/util/locks.h"

#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

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

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
struct VectorLSMTypes {
  using VectorIndex = VectorIndexIf<Vector, DistanceResult>;
  using VectorIndexPtr = VectorIndexIfPtr<Vector, DistanceResult>;
  using VectorIndexFactory = vector_index::VectorIndexFactory<Vector, DistanceResult>;
  using SearchResults = typename VectorIndex::SearchResult;
  using InsertEntry = VectorLSMInsertEntry<Vector>;
  using InsertEntries = std::vector<InsertEntry>;
  using Options = VectorLSMOptions<Vector, DistanceResult>;
  using InsertRegistry = VectorLSMInsertRegistry<Vector, DistanceResult>;
  using VectorWithDistance = vector_index::VectorWithDistance<DistanceResult>;
};

struct VectorLSMInsertContext {
  const rocksdb::UserFrontiers* frontiers = nullptr;
};

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
struct VectorLSMOptions {
  using Types = VectorLSMTypes<Vector, DistanceResult>;

  std::string log_prefix;
  std::string storage_dir;
  typename Types::VectorIndexFactory vector_index_factory;
  size_t points_per_chunk;
  rpc::ThreadPool* thread_pool;
  std::function<rocksdb::UserFrontiersPtr()> frontiers_factory;
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
  using Types = VectorLSMTypes<Vector, DistanceResult>;
  using VectorIndex = typename Types::VectorIndex;
  using VectorIndexPtr = typename Types::VectorIndexPtr;
  using VectorIndexFactory = typename Types::VectorIndexFactory;
  using SearchResults = typename Types::SearchResults;
  using InsertEntry = typename Types::InsertEntry;
  using InsertEntries = typename Types::InsertEntries;
  using Options = typename Types::Options;
  using InsertRegistry = typename Types::InsertRegistry;

  VectorLSM();
  ~VectorLSM();

  Status Open(Options options);
  Status CreateCheckpoint(const std::string& out);

  rocksdb::UserFrontierPtr GetFlushedFrontier();
  rocksdb::FlushAbility GetFlushAbility();

  Status Insert(std::vector<InsertEntry> entries, const VectorLSMInsertContext& context);

  Result<SearchResults> Search(
      const Vector& query_vector, const SearchOptions& options) const;

  Status Flush(bool wait);
  Status WaitForFlush();

  void StartShutdown();
  void CompleteShutdown();

  size_t TEST_num_immutable_chunks() const;
  bool TEST_HasBackgroundInserts() const;

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const;

  const Options& options() const;

  struct MutableChunk;
  struct ImmutableChunk;
  using ImmutableChunkPtr = std::shared_ptr<ImmutableChunk>;

 private:
  friend class VectorLSMInsertTask<Vector, DistanceResult>;
  friend struct MutableChunk;

  const std::string& LogPrefix() const {
    return options_.log_prefix;
  }

  // Saves the current mutable chunk to disk and creates a new one.
  Status RollChunk(size_t min_points) REQUIRES(mutex_);
  Status DoFlush(std::promise<Status>* promise) REQUIRES(mutex_);

  // Use var arg to avoid specifying arguments twice in SaveChunk and DoSaveChunk.
  void SaveChunk(const ImmutableChunkPtr& chunk) EXCLUDES(mutex_);
  void CheckFailure(const Status& status) EXCLUDES(mutex_);

  // Actual implementation for SaveChunk, to have ability simply return Status in case of failure.
  Status DoSaveChunk(const ImmutableChunkPtr& chunk) EXCLUDES(mutex_);
  Status UpdateManifest(
      rocksdb::WritableFile* metadata_file, ImmutableChunkPtr chunk) EXCLUDES(mutex_);

  Status CreateNewMutableChunk(size_t min_vectors) REQUIRES(mutex_);

  Status RemoveUpdateQueueEntry(size_t order_no) REQUIRES(mutex_);

  Options options_;
  rocksdb::Env* const env_;

  mutable rw_spinlock mutex_;
  size_t current_chunk_serial_no_ GUARDED_BY(mutex_) = 0;
  std::shared_ptr<MutableChunk> mutable_chunk_ GUARDED_BY(mutex_);
  std::vector<ImmutableChunkPtr> immutable_chunks_ GUARDED_BY(mutex_);
  std::unique_ptr<InsertRegistry> insert_registry_;
  // Does not change after Open.
  size_t metadata_file_no_ = 0;
  std::unique_ptr<rocksdb::WritableFile> metadata_file_ GUARDED_BY(mutex_);
  bool stopping_ GUARDED_BY(mutex_) = false;

  // order_no is used as key in this map.
  std::map<size_t, ImmutableChunkPtr> updates_queue_ GUARDED_BY(mutex_);
  std::condition_variable_any updates_queue_empty_;

  bool writing_update_ GUARDED_BY(mutex_) = false;
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
