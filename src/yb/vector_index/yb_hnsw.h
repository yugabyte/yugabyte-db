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

#include "yb/rocksdb/util/heap.h"

#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_util.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"

namespace yb::vector_index {

using VectorIndex = uint32_t;

struct YbHnswVectorData;

struct YbHnswLayerInfo {
  size_t size = 0;
  size_t block = 0;
  size_t last_block_index = 0;
  size_t last_block_vectors_amount = 0;

  std::string ToString() const;
};

struct YbHnswHeader {
  size_t dimensions;
  size_t vector_data_size;
  VectorIndex entry;
  size_t max_level;
  unum::usearch::index_dense_config_t config;
  size_t max_block_size;
  size_t max_vectors_per_non_base_block;

  size_t vector_data_block;
  size_t vector_data_amount_per_block;
  std::vector<YbHnswLayerInfo> layers;

  void Init(const unum::usearch::index_dense_gt<VectorId>& index);
};

using YbHnswDataBlock = std::vector<std::byte>;
using YbHnswEndian = LittleEndian;
using YbHnswDistanceType = float;

class YbHnswStorage {
 public:
  using Metric = unum::usearch::metric_punned_t;
  using DistanceType = YbHnswDistanceType;
  using CoordinateType = float;

 protected:
  void Take(YbHnswStorage&& source) {
    *this = std::move(source);
  }

  YbHnswHeader header_;
  std::vector<YbHnswDataBlock> blocks_;

 private:
  YbHnswStorage& operator=(YbHnswStorage&&) = default;
};

struct YbHnswSearchContext {
  using HeapEntry = std::pair<YbHnswDistanceType, VectorIndex>;

  struct HeapEntryCmp {
    bool operator()(const HeapEntry& lhs, const HeapEntry& rhs) const {
      return lhs.first > rhs.first;
    }
  };

  using VisitedSet = unum::usearch::growing_hash_set_gt<VectorIndex>;
  using NextQueue = rocksdb::BinaryHeap<HeapEntry, HeapEntryCmp>;
  using Top = rocksdb::BinaryHeap<HeapEntry>;
  using ExtraTop = rocksdb::BinaryHeap<YbHnswDistanceType>;

  VisitedSet visited;
  Top top;
  ExtraTop extra_top;
  NextQueue next;
};

class YbHnsw : public YbHnswStorage {
 public:
  using VectorData = YbHnswVectorData;
  using SearchResult = std::vector<VectorWithDistance<YbHnsw::DistanceType>>;

  explicit YbHnsw(Metric& metric) : metric_(metric) {}

  void Import(const unum::usearch::index_dense_gt<VectorId>& index);

  SearchResult Search(
      const std::byte* query_vector, size_t max_results, const VectorFilter& filter,
      YbHnswSearchContext& context) const;

  SearchResult Search(
      const CoordinateType* query_vector, size_t max_results, const VectorFilter& filter,
      YbHnswSearchContext& context) const {
    return Search(pointer_cast<const std::byte*>(query_vector), max_results, filter, context);
  }

 private:
  std::pair<VectorIndex, DistanceType> SearchInNonBaseLayers(const std::byte* query_vector) const;
  void SearchInBaseLayer(
      const std::byte* query_vector, VectorIndex best_vector, DistanceType best_dist,
      size_t max_results, const VectorFilter& filter, YbHnswSearchContext& context) const;
  SearchResult MakeResult(size_t max_results, YbHnswSearchContext& context) const;

  boost::iterator_range<MisalignedPtr<const VectorIndex>> GetNeighborsInNonBaseLayer(
      size_t level, size_t vector) const;

  boost::iterator_range<MisalignedPtr<const VectorIndex>> GetNeighborsInBaseLayer(
      size_t vector) const;

  const std::byte* BlockPtr(
      size_t block, size_t entries_per_block, size_t entry, size_t entry_size) const;

  Slice GetVectorDataSlice(size_t vector) const;
  VectorId GetVectorData(size_t vector) const;
  DistanceType Distance(const std::byte* lhs, const std::byte* rhs) const;
  DistanceType Distance(const std::byte* lhs, size_t vector) const;
  MisalignedPtr<const VectorData> VectorHeader(size_t vector) const;
  const std::byte* CoordinatesPtr(size_t vector) const;
  boost::iterator_range<MisalignedPtr<const CoordinateType>> MakeCoordinates(
      const std::byte* ptr) const;
  boost::iterator_range<MisalignedPtr<const CoordinateType>> Coordinates(size_t vector) const;

  Metric& metric_;
};

}  // namespace yb::vector_index
