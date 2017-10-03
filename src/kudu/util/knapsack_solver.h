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
#ifndef KUDU_UTIL_KNAPSACK_SOLVER_H
#define KUDU_UTIL_KNAPSACK_SOLVER_H

#include <glog/logging.h>
#include <algorithm>
#include <utility>
#include <vector>
#include "kudu/gutil/macros.h"

namespace kudu {

// Solver for the 0-1 knapsack problem. This uses dynamic programming
// to solve the problem exactly.
//
// Given a knapsack capacity of 'W' and a number of potential items 'n',
// this solver is O(nW) time and space.
//
// This implementation is cribbed from wikipedia. The only interesting
// bit here that doesn't directly match the pseudo-code is that we
// maintain the "taken" bitmap keeping track of which items were
// taken, so we can efficiently "trace back" the chosen items.
template<class Traits>
class KnapsackSolver {
 public:
  typedef typename Traits::item_type item_type;
  typedef typename Traits::value_type value_type;
  typedef std::pair<int, value_type> solution_type;

  KnapsackSolver() {}
  ~KnapsackSolver() {}

  // Solve a knapsack problem in one shot. Finds the set of
  // items in 'items' such that their weights add up to no
  // more than 'knapsack_capacity' and maximizes the sum
  // of their values.
  // The indexes of the chosen items are stored in 'chosen_items',
  // and the maximal value is stored in 'optimal_value'.
  void Solve(std::vector<item_type> &items,
             int knapsack_capacity,
             std::vector<int>* chosen_items,
             value_type* optimal_value);


  // The following functions are a more advanced API for solving
  // knapsack problems, allowing the caller to obtain incremental
  // results as each item is considered. See the implementation of
  // Solve() for usage.

  // Prepare to solve a knapsack problem with the given capacity and
  // item set. The vector of items must remain valid and unchanged
  // until the next call to Reset().
  void Reset(int knapsack_capacity,
             const std::vector<item_type>* items);

  // Process the next item in 'items'. Returns false if there
  // were no more items to process.
  bool ProcessNext();

  // Returns the current best solution after the most recent ProcessNext
  // call. *solution is a pair of (knapsack weight used, value obtained).
  solution_type GetSolution();

  // Trace the path of item indexes used to achieve the given best
  // solution as of the latest ProcessNext() call.
  void TracePath(const solution_type& best,
                 std::vector<int>* chosen_items);

 private:

  // The state kept by the DP algorithm.
  class KnapsackBlackboard {
   public:
    typedef std::pair<int, value_type> solution_type;
    KnapsackBlackboard() :
      n_items_(0),
      n_weights_(0),
      cur_item_idx_(0),
      best_solution_(0, 0) {
    }

    void ResizeAndClear(int n_items, int max_weight);

    // Current maximum value at the given weight
    value_type &max_at(int weight) {
      DCHECK_GE(weight, 0);
      DCHECK_LT(weight, n_weights_);
      return max_value_[weight];
    }

    // Consider the next item to be put into the knapsack
    // Moves the "state" of the solution forward
    void Advance(value_type new_val, int new_wt);

    // How many items have been considered
    int current_item_index() const { return cur_item_idx_; }

    bool item_taken(int item, int weight) const {
      DCHECK_GE(weight, 0);
      DCHECK_LT(weight, n_weights_);
      DCHECK_GE(item, 0);
      DCHECK_LT(item, n_items_);
      return item_taken_[index(item, weight)];
    }

    solution_type best_solution() { return best_solution_; }

    bool done() { return cur_item_idx_ == n_items_; }

   private:
    void MarkTaken(int item, int weight) {
      item_taken_[index(item, weight)] = true;
    }

    // If the dynamic programming matrix has more than this number of cells,
    // then warn.
    static const int kWarnDimension = 10000000;

    int index(int item, int weight) const {
      return n_weights_ * item + weight;
    }

    // vector with maximum value at the i-th position meaning that it is
    // the maximum value you can get given a knapsack of weight capacity i
    // while only considering items 0..cur_item_idx_-1
    std::vector<value_type> max_value_;
    std::vector<bool> item_taken_; // TODO: record difference vectors?
    int n_items_, n_weights_;
    int cur_item_idx_;
    // Best current solution
    solution_type best_solution_;

