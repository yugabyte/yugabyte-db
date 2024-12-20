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

#include <execution>
#include <future>
#include <algorithm>
#include <unordered_set>

#include "yb/util/status.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/ann_validation.h"
#include "yb/vector_index/hnsw_util.h"
#include "yb/vector_index/vectorann_util.h"

namespace yb::vector_index {

namespace {

template<ValidDistanceResultType DistanceResult>
using VerticesWithDistances = std::vector<VertexWithDistance<DistanceResult>>;

template<ValidDistanceResultType DistanceResult>
std::vector<VectorId> VertexIdsOnly(
    const VerticesWithDistances<DistanceResult>& vertices_with_distances) {
  std::vector<VectorId> result;
  result.reserve(vertices_with_distances.size());
  for (const auto& v_dist : vertices_with_distances) {
    result.push_back(v_dist.vertex_id);
  }
  return result;
}

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

} // namespace

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
GroundTruth<Vector, DistanceResult>::GroundTruth(
    const VertexIdToVectorDistanceFunction<Vector, DistanceResult>& distance_fn,
    size_t k,
    const std::vector<Vector>& queries,
    const PrecomputedGroundTruthMatrix& precomputed_ground_truth,
    bool validate_precomputed_ground_truth,
    const IndexReader& index_reader,
    const std::vector<VectorId>& vertex_ids)
    : distance_fn_(distance_fn),
      k_(k),
      queries_(queries),
      precomputed_ground_truth_(precomputed_ground_truth),
      validate_precomputed_ground_truth_(validate_precomputed_ground_truth),
      index_reader_(index_reader),
      vertex_ids_(vertex_ids) {
  if (!precomputed_ground_truth_.empty()) {
    CHECK_EQ(precomputed_ground_truth_.size(), queries_.size());
  }
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<FloatVector> GroundTruth<Vector, DistanceResult>::EvaluateRecall(
    size_t num_threads) {
  SCHECK_GE(num_threads, static_cast<size_t>(1),
            InvalidArgument, "Number of threads must be at least 1");

  // For each index i we maintain the total overlap (set intersection cardinality) between the
  // top (i + 1) ground truth results and the top (i + 1) ANN results, computed across all queries.
  // We apply denominators at the end.
  AtomicUInt64Vector total_overlap(k_);
  for (size_t i = 0; i < k_; ++i) {
    total_overlap[i].store(0);
  }

  RETURN_NOT_OK(ProcessInParallel(
      num_threads,
      /* start_index= */ static_cast<size_t>(0),
      /* end_index_exclusive= */ queries_.size(),
      [this, &total_overlap](size_t query_index) -> Status {
        return ProcessQuery(query_index, total_overlap);
      }));

  // The result is always a vector of floats, regardless of the coordinate type.
  FloatVector result;

  result.resize(k_);
  for (size_t i = 1; i <= k_; ++i) {
    // In the denominator below, we average across all queries. But we also need to divide by the
    // size of the result set of the precise top-i query, which would be i in most cases, except
    // in the degenerate case of a really small input dataset (should never occur in practice).
    result[i - 1] = total_overlap[i - 1].load(std::memory_order_acquire) * 1.0 /
        (queries_.size() * std::min(vertex_ids_.size(), i));
  }
  return result;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status GroundTruth<Vector, DistanceResult>::ProcessQuery(
    size_t query_index,
    AtomicUInt64Vector& total_overlap_counters) {
  auto& query = queries_[query_index];
  if (!precomputed_ground_truth_.empty() && !validate_precomputed_ground_truth_) {
    // Fast path: no need to compute precise results at all. Just use the precomputed ground truth.
    DoApproxSearchAndUpdateStats(
        query, precomputed_ground_truth_[query_index], total_overlap_counters);
    return Status::OK();
  }

  // At this point, we need to compute precise results either because precomputed results are not
  // available, or because we want to validate those precomputed results.
  auto our_correct_top_k = BruteForcePreciseNearestNeighbors<Vector, DistanceResult>(
      query, vertex_ids_, distance_fn_, k_);

  if (!precomputed_ground_truth_.empty() && validate_precomputed_ground_truth_) {
    // Compare the ground truth we've just computed to the the precomputed ground truth.
    const auto& precomputed_correct_results = precomputed_ground_truth_[query_index];
    SCHECK_GE(
        precomputed_correct_results.size(), k_, IllegalState,
        "Precomputed ground truth vector has too few elements.");
    auto precomputed_top_k_with_distance =
        AugmentWithDistancesAndTrimToK(precomputed_correct_results, query);
    if (!ResultSetsEquivalent(our_correct_top_k, precomputed_top_k_with_distance)) {
      return STATUS_FORMAT(
          IllegalState,
          "Precomputed ground truth does not match freshly computed ground truth for query "
          "#$0. Differences (computed by us vs. precomputed):\n$1",
          query_index,
          ResultSetDifferenceStr(our_correct_top_k, precomputed_top_k_with_distance));
    }
  }

  DoApproxSearchAndUpdateStats(query, VertexIdsOnly(our_correct_top_k), total_overlap_counters);

  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
std::vector<VertexWithDistance<DistanceResult>>
GroundTruth<Vector, DistanceResult>::AugmentWithDistancesAndTrimToK(
    const std::vector<VectorId>& precomputed_correct_results,
    const Vector& query) {
  VerticesWithDistances<DistanceResult> result;
  result.reserve(k_);
  for (auto vertex_id : precomputed_correct_results) {
    result.push_back(VertexWithDistance<DistanceResult>(vertex_id, distance_fn_(vertex_id, query)));
    if (result.size() == k_) {
      break;
    }
  }
  // This assumption should never be violated as long as result list size provided by the user of
  // this class is k_ or more.
  CHECK_EQ(result.size(), k_);
  return result;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
void GroundTruth<Vector, DistanceResult>::DoApproxSearchAndUpdateStats(
    const Vector& query,
    const std::vector<VectorId>& correct_result,
    AtomicUInt64Vector& total_overlap_counters) {
  auto approx_result = CHECK_RESULT(index_reader_.Search(
      vector_cast<Vector>(query), SearchOptions{.max_num_results = k_}));
  std::unordered_set<VectorId> approx_set;
  for (const auto& approx_entry : approx_result) {
    approx_set.insert(approx_entry.vertex_id);
  }

  size_t overlap = 0;
  // The correct result might come from a precomputed ground truth dataset and contain more than k
  // elements, but we only use the first k.
  auto n = std::min(correct_result.size(), k_);
  for (size_t j = 1; j <= n; ++j) {
    if (approx_set.contains(correct_result[j - 1])) {
      overlap++;
    }
    // We have calculated the overlap between the true top j results and our list of ANN result of
    // size k. This goes into the computation of what's known as j-recall@k. We apply all the
    // denominators later.
    total_overlap_counters[j - 1].fetch_add(overlap, std::memory_order_acq_rel);
  }
}

template class GroundTruth<FloatVector, float>;
template class GroundTruth<UInt8Vector, float>;
template class GroundTruth<UInt8Vector, int32_t>;

}  // namespace yb::vector_index
