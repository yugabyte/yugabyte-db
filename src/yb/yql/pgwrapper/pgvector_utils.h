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

#include "yb/vector_index/distance.h"

namespace yb::pgwrapper {

// Returns the pgvector operator-class name (e.g. "vector_l2_ops") for the given DistanceKind.
// Suitable for `CREATE INDEX ... USING ybhnsw (embedding <ops_name>)`.
const char* VectorOpsName(vector_index::DistanceKind distance_kind);

// Returns the pgvector distance operator (e.g. "<->") for the given DistanceKind.
// Suitable for `ORDER BY embedding <op> '<query_vec>'::vector` queries.
const char* DistanceOpName(vector_index::DistanceKind distance_kind);

}  // namespace yb::pgwrapper
