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
#include "yb/util/status_format.h"

#include "yb/vector_index/coordinate_codec.h"
#include "yb/vector_index/index_wrapper_base.h"

namespace yb::ann_methods {

namespace {

using vector_index::IndexableVectorType;
using vector_index::VectorId;
using vector_index::ValidDistanceResultType;
using vector_index::VectorStorageKind;

template<IndexableVectorType Vector>
class YbHnswIterator : public AbstractIterator<std::pair<VectorId, Vector>> {
 public:
  using ValueType = std::pair<VectorId, Vector>;
  using Base = AbstractIterator<ValueType>;

  YbHnswIterator(const hnsw::YbHnsw& hnsw, size_t index)
      : cache_scope_(cache_, hnsw), header_(hnsw.header()), index_(index) {
  }

  void Next() override {
    ++index_;
  }

  ValueType Dereference() const override {
    ValueType result;
    result.first = cache_.GetVectorData(index_);
    result.second.resize(header_.dimensions);
    // Records may be narrower than Vector::value_type, so this decodes rather than copies; a
    // memcpy would walk past the end of each record.
    //
    // Where a rerank copy exists it is the one compaction must read. The traversal encoding may be
    // quantized against a scale the merged chunk will not reuse, so decoding it would feed
    // already-quantized values into a fresh quantization and compound the error once per
    // compaction. The rerank encodings round trip exactly.
    if (header_.rerank_kind != vector_index::RerankStorageKind::kNone) {
      vector_index::WidenCoordinates(
          vector_index::StorageKindForRerank(header_.rerank_kind),
          cache_.RerankCoordinatesPtr(index_), header_.dimensions, result.second.data());
    } else {
      vector_index::WidenCoordinates(
          header_.storage_kind, header_.quantization_scale, cache_.CoordinatesPtr(index_),
          header_.dimensions, result.second.data());
    }
    return result;
  }

  bool NotEquals(const Base& other) const override {
    auto& rhs = down_cast<const YbHnswIterator&>(other);
    return index_ != rhs.index_;
  }

 private:
  mutable hnsw::SearchCache cache_;
  hnsw::SearchCacheScope cache_scope_;
  const hnsw::Header& header_;
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

  Status Import(
      const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path) {
    VLOG_WITH_FUNC(3) << "index: " << index.size() << ", path: " << path;
    return index_.Import(index, path);
  }

