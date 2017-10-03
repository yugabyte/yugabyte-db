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

#include <boost/lexical_cast.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <vector>
#include "kudu/util/knapsack_solver.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"

using std::vector;

namespace kudu {

class TestKnapsack : public KuduTest {
};

// A simple test item for use with the knapsack solver.
// The real code will be solving knapsack over RowSet objects --
// using simple value/weight pairs in the tests makes it standalone.
struct TestItem {
  TestItem(double v, int w)
    : value(v), weight(w) {
  }

  double value;
  int weight;
};

// A traits class to adapt the knapsack solver to TestItem.
struct TestItemTraits {
  typedef TestItem item_type;
  typedef double value_type;
  static int get_weight(const TestItem &item) {
    return item.weight;
  }
  static value_type get_value(const TestItem &item) {
    return item.value;
  }
};

// Generate random items into the provided vector.
static void GenerateRandomItems(int n_items, int max_weight,
                                vector<TestItem> *out) {
  for (int i = 0; i < n_items; i++) {
    double value = 10000.0 / (random() % 10000 + 1);
    int weight = random() % max_weight;
    out->push_back(TestItem(value, weight));
  }
}

// Join and stringify the given list of ints.
static string JoinInts(const vector<int> &ints) {
  string ret;
  for (int i = 0; i < ints.size(); i++) {
    if (i > 0) {
      ret.push_back(',');
    }
    ret.append(boost::lexical_cast<string>(ints[i]));
  }
  return ret;
}

TEST_F(TestKnapsack, Basics) {
  KnapsackSolver<TestItemTraits> solver;

  vector<TestItem> in;
  in.push_back(TestItem(500, 3));
  in.push_back(TestItem(110, 1));
  in.push_back(TestItem(125, 1));
  in.push_back(TestItem(100, 1));

  vector<int> out;
  double max_val;

  // For 1 weight, pick item 2
  solver.Solve(in, 1, &out, &max_val);
  ASSERT_DOUBLE_EQ(125, max_val);
  ASSERT_EQ("2", JoinInts(out));
  out.clear();

  // For 2 weight, pick item 1, 2
  solver.Solve(in, 2, &out, &max_val);
  ASSERT_DOUBLE_EQ(110 + 125, max_val);
  ASSERT_EQ("2,1", JoinInts(out));
  out.clear();

  // For 3 weight, pick item 0
  solver.Solve(in, 3, &out, &max_val);
  ASSERT_DOUBLE_EQ(500, max_val);
  ASSERT_EQ("0", JoinInts(out));
  out.clear();

  // For 10 weight, pick all.
  solver.Solve(in, 10, &out, &max_val);
  ASSERT_DOUBLE_EQ(500 + 110 + 125 + 100, max_val);
  ASSERT_EQ("3,2,1,0", JoinInts(out));
  out.clear();
}

// Test which generates random knapsack instances and verifies
// that the result satisfies the constraints.
TEST_F(TestKnapsack, Randomized) {
  SeedRandom();
  KnapsackSolver<TestItemTraits> solver;

  const int kNumTrials = AllowSlowTests() ? 200 : 1;
  const int kMaxWeight = 1000;
  const int kNumItems = 1000;

  for (int i = 0; i < kNumTrials; i++) {
    vector<TestItem> in;
    vector<int> out;
    GenerateRandomItems(kNumItems, kMaxWeight, &in);
    double max_val;
    int max_weight = random() % kMaxWeight;
    solver.Solve(in, max_weight, &out, &max_val);

    // Verify that the max_val is equal to the sum of the chosen items' values.
    double sum_val = 0;
    int sum_weight = 0;
    for (int i : out) {
      sum_val += in[i].value;
      sum_weight += in[i].weight;
    }
    ASSERT_NEAR(max_val, sum_val, 0.000001);
    ASSERT_LE(sum_weight, max_weight);
  }
}

#ifdef NDEBUG
TEST_F(TestKnapsack, Benchmark) {
  KnapsackSolver<TestItemTraits> solver;

  const int kNumTrials = 1000;
  const int kMaxWeight = 1000;
  const int kNumItems = 1000;

  vector<TestItem> in;
  GenerateRandomItems(kNumItems, kMaxWeight, &in);

  LOG_TIMING(INFO, "benchmark") {
    vector<int> out;
    for (int i = 0; i < kNumTrials; i++) {
      out.clear();
      double max_val;
      solver.Solve(in, random() % kMaxWeight, &out, &max_val);
    }
  }
}
#endif

} // namespace kudu
