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

#include <functional>
#include <queue>

#include <boost/range/iterator_range.hpp>

#include "yb/hnsw/types.h"

#include "yb/rocksdb/util/heap.h"

#include "yb/util/misaligned_ptr.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_util.h"
#include "yb/vector_index/hnswlib_include.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"

namespace yb {

class Env;
class RandomAccessFile;

} // namespace yb

namespace yb::hnsw {

struct YbHnswVectorData;

// Provides access to a raw bytes data for a single search.
// Could be reused between searches using Bind/Release method.
class SearchCache {
 public:
  const std::byte* Data(size_t index);

  void Bind(std::reference_wrapper<const Header> header, FileBlockCache& cache);
  void Release();

  boost::iterator_range<MisalignedPtr<const VectorNo>> GetNeighborsInNonBaseLayer(
    size_t level, size_t vector);
  MisalignedPtr<const YbHnswVectorData> VectorHeader(size_t vector);
  boost::iterator_range<MisalignedPtr<const VectorNo>> GetNeighborsInBaseLayer(
      size_t vector);
  // Returns the vector id and the payload attached to the vector, decoded from the same aux
  // data slice. The payload is an empty slice when there is no payload.
  std::pair<vector_index::VectorId, Slice> GetVectorIdAndPayload(size_t vector);
  const std::byte* CoordinatesPtr(size_t vector);

  // Prefetches the blocks_ slot that VectorHeader(vector) will load. The slot index is pure
  // arithmetic on the vector id, so it can be issued ahead of time, while the data-dependent
  // load of the slot itself is something the CPU cannot speculate through.
  void PrefetchVectorHeaderBlock(size_t vector);

 private:
  Slice GetVectorDataSlice(size_t vector);
  const std::byte* BlockPtr(
      size_t block, size_t entries_per_block, size_t entry, size_t entry_size);

  const Header* header_ = nullptr;
  FileBlockCache* file_block_cache_ = nullptr;
  std::vector<const std::byte*> blocks_;
  std::vector<size_t> used_blocks_;

  // Block cache query/hit counts for the current search, flushed to the metrics in Release().
  size_t takes_ = 0;
  size_t hits_ = 0;
};

class SearchCacheScope {
 public:
  SearchCacheScope(SearchCache& cache, const YbHnsw& hnsw);

  ~SearchCacheScope() {
    cache_.Release();
  }

  SearchCache* operator->() const {
    return &cache_;
  }

  SearchCacheScope(const SearchCacheScope&) = delete;
  void operator=(const SearchCacheScope&) = delete;
 private:
  SearchCache& cache_;
};

struct YbHnswSearchContext {
  using HeapEntry = std::pair<HnswDistanceType, VectorNo>;

  struct HeapEntryCmp {
    bool operator()(const HeapEntry& lhs, const HeapEntry& rhs) const {
      return lhs.first > rhs.first;
    }
  };

  using VisitedSet = unum::usearch::growing_hash_set_gt<VectorNo>;
  using NextQueue = rocksdb::BinaryHeap<HeapEntry, HeapEntryCmp>;
  using Top = rocksdb::BinaryHeap<HeapEntry>;
  using ExtraTop = rocksdb::BinaryHeap<HnswDistanceType>;

  VisitedSet visited;
  Top top;
  ExtraTop extra_top;
  NextQueue next;
  SearchCache search_cache;

  // Query narrowed to the file's storage encoding. Grown once per pooled context and reused;
  // unused when the file stores float32.
  std::vector<std::byte> narrowed_query;

  // Neighbours of the current node that passed the visited filter, with their record addresses
  // already resolved. Reused across calls; bounded by the neighbour count, i.e. by
  // config.connectivity_base.
  std::vector<std::pair<VectorNo, const std::byte*>> unvisited;
};

class YbHnswMetric {
 public:
  virtual ~YbHnswMetric() = default;
  virtual HnswDistanceType Distance(const std::byte* lhs, const std::byte* rhs) = 0;
};

class UsearchMetric : public YbHnswMetric {
 public:
  using Impl = unum::usearch::metric_punned_t;

  explicit UsearchMetric(const Impl& metric) : impl_(metric) {}

  HnswDistanceType Distance(const std::byte* lhs, const std::byte* rhs) override;

 private:
  unum::usearch::metric_punned_t impl_;
};

class YbHnsw {
 public:
  using CoordinateType = float;
  using DistanceType = HnswDistanceType;
  using Metric = YbHnswMetric;
  using MetricPtr = std::unique_ptr<Metric>;
  using SearchResult = std::vector<vector_index::VectorWithDistance<DistanceType>>;

  // Builds the metric for a dimension count and coordinate encoding. The metric decodes stored
  // records, so it must agree with how they were written: Init() passes the encoding from the
  // file's own footer, never caller-supplied configuration, which can drift from what the chunk
  // actually contains.
  using MetricFactory =
      std::function<MetricPtr(size_t dimensions, vector_index::VectorStorageKind storage_kind)>;

  // Convenience constructor for a fixed float32 metric.
  YbHnsw(const UsearchMetric::Impl& metric, BlockCachePtr block_cache);

  YbHnsw(MetricFactory metric_factory, BlockCachePtr block_cache);
  ~YbHnsw();

  // Imports specified index to YbHnsw structure, also storing this structure to disk.
  // payloads provides payloads attached to vectors, stored in the aux data.
  // It is null when the chunk does not store payloads.
  Status Import(
    const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path,
    const vector_index::VectorPayloadMap* payloads);
  Status Import(
    const HnswlibIndex<DistanceType>& index, const std::string& path,
    const vector_index::VectorPayloadMap* payloads,
    vector_index::VectorStorageKind storage_kind = vector_index::VectorStorageKind::kFloat32);

  // Initialize YbHnsw from specified file, using block_cache to cache blocks.
  Status Init(const std::string& path);

  // Searches with a query already in this file's storage encoding.
  SearchResult Search(
      const std::byte* query_vector, const vector_index::SearchOptions& options,
      YbHnswSearchContext& context) const;

  // Narrows the query through the same conversion the stored records went through, then
  // delegates to the overload above.
  SearchResult Search(
      const CoordinateType* query_vector, const vector_index::SearchOptions& options,
      YbHnswSearchContext& context) const;

  // Distance between two records in this file's storage encoding. Full-precision callers must
  // narrow first -- see NarrowCoordinates.
  DistanceType Distance(const std::byte* lhs, const std::byte* rhs) const;

  const Header& header() const;

 private:
  friend class SearchCacheScope;

  std::pair<VectorNo, DistanceType> SearchInNonBaseLayers(
      const std::byte* query_vector, SearchCache& cache) const;
  void SearchInBaseLayer(
      const std::byte* query_vector, VectorNo best_vector, DistanceType best_dist,
      const vector_index::SearchOptions& options, YbHnswSearchContext& context) const;
  SearchResult MakeResult(size_t max_results, YbHnswSearchContext& context) const;

  DistanceType Distance(const std::byte* lhs, size_t vector, SearchCache& cache) const;

  // Builds metric_ from header_, which must already be populated.
  void InitMetrics();

  const MetricFactory metric_factory_;
  const BlockCachePtr block_cache_;
  MetricPtr metric_;

  Header header_;
  FileBlockCachePtr file_block_cache_;
};

}  // namespace yb::hnsw
