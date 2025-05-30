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

#include <queue>

#include <boost/range/iterator_range.hpp>

#include "yb/hnsw/types.h"

#include "yb/rocksdb/util/heap.h"

#include "yb/util/misaligned_ptr.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_util.h"

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
  vector_index::VectorId GetVectorData(size_t vector);
  const std::byte* CoordinatesPtr(size_t vector);

 private:
  Slice GetVectorDataSlice(size_t vector);
  const std::byte* BlockPtr(
      size_t block, size_t entries_per_block, size_t entry, size_t entry_size);

  const Header* header_ = nullptr;
  FileBlockCache* file_block_cache_ = nullptr;
  std::vector<const std::byte*> blocks_;
  std::vector<size_t> used_blocks_;
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
};

class YbHnsw {
 public:
  using CoordinateType = float;
  using DistanceType = HnswDistanceType;
  using Metric = unum::usearch::metric_punned_t;
  using SearchResult = std::vector<vector_index::VectorWithDistance<DistanceType>>;

  explicit YbHnsw(Metric& metric);
  ~YbHnsw();

  // Imports specified index to YbHnsw structure, also storing this structure to disk.
  Status Import(
    const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path,
    BlockCachePtr block_cache);

  // Initialize YbHnsw from specified file, using block_cache to cache blocks.
  Status Init(const std::string& path, BlockCachePtr block_cache);

  SearchResult Search(
      const std::byte* query_vector, size_t max_results, const vector_index::VectorFilter& filter,
      YbHnswSearchContext& context) const;

  SearchResult Search(
      const CoordinateType* query_vector, size_t max_results,
      const vector_index::VectorFilter& filter, YbHnswSearchContext& context) const {
    return Search(pointer_cast<const std::byte*>(query_vector), max_results, filter, context);
  }

 private:
  std::pair<VectorNo, DistanceType> SearchInNonBaseLayers(
      const std::byte* query_vector, SearchCache& cache) const;
  void SearchInBaseLayer(
      const std::byte* query_vector, VectorNo best_vector, DistanceType best_dist,
      size_t max_results, const vector_index::VectorFilter& filter,
      YbHnswSearchContext& context) const;
  SearchResult MakeResult(size_t max_results, YbHnswSearchContext& context) const;

  DistanceType Distance(const std::byte* lhs, const std::byte* rhs) const;
  DistanceType Distance(const std::byte* lhs, size_t vector, SearchCache& cache) const;
  boost::iterator_range<MisalignedPtr<const CoordinateType>> MakeCoordinates(
      const std::byte* ptr) const;
  boost::iterator_range<MisalignedPtr<const CoordinateType>> Coordinates(
      size_t vector, SearchCache& cache) const;

  Metric& metric_;
  Header header_;
  BlockCachePtr block_cache_;
  FileBlockCachePtr file_block_cache_;
};

}  // namespace yb::hnsw
