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

#pragma once

#include <cstddef>
#include <memory>

#include "yb/util/status.h"

#include "yb/vector/distance.h"
#include "yb/vector/hnsw_options.h"
#include "yb/vector/coordinate_types.h"
#include "yb/vector/vector_index_if.h"

// Derived from the available add() overloads in index_dense.hpp.
#define YB_USEARCH_SUPPORTED_COORDINATE_TYPES \
    (float) /* NOLINT */  \
    (double) /* NOLINT */ \
    (int8_t)

namespace yb::vectorindex {

template<IndexableVectorType Vector>
class UsearchIndex : public VectorIndexReaderIf<Vector>, public VectorIndexWriterIf<Vector> {
 public:
  explicit UsearchIndex(const HNSWOptions& options);
  virtual ~UsearchIndex();

  void Reserve(size_t num_vectors) override;

  Status Insert(VertexId vertex_id, const Vector& vector) override;

  std::vector<VertexWithDistance> Search(
      const Vector& query_vector, size_t max_num_results) const override;

  Vector GetVector(VertexId vertex_id) const override;

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

// Maps the given coordinate kind to a type supported by the usearch.
CoordinateKind UsearchSupportedCoordinateKind(CoordinateKind coordinate_kind);

}  // namespace yb::vectorindex
