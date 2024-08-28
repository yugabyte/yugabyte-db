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

#include "yb/vector/usearch_wrapper.h"

#include <memory>

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/gutil/casts.h"

#include "yb/vector/distance.h"
#include "yb/vector/usearch_include_wrapper_internal.h"
#include "yb/vector/coordinate_types.h"

namespace yb::vectorindex {

using unum::usearch::metric_punned_t;
using unum::usearch::metric_kind_t;
using unum::usearch::scalar_kind_t;
using unum::usearch::index_dense_gt;
using unum::usearch::index_dense_config_t;

index_dense_config_t CreateIndexDenseConfig(const HNSWOptions& options) {
  index_dense_config_t config;
  config.connectivity = options.num_neighbors_per_vertex;
  config.connectivity_base = options.num_neighbors_per_vertex_base;
  config.expansion_add = options.ef_construction;
  config.expansion_search = options.ef;
  return config;
}

metric_kind_t MetricKindFromDistanceType(VectorDistanceType distance_type) {
  switch (distance_type) {
    case VectorDistanceType::kL2Squared:
      return metric_kind_t::l2sq_k;
    case VectorDistanceType::kCosine:
      return metric_kind_t::cos_k;
  }
  FATAL_INVALID_ENUM_VALUE(VectorDistanceType, distance_type);
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

template<IndexableVectorType Vector>
class UsearchIndex<Vector>::Impl {
 public:
  explicit Impl(const HNSWOptions& options)
      : dimensions_(options.dimensions),
        distance_type_(options.distance_type),
        metric_(dimensions_,
                MetricKindFromDistanceType(distance_type_),
                ConvertCoordinateKind(
                    CoordinateTypeTraits<typename Vector::value_type>::Kind())),
        index_(decltype(index_)::make(
            metric_,
            CreateIndexDenseConfig(options))) {
    CHECK_GT(dimensions_, 0);
  }

  void Reserve(size_t num_vectors) {
    index_.reserve(num_vectors);
  }

  Status Insert(VertexId vertex_id, const Vector& v) {
    if (!index_.add(vertex_id, v.data())) {
      return STATUS_FORMAT(RuntimeError, "Failed to add a vector");
    }
    return Status::OK();
  }

  std::vector<VertexWithDistance> Search(const Vector& query_vector, size_t max_num_results) {
    auto usearch_results = index_.search(query_vector.data(), max_num_results);
    std::vector<VertexWithDistance> result_vec;
    result_vec.reserve(usearch_results.size());
    for (size_t i = 0; i < usearch_results.size(); ++i) {
      auto match = usearch_results[i];
      result_vec.push_back(VertexWithDistance(match.member.key, match.distance));
    }
    return result_vec;
  }

  Vector GetVector(VertexId vertex_id) const {
    Vector result;
    result.resize(dimensions_);
    if (index_.get(vertex_id, result.data())) {
      return result;
    }
    return {};
  }

 private:
  size_t dimensions_;
  VectorDistanceType distance_type_;
  metric_punned_t metric_;
  index_dense_gt<VertexId> index_;
};

template<IndexableVectorType Vector>
UsearchIndex<Vector>::UsearchIndex(const HNSWOptions& options)
    : impl_(std::make_unique<UsearchIndex::Impl>(options)) {
}

template<IndexableVectorType Vector>
UsearchIndex<Vector>::~UsearchIndex() = default;

template<IndexableVectorType Vector>
void UsearchIndex<Vector>::Reserve(size_t num_vectors) {
  impl_->Reserve(num_vectors);
}

template<IndexableVectorType Vector>
Status UsearchIndex<Vector>::Insert(VertexId vertex_id, const Vector& v) {
  return impl_->Insert(vertex_id, v);
}

template<IndexableVectorType Vector>
std::vector<VertexWithDistance> UsearchIndex<Vector>::Search(
    const Vector& query_vector, size_t max_num_results) const {
  return impl_->Search(query_vector, max_num_results);
}

template<IndexableVectorType Vector>
Vector UsearchIndex<Vector>::GetVector(VertexId vertex_id) const {
  return impl_->GetVector(vertex_id);
}

BOOST_PP_SEQ_FOR_EACH(
    YB_INSTANTIATE_TEMPLATE_FOR_VECTOR_OF, UsearchIndex, YB_USEARCH_SUPPORTED_COORDINATE_TYPES)

}  // namespace yb::vectorindex
