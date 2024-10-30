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

#include "yb/util/enums.h"
#include "yb/util/result.h"

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/hnsw_options.h"
#include "yb/vector_index/hnswlib_wrapper.h"
#include "yb/vector_index/usearch_wrapper.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

YB_DEFINE_ENUM(
    ANNMethodKind,
    (kUsearch)
    (kHnswlib));

template<ANNMethodKind method_kind>
struct ANNMethodTraits {
  static constexpr ANNMethodKind kKind = method_kind;
};

// TODO: use preprocessing to reduce duplication below.

template<>
struct ANNMethodTraits<ANNMethodKind::kUsearch> {
  template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
  using FactoryType = UsearchIndexFactory<Vector, DistanceResult>;
};

template<>
struct ANNMethodTraits<ANNMethodKind::kHnswlib> {
  template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
  using FactoryType = HnswlibIndexFactory<Vector, DistanceResult>;
};

}  // namespace yb::vector_index
