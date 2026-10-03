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

#include "yb/rocksdb/cache.h"

#include "yb/util/kv_util.h"
#include "yb/util/polymorphic_iterator.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/tostring.h"

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnsw_options.h"

namespace yb {

class MemTracker;

}

namespace yb::vector_index {

// A single vector index entry: the vector itself and an opaque payload attached to it.
// The payload stores data associated with the vector (currently ybctid of the corresponding row,
// in future it could also contain other columns for covering indexes). Could be empty.
template <IndexableVectorType Vector>
struct VectorIndexEntry {
  VectorId vector_id;
  Vector vector;
  ValueBuffer payload;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(vector_id, vector, payload);
  }
};

// A single entry produced by vector index iteration.
// Unlike VectorIndexEntry does not own the attached payload: the slice points into storage owned
// by the index and is valid only while the iterator it was obtained from is alive.
template <IndexableVectorType Vector>
struct VectorIndexIteratorEntry {
  VectorId vector_id;
  Vector vector;
  Slice payload;

  // Converts to VectorIndexEntry that owns a copy of the payload.
  VectorIndexEntry<Vector> ToOwned() && {
    return {.vector_id = vector_id, .vector = std::move(vector), .payload = ValueBuffer(payload)};
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(vector_id, vector, payload);
  }
};

struct SearchOptions {
  size_t max_num_results;
  size_t ef;
  VectorFilter filter = [](const auto&, const auto&) { return true; };

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(max_num_results, ef);
  }
};

template <IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexReaderIf;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexReaderIf {
 public:
  using SearchResult  = std::vector<VectorWithDistance<DistanceResult>>;
  using IteratorValue = VectorIndexIteratorEntry<Vector>;
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
      size_t num_vectors, size_t max_concurrent_inserts, size_t max_concurrent_reads,
      rocksdb::Cache::ReservationMode reservation_mode) = 0;

  // Returns current number of vectors.
  virtual size_t Size() const = 0;

  // Returns the number of reserved vectors
  virtual size_t Capacity() const = 0;

  // Returns an estimate of the number of vectors that this index implementation will fit into the
  // given amount of memory. The estimate is derived from the underlying library's per-vector
  // memory layout (vector data, neighbor lists at each level, lookup tables, etc.) so that
  // reserving the returned number of vectors is expected to consume approximately bytes_limit
  // bytes of memory.
  virtual size_t EstimateNumVectorsForBytes(size_t bytes_limit) const = 0;

  // Inserts the vector with the specified id and attached payload (could be empty).
  // The payload is stored in the index and returned by search together with the vector id.
  virtual Status Insert(VectorId vector_id, const Vector& vector, Slice payload) = 0;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexIf;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
using VectorIndexIfPtr = std::shared_ptr<VectorIndexIf<Vector, DistanceResult>>;

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
  //
  // On success could return new vector index, that is attached to saved file.
  // Otherwise, returns nullptr, to keep existing index.
  virtual Result<VectorIndexIfPtr<Vector, DistanceResult>> SaveToFile(const std::string& path) = 0;

  // Loads index from the file in immutable state.
  // Implementation could load index partially, fetching data on demand and unload it if necessary.
  // max_concurrent_reads - max number of concurrent reads that could be run against this index.
  virtual Status LoadFromFile(const std::string& path, size_t max_concurrent_reads) = 0;

  // Returns paths of all files that store the index saved at the specified path.
  // Every returned path starts with the specified path.
  virtual std::vector<std::string> StoredFiles(const std::string& path) const {
    return {path};
  }

  // Allows to attach a custom object that will be destroyed when the vector index does. Only one
  // object can be attached. Returns previously attached object or nullptr if nothing was attached.
  // Could be considered as a variation of cleanup paradigm rocskdb::Cleanable.
  virtual std::shared_ptr<void> Attach(std::shared_ptr<void>) = 0;

  virtual ~VectorIndexIf() = default;
};

YB_DEFINE_ENUM(FactoryMode, (kCreate)(kLoad));

// Whether payloads attached to vectors are stored in the created vector index chunk.
// When stored, every inserted vector is expected to have a non-empty payload.
YB_STRONGLY_TYPED_BOOL(StoreVectorPayload);

// Instance independent functionality of a vector index implementation.
// Also acts as the factory for vector index instances.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorIndexTraitsIf {
 public:
  virtual ~VectorIndexTraitsIf() = default;

  // Creates a vector index instance.
  // store_vector_payload only applies to newly created chunks (FactoryMode::kCreate), for loaded
  // chunks it is derived from the stored state.
  virtual VectorIndexIfPtr<Vector, DistanceResult> Create(
      FactoryMode mode, StoreVectorPayload store_vector_payload) const = 0;

  virtual DistanceResult Distance(const Vector& lhs, const Vector& rhs) const = 0;

  // Estimates the number of vectors that fit into the specified amount of memory.
  // Always reflects the in-memory representation used to build chunks, see
  // VectorIndexWriterIf::EstimateNumVectorsForBytes.
  virtual size_t EstimateNumVectorsForBytes(size_t bytes_limit) const = 0;

  // Whether the implementation stores attached vector payloads in a separate payload file next
  // to the index file, because its own format cannot store them. See VectorPayloadMap.
  virtual bool StoresPayloadInSeparateFile() const {
    return false;
  }
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
using VectorIndexTraitsPtr = std::shared_ptr<VectorIndexTraitsIf<Vector, DistanceResult>>;

}  // namespace yb::vector_index
