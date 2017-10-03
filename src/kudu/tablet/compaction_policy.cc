// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/tablet/compaction_policy.h"

#include <glog/logging.h>

#include <algorithm>
#include <utility>
#include <string>
#include <vector>

#include "kudu/gutil/map-util.h"
#include "kudu/gutil/mathlimits.h"
#include "kudu/tablet/rowset.h"
#include "kudu/tablet/rowset_info.h"
#include "kudu/tablet/rowset_tree.h"
#include "kudu/tablet/svg_dump.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/knapsack_solver.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

using std::vector;

DEFINE_int32(budgeted_compaction_target_rowset_size, 32*1024*1024,
             "The target size for DiskRowSets during flush/compact when the "
             "budgeted compaction policy is used");
TAG_FLAG(budgeted_compaction_target_rowset_size, experimental);
TAG_FLAG(budgeted_compaction_target_rowset_size, advanced);

namespace kudu {
namespace tablet {

// Adjust the result downward slightly for wider solutions.
// Consider this input:
//
//  |-----A----||----C----|
//  |-----B----|
//
// where A, B, and C are all 1MB, and the budget is 10MB.
//
// Without this tweak, the solution {A, B, C} has the exact same
// solution value as {A, B}, since both compactions would yield a
// tablet with average height 1. Since both solutions fit within
// the budget, either would be a valid pick, and it would be up
// to chance which solution would be selected.
// Intuitively, though, there's no benefit to including "C" in the
// compaction -- it just uses up some extra IO. If we slightly
// penalize wider solutions as a tie-breaker, then we'll pick {A, B}
// here.
static const double kSupportAdjust = 1.01;

////////////////////////////////////////////////////////////
// BudgetedCompactionPolicy
////////////////////////////////////////////////////////////

BudgetedCompactionPolicy::BudgetedCompactionPolicy(int budget)
  : size_budget_mb_(budget) {
  CHECK_GT(budget, 0);
}

uint64_t BudgetedCompactionPolicy::target_rowset_size() const {
  CHECK_GT(FLAGS_budgeted_compaction_target_rowset_size, 0);
  return FLAGS_budgeted_compaction_target_rowset_size;
}

// Returns in min-key and max-key sorted order
void BudgetedCompactionPolicy::SetupKnapsackInput(const RowSetTree &tree,
                                                  vector<RowSetInfo>* min_key,
                                                  vector<RowSetInfo>* max_key) {
  RowSetInfo::CollectOrdered(tree, min_key, max_key);

  if (min_key->size() < 2) {
    // require at least 2 rowsets to compact
    min_key->clear();
    max_key->clear();
    return;
  }
}

namespace {

struct CompareByDescendingDensity {
  bool operator()(const RowSetInfo& a, const RowSetInfo& b) const {
    return a.density() > b.density();
  }
};

struct KnapsackTraits {
  typedef RowSetInfo item_type;
  typedef double value_type;
  static int get_weight(const RowSetInfo &item) {
    return item.size_mb();
  }
  static value_type get_value(const RowSetInfo &item) {
    return item.width();
  }
};

// Dereference-then-compare comparator
template<class Compare>
struct DerefCompare {
  template<class T>
  bool operator()(T* a, T* b) const {
    static const Compare comp = Compare();
    return comp(*a, *b);
  }
};

// Incremental calculator for the upper bound on a knapsack solution,
// given a set of items. The upper bound is computed by solving the
// simpler "fractional knapsack problem" -- i.e the related problem
// in which each input may be fractionally put in the knapsack, instead
// of all-or-nothing. The fractional knapsack problem has a very efficient
// solution: sort by descending density and greedily choose elements
// until the budget is reached. The last element to be chosen may be
// partially included in the knapsack.
//
// Because this greedy solution only depends on sorting, it can be computed
// incrementally as items are considered by maintaining a min-heap, ordered
// by the density of the input elements. We need only maintain enough elements
// to satisfy the budget, making this logarithmic in the budget and linear
// in the number of elements added.
class UpperBoundCalculator {
 public:
  explicit UpperBoundCalculator(int max_weight)
    : total_weight_(0),
      total_value_(0),
      max_weight_(max_weight),
      topdensity_(MathLimits<double>::kNegInf) {
  }

  void Add(const RowSetInfo& candidate) {
    // No need to add if less dense than the top and have no more room
    if (total_weight_ >= max_weight_ &&
        candidate.density() <= topdensity_)
      return;

    fractional_solution_.push_back(&candidate);
    std::push_heap(fractional_solution_.begin(), fractional_solution_.end(),
                   DerefCompare<CompareByDescendingDensity>());

    total_weight_ += candidate.size_mb();
    total_value_ += candidate.width();
    const RowSetInfo& top = *fractional_solution_.front();
    if (total_weight_ - top.size_mb() >= max_weight_) {
      total_weight_ -= top.size_mb();
      total_value_ -= top.width();
      std::pop_heap(fractional_solution_.begin(), fractional_solution_.end(),
                    DerefCompare<CompareByDescendingDensity>());
      fractional_solution_.pop_back();
    }
    topdensity_ = fractional_solution_.front()->density();
  }

  // Compute the upper-bound to the 0-1 knapsack problem with the elements
  // added so far.
  double ComputeUpperBound() const {
    int excess_weight = total_weight_ - max_weight_;
    if (excess_weight <= 0) {
      return total_value_;
    }

    const RowSetInfo& top = *fractional_solution_.front();
    double fraction_of_top_to_remove = static_cast<double>(excess_weight) / top.size_mb();
    DCHECK_GT(fraction_of_top_to_remove, 0);
    return total_value_ - fraction_of_top_to_remove * top.width();
  }

