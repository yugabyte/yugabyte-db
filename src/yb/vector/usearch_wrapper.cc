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

#include "yb/util/locks.h"
#include "yb/util/shared_lock.h"

#include "yb/vector/distance.h"
#include "yb/vector/index_wrapper_base.h"
#include "yb/vector/usearch_include_wrapper_internal.h"
#include "yb/vector/coordinate_types.h"

namespace yb::vectorindex {

using unum::usearch::byte_t;
using unum::usearch::index_dense_config_t;
using unum::usearch::index_dense_gt;
using unum::usearch::metric_kind_t;
using unum::usearch::metric_punned_t;
using unum::usearch::output_file_t;
using unum::usearch::scalar_kind_t;

index_dense_config_t CreateIndexDenseConfig(const HNSWOptions& options) {
  index_dense_config_t config;
  config.connectivity = options.num_neighbors_per_vertex;
  config.connectivity_base = options.num_neighbors_per_vertex_base;
  config.expansion_add = options.ef_construction;
  config.expansion_search = options.ef;
  return config;
}

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

namespace {

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class UsearchIndex :
    public IndexWrapperBase<UsearchIndex<Vector, DistanceResult>, Vector, DistanceResult> {
 public:
  using IndexImpl = index_dense_gt<VertexId>;

  explicit UsearchIndex(const HNSWOptions& options)
      : dimensions_(options.dimensions),
        distance_kind_(options.distance_kind),
        metric_(dimensions_,
                MetricKindFromDistanceType(distance_kind_),
                ConvertCoordinateKind(CoordinateTypeTraits<typename Vector::value_type>::kKind)),
        index_(IndexImpl::make(
            metric_,
            CreateIndexDenseConfig(options))) {
    CHECK_GT(dimensions_, 0);
  }

  Status Reserve(size_t num_vectors) override {
    index_.reserve(num_vectors);
    return Status::OK();
  }

  Status DoInsert(VertexId vertex_id, const Vector& v) {
    if (!index_.add(vertex_id, v.data())) {
      return STATUS_FORMAT(RuntimeError, "Failed to add a vector");
    }
    return Status::OK();
  }

  Status DoSaveToFile(const std::string& path) {
    // TODO(lsm) Reload via memory mapped file
    if (!index_.save(output_file_t(path.c_str()))) {
      return STATUS_FORMAT(IOError, "Failed to save index to file: $0", path);
    }
    return Status::OK();
  }

  Status DoLoadFromFile(const std::string& path) {
    auto result = decltype(index_)::make(path.c_str(), /* view= */ true);
    if (result) {
      index_ = std::move(result.index);
      return Status::OK();
    }
    return STATUS_FORMAT(IOError, "Failed to load index from file: $0", path);
  }

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const override {
    return metric_(
        pointer_cast<const byte_t*>(lhs.data()), pointer_cast<const byte_t*>(rhs.data()));
  }

  std::vector<VertexWithDistance<DistanceResult>> DoSearch(
      const Vector& query_vector, size_t max_num_results) const {
    auto usearch_results = index_.search(query_vector.data(), max_num_results);
    std::vector<VertexWithDistance<DistanceResult>> result_vec;
    result_vec.reserve(usearch_results.size());
    for (size_t i = 0; i < usearch_results.size(); ++i) {
      auto match = usearch_results[i];
      result_vec.push_back(VertexWithDistance<DistanceResult>(match.member.key, match.distance));
    }
    return result_vec;
  }

  Result<Vector> GetVector(VertexId vertex_id) const override {
    Vector result;
    result.resize(dimensions_);
    if (index_.get(vertex_id, result.data())) {
      return result;
    }
    return Vector();
  }

  static std::string StatsToStringHelper(const IndexImpl::stats_t& stats) {
    return Format(
        "$0 nodes, $1 edges, $2 average edges per node",
        stats.nodes,
        stats.edges,
        StringPrintf("%.2f", stats.edges * 1.0 / stats.nodes));
  }

  std::string IndexStatsStr() const override {
    std::ostringstream output;

    // Get the maximum level of the index
    auto max_level = index_.max_level();
    output << "Usearch index with " << (max_level + 1) << " levels" << std::endl;

    const auto& config = index_.config();
    output << "    connectivity: " << config.connectivity << std::endl;
    output << "    connectivity_base: " << config.connectivity_base << std::endl;
    output << "    expansion_add: " << config.expansion_add << std::endl;
    output << "    expansion_search: " << config.expansion_search << std::endl;
    output << "    inverse_log_connectivity: " << index_.inverse_log_connectivity() << std::endl;

    std::vector<IndexImpl::stats_t> stats_per_level;
    stats_per_level.resize(max_level + 1);
    auto total_stats = index_.stats(stats_per_level.data(), max_level);

    // Print connectivity distribution for each level
    for (size_t level = 0; level <= max_level; ++level) {
      output << "    Level " << level << ": " << StatsToStringHelper(stats_per_level[level])
             << std::endl;
    }

    output << "    Totals: " << StatsToStringHelper(total_stats) << std::endl;

    return output.str();
  }

 private:
  size_t dimensions_;
  DistanceKind distance_kind_;
  metric_punned_t metric_;
  index_dense_gt<VertexId> index_;
};

}  // namespace

template <class Vector, class DistanceResult>
VectorIndexIfPtr<Vector, DistanceResult> UsearchIndexFactory<Vector, DistanceResult>::Create(
    const HNSWOptions& options) {
  return std::make_shared<UsearchIndex<Vector, DistanceResult>>(options);
}

template class UsearchIndexFactory<FloatVector, float>;

}  // namespace yb::vectorindex
