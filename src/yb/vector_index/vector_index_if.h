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

struct SearchOptions {
  size_t max_num_results;
  size_t ef;
  VectorFilter filter = [](const auto&) { return true; };
};

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexReaderIf;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexReaderIf {
 public:
  using SearchResult  = std::vector<VectorWithDistance<DistanceResult>>;
  using IteratorValue = std::pair<VectorId, Vector>;
  using Iterator      = PolymorphicIterator<IteratorValue>;

  virtual ~VectorIndexReaderIf() = default;
  virtual DistanceResult Distance(const Vector& lhs, const Vector& rhs) const = 0;
  virtual Result<SearchResult> Search(
      const Vector& query_vector, const SearchOptions& options) const = 0;

  // Returns the vector with the given id or NotFound error when vector is not found.
  virtual Result<Vector> GetVector(VectorId vector_id) const = 0;

  virtual std::unique_ptr<AbstractIterator<IteratorValue>> BeginImpl() const = 0;
  virtual std::unique_ptr<AbstractIterator<IteratorValue>> EndImpl()   const = 0;
  virtual std::string IndexStatsStr() const { return "N/A"; }

  Iterator begin() const { return Iterator(BeginImpl()); }
  Iterator end()   const { return Iterator(EndImpl()); }
};

template<IndexableVectorType Vector>
class VectorIndexWriterIf {
 public:
  virtual ~VectorIndexWriterIf() = default;

  // Reserves capacity for this number of vectors.
  virtual Status Reserve(
      size_t num_vectors, size_t max_concurrent_inserts, size_t max_concurrent_reads) = 0;

  // Returns current number of vectors.
  virtual size_t Size() const = 0;

  // Returns the number of reserved vectors
  virtual size_t Capacity() const = 0;

  virtual Status Insert(VectorId vector_id, const Vector& vector) = 0;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexIf : public VectorIndexReaderIf<Vector, DistanceResult>,
                      public VectorIndexWriterIf<Vector> {
 public:
  using VectorType = Vector;
  using DistanceResultType = DistanceResult;

  // Returns the number of dimensions per vector;
  virtual size_t Dimensions() const = 0;

  // Saves index to the file, switching it to immutable state.
  // Implementation could partially unload index and load it on demand from this file.
  virtual Status SaveToFile(const std::string& path) = 0;

  // Loads index from the file in immutable state.
  // Implementation could load index partially, fetching data on demand and unload it if necessary.
  // max_concurrent_reads - max number of concurrent reads that could be run against this index.
  virtual Status LoadFromFile(const std::string& path, size_t max_concurrent_reads) = 0;

  // Allows to attach a custom object that will be destroyed when the vector index does. Only one
  // object can be attached. Returns previously attached object or nullptr if nothing was attached.
  // Could be considered as a variation of cleanup paradigm rocskdb::Cleanable.
  virtual std::shared_ptr<void> Attach(std::shared_ptr<void>) = 0;

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
