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

#include "yb/vector/hnswlib_wrapper.h"

#include <memory>

#pragma GCC diagnostic push

// For https://gist.githubusercontent.com/mbautin/db70c2fcaa7dd97081b0c909d72a18a8/raw
#pragma GCC diagnostic ignored "-Wunused-function"

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#endif

#include "hnswlib/hnswlib.h"
#include "hnswlib/hnswalg.h"
#include "hnswlib/space_ip.h"
#include "hnswlib/space_l2.h"

#pragma GCC diagnostic pop

#include "yb/util/status.h"

#include "yb/vector/distance.h"

namespace yb::vectorindex {

namespace detail {
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class HnswlibIndexImpl {
 public:
  using Scalar = typename Vector::value_type;

  using HNSWImpl = typename hnswlib::HierarchicalNSW<DistanceResult>;

  explicit HnswlibIndexImpl(const HNSWOptions& options)
      : options_(options) {
  }

  Status Reserve(size_t num_vectors) {
    if (hnsw_) {
      return STATUS_FORMAT(
          IllegalState, "Cannot reserve space for $0 vectors: Hnswlib index already initialized",
          num_vectors);
    }
    RETURN_NOT_OK(CreateSpaceImpl());
    hnsw_ = std::make_unique<HNSWImpl>(
        space_.get(),
        /* max_elements= */ num_vectors,
        /* M= */ options_.max_neighbors_per_vertex,
        /* ef_construction= */ options_.ef_construction);
    return Status::OK();
  }

  Status Insert(VertexId vertex_id, const Vector& v) {
    hnsw_->addPoint(v.data(), vertex_id);
    return Status::OK();
  }

  std::vector<VertexWithDistance<DistanceResult>> Search(
      const Vector& query_vector, size_t max_num_results) {
    std::vector<VertexWithDistance<DistanceResult>> result;
    auto tmp_result = hnsw_->searchKnnCloserFirst(query_vector.data(), max_num_results);
    result.reserve(tmp_result.size());
    for (const auto& entry : tmp_result) {
      // Being careful to avoid switching the order of distance and vertex id..
      const auto distance = entry.first;
      static_assert(std::is_same_v<std::remove_const_t<decltype(distance)>, DistanceResult>);

      const auto label = entry.second;
      static_assert(VertexIdCompatible<decltype(label)>);

      result.push_back(VertexWithDistance<DistanceResult>(label, distance));
    }
    return result;
  }

  Result<Vector> GetVector(VertexId vertex_id) const {
    return STATUS(
        NotSupported, "Hnswlib wrapper currently does not allow retriving vectors by id");
  }

 private:
  Status CreateSpaceImpl() {
    switch (options_.distance_kind) {
      case DistanceKind::kL2Squared: {
        if constexpr (std::is_same<Vector, FloatVector>::value) {
          space_ = std::make_unique<hnswlib::L2Space>(options_.dimensions);
        } else if constexpr (std::is_same<Vector, std::vector<uint8_t>>::value) {
          space_ = std::make_unique<hnswlib::L2SpaceI>(options_.dimensions);
        } else {
          return STATUS_FORMAT(
              InvalidArgument,
              "Unsupported combination of distance type and vector type: $0 and $1",
              options_.distance_kind, CoordinateTypeTraits<Scalar>::Kind());
        }

        return Status::OK();
      }
      default:
        return STATUS_FORMAT(
            InvalidArgument, "Unsupported distance type for Hnswlib: $0",
            options_.distance_kind);
    }
  }

  HNSWOptions options_;
  std::unique_ptr<hnswlib::SpaceInterface<DistanceResult>> space_;
  std::unique_ptr<HNSWImpl> hnsw_;
};

}  // namespace detail

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
HnswlibIndex<Vector, DistanceResult>::HnswlibIndex(const HNSWOptions& options)
    : VectorIndexBase<Impl, Vector, DistanceResult>(std::make_unique<Impl>(options)) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
HnswlibIndex<Vector, DistanceResult>::~HnswlibIndex() = default;

template class HnswlibIndex<FloatVector, float>;
template class HnswlibIndex<UInt8Vector, int32_t>;

}  // namespace yb::vectorindex
