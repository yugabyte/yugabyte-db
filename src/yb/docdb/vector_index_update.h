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

#include <set>
#include <vector>

#include "yb/docdb/vector_index.h"

#include "yb/rocksdb/write_batch.h"

namespace yb::docdb {

template <class CoordinateType>
class VectorIndexUpdate : public VectorIndexFetcher<CoordinateType> {
 public:
  using Types = VectorIndexTypes<CoordinateType>;
  using IndexedVector = typename Types::IndexedVector;

  explicit VectorIndexUpdate(HybridTime ht, rocksdb::WriteBatch& write_batch,
                             VectorIndexFetcher<CoordinateType>& fetcher)
      : doc_ht_(ht), write_batch_(write_batch), fetcher_(fetcher) {}

  void AddVector(VertexId id, IndexedVector v);
  void DeleteVector(VertexId id);
  void SetNeighbors(VertexId id, VectorIndexLevel level, VectorNodeNeighbors new_neighbors);
  void AddDirectedEdge(VertexId a, VertexId b, VectorIndexLevel level);
  void DeleteDirectedEdge(VertexId a, VertexId b, VectorIndexLevel level);

  Result<IndexedVector> GetVector(
      const ReadOperationData& read_operation_data, VertexId id) override;
  Result<VectorNodeNeighbors> GetNeighbors(
      const ReadOperationData& read_operation_data, VertexId id, VectorIndexLevel level) override;

 private:
  struct IndexedVectorLevelInfo {
    bool overwrite = false;
    VectorNodeNeighbors neighbors;
    VectorNodeNeighbors deleted_neighbors;
  };

  IndexedVectorLevelInfo& GetLevel(VertexId id, VectorIndexLevel level);
  template <class... Subkeys>
  dockv::KeyBytes MakeKey(VertexId id, Subkeys&&... subkeys);

  struct IndexedVectorInfo {
    bool tombstone = false;
    IndexedVector vector;
    std::vector<IndexedVectorLevelInfo> levels;
  };

  DocHybridTime doc_ht_;
  std::unordered_map<VertexId, IndexedVectorInfo> nodes_;
  rocksdb::WriteBatch& write_batch_;
  VectorIndexFetcher<CoordinateType>& fetcher_;
};

using FloatVectorIndexUpdate = VectorIndexUpdate<float>;

}  // namespace yb::docdb
