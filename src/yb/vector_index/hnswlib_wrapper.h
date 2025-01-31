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

#include <memory>

#include "yb/util/result.h"

#include "yb/vector_index/hnsw_options.h"
#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/vector_index_if.h"
#include "yb/vector_index/vector_index_wrapper_util.h"

namespace yb::vector_index {

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class HnswlibIndexFactory {
 public:
  static VectorIndexIfPtr<Vector, DistanceResult> Create(const HNSWOptions& options);
};

}  // namespace yb::vector_index
