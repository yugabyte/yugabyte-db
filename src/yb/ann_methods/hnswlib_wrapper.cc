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

#include "yb/ann_methods/hnswlib_wrapper.h"

#include <memory>
#include <utility>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

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

#include "yb/gutil/casts.h"

#include "yb/util/status.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/index_wrapper_base.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::ann_methods {

using hnswlib::Stats;
using vector_index::CoordinateTypeTraits;
using vector_index::DistanceKind;
using vector_index::HNSWOptions;
using vector_index::IndexWrapperBase;
using vector_index::IndexableVectorType;
using vector_index::SearchOptions;
using vector_index::ValidDistanceResultType;
using vector_index::VectorId;
using vector_index::VectorIndexIfPtr;
using vector_index::VectorWithDistance;

namespace {

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class HnswlibVectorIterator;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class HnswlibIndex :
    public IndexWrapperBase<HnswlibIndex<Vector, DistanceResult>, Vector, DistanceResult> {
 public:
  using Scalar = typename Vector::value_type;

  using HNSWImpl = hnswlib::HierarchicalNSW<DistanceResult, VectorId>;

  explicit HnswlibIndex(const HNSWOptions& options)
      : options_(options) {
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> BeginImpl() const override {
    return std::make_unique<HnswlibVectorIterator<Vector, DistanceResult>>(
        hnsw_->vectors_begin(), options_.dimensions);
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> EndImpl() const override {
    return std::make_unique<HnswlibVectorIterator<Vector, DistanceResult>>(
        hnsw_->vectors_end(), options_.dimensions);
  }

  Status Reserve(size_t num_vectors, size_t, size_t) override {
    if (hnsw_) {
      return STATUS_FORMAT(
          IllegalState, "Cannot reserve space for $0 vectors: Hnswlib index already initialized",
          num_vectors);
    }
    RETURN_NOT_OK(CreateSpaceImpl());
    // Please be careful about adding and removing arguments here and make sure they match the
    // actual list of arguments in hnswalg.h.
    hnsw_ = std::make_unique<HNSWImpl>(
        /* s= */ space_.get(),
        /* max_elements= */ num_vectors,
        /* M= */ options_.num_neighbors_per_vertex,
        /* ef_construction= */ options_.ef_construction,
        /* random_seed= */ 100,              // Default value from hnswalg.h
        /* allow_replace_deleted= */ false,  // Default value from hnswalg.h
        /* ef= */ 128);
    return Status::OK();
  }

  Status DoInsert(VectorId vector_id, const Vector& v) {
    hnsw_->addPoint(v.data(), vector_id);

    return Status::OK();
  }

  size_t Size() const override {
    return hnsw_->getCurrentElementCount();
  }

  size_t Capacity() const override {
    return hnsw_->getInternalParameters().max_elements;
  }

  size_t Dimensions() const override {
    return options_.dimensions;
  }

  Result<VectorIndexIfPtr<Vector, DistanceResult>> DoSaveToFile(const std::string& path) {
    try {
      hnsw_->saveIndex(path);
    } catch (std::exception& e) {
      return STATUS_FORMAT(
          IOError, "Failed to save Hnswlib index to file $0: $1", path, e.what());
    }
    return nullptr;
  }

  Status DoLoadFromFile(const std::string& path, size_t) {
    // Create hnsw_ before loading from file.
    RETURN_NOT_OK(Reserve(0, 0, 0));
    try {
      hnsw_->loadIndex(path, space_.get());
    } catch (std::exception& e) {
      return STATUS_FORMAT(
          IOError, "Failed to load Hnswlib index from file $0: $1", path, e.what());
    }
    return Status::OK();
  }

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const override {
    return space_->get_dist_func()(lhs.data(), rhs.data(), space_->get_dist_func_param());
  }

  std::vector<VectorWithDistance<DistanceResult>> DoSearch(
      const Vector& query_vector, const SearchOptions& options) const {
    std::vector<VectorWithDistance<DistanceResult>> result;
    auto tmp_result = hnsw_->searchKnnCloserFirst(query_vector.data(), options.max_num_results);
    result.reserve(tmp_result.size());
    for (const auto& entry : tmp_result) {
      // Being careful to avoid switching the order of distance and vertex id.
      const auto distance = entry.first;
      static_assert(std::is_same_v<std::remove_const_t<decltype(distance)>, DistanceResult>);

      result.push_back({ entry.second, distance });
    }
    return result;
  }

  Result<Vector> GetVector(VectorId vector_id) const override {
    return STATUS(
        NotSupported, "Hnswlib wrapper currently does not allow retriving vectors by id");
  }

  static std::string StatsToStringHelper(const Stats& stats) {
    return Format(
        "$0 nodes, $1 edges, $2 average edges per node",
        stats.nodes,
        stats.edges,
        StringPrintf("%.2f", stats.edges * 1.0 / stats.nodes));
  }

  std::string IndexStatsStr() const override {
    auto internal_params = hnsw_->getInternalParameters();
    std::ostringstream output;

    // Get the maximum level of the index
    auto max_level = hnsw_->getMaxLevel();
    output << "Hnswlib index with " << (max_level + 1) << " levels" << std::endl;
    output << "    max_elements: " << internal_params.max_elements << std::endl;
    output << "    M: " << internal_params.M << std::endl;
    output << "    maxM: " << internal_params.maxM << std::endl;
    output << "    maxM0: " << internal_params.maxM0 << std::endl;
    output << "    ef_construction: " << internal_params.ef_construction << std::endl;
    output << "    ef: " << internal_params.ef << std::endl;
    output << "    mult: " << internal_params.mult << std::endl;

    // Prepare stats per level
    std::vector<Stats> stats_per_level(max_level + 1);
    Stats total_stats = hnsw_->getStats(stats_per_level.data(), max_level);

    // Print connectivity distribution for each level
    for (int level = 0; level <= max_level; ++level) {
        output << "    Level " << level
               << ": " << StatsToStringHelper(stats_per_level[level]) << std::endl;
    }

    // Print the total stats for all levels
    output << "    Totals: " << StatsToStringHelper(total_stats) << std::endl;

    return output.str();
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


template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class HnswlibVectorIterator : public AbstractIterator<std::pair<VectorId, Vector>> {
 public:
  using VectorIndex = HnswlibIndex<Vector, DistanceResult>;
  using HNSWIterator = hnswlib::VectorIterator<DistanceResult, VectorId>;

  HnswlibVectorIterator(HNSWIterator position, int dimensions)
      : internal_iterator_(position), dimensions_(dimensions) {}

 protected:
  std::pair<VectorId, Vector> Dereference() const override {
    auto pair_data = *internal_iterator_;
    Vector result_vector(dimensions_);
    std::memcpy(result_vector.data(), pair_data.first,
                dimensions_ * sizeof(typename Vector::value_type));
    return { pair_data.second, result_vector};
  }

  void Next() override { ++internal_iterator_; }

  bool NotEquals(const AbstractIterator<std::pair<VectorId, Vector>>& other) const override {
    const auto& other_casted = down_cast<const HnswlibVectorIterator&>(other);
    return internal_iterator_ != other_casted.internal_iterator_;
  }

 private:
  HNSWIterator internal_iterator_;
  int dimensions_;
};

}  // namespace

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorIndexIfPtr<Vector, DistanceResult> HnswlibIndexFactory<Vector, DistanceResult>::Create(
    vector_index::FactoryMode mode, const HNSWOptions& options) {
  return std::make_shared<HnswlibIndex<Vector, DistanceResult>>(options);
}

template class HnswlibIndexFactory<FloatVector, float>;
template class HnswlibIndexFactory<UInt8Vector, int32_t>;

}  // namespace yb::ann_methods
