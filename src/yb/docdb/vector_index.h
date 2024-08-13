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

#include "yb/dockv/key_bytes.h"
#include "yb/dockv/primitive_value.h"

namespace yb::docdb {

using VertexId = uint64_t;
using VectorIndexLevel = uint8_t;
using VectorNodeNeighbors = std::set<VertexId>;
constexpr VertexId kInvalidVertexId = 0;

template <class CoordinateType>
struct VectorIndexTypes {
  using IndexedVector = std::vector<CoordinateType>;
};

template <class CoordinateType>
class VectorIndexFetcher {
 public:
  using Types = VectorIndexTypes<CoordinateType>;
  using IndexedVector = typename Types::IndexedVector;

  virtual Result<IndexedVector> GetVector(
      const ReadOperationData& read_operation_data, VertexId id) = 0;
  virtual Result<VectorNodeNeighbors> GetNeighbors(
      const ReadOperationData& read_operation_data, VertexId id, VectorIndexLevel level) = 0;
  virtual ~VectorIndexFetcher() = default;
};

namespace detail {

void AppendSubkey(dockv::KeyBytes& key, VectorIndexLevel level);
void AppendSubkey(dockv::KeyBytes& key, VertexId id);

inline void AppendSubkeys(dockv::KeyBytes& key) {}

template <class T, class... Subkeys>
void AppendSubkeys(dockv::KeyBytes& key, const T& t, Subkeys&&... subkeys) {
  AppendSubkey(key, t);
  AppendSubkeys(key, std::forward<Subkeys>(subkeys)...);
}

} // namespace detail

template <class... Subkeys>
dockv::KeyBytes MakeVectorIndexKey(VertexId id, Subkeys&&... subkeys) {
  dockv::KeyBytes key;
  auto key_entry_value = dockv::KeyEntryValue::VectorVertexId(id);
  key_entry_value.AppendToKey(&key);
  key.AppendGroupEnd();
  detail::AppendSubkeys(key, std::forward<Subkeys>(subkeys)...);
  return key;
}

}  // namespace yb::docdb
