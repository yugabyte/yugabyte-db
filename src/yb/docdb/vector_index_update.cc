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

#include "yb/docdb/vector_index_update.h"

#include "yb/dockv/doc_key.h"

#include "yb/util/decimal.h"

namespace yb::docdb {

template <class CoordinateType>
void VectorIndexUpdate<CoordinateType>::AddVector(VertexId id, IndexedVector vector) {
  write_batch_.Put(MakeKey(id).AsSlice(), dockv::PrimitiveValue::Encoded(vector).AsSlice());
  nodes_[id].vector = std::move(vector);
}

template <class CoordinateType>
void VectorIndexUpdate<CoordinateType>::DeleteVector(yb::docdb::VertexId id) {
  write_batch_.Put(MakeKey(id).AsSlice(), dockv::PrimitiveValue::TombstoneSlice());
  nodes_[id].tombstone = true;
}

template <class CoordinateType>
void VectorIndexUpdate<CoordinateType>::SetNeighbors(
    VertexId id, VectorIndexLevel level, VectorNodeNeighbors new_neighbors) {
  write_batch_.Put(
      MakeKey(id, level),
      dockv::PrimitiveValue::Encoded(
          dockv::UInt64Vector{new_neighbors.begin(), new_neighbors.end()}).AsSlice());

  GetLevel(id, level).neighbors = std::move(new_neighbors);
}

template <class CoordinateType>
void VectorIndexUpdate<CoordinateType>::AddDirectedEdge(
    VertexId a, VertexId b, VectorIndexLevel level) {
  write_batch_.Put(MakeKey(a, level, b), dockv::PrimitiveValue::NullSlice());

  auto& vector_info = GetLevel(a, level);
  vector_info.neighbors.insert(b);
  vector_info.deleted_neighbors.erase(b);
}

template <class CoordinateType>
void VectorIndexUpdate<CoordinateType>::DeleteDirectedEdge(
    VertexId a, VertexId b, VectorIndexLevel level) {
  write_batch_.Put(MakeKey(a, level, b), dockv::PrimitiveValue::TombstoneSlice());

  auto& vector_info = GetLevel(a, level);
  vector_info.neighbors.erase(b);
  vector_info.deleted_neighbors.insert(b);
}

template <class CoordinateType>
auto VectorIndexUpdate<CoordinateType>::GetLevel(VertexId id, VectorIndexLevel level) ->
    VectorIndexUpdate<CoordinateType>::IndexedVectorLevelInfo& {
  auto& node = nodes_[id];
  if (level >= node.levels.size()) {
    node.levels.resize(level + 1);
  }
  return node.levels[level];
}

namespace {

void AppendSubkeys(dockv::KeyBytes& key) {
}

void AppendSubkey(dockv::KeyBytes& key, VectorIndexLevel level) {
  key.AppendKeyEntryType(dockv::KeyEntryType::kUInt32);
  key.AppendUInt32(level);
}

void AppendSubkey(dockv::KeyBytes& key, VertexId id) {
  key.AppendKeyEntryType(dockv::KeyEntryType::kUInt64);
  key.AppendUInt64(id);
}

template <class T, class... Subkeys>
void AppendSubkeys(dockv::KeyBytes& key, const T& t, Subkeys&&... subkeys) {
  AppendSubkey(key, t);
  AppendSubkeys(key, std::forward<Subkeys>(subkeys)...);
}

} // namespace

template <class CoordinateType>
template <class... Subkeys>
dockv::KeyBytes VectorIndexUpdate<CoordinateType>::MakeKey(VertexId id, Subkeys&&... subkeys) {
  dockv::KeyBytes key;
  auto key_entry_value = dockv::KeyEntryValue::VectorVertexId(id);
  key_entry_value.AppendToKey(&key);
  key.AppendGroupEnd();
  AppendSubkeys(key, std::forward<Subkeys>(subkeys)...);
  key.AppendKeyEntryType(dockv::KeyEntryType::kHybridTime);
  key.AppendHybridTime(doc_ht_);

  doc_ht_.IncrementWriteId();

  return key;
}

template class VectorIndexUpdate<float>;

}  // namespace yb::docdb
