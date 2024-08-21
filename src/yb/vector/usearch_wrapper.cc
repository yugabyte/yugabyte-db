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

#include <memory>

#include "yb/vector/usearch_wrapper.h"

#include "yb/gutil/casts.h"

#include "yb/vector/distance.h"
#include "yb/vector/usearch_include_wrapper_internal.h"

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

class UsearchIndex::Impl {
 public:
  explicit Impl(const HNSWOptions& options)
      : dimensions_(options.dimensions),
        distance_type_(options.distance_type),
        metric_(dimensions_, MetricKindFromDistanceType(distance_type_), scalar_kind_t::f32_k),
        index_(decltype(index_)::make(
            metric_,
            CreateIndexDenseConfig(options))) {
    CHECK_GT(dimensions_, 0);
  }

  void Reserve(size_t num_vectors) {
    index_.reserve(num_vectors);
  }

  Status Insert(VertexId vertex_id, const FloatVector& v) {
    if (!index_.add(vertex_id, v.data())) {
      return STATUS_FORMAT(RuntimeError, "Failed to add a vector");
    }
    return Status::OK();
  }

  std::vector<VertexWithDistance> Search(const FloatVector& query_vector, size_t max_num_results) {
    auto usearch_results = index_.search(query_vector.data(), max_num_results);
    std::vector<VertexWithDistance> result_vec;
    result_vec.reserve(usearch_results.size());
    for (size_t i = 0; i < usearch_results.size(); ++i) {
      auto match = usearch_results[i];
      result_vec.push_back(VertexWithDistance(match.member.key, match.distance));
    }
    return result_vec;
  }

  FloatVector GetVector(VertexId vertex_id) const {
    FloatVector result;
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

UsearchIndex::UsearchIndex(const HNSWOptions& options)
    : impl_(std::make_unique<UsearchIndex::Impl>(options)) {
}

UsearchIndex::~UsearchIndex() = default;

void UsearchIndex::Reserve(size_t num_vectors) {
  impl_->Reserve(num_vectors);
}

Status UsearchIndex::Insert(VertexId vertex_id, const FloatVector& v) {
  return impl_->Insert(vertex_id, v);
}

std::vector<VertexWithDistance> UsearchIndex::Search(
    const FloatVector& query_vector, size_t max_num_results) const {
  return impl_->Search(query_vector, max_num_results);
}

FloatVector UsearchIndex::GetVector(VertexId vertex_id) const {
  return impl_->GetVector(vertex_id);
}

}  // namespace yb::vectorindex
