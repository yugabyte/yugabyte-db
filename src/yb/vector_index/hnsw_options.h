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

#include <string>

#include "yb/vector_index/distance.h"

namespace unum::usearch {
class metric_punned_t;
}

namespace yb::vector_index {

struct HNSWOptions {
  size_t dimensions = 0;

  bool extend_candidates = false;
  bool keep_pruned_connections = false;

  // M in the paper, connectivity in usearch.
  size_t num_neighbors_per_vertex = 16;

  size_t max_neighbors_per_vertex = 32;

  // M0 in the paper, connectivity_base in usearch.
  size_t num_neighbors_per_vertex_base = 32;

  size_t max_neighbors_per_vertex_base = 64;

  // The "expansion" parameter during graph construction. The maximum number of results the
  // algorithm maintains internally while constructing the candidate list for the new node's
  // neighbors. This is expansion_add in usearch.
  size_t ef_construction = 128;

  // This is the "expansion" parameter for search. E.g. if we request top 10 elements, we will
  // internally still maintain this many results during the search. But if we request more than
  // this many results, the requested number of results supersedes this parameter, of course.
  // This is expansion_search in usearch.
  size_t ef = 64;

  // The alpha parameter used in the RobustPrune method of DiskANN. HNSW's neighbor selection
  // heuristic is very simimlar to DiskANN's RobustPrune and we extend it with this additional
  // parameter.
  //
  // This is not used by usearch.
  float robust_prune_alpha = 1.0;

  DistanceKind distance_kind = DistanceKind::kL2Squared;

  std::string ToString() const;
  template <class Vector>
  unum::usearch::metric_punned_t CreateMetric() const;
};

}  // namespace yb::vector_index
