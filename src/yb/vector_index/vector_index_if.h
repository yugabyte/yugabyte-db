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

// Interface definitions for a vector index.

#pragma once

#include "yb/util/polymorphic_iterator.h"
#include "yb/util/result.h"

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_options.h"

namespace yb::vector_index {

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexReaderIf;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexReaderIf {
 public:
  using SearchResult = std::vector<VertexWithDistance<DistanceResult>>;
  using IteratorValueType = std::pair<Vector, VertexId>;
  using Iterator = yb::PolymorphicIterator<IteratorValueType>;

  virtual ~VectorIndexReaderIf() = default;
  virtual DistanceResult Distance(const Vector& lhs, const Vector& rhs) const = 0;
  virtual SearchResult Search(const Vector& query_vector, size_t max_num_results) const = 0;

  virtual std::unique_ptr<yb::AbstractIterator<IteratorValueType>> BeginImpl() const = 0;
  virtual std::unique_ptr<yb::AbstractIterator<IteratorValueType>> EndImpl() const = 0;
  virtual std::string IndexStatsStr() const { return "N/A"; }

  Iterator begin() const { return Iterator(BeginImpl()); }
  Iterator end() const { return Iterator(EndImpl()); }
};

template<IndexableVectorType Vector>
class VectorIndexWriterIf {
 public:
  virtual ~VectorIndexWriterIf() = default;

  // Reserves capacity for this number of vectors.
  virtual Status Reserve(size_t num_vectors) = 0;

  virtual Status Insert(VertexId vertex_id, const Vector& vector) = 0;

  // Returns the vector with the given id, an empty vector if such VertexId does not exist, or
  // a non-OK status if an error occurred.
  virtual Result<Vector> GetVector(VertexId vertex_id) const = 0;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexIf : public VectorIndexReaderIf<Vector, DistanceResult>,
                      public VectorIndexWriterIf<Vector> {
 public:
  using VectorType = Vector;
  using DistanceResultType = DistanceResult;

  // Saves index to the file, switching it to immutable state.
  // Implementation could partially unload index and load it on demand from this file.
  virtual Status SaveToFile(const std::string& path) = 0;

  // Loads index from the file in immutable state.
  // Implementation could load index partially, fetching data on demand and unload it if necessary.
  virtual Status LoadFromFile(const std::string& path) = 0;

  virtual ~VectorIndexIf() = default;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
using VectorIndexIfPtr = std::shared_ptr<VectorIndexIf<Vector, DistanceResult>>;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
using VectorIndexFactory = std::function<VectorIndexIfPtr<Vector, DistanceResult>()>;

template<class Index>
auto CreateIndexFactory(const HNSWOptions& options) {
  return [options]() {
    return std::make_shared<Index>(options);
  };
}

}  // namespace yb::vector_index
