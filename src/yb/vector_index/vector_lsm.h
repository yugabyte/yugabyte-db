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

#include <future>
#include <map>

#include "yb/rocksdb/rocksdb_fwd.h"
#include "yb/rocksdb/metadata.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/kv_util.h"
#include "yb/util/locks.h"

#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

template<ValidDistanceResultType DistanceResult>
struct VectorLSMSearchEntry {
  DistanceResult distance;
  // base_table_key could be the encoded DocKey of the corresponding row in the base
  // (indexed) table, and the hybrid time of the vector insertion.
  KeyBuffer base_table_key;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        distance, (base_table_key, base_table_key.AsSlice().ToDebugHexString()));
  }
};

struct VectorLSMSearchOptions {
  size_t max_num_results;
};

template<IndexableVectorType Vector>
struct VectorLSMInsertEntry {
  VectorId  vertex_id;
  KeyBuffer base_table_key;
  Vector    vector;
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
  using SearchResults = std::vector<VectorLSMSearchEntry<DistanceResult>>;
  using InsertEntry = VectorLSMInsertEntry<Vector>;
  using InsertEntries = std::vector<InsertEntry>;
  using Options = VectorLSMOptions<Vector, DistanceResult>;
  using InsertRegistry = VectorLSMInsertRegistry<Vector, DistanceResult>;
  using SearchOptions = VectorLSMSearchOptions;
  using VertexWithDistance = vector_index::VertexWithDistance<DistanceResult>;
};

using BaseTableKeysBatch = std::vector<std::pair<VectorId, Slice>>;

struct VectorLSMInsertContext {
  const rocksdb::UserFrontiers* frontiers = nullptr;
};

class VectorLSMKeyValueStorage {
 public:
  virtual Status StoreBaseTableKeys(
      const BaseTableKeysBatch& batch, const VectorLSMInsertContext& context) = 0;

  virtual Result<KeyBuffer> ReadBaseTableKey(VectorId vertex_id) = 0;

  virtual ~VectorLSMKeyValueStorage() = default;
};

template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
struct VectorLSMOptions {
  using Types = VectorLSMTypes<Vector, DistanceResult>;
  std::string storage_dir;
  typename Types::VectorIndexFactory vector_index_factory;
  size_t points_per_chunk;
  VectorLSMKeyValueStorage* key_value_storage;
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
  using SearchOptions = typename Types::SearchOptions;

  VectorLSM();
  ~VectorLSM();

  Status Open(Options options);

  rocksdb::UserFrontierPtr GetFlushedFrontier();

  Status Insert(std::vector<InsertEntry> entries, const VectorLSMInsertContext& context);

  Result<SearchResults> Search(const Vector& query_vector, const SearchOptions& options) const;

  Status Flush();
  void StartShutdown();
  void CompleteShutdown();

  size_t TEST_num_immutable_chunks() const;
  bool TEST_HasBackgroundInserts() const;

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const;

  struct MutableChunk;
  struct ImmutableChunk;
  using ImmutableChunkPtr = std::shared_ptr<ImmutableChunk>;

 private:
  friend class VectorLSMInsertTask<Vector, DistanceResult>;
  friend struct MutableChunk;

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

  Status CreateNewMutableChunk(size_t min_points) REQUIRES(mutex_);

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

  bool writing_update_ GUARDED_BY(mutex_) = false;
  Status failed_status_ GUARDED_BY(mutex_);
};

template <template<class, class> class Factory, class VectorIndex>
using MakeVectorIndexFactory =
    Factory<typename VectorIndex::Vector, typename VectorIndex::DistanceResult>;

template<ValidDistanceResultType DistanceResult>
void MergeChunkResults(
    std::vector<VertexWithDistance<DistanceResult>>& combined_results,
    std::vector<VertexWithDistance<DistanceResult>>& chunk_results,
    size_t max_num_results);

}  // namespace yb::vector_index
