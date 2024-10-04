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

// Facilities for validating approximate nearest neighbor search results.

#pragma once

#include <atomic>
#include <functional>
#include <vector>

#include "yb/util/status.h"

#include "yb/common/vector_types.h"

#include "yb/vector/benchmark_data.h"
#include "yb/vector/coordinate_types.h"
#include "yb/vector/distance.h"
#include "yb/vector/vector_index_if.h"

namespace yb::vectorindex {

using PrecomputedGroundTruthMatrix = std::vector<std::vector<VertexId>>;

// For such vectors of atomics to be effective, they should be aligned to cache lines. However, this
// is only used during multi-threaded validation, and we believe the contention on these counters is
// only a minor part of the overall time spent querying the vector index.
using AtomicUInt64Vector = std::vector<std::atomic<uint64_t>>;

// Computes ground truth of approximate nearest neighbor search.
template<IndexableVectorType Vector,
         ValidDistanceResultType DistanceResult>
class GroundTruth {
 public:
  using IndexReader = VectorIndexReaderIf<Vector, DistanceResult>;

  GroundTruth(
      const VertexIdToVectorDistanceFunction<Vector, DistanceResult>& distance_fn,
      size_t k,
      const std::vector<Vector>& queries,
      const PrecomputedGroundTruthMatrix& precomputed_ground_truth,
      bool validate_precomputed_ground_truth,
      const IndexReader& index_reader,
      const std::vector<VertexId>& vertex_ids);

  // Returns a vector with i-recall@k stored at each element with index i - 1.
  // i@k recall means we execute ANN search to get top k results and check what percentage of the
  // true i results were included in that list, averaged over all queries.
  // precomputed_ground_truth is the ground truth loaded from a provided file, or it could be an
  // empty vector, if not available.
  //
  // The result is always a FloatVector -- it is a vector of recall values.
  Result<FloatVector> EvaluateRecall(size_t num_threads);

 private:
  // Process a single query and increment stats of correct result vs. returned result overlap for
  // various sizes of the result set. Invoked from multiple threads once for each query index.
  Status ProcessQuery(
      size_t query_index,
      AtomicUInt64Vector& total_overlap_counters);

  void DoApproxSearchAndUpdateStats(
      const Vector& query,
      const std::vector<VertexId>& correct_result,
      AtomicUInt64Vector& total_overlap_counters);

  // This works on queries convertered from input vector io indexed vector format.
  VerticesWithDistances<DistanceResult> AugmentWithDistances(
      const std::vector<VertexId>& vertex_ids, const Vector& converted_query);

  VertexIdToVectorDistanceFunction<Vector, DistanceResult> distance_fn_;
  size_t k_;
  const std::vector<Vector>& queries_;
  const PrecomputedGroundTruthMatrix& precomputed_ground_truth_;
  bool validate_precomputed_ground_truth_;
  const IndexReader& index_reader_;
  const std::vector<VertexId>& vertex_ids_;
};

// Checks if the given two result sets are non-contradictory. This means that consecutive items
// with the same distance to the query could be ordered in different ways, and that at the end of
// the result set, we might have different vertex ids altogether provided that they all have the
// same distance to the query. This corresponds to a situation when a group of items with the same
// distance to the query, ordered differently in the two result sets, was cut in the middle by the
// result set boundary.
template<ValidDistanceResultType DistanceResult>
bool ResultSetsEquivalent(const VerticesWithDistances<DistanceResult>& a,
                          const VerticesWithDistances<DistanceResult>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  const auto k = a.size();
  bool matches = true;
  for (size_t i = 0; i < k; ++i) {
    if (a[i] != b[i]) {
      matches = false;
      break;
    }
  }
  if (matches) {
    return true;
  }

  // Sort both result sets by increasing distance, and for the same distance, increasing vertex id.
  auto a_sorted = a;
  std::sort(a_sorted.begin(), a_sorted.end());
  auto b_sorted = b;
  std::sort(b_sorted.begin(), b_sorted.end());

  size_t discrepancy_index = k;
  for (size_t i = 0; i < k; ++i) {
    if (a_sorted[i] != b_sorted[i]) {
      discrepancy_index = i;
      break;
    }
  }
  if (discrepancy_index == k) {
    // The arrays became the same after sorting.
    return true;
  }

  // We allow a situation where vertex ids are different as long as distances are the same until
  // the end of both result sets. In that case we still consider the two result sets equivalent.
  float expected_distance = a_sorted[discrepancy_index].distance;
  for (size_t i = discrepancy_index; i < k; ++i) {
    float a_dist = a_sorted[i].distance;
    float b_dist = b_sorted[i].distance;
    if (a_dist != expected_distance && b_dist != expected_distance) {
      return false;
    }
  }
  return true;
}

template<ValidDistanceResultType DistanceResult>
std::string ResultSetDifferenceStr(const VerticesWithDistances<DistanceResult>& a,
                                   const VerticesWithDistances<DistanceResult>& b) {
  if (a.size() != b.size()) {
    // This should not occur, so no details here.
    return Format("Result set size: $0 vs. $1", a.size(), b.size());
  }
  const size_t k = a.size();

  auto a_sorted = a;
  std::sort(a_sorted.begin(), a_sorted.end());
  auto b_sorted = b;
  std::sort(b_sorted.begin(), b_sorted.end());

  std::ostringstream diff_str;

  bool found_differences = false;
  for (size_t j = 0; j < k; ++j) {
    if (a_sorted[j] != b_sorted[j]) {
      if (found_differences) {
        diff_str << "\n";
      }
      found_differences = true;
      diff_str << "    " << a_sorted[j].ToString() << " vs. " << b_sorted[j].ToString();
    }
  }
  if (found_differences) {
    return diff_str.str();
  }
  return "No differences";
}

}  // namespace yb::vectorindex
