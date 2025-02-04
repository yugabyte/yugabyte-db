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

#include "yb/vector_index/benchmark_data.h"
#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

using PrecomputedGroundTruthMatrix = std::vector<std::vector<VectorId>>;

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
      const std::vector<VectorId>& vertex_ids);

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
      const std::vector<VectorId>& correct_result,
      AtomicUInt64Vector& total_overlap_counters);

  // This works on queries convertered from input vector io indexed vector format. Only uses up to
  // k_ first elements of precomputed_correct_results.
  std::vector<VectorWithDistance<DistanceResult>> AugmentWithDistancesAndTrimToK(
      const std::vector<VectorId>& precomputed_correct_results, const Vector& converted_query);

  VertexIdToVectorDistanceFunction<Vector, DistanceResult> distance_fn_;
  size_t k_;
  const std::vector<Vector>& queries_;
  const PrecomputedGroundTruthMatrix& precomputed_ground_truth_;
  bool validate_precomputed_ground_truth_;
  const IndexReader& index_reader_;
  const std::vector<VectorId>& vertex_ids_;
};

}  // namespace yb::vector_index
