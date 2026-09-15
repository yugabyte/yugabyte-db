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

#include "yb/hnsw/hnsw_fwd.h"

#include "yb/util/logging.h"
#include "yb/util/tostring.h"

#include "yb/vector_index/coordinate_codec.h"
#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/vector_storage_kind.h"

namespace hnswlib {

template <typename dist_t, typename label_t>
class HierarchicalNSW;

}

namespace unum::usearch {

struct index_dense_config_t;
template <typename, typename> class index_dense_gt;

}

namespace yb::hnsw {

using VectorNo = uint32_t;
using UsearchIndexDense = unum::usearch::index_dense_gt<vector_index::VectorId, uint32_t>;

template <class DistanceResult>
using HnswlibIndex = hnswlib::HierarchicalNSW<DistanceResult, vector_index::VectorId>;

struct Config {
  Config() = default;
  explicit Config(const unum::usearch::index_dense_config_t& input);

  uint64_t connectivity_base = 0;
  uint64_t connectivity = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(connectivity_base, connectivity);
  }
};

struct LayerInfo {
  size_t size = 0;
  size_t block = 0;
  size_t last_block_index = 0;
  size_t last_block_vectors_amount = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(size, block, last_block_index, last_block_vectors_amount);
  }
};

class YbHnswIndexAdapter;

struct Header {
  size_t dimensions;
  size_t vector_data_size;
  VectorNo entry;
  size_t max_level;
  Config config;
  size_t max_block_size;
  size_t max_vectors_per_non_base_block;

  size_t vector_data_block;
  size_t vector_data_amount_per_block;
  std::vector<LayerInfo> layers;

  // Coordinate encoding the graph traversal computes distances on, stored first in each vector
  // data record. Absent from version-1 footers, which are always float32.
  vector_index::VectorStorageKind storage_kind = vector_index::VectorStorageKind::kFloat32;

  // Encoding of the rerank copy, stored right after the traversal coordinates in the same
  // record. kNone means no second copy and traversal distances are returned as-is. Absent
  // from footers before version 3.
  vector_index::RerankStorageKind rerank_kind = vector_index::RerankStorageKind::kNone;

  // Quantization step for kInt8 traversal coordinates: stored = round(coordinate / scale).
  // Derived per chunk, so it must be read from the file and never recomputed. Zero otherwise.
  float quantization_scale = 0;

  // Bytes the traversal metric reads from a record, i.e. the offset of the rerank copy in it.
  size_t coordinates_size() const {
    return vector_index::CoordinateBytes(storage_kind, dimensions);
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        dimensions, vector_data_size, entry, max_level, config, max_block_size,
        max_vectors_per_non_base_block, vector_data_block, vector_data_amount_per_block, layers,
        storage_kind, rerank_kind, quantization_scale);
  }
};

struct DataBlock {
  std::unique_ptr<std::byte[]> data;
  size_t size = 0;

  DataBlock() = default;

  explicit DataBlock(size_t size) {
    Allocate(size);
  }

  void Allocate(size_t sz) {
    DCHECK(!data);
    data.reset(new std::byte[sz]);
    size = sz;
  }

  Slice AsSlice() const {
    return Slice(data.get(), size);
  }
};

} // namespace yb::hnsw
