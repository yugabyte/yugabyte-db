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

#include "yb/vector_index/hnsw_options.h"

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/util/tostring.h"

#include "yb/vector_index/usearch_include_wrapper_internal.h"

namespace yb::vector_index {

namespace {

using unum::usearch::metric_kind_t;
using unum::usearch::scalar_kind_t;

metric_kind_t MetricKindFromDistanceType(DistanceKind distance_kind) {
  switch (distance_kind) {
    case DistanceKind::kL2Squared:
      return metric_kind_t::l2sq_k;
    case DistanceKind::kInnerProduct:
      return metric_kind_t::ip_k;
    case DistanceKind::kCosine:
      return metric_kind_t::cos_k;
  }
  FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind);
}

scalar_kind_t ConvertCoordinateKind(CoordinateKind coordinate_kind) {
  switch (coordinate_kind) {
#define YB_COORDINATE_KIND_TO_USEARCH_CASE(r, data, coordinate_info_tuple) \
    case CoordinateKind::YB_COORDINATE_ENUM_ELEMENT_NAME(coordinate_info_tuple): \
      return scalar_kind_t::BOOST_PP_CAT( \
          YB_EXTRACT_COORDINATE_TYPE_SHORT_NAME(coordinate_info_tuple), _k);
    BOOST_PP_SEQ_FOR_EACH(YB_COORDINATE_KIND_TO_USEARCH_CASE, _, YB_COORDINATE_TYPE_INFO)
#undef YB_COORDINATE_KIND_TO_USEARCH_CASE
  }
  FATAL_INVALID_ENUM_VALUE(CoordinateKind, coordinate_kind);
}

} // namespace

std::string HNSWOptions::ToString() const {
  return YB_STRUCT_TO_STRING(
      extend_candidates,
      keep_pruned_connections,
      num_neighbors_per_vertex,
      max_neighbors_per_vertex,
      num_neighbors_per_vertex_base,
      max_neighbors_per_vertex_base,
      ef_construction,
      robust_prune_alpha);
}

template <class Vector>
unum::usearch::metric_punned_t HNSWOptions::CreateMetric() const {
  return unum::usearch::metric_punned_t(
      dimensions,
      MetricKindFromDistanceType(distance_kind),
      ConvertCoordinateKind(CoordinateTypeTraits<typename Vector::value_type>::kKind));
}

template
unum::usearch::metric_punned_t HNSWOptions::CreateMetric<FloatVector>() const;

}  // namespace yb::vector_index
