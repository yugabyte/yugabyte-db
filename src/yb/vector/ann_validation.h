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
#include "yb/vector/distance.h"
#include "yb/vector/vector_index_if.h"

namespace yb::vectorindex {

using PrecomputedGroundTruthMatrix = std::vector<std::vector<VertexId>>;

// For such vectors of atomics to be effective, they should be aligned to cache lines. However, this
// is only used during multi-threaded validation, and we believe the contention on these counters is
// only a minor part of the overall time spent querying the vector index.
using AtomicUInt64Vector = std::vector<std::atomic<uint64_t>>;

// Computes ground truth of approximate nearest neighbor search. Parameterized by the query.
class GroundTruth {
 public:
  GroundTruth(
      const VertexIdToVectorDistanceFunction& distance_fn,
      size_t k,
      const std::vector<FloatVector>& queries,
      const PrecomputedGroundTruthMatrix& precomputed_ground_truth,
      bool validate_precomputed_ground_truth,
      const VectorIndexReader& index_reader,
      const std::vector<VertexId>& vertex_ids);

  // Returns a vector with i-recall@k stored at each element with index i - 1.
  // i@k recall means we execute ANN search to get top k results and check what percentage of the
  // true i results were included in that list, averaged over all queries.
  // precomputed_ground_truth is the ground truth loaded from a provided file, or it could be an
  // empty vector, if not available.
  Result<FloatVector> EvaluateRecall(size_t num_threads);

 private:
  // Process a single query and increment stats of correct result vs. returned result overlap for
  // various sizes of the result set. Invoked from multiple threads once for each query index.
  Status ProcessQuery(
      size_t query_index,
      AtomicUInt64Vector& total_overlap_counters);

  void DoApproxSearchAndUpdateStats(
      const FloatVector& query,
      const std::vector<VertexId>& correct_result,
      AtomicUInt64Vector& total_overlap_counters);

  VerticesWithDistances AugmentWithDistances(
      const std::vector<VertexId>& vertex_ids, const FloatVector& query);

  VertexIdToVectorDistanceFunction distance_fn_;
  size_t k_;
  const std::vector<FloatVector>& queries_;
  const PrecomputedGroundTruthMatrix& precomputed_ground_truth_;
  bool validate_precomputed_ground_truth_;
  const VectorIndexReader& index_reader_;
  const std::vector<VertexId>& vertex_ids_;
};

// Checks if the given two result sets are non-contradictory. This means that consecutive items
// with the same distance to the query could be ordered in different ways, and that at the end of
// the result set, we might have different vertex ids altogether provided that they all have the
// same distance to the query. This corresponds to a situation when a group of items with the same
// distance to the query, ordered differently in the two result sets, was cut in the middle by the
// result set boundary.
bool ResultSetsEquivalent(const VerticesWithDistances& a, const VerticesWithDistances& b);

std::string ResultSetDifferenceStr(const VerticesWithDistances& a, const VerticesWithDistances& b);

}  // namespace yb::vectorindex