  Status Import(
      const hnsw::HnswlibIndex<DistanceResult>& index, const std::string& path,
      VectorStorageKind storage_kind, vector_index::RerankStorageKind rerank_kind) {
    VLOG_WITH_FUNC(3)
        << "index: " << index.cur_element_count << ", path: " << path
        << ", storage_kind: " << storage_kind << ", rerank_kind: " << rerank_kind;
    return index_.Import(index, path, storage_kind, rerank_kind);
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> BeginImpl() const override {
    return std::make_unique<YbHnswIterator<Vector>>(index_, 0);
  }

  std::unique_ptr<AbstractIterator<std::pair<VectorId, Vector>>> EndImpl() const override {
    return std::make_unique<YbHnswIterator<Vector>>(index_, Size());
  }

  Status Reserve(
      size_t num_vectors, size_t max_concurrent_inserts, size_t max_concurrent_reads,
      rocksdb::Cache::ReservationMode reservation_mode) override {
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

  // YbHnsw is a read-only, file-backed format that does not allocate per-vector heap memory in
  // the same way as the in-memory hnswlib/usearch indexes. We don't size new chunks against it,
  // so this estimate is unused in practice.
  size_t EstimateNumVectorsForBytes(size_t bytes_limit) const override {
    return 0;
  }

  DistanceResult Distance(const Vector& lhs, const Vector& rhs) const override {
    const auto& header = index_.header();
    // Compared against distances from other chunks -- VectorLSM scores the mutable chunk's
    // in-flight vectors through it -- so it must be in the metric's own units. That means the
    // rerank encoding and metric where a tier exists: a quantized traversal distance is only
    // comparable within its own chunk.
    //
    // Cold path (SearchExact and tests), so a scratch allocation per call is fine.
    if (header.rerank_kind != vector_index::RerankStorageKind::kNone) {
      const auto kind = vector_index::StorageKindForRerank(header.rerank_kind);
      if (kind == VectorStorageKind::kFloat32) {
        return index_.RerankDistance(
            pointer_cast<const std::byte*>(lhs.data()),
            pointer_cast<const std::byte*>(rhs.data()));
      }
      // No rerank encoding quantizes, so none of them needs the scale.
      const auto bytes = vector_index::CoordinateBytes(kind, header.dimensions);
      std::vector<std::byte> buffer(bytes * 2);
      vector_index::NarrowCoordinates(kind, lhs.data(), header.dimensions, buffer.data());
      vector_index::NarrowCoordinates(
          kind, rhs.data(), header.dimensions, buffer.data() + bytes);
      return index_.RerankDistance(buffer.data(), buffer.data() + bytes);
    }

    if (header.storage_kind == VectorStorageKind::kFloat32) {
      return index_.Distance(
          pointer_cast<const std::byte*>(lhs.data()), pointer_cast<const std::byte*>(rhs.data()));
    }
    // The metric decodes this file's encoding, so full-precision arguments must go through the
    // same narrowing the stored records did.
    const auto bytes = vector_index::CoordinateBytes(header.storage_kind, header.dimensions);
    std::vector<std::byte> buffer(bytes * 2);
    vector_index::NarrowCoordinates(
        header.storage_kind, header.quantization_scale, lhs.data(), header.dimensions,
        buffer.data());
    vector_index::NarrowCoordinates(
        header.storage_kind, header.quantization_scale, rhs.data(), header.dimensions,
        buffer.data() + bytes);
    return index_.Distance(buffer.data(), buffer.data() + bytes);
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
      context->context.search_cache.Release();
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

// Builds metrics from `options` for whichever encoding YbHnsw asks about: what Import is about
// to write, or what Init read from a file's footer.
hnsw::YbHnsw::MetricFactory MakeMetricFactory(const vector_index::HNSWOptions& options) {
  return [options](size_t dimensions, VectorStorageKind storage_kind) {
    return std::make_unique<hnsw::UsearchMetric>(
        options.CreateStoredMetric(dimensions, storage_kind));
  };
}

} // namespace

template <class Vector, class DistanceResult>
Result<vector_index::VectorIndexIfPtr<Vector, DistanceResult>> ImportYbHnsw(
    const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path,
    const hnsw::BlockCachePtr& block_cache) {
  // The usearch index owns its own float32 metric; narrowed storage is not wired through this
  // backend, so the chunk is written and served at full precision.
  auto result = std::make_shared<YbHnswIndex<Vector, DistanceResult>>(
      index.metric(), block_cache);
  RETURN_NOT_OK(result->Import(index, path));
  return result;
}

template <class Vector, class DistanceResult>
Result<vector_index::VectorIndexIfPtr<Vector, DistanceResult>> ImportYbHnsw(
    const hnsw::HnswlibIndex<DistanceResult>& index, const std::string& path,
    const hnsw::BlockCachePtr& block_cache, const vector_index::HNSWOptions& options) {
  auto result = std::make_shared<YbHnswIndex<Vector, DistanceResult>>(
      MakeMetricFactory(options), block_cache);
  RETURN_NOT_OK(result->Import(index, path, options.storage_kind, options.rerank_kind));
  return result;
}

template
Result<vector_index::VectorIndexIfPtr<FloatVector, float>> ImportYbHnsw<FloatVector, float>(
    const unum::usearch::index_dense_gt<vector_index::VectorId>& index, const std::string& path,
    const hnsw::BlockCachePtr& block_cache);

template
Result<vector_index::VectorIndexIfPtr<FloatVector, float>> ImportYbHnsw<FloatVector, float>(
    const hnsw::HnswlibIndex<float>& index, const std::string& path,
    const hnsw::BlockCachePtr& block_cache, const vector_index::HNSWOptions& options);

template <class Vector, class DistanceResult>
vector_index::VectorIndexIfPtr<Vector, DistanceResult> CreateYbHnsw(
    const hnsw::BlockCachePtr& block_cache, const vector_index::HNSWOptions& options) {
  return std::make_shared<YbHnswIndex<Vector, DistanceResult>>(
      MakeMetricFactory(options), block_cache);
}

template
vector_index::VectorIndexIfPtr<FloatVector, float> CreateYbHnsw<FloatVector, float>(
    const hnsw::BlockCachePtr& block_cache, const vector_index::HNSWOptions& options);

} // namespace yb::ann_methods
