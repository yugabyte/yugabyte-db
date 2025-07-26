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

#include "yb/util/tostring.h"

#include "yb/vector_index/vector_index_fwd.h"

namespace unum::usearch {

struct index_dense_config_t;
template <typename, typename> class index_dense_gt;

}

namespace yb::hnsw {

using VectorNo = uint32_t;
using UsearchIndexDense = unum::usearch::index_dense_gt<vector_index::VectorId, uint32_t>;

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

  void Init(const UsearchIndexDense& index);

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        dimensions, vector_data_size, entry, max_level, config, max_block_size,
        max_vectors_per_non_base_block, vector_data_block, vector_data_amount_per_block, layers);
  }
};

} // namespace yb::hnsw
