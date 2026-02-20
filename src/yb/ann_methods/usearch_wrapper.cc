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

#include "yb/ann_methods/usearch_wrapper.h"

#include <memory>
#include <semaphore>

#include "yb/ann_methods/yb_hnsw_wrapper.h"

#include "yb/gutil/casts.h"

#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/index_wrapper_base.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"
#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/vectorann_util.h"

DEFINE_test_flag(bool, usearch_exact, false,
    "Use exact search in usearch wrapper to guarantee deterministic results.");

namespace unum::usearch {

// Explicit specialization for the operator== is required for usearch need, as a compiler is not
// smart enough to resolve StronglyTypedUuid's comparison operator, where one argument is different
// from StronglyTypedUuid but can be implicitly casted the corresponding StronglyTypedUuid.
bool operator==(const yb::vector_index::VectorId& lhs,
                const yb::vector_index::VectorId& rhs) noexcept {
  return *lhs == *rhs;
}

} // namespace unum::usearch

namespace yb::ann_methods {

using unum::usearch::byte_t;
using unum::usearch::index_dense_config_t;
using unum::usearch::index_dense_gt;
using unum::usearch::metric_kind_t;
using unum::usearch::metric_punned_t;
using unum::usearch::output_file_t;
using unum::usearch::scalar_kind_t;

using vector_index::HNSWOptions;
using vector_index::DistanceKind;
using vector_index::CoordinateKind;
using vector_index::IndexableVectorType;
using vector_index::VectorId;
using vector_index::ValidDistanceResultType;

index_dense_config_t CreateIndexDenseConfig(const HNSWOptions& options) {
  index_dense_config_t config;
  config.connectivity = options.num_neighbors_per_vertex;
  config.connectivity_base = options.num_neighbors_per_vertex_base;
  config.expansion_add = options.ef_construction;
  return config;
}

namespace {

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class UsearchVectorIterator : public AbstractIterator<std::pair<VectorId, Vector>> {
 public:
  using IteratorPair = std::pair<VectorId, Vector>;
  using member_citerator_t = typename unum::usearch::index_dense_gt<VectorId>::member_citerator_t;

  UsearchVectorIterator(
      size_t dimensions, member_citerator_t it, const index_dense_gt<VectorId>* index)
      : dimensions_(dimensions), it_(it), index_(index) {}

 protected:
  IteratorPair Dereference() const override {
    // TODO(vector_index) do it in more efficient way
    Vector result_vector(dimensions_);
    index_->get(it_.key(), result_vector.data());

    return IteratorPair(it_.key(), result_vector);
  }

  void Next() override {
    ++it_;
  }

  bool NotEquals(const AbstractIterator<IteratorPair>& other) const override {
    const auto* other_iterator = down_cast<const UsearchVectorIterator*>(&other);
    if (!other_iterator) return true;
    return it_ != other_iterator->it_;
  }

 private:      // Reference to the Usearch index
  size_t dimensions_;              // Dimensionality of the vectors
  member_citerator_t it_; // Iterator over the internal Usearch entities
  const index_dense_gt<VectorId>* index_;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class UsearchIndex :
    public vector_index::IndexWrapperBase<
        UsearchIndex<Vector, DistanceResult>, Vector, DistanceResult> {
 public:
  using IndexImpl = index_dense_gt<VectorId>;

  UsearchIndex(
      const hnsw::BlockCachePtr& block_cache, const HNSWOptions& options, HnswBackend backend,
      const MemTrackerPtr& mem_tracker)
      : block_cache_(block_cache),
        dimensions_(options.dimensions),
        distance_kind_(options.distance_kind),
        metric_(options.CreateMetric<Vector>()),
        backend_(backend),
        index_(IndexImpl::make(metric_, CreateIndexDenseConfig(options))),
        mem_tracker_(mem_tracker) {
    CHECK_GT(dimensions_, 0);
  }

  ~UsearchIndex() {
    auto current_consumption = current_consumption_.load();
    if (current_consumption) {
      mem_tracker_->Release(current_consumption);
    }
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> BeginImpl() const override {
    return std::make_unique<UsearchVectorIterator<Vector, DistanceResult>>(
        dimensions_, index_.cbegin(), &index_);
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> EndImpl() const override {
    return std::make_unique<UsearchVectorIterator<Vector, DistanceResult>>(
        dimensions_, index_.cend(), &index_);
  }

  Status Reserve(
      size_t num_vectors, size_t max_concurrent_inserts, size_t max_concurrent_reads) override {
    auto se = UpdateConsumptionOnExit();
    // Usearch could allocate 3 times more entries, than requested.
    // Since it always allocate power of 2, we use this weird logic to make it pick minimal
    // power of 2 that is greater or equals than num_vectors.
    auto rounded_num_vectors = unum::usearch::ceil2(num_vectors);
    auto num_members = std::max<size_t>(rounded_num_vectors * 2 / 3, 1);
    index_.reserve(unum::usearch::index_limits_t(
      num_members, max_concurrent_inserts + max_concurrent_reads));
    search_semaphore_.emplace(max_concurrent_reads);
    static std::once_flag log_once;
    std::call_once(log_once, [index = &index_]() {
      LOG(INFO) << "Usearch metric: " << index->metric().isa_name();
    });
    return Status::OK();
  }

  Status DoInsert(VectorId vector_id, const Vector& v) {
    auto se = UpdateConsumptionOnExit();
    auto add_result = index_.add(vector_id, v.data());
    RSTATUS_DCHECK(
        add_result, RuntimeError, "Failed to add a vector $0: $1", vector_id,
        add_result.error.release());
    return Status::OK();
  }

  size_t Size() const override {
    return index_.size();
  }

  size_t Capacity() const override {
    return index_.limits().members;
  }

  size_t Dimensions() const override {
    return dimensions_;
  }

  Result<vector_index::VectorIndexIfPtr<Vector, DistanceResult>> DoSaveToFile(
      const std::string& path) {
    // TODO(vector_index) Reload via memory mapped file
    VLOG_WITH_FUNC(2)
        << path << ", size: " << index_.size() << ", backend: " << HnswBackend_Name(backend_);
    if (backend_ == HnswBackend::YB_HNSW) {
      return ImportYbHnsw<Vector, DistanceResult>(index_, path, block_cache_);
    }
    try {
      if (!index_.save(output_file_t(path.c_str()))) {
        return STATUS_FORMAT(IOError, "Failed to save index to file: $0", path);
      }
    } catch(std::exception& exc) {
      return STATUS_FORMAT(IOError, "Failed to save index to file $0: $1", path, exc.what());
    }
    return nullptr;
  }

  Status DoLoadFromFile(const std::string& path, size_t max_concurrent_reads) {
    auto se = UpdateConsumptionOnExit();
    try {
      auto result = decltype(index_)::make(path.c_str(), /* view= */ true);
      if (result) {
        search_semaphore_.emplace(max_concurrent_reads);
        index_ = std::move(result.index);
        VLOG_WITH_FUNC(2) << path << ": " << index_.size();
        return Status::OK();
      }
      return STATUS_FORMAT(IOError, "Failed to load index from file: $0", path);
    } catch (std::runtime_error& exc) {
      return STATUS_FORMAT(IOError, "Failed to load index from file $0: $1", path, exc.what());
    }
  }

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const override {
    return metric_(
        pointer_cast<const byte_t*>(lhs.data()), pointer_cast<const byte_t*>(rhs.data()));
  }

  Result<std::vector<vector_index::VectorWithDistance<DistanceResult>>> DoSearch(
      const Vector& query_vector, const vector_index::SearchOptions& options) const {
    std::vector<vector_index::VectorWithDistance<DistanceResult>> result_vec;
    if (index_.size() == 0) {
      return result_vec;
    }
    SemaphoreLock lock(*search_semaphore_);
    auto usearch_results = index_.filtered_search_with_ef(
        query_vector.data(), options.max_num_results, options.filter, options.ef,
        IndexImpl::any_thread(), FLAGS_TEST_usearch_exact);
    RSTATUS_DCHECK(
        usearch_results, RuntimeError, "Failed to search a vector: $0",
        usearch_results.error.release());
    result_vec.reserve(usearch_results.size());
    for (size_t i = 0; i < usearch_results.size(); ++i) {
      auto match = usearch_results[i];
      result_vec.emplace_back(match.member.key, match.distance);
    }
    return result_vec;
  }

  Result<Vector> GetVector(VectorId vector_id) const override {
    // TODO(vector_index) do it in more efficient way
    Vector result(dimensions_);
    SCHECK_EQ(
        index_.get(vector_id, result.data()), 1, NotFound,
        Format("Vector $0 is missing in index", vector_id));
    return result;
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
  auto UpdateConsumptionOnExit() {
    return ScopeExit([this] {
      UpdateConsumption();
    });
  }

  void UpdateConsumption() {
    if (!mem_tracker_) {
      return;
    }
    auto new_consumption = index_.impl().tape_allocator().total_allocated();
    auto current_consumption = current_consumption_.load();
    // usearch does not release memory, so lower new_consumption just means that we have 2
    // concurrent calls to UpdateConsumption, and call with lesser new_consumption happened
    // earlier.
    if (new_consumption <= current_consumption) {
      return;
    }
    std::lock_guard lock(consumption_mutex_);
    current_consumption = current_consumption_.load();
    if (new_consumption <= current_consumption) {
      return;
    }
    mem_tracker_->Consume(new_consumption - current_consumption);
    current_consumption_.store(new_consumption);
  }

  const hnsw::BlockCachePtr block_cache_;
  size_t dimensions_;
  DistanceKind distance_kind_;
  metric_punned_t metric_;
  HnswBackend backend_;
  IndexImpl index_;
  mutable std::optional<std::counting_semaphore<1>> search_semaphore_;
  MemTrackerPtr mem_tracker_;
  std::atomic<size_t> current_consumption_;
  simple_spinlock consumption_mutex_;
};

}  // namespace

template <vector_index::IndexableVectorType Vector,
          vector_index::ValidDistanceResultType DistanceResult>
vector_index::VectorIndexIfPtr<Vector, DistanceResult>
    UsearchIndexFactory<Vector, DistanceResult>::Create(
    vector_index::FactoryMode mode, const hnsw::BlockCachePtr& block_cache,
    const HNSWOptions& options, HnswBackend backend, const MemTrackerPtr& mem_tracker) {
  if (backend == HnswBackend::YB_HNSW && mode == vector_index::FactoryMode::kLoad) {
    return CreateYbHnsw<Vector, DistanceResult>(block_cache, options);
  }
  return std::make_shared<UsearchIndex<Vector, DistanceResult>>(
      block_cache, options, backend, mem_tracker);
}

template class UsearchIndexFactory<FloatVector, float>;

}  // namespace yb::ann_methods
