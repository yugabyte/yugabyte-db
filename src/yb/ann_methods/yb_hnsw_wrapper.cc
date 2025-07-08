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

#include "yb/ann_methods/yb_hnsw_wrapper.h"

#include "yb/hnsw/hnsw.h"

#include "yb/util/lockfree.h"
#include "yb/util/scope_exit.h"

#include "yb/vector_index/index_wrapper_base.h"

namespace yb::ann_methods {

namespace {

using vector_index::IndexableVectorType;
using vector_index::VectorId;
using vector_index::ValidDistanceResultType;

template<IndexableVectorType Vector>
class YbHnswIterator : public AbstractIterator<std::pair<VectorId, Vector>> {
 public:
  using ValueType = std::pair<VectorId, Vector>;
  using Base = AbstractIterator<ValueType>;

  YbHnswIterator(const hnsw::YbHnsw& hnsw, size_t index)
      : cache_scope_(cache_, hnsw), dimensions_(hnsw.header().dimensions), index_(index) {
  }

  void Next() override {
    ++index_;
  }

  ValueType Dereference() const override {
    const auto* coordinates = cache_.CoordinatesPtr(index_);
    ValueType result;
    result.first = cache_.GetVectorData(index_);
    result.second.resize(dimensions_);
    memcpy(result.second.data(), coordinates, dimensions_ * sizeof(typename Vector::value_type));
    return result;
  }

  bool NotEquals(const Base& other) const override {
    auto& rhs = down_cast<const YbHnswIterator&>(other);
    return index_ != rhs.index_;
  }

 private:
  mutable hnsw::SearchCache cache_;
  hnsw::SearchCacheScope cache_scope_;
  const size_t dimensions_;
  size_t index_;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class YbHnswIndex :
    public vector_index::IndexWrapperBase<
        YbHnswIndex<Vector, DistanceResult>, Vector, DistanceResult> {
 public:
  using Base = vector_index::IndexWrapperBase<
        YbHnswIndex<Vector, DistanceResult>, Vector, DistanceResult>;

  template <class... Args>
  explicit YbHnswIndex(Args&&... args) : index_(std::forward<Args>(args)...) {}

  ~YbHnswIndex() {
    while (auto* context = search_contexts_.Pop()) {
      delete context;
    }
  }

  template <class... Args>
  Status Import(
      const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path) {
    VLOG_WITH_FUNC(3) << "index: " << index.size() << ", path: " << path;
    return index_.Import(index, path);
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> BeginImpl() const override {
    return std::make_unique<YbHnswIterator<Vector>>(index_, 0);
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> EndImpl() const override {
    return std::make_unique<YbHnswIterator<Vector>>(index_, Size());
  }

  Status Reserve(
      size_t num_vectors, size_t max_concurrent_inserts, size_t max_concurrent_reads) override {
    return Status::OK();
  }

  size_t Size() const override {
    return index_.header().layers.front().size;
  }

  size_t Capacity() const override {
    return Size();
  }

  size_t Dimensions() const override {
    return index_.header().dimensions;
  }

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const override {
    return index_.Distance(
        pointer_cast<const std::byte*>(lhs.data()), pointer_cast<const std::byte*>(rhs.data()));
  }

  Result<Vector> GetVector(VectorId vector_id) const override {
    return STATUS_FORMAT(NotSupported, "GetVector not implemented");
  }

  std::string IndexStatsStr() const override {
    return index_.header().ToString();
  }

  Result<std::vector<vector_index::VectorWithDistance<DistanceResult>>> DoSearch(
      const Vector& query_vector, const vector_index::SearchOptions& options) const {
    auto* context = search_contexts_.Pop();
    if (!context) {
      context = new SearchContextHolder;
    }
    auto se = ScopeExit([this, context] {
      search_contexts_.Push(context);
    });
    VLOG_WITH_FUNC(4)
        << "query_vector: " << AsString(query_vector) << ", options: " << AsString(options);
    return index_.Search(query_vector.data(), options, context->context);
  }

  Status DoInsert(VectorId vector_id, const Vector& v) {
    return STATUS_FORMAT(NotSupported, "DoInsert not implemented");
  }

  Status DoSaveToFile(const std::string& path) {
    return STATUS_FORMAT(NotSupported, "DoSaveToFile not implemented");
  }

  Status DoLoadFromFile(const std::string& path, size_t max_concurrent_reads) {
    return index_.Init(path);
  }

 private:
  hnsw::YbHnsw index_;

  struct SearchContextHolder : public MPSCQueueEntry<SearchContextHolder> {
    hnsw::YbHnswSearchContext context;
  };

  mutable LockFreeStack<SearchContextHolder> search_contexts_;
};

} // namespace

template <class Vector, class DistanceResult>
Result<vector_index::VectorIndexIfPtr<Vector, DistanceResult>> ImportYbHnsw(
    const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path,
    const hnsw::BlockCachePtr& block_cache) {
  auto result = std::make_shared<YbHnswIndex<Vector, DistanceResult>>(index.metric(), block_cache);
  RETURN_NOT_OK(result->Import(index, path));
  return result;
}

template
Result<vector_index::VectorIndexIfPtr<FloatVector, float>> ImportYbHnsw<FloatVector, float>(
    const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path,
    const hnsw::BlockCachePtr& block_cache);

template <class Vector, class DistanceResult>
vector_index::VectorIndexIfPtr<Vector, DistanceResult> CreateYbHnsw(
    const hnsw::BlockCachePtr& block_cache, const vector_index::HNSWOptions& options) {
  return std::make_shared<YbHnswIndex<Vector, DistanceResult>>(
      options.CreateMetric<Vector>(), block_cache);
}

template
vector_index::VectorIndexIfPtr<FloatVector, float> CreateYbHnsw<FloatVector, float>(
    const hnsw::BlockCachePtr& block_cache, const vector_index::HNSWOptions& options);

} // namespace yb::ann_methods
