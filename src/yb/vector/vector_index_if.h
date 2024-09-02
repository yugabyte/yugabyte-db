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

#include "yb/common/vector_types.h"

#include "yb/vector/distance.h"
#include "yb/vector/coordinate_types.h"

#pragma once

namespace yb::vectorindex {

template<IndexableVectorType Vector>
class VectorIndexReaderIf {
 public:
  virtual ~VectorIndexReaderIf() = default;

  virtual std::vector<VertexWithDistance> Search(
      const Vector& query_vector, size_t max_num_results) const = 0;
};

template<IndexableVectorType Vector>
class VectorIndexWriterIf {
 public:
  virtual ~VectorIndexWriterIf() = default;

  // Reserves capacity for this number of vectors.
  virtual void Reserve(size_t num_vectors) = 0;

  virtual Status Insert(VertexId vertex_id, const Vector& vector) = 0;

  // Returns the vector with the given id, or an empty vector if it does not exist.
  virtual Vector GetVector(VertexId vertex_id) const = 0;
};

using FloatVectorIndexReader = VectorIndexReaderIf<FloatVector>;

}  // namespace yb::vectorindex
