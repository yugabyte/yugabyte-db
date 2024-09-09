//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

#include "yb/gutil/macros.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"

#include "yb/common/vector_types.h"

#include "yb/vector/coordinate_types.h"
#include "yb/vector/vectorann_util.h"

#include "yb/rocksdb/status.h"

namespace yb::vectorindex {

// This MUST match the Vector struct definition in
// src/postgres/third-party-extensions/pgvector/src/vector.h.
struct YSQLVector {
  // Commented out as this field is not transferred over the wire for all
  // Varlens.
  // int32  vl_len_;    /* varlena header (do not touch directly!) */
  int16_t dim; /* number of dimensions */
  int16_t unused;
  float elems[];

 private:
  DISALLOW_COPY_AND_ASSIGN(YSQLVector);
};


// Base class for all ANN vector indexes.

// The paging state of an ANN index's iterator must consist of the distance from the query
// vector we are currently iterating through and the ybctid of the next vector we intend on
// on seeing. We must maintain the ybctid part of this paging state in case there are many
// vectors with the same distance from the query vector at a paging boundary.
class ANNPagingState {
 public:
  ANNPagingState() : valid_(false), distance_(0.0), main_key_() {}

  ANNPagingState(double distance, const Slice& main_key)
      : valid_(true), distance_(distance), main_key_(main_key) {}

  bool valid() const { return valid_; }
  const Slice& main_key() const { return main_key_; }
  double distance() const { return distance_; }

 private:
  bool valid_;
  double distance_;
  Slice main_key_;
};

template <IndexableVectorType Vector>
class VectorANN {
 public:
  explicit VectorANN(uint32_t dims) {}

  // Creates a copy of the supplied vector and stores it internally.
  virtual void Add(const Vector& vector, Slice val) = 0;

  virtual std::vector<DocKeyWithDistance> GetTopKVectors(
      Vector query_vec, size_t k, double lb_distance, Slice lb_key, bool is_lb_inclusive) const = 0;

  static Result<Vector> GetVectorFromYSQLWire(const YSQLVector& ysql_vector, size_t total_len);

  // static Vector GetVector(VectorSlice vec_ref) { return pointer_cast<Vector>(vec_ref.data()); }

  virtual ~VectorANN() = default;
};

template <IndexableVectorType Vector>
class VectorANNFactory {
 public:
  VectorANNFactory() {}
  virtual ~VectorANNFactory() {}
  virtual std::unique_ptr<VectorANN<Vector>> Create(uint32_t dims) = 0;
};

template <IndexableVectorType Vector>
class DummyANNFactory final : public VectorANNFactory<Vector> {
 public:
  ~DummyANNFactory() {}
  std::unique_ptr<VectorANN<Vector>> Create(uint32_t dims) override;
};

}  // namespace yb::vectorindex
