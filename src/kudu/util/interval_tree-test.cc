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

// All rights reserved.

#include <gtest/gtest.h>
#include <stdlib.h>

#include <algorithm>

#include "kudu/gutil/stringprintf.h"
#include "kudu/util/interval_tree.h"
#include "kudu/util/interval_tree-inl.h"
#include "kudu/util/test_util.h"

using std::vector;

namespace kudu {

// Test harness.
class TestIntervalTree : public KuduTest {
};

// Simple interval class for integer intervals.
struct IntInterval {
  IntInterval(int left_, int right_) : left(left_), right(right_) {}

  bool Intersects(const IntInterval &other) const {
    if (other.left > right) return false;
    if (left > other.right) return false;
    return true;
  }

  int left, right;
};

// Traits definition for intervals made up of ints on either end.
struct IntTraits {
  typedef int point_type;
  typedef IntInterval interval_type;
  static point_type get_left(const IntInterval &x) {
    return x.left;
  }
  static point_type get_right(const IntInterval &x) {
    return x.right;
  }
  static int compare(int a, int b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
  }
};

// Compare intervals in a consistent way - this is only used for verifying
// that the two algorithms come up with the same results. It's not necessary
// to define this to use an interval tree.
static bool CompareIntervals(const IntInterval &a, const IntInterval &b) {
  if (a.left < b.left) return true;
  if (a.left > b.left) return false;
  if (a.right < b.right) return true;
  if (b.right > b.right) return true;
  return false; // equal
}

// Stringify a list of int intervals, for easy test error reporting.
static string Stringify(const vector<IntInterval> &intervals) {
  string ret;
  bool first = true;
  for (const IntInterval &interval : intervals) {
    if (!first) {
      ret.append(",");
    }
    StringAppendF(&ret, "[%d, %d]", interval.left, interval.right);
  }
  return ret;
}

// Find any intervals in 'intervals' which contain 'query_point' by brute force.
static void FindContainingBruteForce(const vector<IntInterval> &intervals,
                                     int query_point,
                                     vector<IntInterval> *results) {
  for (const IntInterval &i : intervals) {
    if (query_point >= i.left && query_point <= i.right) {
      results->push_back(i);
    }
  }
}


// Find any intervals in 'intervals' which intersect 'query_interval' by brute force.
static void FindIntersectingBruteForce(const vector<IntInterval> &intervals,
                                       IntInterval query_interval,
                                       vector<IntInterval> *results) {
  for (const IntInterval &i : intervals) {
    if (query_interval.Intersects(i)) {
      results->push_back(i);
    }
  }
}


// Verify that IntervalTree::FindContainingPoint yields the same results as the naive
// brute-force O(n) algorithm.
static void VerifyFindContainingPoint(const vector<IntInterval> all_intervals,
                                      const IntervalTree<IntTraits> &tree,
                                      int query_point) {
  vector<IntInterval> results;
  tree.FindContainingPoint(query_point, &results);
  std::sort(results.begin(), results.end(), CompareIntervals);

  vector<IntInterval> brute_force;
  FindContainingBruteForce(all_intervals, query_point, &brute_force);
  std::sort(brute_force.begin(), brute_force.end(), CompareIntervals);

  SCOPED_TRACE(Stringify(all_intervals) + StringPrintf(" (q=%d)", query_point));
  EXPECT_EQ(Stringify(brute_force), Stringify(results));
}

// Verify that IntervalTree::FindIntersectingInterval yields the same results as the naive
// brute-force O(n) algorithm.
static void VerifyFindIntersectingInterval(const vector<IntInterval> all_intervals,
                                           const IntervalTree<IntTraits> &tree,
                                           const IntInterval &query_interval) {
  vector<IntInterval> results;
  tree.FindIntersectingInterval(query_interval, &results);
  std::sort(results.begin(), results.end(), CompareIntervals);

  vector<IntInterval> brute_force;
  FindIntersectingBruteForce(all_intervals, query_interval, &brute_force);
  std::sort(brute_force.begin(), brute_force.end(), CompareIntervals);

  SCOPED_TRACE(Stringify(all_intervals) +
               StringPrintf(" (q=[%d,%d])", query_interval.left, query_interval.right));
  EXPECT_EQ(Stringify(brute_force), Stringify(results));
}


TEST_F(TestIntervalTree, TestBasic) {
  vector<IntInterval> intervals;
  intervals.push_back(IntInterval(1, 2));
  intervals.push_back(IntInterval(3, 4));
  intervals.push_back(IntInterval(1, 4));
  IntervalTree<IntTraits> t(intervals);

  for (int i = 0; i <= 5; i++) {
    VerifyFindContainingPoint(intervals, t, i);

    for (int j = i; j <= 5; j++) {
      VerifyFindIntersectingInterval(intervals, t, IntInterval(i, j));
    }
  }
}

TEST_F(TestIntervalTree, TestRandomized) {
  SeedRandom();

  // Generate 100 random intervals spanning 0-200 and build an interval tree from them.
  vector<IntInterval> intervals;
  for (int i = 0; i < 100; i++) {
    int l = rand() % 100; // NOLINT(runtime/threadsafe_fn)
    int r = l + rand() % 100; // NOLINT(runtime/threadsafe_fn)
    intervals.push_back(IntInterval(l, r));
  }
  IntervalTree<IntTraits> t(intervals);

  // Test that we get the correct result on every possible query.
  for (int i = -1; i < 201; i++) {
    VerifyFindContainingPoint(intervals, t, i);
  }

  // Test that we get the correct result for random intervals
  for (int i = 0; i < 100; i++) {
    int l = rand() % 100; // NOLINT(runtime/threadsafe_fn)
    int r = l + rand() % 100; // NOLINT(runtime/threadsafe_fn)
    VerifyFindIntersectingInterval(intervals, t, IntInterval(l, r));
  }
}

TEST_F(TestIntervalTree, TestEmpty) {
  vector<IntInterval> empty;
  IntervalTree<IntTraits> t(empty);

  VerifyFindContainingPoint(empty, t, 1);
  VerifyFindIntersectingInterval(empty, t, IntInterval(1, 2));
}

} // namespace kudu