    DISALLOW_COPY_AND_ASSIGN(KnapsackBlackboard);
  };

  KnapsackBlackboard bb_;
  const std::vector<item_type>* items_;
  int knapsack_capacity_;

  DISALLOW_COPY_AND_ASSIGN(KnapsackSolver);
};

template<class Traits>
inline void KnapsackSolver<Traits>::Reset(int knapsack_capacity,
                                          const std::vector<item_type>* items) {
  DCHECK_GE(knapsack_capacity, 0);
  items_ = items;
  knapsack_capacity_ = knapsack_capacity;
  bb_.ResizeAndClear(items->size(), knapsack_capacity);
}

template<class Traits>
inline bool KnapsackSolver<Traits>::ProcessNext() {
  if (bb_.done()) return false;

  const item_type& item = (*items_)[bb_.current_item_index()];
  int item_weight = Traits::get_weight(item);
  value_type item_value = Traits::get_value(item);
  bb_.Advance(item_value, item_weight);

  return true;
}

template<class Traits>
inline void KnapsackSolver<Traits>::Solve(std::vector<item_type> &items,
                                          int knapsack_capacity,
                                          std::vector<int>* chosen_items,
                                          value_type* optimal_value) {
  Reset(knapsack_capacity, &items);

  while (ProcessNext()) {
  }

  solution_type best = GetSolution();
  *optimal_value = best.second;
  TracePath(best, chosen_items);
}

template<class Traits>
inline typename KnapsackSolver<Traits>::solution_type KnapsackSolver<Traits>::GetSolution() {
  return bb_.best_solution();
}

template<class Traits>
inline void KnapsackSolver<Traits>::TracePath(const solution_type& best,
                                              std::vector<int>* chosen_items) {
  chosen_items->clear();
  // Retrace back which set of items corresponded to this value.
  int w = best.first;
  chosen_items->clear();
  for (int k = bb_.current_item_index() - 1; k >= 0; k--) {
    if (bb_.item_taken(k, w)) {
      const item_type& taken = (*items_)[k];
      chosen_items->push_back(k);
      w -= Traits::get_weight(taken);
      DCHECK_GE(w, 0);
    }
  }
}

template<class Traits>
void KnapsackSolver<Traits>::KnapsackBlackboard::ResizeAndClear(int n_items,
                                                                int max_weight) {
  CHECK_GT(n_items, 0);
  CHECK_GE(max_weight, 0);

  // Rather than zero-indexing the weights, we size the array from
  // 0 to max_weight. This avoids having to subtract 1 every time
  // we index into the array.
  n_weights_ = max_weight + 1;
  max_value_.resize(n_weights_);

  int dimension = index(n_items, n_weights_);
  if (dimension > kWarnDimension) {
    LOG(WARNING) << "Knapsack problem " << n_items << "x" << n_weights_
                 << " is large: may be inefficient!";
  }
  item_taken_.resize(dimension);
  n_items_ = n_items;

  // Clear
  std::fill(max_value_.begin(), max_value_.end(), 0);
  std::fill(item_taken_.begin(), item_taken_.end(), false);
  best_solution_ = std::make_pair(0, 0);

  cur_item_idx_ = 0;
}

template<class Traits>
void KnapsackSolver<Traits>::KnapsackBlackboard::Advance(value_type new_val, int new_wt) {
  // Use the dynamic programming formula:
  // Define mv(i, j) as maximum value considering items 0..i-1 with knapsack weight j
  // Then:
  // if j - weight(i) >= 0, then:
  // mv(i, j) = max(mv(i-1, j), mv(i-1, j-weight(i)) + value(j))
  // else mv(i, j) = mv(i-1, j)
  // Since the recursive formula requires an access of j-weight(i), we go in reverse.
  for (int j = n_weights_ - 1; j >= new_wt ; --j) {
    value_type val_if_taken = max_value_[j - new_wt] + new_val;
    if (max_value_[j] < val_if_taken) {
      max_value_[j] = val_if_taken;
      MarkTaken(cur_item_idx_, j);
      // Check if new solution found
      if (best_solution_.second < val_if_taken) {
        best_solution_ = std::make_pair(j, val_if_taken);
      }
    }
  }

  cur_item_idx_++;
}

} // namespace kudu
#endif
