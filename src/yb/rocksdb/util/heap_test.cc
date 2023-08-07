//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <climits>

#include <queue>
#include <random>
#include <utility>

#include <gtest/gtest.h>

#include "yb/util/logging.h"

#include "yb/rocksdb/util/heap.h"
#include "yb/rocksdb/util/testutil.h"

#ifndef GFLAGS
const int64_t ANNOTATE_UNPROTECTED_WRITE(FLAGS_iters) = 100000;
#else
#include "yb/util/flags.h"
DEFINE_NON_RUNTIME_int64(iters, 100000, "number of pseudo-random operations in each test");
#endif  // GFLAGS

/*
 * Compares the custom heap implementation in util/heap.h against
 * std::priority_queue on a pseudo-random sequence of operations.
 */

namespace rocksdb {

using HeapTestValue = uint64_t;
using Params = std::tuple<size_t, HeapTestValue, int64_t>;

class HeapTest : public RocksDBTest,
                 public ::testing::WithParamInterface<Params> {
};

TEST_P(HeapTest, Test) {
  // This test performs the same pseudorandom sequence of operations on a
  // BinaryHeap and an std::priority_queue, comparing output.  The three
  // possible operations are insert, replace top and pop.
  //
  // Insert is chosen slightly more often than the others so that the size of
  // the heap slowly grows.  Once the size heats the MAX_HEAP_SIZE limit, we
  // disallow inserting until the heap becomes empty, testing the "draining"
  // scenario.

  const auto MAX_HEAP_SIZE = std::get<0>(GetParam());
  const auto MAX_VALUE = std::get<1>(GetParam());
  const auto RNG_SEED = std::get<2>(GetParam());

  BinaryHeap<HeapTestValue> heap;
  std::priority_queue<HeapTestValue> ref;

  std::mt19937 rng(static_cast<unsigned int>(RNG_SEED));
  std::uniform_int_distribution<HeapTestValue> value_dist(0, MAX_VALUE);
  int ndrains = 0;
  bool draining = false;     // hit max size, draining until we empty the heap
  size_t size = 0;
  for (int64_t i = 0; i < FLAGS_iters; ++i) {
    if (size == 0) {
      draining = false;
    }

    if (!draining &&
        (size == 0 || std::bernoulli_distribution(0.4)(rng))) {
      // insert
      HeapTestValue val = value_dist(rng);
      heap.push(val);
      ref.push(val);
      ++size;
      if (size == MAX_HEAP_SIZE) {
        draining = true;
        ++ndrains;
      }
    } else if (std::bernoulli_distribution(0.5)(rng)) {
      // replace top
      HeapTestValue val = value_dist(rng);
      heap.replace_top(val);
      ref.pop();
      ref.push(val);
    } else {
      // pop
      assert(size > 0);
      heap.pop();
      ref.pop();
      --size;
    }

    // After every operation, check that the public methods give the same
    // results
    assert((size == 0) == ref.empty());
    ASSERT_EQ(size == 0, heap.empty());
    if (size > 0) {
      ASSERT_EQ(ref.top(), heap.top());
    }
  }

  // Probabilities should be set up to occasionally hit the max heap size and
  // drain it
  ASSERT_GT(ndrains, 0);

  heap.clear();
  ASSERT_TRUE(heap.empty());
}

// Basic test, MAX_VALUE = 3*MAX_HEAP_SIZE (occasional duplicates)
INSTANTIATE_TEST_CASE_P(
  Basic, HeapTest,
  ::testing::Values(Params(1000, 3000, 0x1b575cf05b708945))
);
// Mid-size heap with small values (many duplicates)
INSTANTIATE_TEST_CASE_P(
  SmallValues, HeapTest,
  ::testing::Values(Params(100, 10, 0x5ae213f7bd5dccd0))
);
// Small heap, large value range (no duplicates)
INSTANTIATE_TEST_CASE_P(
  SmallHeap, HeapTest,
  ::testing::Values(Params(10, ULLONG_MAX, 0x3e1fa8f4d01707cf))
);
// Two-element heap
INSTANTIATE_TEST_CASE_P(
  TwoElementHeap, HeapTest,
  ::testing::Values(Params(2, 5, 0x4b5e13ea988c6abc))
);
// One-element heap
INSTANTIATE_TEST_CASE_P(
  OneElementHeap, HeapTest,
  ::testing::Values(Params(1, 3, 0x176a1019ab0b612e))
);

TEST(HeapTest, SecondTopTest) {
  BinaryHeap<HeapTestValue> heap;
  heap.push(100);
  ASSERT_EQ(100, heap.top());
  heap.push(120);
  ASSERT_EQ(120, heap.top());
  ASSERT_EQ(100, heap.second_top());
  heap.push(140);
  ASSERT_EQ(140, heap.top());
  ASSERT_EQ(120, heap.second_top());

  heap.push(110);
  ASSERT_EQ(140, heap.top());
  ASSERT_EQ(120, heap.second_top());

  heap.push(130);
  ASSERT_EQ(140, heap.top());
  ASSERT_EQ(130, heap.second_top());
}


}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
#ifdef GFLAGS
  GFLAGS::ParseCommandLineFlags(&argc, &argv, true);
#endif  // GFLAGS
  return RUN_ALL_TESTS();
}
