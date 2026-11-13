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
#include <cmath>
#include <unordered_set>

#include "yb/gutil/strings/human_readable.h"

#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/ann_validation.h"
#include "yb/vector_index/hnsw_util.h"
#include "yb/vector_index/vectorann_util.h"

namespace yb::vector_index {

namespace {

template<ValidDistanceResultType DistanceResult>
using VerticesWithDistances = std::vector<VectorWithDistance<DistanceResult>>;

template<ValidDistanceResultType DistanceResult>
std::vector<VectorId> VertexIdsOnly(
    const VerticesWithDistances<DistanceResult>& vertices_with_distances) {
  std::vector<VectorId> result;
  result.reserve(vertices_with_distances.size());
  for (const auto& v_dist : vertices_with_distances) {
    result.push_back(v_dist.vector_id);
  }
  return result;
}

template<ValidDistanceResultType DistanceResult>
bool DistancesMatch(DistanceResult a, DistanceResult b) {
  if constexpr (std::is_floating_point_v<DistanceResult>) {
    // Kernels and tools differ in summation order and approximate normalization. The errors of
    // 1 - dot and 1 - cos scale with the norms, so the floor of 1 assumes near-unit vectors.
    constexpr DistanceResult kTolerance = 1e-5;
    return std::abs(a - b) <= kTolerance * std::max({DistanceResult(1), std::abs(a), std::abs(b)});
  } else {
    return a == b;
  }
}

// Whether two result sets of a query are equally good: the same distances when sorted.
template<ValidDistanceResultType DistanceResult>
bool ResultSetsEquivalent(const VerticesWithDistances<DistanceResult>& a,
                          const VerticesWithDistances<DistanceResult>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  auto a_sorted = a;
  std::sort(a_sorted.begin(), a_sorted.end());
  auto b_sorted = b;
  std::sort(b_sorted.begin(), b_sorted.end());
  for (size_t i = 0; i < a.size(); ++i) {
    if (!DistancesMatch(a_sorted[i].distance, b_sorted[i].distance)) {
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

// Logs the progress of a brute force search, which takes hours on large datasets.
class BruteForceProgress {
 public:
  explicit BruteForceProgress(size_t num_queries)
      : num_queries_(num_queries),
        start_time_(CoarseMonoClock::Now()),
        next_log_time_(start_time_ + kLogInterval) {}

  // Called once per computed query, from multiple threads.
  void QueryDone() {
    const auto num_done = num_done_.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto now = CoarseMonoClock::Now();
    auto next_log_time = next_log_time_.load(std::memory_order_relaxed);
    if (now < next_log_time ||
        !next_log_time_.compare_exchange_strong(
            next_log_time, now + kLogInterval, std::memory_order_relaxed)) {
      return;
    }
    const auto elapsed = ToSeconds(now - start_time_);
    LOG(INFO) << "Computed the ground truth of " << num_done << " of " << num_queries_
              << " queries in " << HumanReadableElapsedTime::ToShortString(elapsed) << ", about "
              << HumanReadableElapsedTime::ToShortString(
                     elapsed * (num_queries_ - num_done) / num_done)
              << " left";
  }

 private:
  static constexpr auto kLogInterval = std::chrono::minutes(1);

  const size_t num_queries_;
  const CoarseTimePoint start_time_;
  std::atomic<size_t> num_done_{0};
  std::atomic<CoarseTimePoint> next_log_time_;
};

} // namespace

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
GroundTruth<Vector, DistanceResult>::GroundTruth(
    const VertexIdToVectorDistanceFunction<Vector, DistanceResult>& distance_fn,
    size_t max_k,
    const std::vector<Vector>& queries,
    const PrecomputedGroundTruthMatrix& precomputed_ground_truth,
    bool validate_precomputed_ground_truth,
    const std::vector<VectorId>& vertex_ids)
    : distance_fn_(distance_fn),
      max_k_(max_k),
      queries_(queries),
      precomputed_ground_truth_(precomputed_ground_truth),
      validate_precomputed_ground_truth_(validate_precomputed_ground_truth),
      vertex_ids_(vertex_ids) {
  CHECK_LE(precomputed_ground_truth_.size(), queries_.size());
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status GroundTruth<Vector, DistanceResult>::Prepare(size_t num_threads) {
  const auto num_precomputed = precomputed_ground_truth_.size();
  size_t num_validated = 0;
  for (size_t i = 0; i != num_precomputed; ++i) {
    num_validated += ShouldValidate(i);
  }
  const auto num_to_compute = queries_.size() - num_precomputed + num_validated;
  if (num_to_compute == 0) {
    return Status::OK();
  }
  LOG(INFO) << "Computing top " << max_k_ << " ground truth of " << num_to_compute
            << " queries using " << num_threads << " threads"
            << (num_validated ? Format(", validating $0 precomputed ones", num_validated) : "");
  auto start_time = MonoTime::Now();
  computed_ground_truth_.resize(queries_.size());
  BruteForceProgress progress(num_to_compute);
  RETURN_NOT_OK(ProcessInParallel(
      num_threads,
      /* start_index= */ static_cast<size_t>(0),
      /* end_index_exclusive= */ queries_.size(),
      [this, &progress](size_t query_index) -> Status {
        if (query_index < precomputed_ground_truth_.size() && !ShouldValidate(query_index)) {
          computed_ground_truth_[query_index] = precomputed_ground_truth_[query_index];
          return Status::OK();
        }
        RETURN_NOT_OK(ComputeQuery(query_index));
        progress.QueryDone();
        return Status::OK();
      }));
  LOG(INFO) << "Ground truth finished in " << MonoTime::Now() - start_time;
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<FloatVector> GroundTruth<Vector, DistanceResult>::EvaluateRecall(
    const IndexReader& index_reader, size_t num_threads, size_t k, size_t ef) {
  SCHECK_GE(num_threads, static_cast<size_t>(1),
            InvalidArgument, "Number of threads must be at least 1");
  SCHECK_LE(k, max_k_, InvalidArgument, "k exceeds max_k");
  const auto& ground_truth = this->ground_truth();
  SCHECK_EQ(ground_truth.size(), queries_.size(), IllegalState, "Ground truth is not prepared");

  // For each index i we maintain the total overlap (set intersection cardinality) between the
  // top (i + 1) ground truth results and the top (i + 1) ANN results, computed across all queries.
  // We apply denominators at the end.
  AtomicUInt64Vector total_overlap(k);
  for (size_t i = 0; i < k; ++i) {
    total_overlap[i].store(0);
  }

  RETURN_NOT_OK(ProcessInParallel(
      num_threads,
      /* start_index= */ static_cast<size_t>(0),
      /* end_index_exclusive= */ queries_.size(),
      [this, &index_reader, k, ef, &ground_truth, &total_overlap](size_t query_index) -> Status {
        return DoApproxSearchAndUpdateStats(
            index_reader, queries_[query_index], k, ef, ground_truth[query_index],
            total_overlap);
      }));

  // The result is always a vector of floats, regardless of the coordinate type.
  FloatVector result;

  result.resize(k);
  for (size_t i = 1; i <= k; ++i) {
    // In the denominator below, we average across all queries. But we also need to divide by the
    // size of the result set of the precise top-i query, which would be i in most cases, except
    // in the degenerate case of a really small input dataset (should never occur in practice).
    result[i - 1] = total_overlap[i - 1].load(std::memory_order_acquire) * 1.0 /
        (queries_.size() * std::min(vertex_ids_.size(), i));
  }
  return result;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool GroundTruth<Vector, DistanceResult>::ShouldValidate(size_t query_index) const {
  if (validate_precomputed_ground_truth_) {
    return true;
  }
  const auto num_precomputed = precomputed_ground_truth_.size();
  if (num_precomputed == queries_.size()) {
    return false;
  }
  constexpr size_t kNumSampled = 10;
  return query_index % ((num_precomputed + kNumSampled - 1) / kNumSampled) == 0;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status GroundTruth<Vector, DistanceResult>::ComputeQuery(size_t query_index) {
  const auto& query = queries_[query_index];
  auto our_correct_top_k = BruteForcePreciseNearestNeighbors<Vector, DistanceResult>(
      query, vertex_ids_, distance_fn_, max_k_);

  computed_ground_truth_[query_index] = VertexIdsOnly(our_correct_top_k);
  if (query_index >= precomputed_ground_truth_.size()) {
    return Status::OK();
  }

  // Compare the ground truth we've just computed to the the precomputed ground truth.
  const auto& precomputed_correct_results = precomputed_ground_truth_[query_index];
  SCHECK_GE(
      precomputed_correct_results.size(), max_k_, IllegalState,
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
  return Status::OK();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
std::vector<VectorWithDistance<DistanceResult>>
GroundTruth<Vector, DistanceResult>::AugmentWithDistancesAndTrimToK(
    const std::vector<VectorId>& precomputed_correct_results,
    const Vector& query) {
  VerticesWithDistances<DistanceResult> result;
  result.reserve(max_k_);
  for (auto vertex_id : precomputed_correct_results) {
    result.push_back(VectorWithDistance<DistanceResult>(vertex_id, distance_fn_(vertex_id, query)));
    if (result.size() == max_k_) {
      break;
    }
  }
  // This assumption should never be violated as long as result list size provided by the user of
  // this class is max_k_ or more.
  CHECK_EQ(result.size(), max_k_);
  return result;
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Status GroundTruth<Vector, DistanceResult>::DoApproxSearchAndUpdateStats(
    const IndexReader& index_reader,
    const Vector& query,
    size_t k,
    size_t ef,
    const std::vector<VectorId>& correct_result,
    AtomicUInt64Vector& total_overlap_counters) {
  auto approx_result = VERIFY_RESULT(index_reader.Search(
      vector_cast<Vector>(query), SearchOptions{.max_num_results = k, .ef = ef}));
  std::unordered_set<VectorId> approx_set;
  for (const auto& approx_entry : approx_result) {
    approx_set.insert(approx_entry.vector_id);
  }

  size_t overlap = 0;
  // The correct result might come from a precomputed ground truth dataset and contain more than k
  // elements, but we only use the first k.
  auto n = std::min(correct_result.size(), k);
  for (size_t j = 1; j <= n; ++j) {
    if (approx_set.contains(correct_result[j - 1])) {
      overlap++;
    }
    // We have calculated the overlap between the true top j results and our list of ANN result of
    // size k. This goes into the computation of what's known as j-recall@k. We apply all the
    // denominators later.
    total_overlap_counters[j - 1].fetch_add(overlap, std::memory_order_acq_rel);
  }
  // With fewer than k base vectors, the true top j for larger j is the same n vectors.
  for (size_t j = n + 1; j <= k; ++j) {
    total_overlap_counters[j - 1].fetch_add(overlap, std::memory_order_acq_rel);
  }
  return Status::OK();
}

template class GroundTruth<FloatVector, float>;
template class GroundTruth<UInt8Vector, float>;
template class GroundTruth<UInt8Vector, int32_t>;

}  // namespace yb::vector_index
