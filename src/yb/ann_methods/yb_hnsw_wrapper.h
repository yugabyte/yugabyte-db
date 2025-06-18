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

#include "yb/hnsw/types.h"

#include "yb/vector_index/hnsw_options.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::ann_methods {

template <class Vector, class DistanceResult>
vector_index::VectorIndexIfPtr<Vector, DistanceResult> CreateYbHnsw(
      const hnsw::BlockCachePtr& block_cache, const vector_index::HNSWOptions& options);

template <class Vector, class DistanceResult>
Result<vector_index::VectorIndexIfPtr<Vector, DistanceResult>> ImportYbHnsw(
    const hnsw::UsearchIndexDense& index, const std::string& path,
    const hnsw::BlockCachePtr& block_cache);

} // namespace yb::ann_methods
