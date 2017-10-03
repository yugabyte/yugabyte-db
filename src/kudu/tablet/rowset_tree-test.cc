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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>
#include <stdio.h>
#include <unordered_set>

#include "kudu/gutil/map-util.h"
#include "kudu/tablet/mock-rowsets.h"
#include "kudu/tablet/rowset.h"
#include "kudu/tablet/rowset_tree.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"

using std::shared_ptr;
using std::string;
using std::unordered_set;

namespace kudu { namespace tablet {

class TestRowSetTree : public KuduTest {
};

namespace {

// Generates random rowsets with keys between 0 and 10000
static RowSetVector GenerateRandomRowSets(int num_sets) {
  RowSetVector vec;
  for (int i = 0; i < num_sets; i++) {
    int min = rand() % 9000;
    int max = min + 1000;

    vec.push_back(shared_ptr<RowSet>(new MockDiskRowSet(StringPrintf("%04d", min),
                                                        StringPrintf("%04d", max))));
  }
  return vec;
}

} // anonymous namespace

TEST_F(TestRowSetTree, TestTree) {
  RowSetVector vec;
  vec.push_back(shared_ptr<RowSet>(new MockDiskRowSet("0", "5")));
  vec.push_back(shared_ptr<RowSet>(new MockDiskRowSet("3", "5")));
  vec.push_back(shared_ptr<RowSet>(new MockDiskRowSet("5", "9")));
  vec.push_back(shared_ptr<RowSet>(new MockMemRowSet()));

  RowSetTree tree;
  ASSERT_OK(tree.Reset(vec));

  // "2" overlaps 0-5 and the MemRowSet.
  vector<RowSet *> out;
  tree.FindRowSetsWithKeyInRange("2", &out);
  ASSERT_EQ(2, out.size());
  ASSERT_EQ(vec[3].get(), out[0]); // MemRowSet
  ASSERT_EQ(vec[0].get(), out[1]);

  // "4" overlaps 0-5, 3-5, and the MemRowSet
  out.clear();
  tree.FindRowSetsWithKeyInRange("4", &out);
  ASSERT_EQ(3, out.size());
  ASSERT_EQ(vec[3].get(), out[0]); // MemRowSet
  ASSERT_EQ(vec[0].get(), out[1]);
  ASSERT_EQ(vec[1].get(), out[2]);

  // interval (2,4) overlaps 0-5, 3-5 and the MemRowSet
  out.clear();
  tree.FindRowSetsIntersectingInterval("3", "4", &out);
  ASSERT_EQ(3, out.size());
  ASSERT_EQ(vec[3].get(), out[0]);
  ASSERT_EQ(vec[0].get(), out[1]);
  ASSERT_EQ(vec[1].get(), out[2]);

  // interval (0,2) overlaps 0-5 and the MemRowSet
  out.clear();
  tree.FindRowSetsIntersectingInterval("0", "2", &out);
  ASSERT_EQ(2, out.size());
  ASSERT_EQ(vec[3].get(), out[0]);
  ASSERT_EQ(vec[0].get(), out[1]);

  // interval (5,7) overlaps 0-5, 3-5, 5-9 and the MemRowSet
  out.clear();
  tree.FindRowSetsIntersectingInterval("5", "7", &out);
  ASSERT_EQ(4, out.size());
  ASSERT_EQ(vec[3].get(), out[0]);
  ASSERT_EQ(vec[0].get(), out[1]);
  ASSERT_EQ(vec[1].get(), out[2]);
  ASSERT_EQ(vec[2].get(), out[3]);
}

TEST_F(TestRowSetTree, TestPerformance) {
  const int kNumRowSets = 200;
  const int kNumQueries = AllowSlowTests() ? 1000000 : 10000;
  SeedRandom();

  // Create a bunch of rowsets, each of which spans about 10% of the "row space".
  // The row space here is 4-digit 0-padded numbers.
  RowSetVector vec = GenerateRandomRowSets(kNumRowSets);

  RowSetTree tree;
  ASSERT_OK(tree.Reset(vec));

  LOG_TIMING(INFO, StringPrintf("Querying rowset %d times", kNumQueries)) {
    vector<RowSet *> out;
    char buf[32];
    for (int i = 0; i < kNumQueries; i++) {
      out.clear();
      int query = rand() % 10000;
      snprintf(buf, arraysize(buf), "%04d", query);
      tree.FindRowSetsWithKeyInRange(Slice(buf, 4), &out);
    }
  }
}

TEST_F(TestRowSetTree, TestEndpointsConsistency) {
  const int kNumRowSets = 1000;
  RowSetVector vec = GenerateRandomRowSets(kNumRowSets);
  // Add pathological one-key rows
  for (int i = 0; i < 10; ++i) {
    vec.push_back(shared_ptr<RowSet>(new MockDiskRowSet(StringPrintf("%04d", 11000),
                                                        StringPrintf("%04d", 11000))));
  }
  vec.push_back(shared_ptr<RowSet>(new MockDiskRowSet(StringPrintf("%04d", 12000),
                                                      StringPrintf("%04d", 12000))));
  // Make tree
  RowSetTree tree;
  ASSERT_OK(tree.Reset(vec));
  // Keep track of "currently open" intervals defined by the endpoints
  unordered_set<RowSet*> open;
  // Keep track of all rowsets that have been visited
  unordered_set<RowSet*> visited;

  Slice prev;
  for (const RowSetTree::RSEndpoint& rse : tree.key_endpoints()) {
    RowSet* rs = rse.rowset_;
    enum RowSetTree::EndpointType ept = rse.endpoint_;
    const Slice& slice = rse.slice_;

    ASSERT_TRUE(rs != nullptr) << "RowSetTree has an endpoint with no rowset";
    ASSERT_TRUE(!slice.empty()) << "RowSetTree has an endpoint with no key";

    if (!prev.empty()) {
      ASSERT_LE(prev.compare(slice), 0);
    }

    Slice min, max;
    ASSERT_OK(rs->GetBounds(&min, &max));
    if (ept == RowSetTree::START) {
      ASSERT_EQ(min.data(), slice.data());
      ASSERT_EQ(min.size(), slice.size());
      ASSERT_TRUE(InsertIfNotPresent(&open, rs));
      ASSERT_TRUE(InsertIfNotPresent(&visited, rs));
    } else if (ept == RowSetTree::STOP) {
      ASSERT_EQ(max.data(), slice.data());
      ASSERT_EQ(max.size(), slice.size());
      ASSERT_TRUE(open.erase(rs) == 1);
    } else {
      FAIL() << "No such endpoint type exists";
    }
  }
}

} // namespace tablet
} // namespace kudu
