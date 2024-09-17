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

// This file contains definitions needed to represent HNSW-style multi-level graphs in DocDB,
// such as vertex id, level, and a representation of a list of a node's neighbors.

#pragma once

#include <cstdint>
#include <set>

namespace yb::vectorindex {

// Vertex id is a unique identifier of a vector (unique inside a particular vector index table).
// A value of a vertex id never gets reused, even if the same vector is deleted and re-inserted
// later.
using VertexId = uint64_t;

constexpr VertexId kInvalidVertexId = 0;

template<typename T>
concept VertexIdCompatible = std::is_unsigned_v<T> && std::is_integral_v<T> && sizeof(T) == 8;

using VectorIndexLevel = uint8_t;
using VectorNodeNeighbors = std::set<VertexId>;

}  // namespace yb::vectorindex
