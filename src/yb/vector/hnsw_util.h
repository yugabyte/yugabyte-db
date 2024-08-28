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

#include <cstdint>
#include <queue>
#include <variant>

#include "yb/util/random_util.h"

#include "yb/vector/distance.h"
#include "yb/vector/graph_repr_defs.h"
#include "yb/common/vector_types.h"

namespace yb::vectorindex {

// Generates a random value as floor(-log(uniform_random) * ml), but clipped at max_level.
// Ignoring the clipping, this corresponds to be a geometric distribution with probability of
// success p = 1 - exp(-1/ml). The average level selected by this function will be 1/p.
//
// Example values:
// 1. If ml = 1/log(2)  (~1.4427), then p = 0.5, and the expected level is 2.
// 2. If ml = 1/log(3)  (~0.9102), then p = 1 - 1/3  (~0.6667), and the expected level is 1.5.
// 3. If ml = 1/log(4)  (~0.7213), then p = 1 - 1/4  (~0.75), and the expected level is ~1.333.
VectorIndexLevel SelectRandomLevel(double ml, VectorIndexLevel max_level);

namespace detail {

struct CompareDistanceForMinHeap {
  bool operator()(const VertexWithDistance& a, const VertexWithDistance& b) {
    return a > b;
  }
};

}  // namespace detail

using MinDistanceQueue =
    std::priority_queue<VertexWithDistance,
                        std::vector<VertexWithDistance>,
                        detail::CompareDistanceForMinHeap>;

// Our default comparator for VertexWithDistance already orders the pairs by increasing distance.
using MaxDistanceQueue =
    std::priority_queue<VertexWithDistance,
                        std::vector<VertexWithDistance>>;

// Drain a max-queue of (vertex, distance) pairs and return a list of VertexWithDistance instances
// ordered by increasing distance.
std::vector<VertexWithDistance> DrainMaxQueueToIncreasingDistanceList(MaxDistanceQueue& queue);

// Computes precise nearest neighbors for the given query by brute force search. In case of
// multiple results having the same distance from the query, results with lower vertex ids are
// preferred.
template<IndexableVectorType Vector>
std::vector<VertexWithDistance> BruteForcePreciseNearestNeighbors(
    const Vector& query,
    const std::vector<VertexId>& vertex_ids,
    const VertexIdToVectorDistanceFunction<Vector>& distance_fn,
    size_t num_results) {
  MaxDistanceQueue queue;
  for (const auto& vertex_id : vertex_ids) {
    auto distance = distance_fn(vertex_id, query);
    auto new_element = VertexWithDistance(vertex_id, distance);
    if (queue.size() < num_results || new_element < queue.top()) {
      // Add a new element if there is a room in the result set, or if the new element is better
      // than the worst element of the result set. The comparsion is done using the (distance,
      // vertex_id) as a lexicographic pair, so we should prefer elements that have the lowest
      // vertex_id among those that have the same distance from the query.
      queue.push(new_element);
    }
    if (queue.size() > num_results) {
      // Always remove the furthest element from the query.
      queue.pop();
    }
  }

  auto result = DrainMaxQueueToIncreasingDistanceList(queue);
  CHECK_GE(result.size(), std::min(vertex_ids.size(), num_results))
      << "Too few records returned by brute-force precise nearest neighbor search on a "
      << "dataset with " << vertex_ids.size() << " vectors. Requested number of results: "
      << num_results << ", returned: " << result.size();

  return result;
}

template<IndexableVectorType Vector>
FloatVector ToFloatVector(const Vector& v) {
  FloatVector fv;
  fv.reserve(v.size());
  for (auto x : v) {
    fv.push_back(static_cast<float>(x));
  }
  return fv;
}

template<IndexableVectorType Vector>
std::vector<FloatVector> ToFloatVectorOfVectors(const std::vector<Vector>& v) {
  std::vector<FloatVector> result;
  result.reserve(v.size());
  for (const auto& subvector : v) {
    result.push_back(ToFloatVector(subvector));
  }
  return result;
}

}  // namespace yb::vectorindex
