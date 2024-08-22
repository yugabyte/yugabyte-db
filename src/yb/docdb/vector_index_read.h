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

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/vector_index.h"

namespace yb::docdb {

template <class CoordinateType>
class VectorIndexStorage : public VectorIndexFetcher<CoordinateType> {
 public:
  using typename VectorIndexFetcher<CoordinateType>::IndexedVector;

  explicit VectorIndexStorage(const DocDB& doc_db)
      : doc_db_(doc_db) {}

  Result<IndexedVector> GetVector(
      const ReadOperationData& read_operation_data, VertexId id) override;
  Result<VectorNodeNeighbors> GetNeighbors(
      const ReadOperationData& read_operation_data, VertexId id, VectorIndexLevel level) override;
 private:
  const DocDB doc_db_;
};

using FloatVectorIndexStorage = VectorIndexStorage<float>;

}  // namespace yb::docdb
