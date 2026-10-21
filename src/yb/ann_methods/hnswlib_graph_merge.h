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

#include "yb/util/status.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/hnswlib_include.h"
#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::ann_methods {

/// Links vectors from offset to the end of the graph into the graph of the preceding vectors, as
/// HNSW-Merger does (SIGMOD 2026, "Efficient Vector Index Merging in Vector Databases"): each
/// such vector searches the preceding vectors for ef nearest ones to connect with, and the found
/// vectors get links back to it. The vectors from offset must already be linked into a graph with
/// the entry point source_entry, which becomes the entry point when its graph is taller.
template <vector_index::ValidDistanceResultType DistanceResult>
Status MergeHnswlibGraph(
    hnswlib::HierarchicalNSW<DistanceResult, vector_index::VectorId>& hnsw,
    vector_index::DistanceKind distance_kind, uint32_t offset, uint32_t source_entry, size_t ef,
    const vector_index::ParallelForFunc& parallel_for);

}  // namespace yb::ann_methods
