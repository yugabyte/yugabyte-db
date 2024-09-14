//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include <queue>

#include "yb/common/vector_types.h"

#include "yb/rocksdb/status.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"

#include "yb/vector/coordinate_types.h"
#include "yb/vector/distance.h"

namespace yb::vectorindex {

// A simple struct to hold a DocKey that's stored in the value of a vectorann entry and its distance
class DocKeyWithDistance {
 public:
  Slice dockey_;
  double distance_;

  // Constructor
  DocKeyWithDistance(const Slice& val, double dist) : dockey_(val), distance_(dist) {}

  int Compare(const DocKeyWithDistance& other) const {
    if (distance_ != other.distance_) {
      return distance_ < other.distance_ ? -1 : 1;
    }

    return dockey_.compare(other.dockey_);
  }

  // Comparator for sorting:
  // We want the last element output to be the one with the largest distance.
  // If all distances are equal we want the last element to be the one
  // with the largest value.
  bool operator<(const DocKeyWithDistance& other) const { return Compare(other) < 0; }

  bool operator>(const DocKeyWithDistance& other) const { return Compare(other) > 0; }
};

// Our default comparator for VertexWithDistance already orders the pairs by increasing distance.
template<ValidDistanceResultType DistanceResult>
using MaxDistanceQueue =
    std::priority_queue<VertexWithDistance<DistanceResult>,
                        std::vector<VertexWithDistance<DistanceResult>>>;


// Drain a max-queue of (vertex, distance) pairs and return a list of VertexWithDistance instances
// ordered by increasing distance.
template<ValidDistanceResultType DistanceResult>
auto DrainMaxQueueToIncreasingDistanceList(MaxDistanceQueue<DistanceResult>& queue) {
  std::vector<VertexWithDistance<DistanceResult>> result_list;
  while (!queue.empty()) {
    result_list.push_back(queue.top());
    queue.pop();
  }
  // results is a max-heap, so we've got a list going from furthest to closest vector to the query.
  // What we need is a list from lowest to highest distance.
  std::reverse(result_list.begin(), result_list.end());
  return result_list;
}

// Computes precise nearest neighbors for the given query by brute force search. In case of
// multiple results having the same distance from the query, results with lower vertex ids are
// preferred.
template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
std::vector<VertexWithDistance<DistanceResult>> BruteForcePreciseNearestNeighbors(
    const Vector& query,
    const std::vector<VertexId>& vertex_ids,
    const VertexIdToVectorDistanceFunction<Vector, DistanceResult>& distance_fn,
    size_t num_results) {
  if (num_results == 0) {
    return {};
  }
  MaxDistanceQueue<DistanceResult> queue;
  for (const auto& vertex_id : vertex_ids) {
    auto distance = distance_fn(vertex_id, query);
    auto new_element = VertexWithDistance<DistanceResult>(vertex_id, distance);
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

}  // namespace yb::vectorindex
