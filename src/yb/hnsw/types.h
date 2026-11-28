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

#include "yb/vector_index/vector_index_fwd.h"

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
  size_t dimensions = 0;
  size_t vector_data_size = 0;
  VectorNo entry = 0;
  size_t max_level = 0;
  Config config;
  size_t max_block_size = 0;
  size_t max_vectors_per_non_base_block = 0;

  size_t vector_data_block = 0;
  size_t vector_data_amount_per_block = 0;
  std::vector<LayerInfo> layers;

  // Vectors of different tenants are not linked. num_tenant_directory_blocks blocks, consecutive
  // from tenant_directory_block, hold the entry vector of every tenant ordered by tenant, and
  // tenant_directory_boundaries holds the first tenant of every such block except the first one.
  // 0 blocks means vectors have no tenants.
  size_t tenant_directory_block = 0;
  size_t num_tenant_directory_blocks = 0;
  std::vector<std::string> tenant_directory_boundaries;

  // num_deletions vector deletions saved with the chunk, see vector_index::VectorLSMDeletion.
  // Vector ids are random, so their leading bits address blocks. 2^deletion_bits blocks,
  // consecutive from deletion_block, hold the deletions sorted by vector id: those whose vector ids
  // start with the deletion_bits bits p are in block deletion_block + p. An entry is the vector id
  // without its first deletion_bits / 8 bytes, which the block implies, followed by the hybrid
  // time. Likewise 2^deletion_filter_bits blocks of deletion_filter_size bytes from
  // deletion_filter_block hold fixed size bloom filters over the vector ids by their first
  // deletion_filter_bits bits.
  size_t num_deletions = 0;
  size_t deletion_bits = 0;
  size_t deletion_block = 0;
  size_t deletion_filter_bits = 0;
  size_t deletion_filter_block = 0;
  size_t deletion_filter_size = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        dimensions, vector_data_size, entry, max_level, config, max_block_size,
        max_vectors_per_non_base_block, vector_data_block, vector_data_amount_per_block, layers,
        tenant_directory_block, num_tenant_directory_blocks, tenant_directory_boundaries,
        num_deletions, deletion_bits, deletion_block, deletion_filter_bits, deletion_filter_block,
        deletion_filter_size);
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