  void clear() {
    fractional_solution_.clear();
    total_weight_ = 0;
    total_value_ = 0;
  }

 private:

  // Store pointers to RowSetInfo rather than whole copies in order
  // to allow for fast swapping in the heap.
  vector<const RowSetInfo*> fractional_solution_;
  int total_weight_;
  double total_value_;
  int max_weight_;
  double topdensity_;
};

} // anonymous namespace

Status BudgetedCompactionPolicy::PickRowSets(const RowSetTree &tree,
                                             unordered_set<RowSet*>* picked,
                                             double* quality,
                                             std::vector<std::string>* log) {
  vector<RowSetInfo> asc_min_key, asc_max_key;
  SetupKnapsackInput(tree, &asc_min_key, &asc_max_key);
  if (asc_max_key.empty()) {
    if (log) {
      LOG_STRING(INFO, log) << "No rowsets to compact";
    }
    // nothing to compact.
    return Status::OK();
  }

  UpperBoundCalculator ub_calc(size_budget_mb_);
  KnapsackSolver<KnapsackTraits> solver;

  // The best set of rowsets chosen so far
  unordered_set<RowSet *> best_chosen;
  // The value attained by the 'best_chosen' solution.
  double best_optimal = 0;

  vector<int> chosen_indexes;
  vector<RowSetInfo> inrange_candidates;
  inrange_candidates.reserve(asc_min_key.size());
  vector<double> upper_bounds;

  for (const RowSetInfo& cc_a : asc_min_key) {
    chosen_indexes.clear();
    inrange_candidates.clear();
    ub_calc.clear();
    upper_bounds.clear();

    double ab_min = cc_a.cdf_min_key();
    double ab_max = cc_a.cdf_max_key();

    // Collect all other candidates which would not expand the support to the
    // left of this one. Because these are sorted by ascending max key, we can
    // easily ensure that whenever we add a 'cc_b' to our candidate list for the
    // knapsack problem, we've already included all rowsets which fall in the
    // range from cc_a.min to cc_b.max.
    //
    // For example:
    //
    //  |-----A----|
    //      |-----B----|
    //         |----C----|
    //       |--------D-------|
    //
    // We process in the order: A, B, C, D.
    //
    // This saves us from having to iterate through the list again to find all
    // such rowsets.
    //
    // Additionally, each knapsack problem builds on the previous knapsack
    // problem by adding just a single rowset, meaning that we can reuse the
    // existing dynamic programming state to incrementally update the solution,
    // rather than having to rebuild from scratch.
    for (const RowSetInfo& cc_b : asc_max_key) {
      if (cc_b.cdf_min_key() < ab_min) {
        // Would expand support to the left.
        // TODO: possible optimization here: binary search to skip to the first
        // cc_b with cdf_max_key() > cc_a.cdf_min_key()
        continue;
      }
      inrange_candidates.push_back(cc_b);

      // While we're iterating, also calculate the upper bound for the solution
      // on the set within the [ab_min, ab_max] output range.
      ab_max = std::max(cc_b.cdf_max_key(), ab_max);
      double union_width = ab_max - ab_min;

      ub_calc.Add(cc_b);
      upper_bounds.push_back(ub_calc.ComputeUpperBound() - union_width * kSupportAdjust);
    }
    if (inrange_candidates.empty()) continue;
    // If the best upper bound across this whole range is worse than our current
    // optimal, we can short circuit all the knapsack-solving.
    if (*std::max_element(upper_bounds.begin(), upper_bounds.end()) < best_optimal) continue;

    solver.Reset(size_budget_mb_, &inrange_candidates);

    ab_max = cc_a.cdf_max_key();

    int i = 0;
    while (solver.ProcessNext()) {
      // If this candidate's upper bound is worse than the optimal, we don't
      // need to look at it.
      const RowSetInfo& item = inrange_candidates[i];
      double upper_bound = upper_bounds[i];
      i++;
      if (upper_bound < best_optimal) continue;

      std::pair<int, double> best_with_this_item = solver.GetSolution();
      double best_value = best_with_this_item.second;

      ab_max = std::max(item.cdf_max_key(), ab_max);
      DCHECK_GE(ab_max, ab_min);
      double solution = best_value - (ab_max - ab_min) * kSupportAdjust;
      DCHECK_LE(solution, upper_bound + 0.0001);

      if (solution > best_optimal) {
        solver.TracePath(best_with_this_item, &chosen_indexes);
        best_optimal = solution;
      }
    }

    // If we came up with a new solution, replace.
    if (!chosen_indexes.empty()) {
      best_chosen.clear();
      for (int i : chosen_indexes) {
        best_chosen.insert(inrange_candidates[i].rowset());
      }
    }
  }

  // Log the input and output of the selection.
  if (VLOG_IS_ON(1) || log != nullptr) {
    LOG_STRING(INFO, log) << "Budgeted compaction selection:";
    for (RowSetInfo &cand : asc_min_key) {
      const char *checkbox = "[ ]";
      if (ContainsKey(best_chosen, cand.rowset())) {
        checkbox = "[x]";
      }
      LOG_STRING(INFO, log) << "  " << checkbox << " " << cand.ToString();
    }
    LOG_STRING(INFO, log) << "Solution value: " << best_optimal;
  }

  *quality = best_optimal;

  if (best_optimal <= 0) {
    VLOG(1) << "Best compaction available makes things worse. Not compacting.";
    return Status::OK();
  }

  picked->swap(best_chosen);
  DumpCompactionSVG(asc_min_key, *picked);

  return Status::OK();
}

} // namespace tablet
} // namespace kudu
