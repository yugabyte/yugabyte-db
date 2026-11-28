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
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/random_util.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <vector>

#include "yb/util/random.h"
#include "yb/util/test_util.h"

namespace yb {

class RandomUtilTest : public YBTest {
 protected:
  RandomUtilTest() : rng_(SeedRandom()) {}

  Random rng_;

  static const int kLenMax = 100;
  static const int kNumTrials = 100;
};

namespace {

// Checks string defined at start is set to \0 everywhere but [from, to)
void CheckEmpty(char* start, int from, int to, int stop) {
  DCHECK_LE(0, from);
  DCHECK_LE(from, to);
  DCHECK_LE(to, stop);
  for (int j = 0; (j == from ? j = to : j) < stop; ++j) {
    CHECK_EQ(start[j], '\0') << "Index " << j << " not null after defining"
                             << "indices [" << from << "," << to << ") of "
                             << "a nulled string [0," << stop << ").";
  }
}

} // anonymous namespace

// Makes sure that RandomString only writes the specified amount
TEST_F(RandomUtilTest, TestRandomString) {
  char start[kLenMax];

  for (int i = 0; i < kNumTrials; ++i) {
    memset(start, '\0', kLenMax);
    int to = rng_.Uniform(kLenMax + 1);
    int from = rng_.Uniform(to + 1);
    RandomString(start + from, to - from, &rng_);
    CheckEmpty(start, from, to, kLenMax);
  }

  // Corner case
  memset(start, '\0', kLenMax);
  RandomString(start, 0, &rng_);
  CheckEmpty(start, 0, 0, kLenMax);
}

TEST_F(RandomUtilTest, FloydSampleShape) {
  std::mt19937_64 rng(SeedRandom());
  ASSERT_TRUE(FloydSample(0, 0, &rng).empty());
  ASSERT_TRUE(FloydSample(10, 0, &rng).empty());
  // k == n takes every value.
  const auto all = FloydSample(7, 7, &rng);
  ASSERT_EQ(all, (std::vector<uint64_t>{0, 1, 2, 3, 4, 5, 6}));
  // k > n is clamped to n.
  ASSERT_EQ(FloydSample(3, 10, &rng), (std::vector<uint64_t>{0, 1, 2}));
  ASSERT_TRUE(FloydSample(0, 5, &rng).empty());

  // Including a range far larger than k.
  constexpr auto kHugeN = std::numeric_limits<uint64_t>::max() / 2;
  for (uint64_t n : std::initializer_list<uint64_t>{1, 2, 5, 100, 1000, kHugeN}) {
    for (uint64_t k : std::initializer_list<uint64_t>{1, 2, 3, 50, 1000}) {
      for (int trial = 0; trial < 20; ++trial) {
        const auto values = FloydSample(n, k, &rng);
        ASSERT_EQ(values.size(), std::min(k, n));
        for (size_t i = 0; i < values.size(); ++i) {
          ASSERT_LT(values[i], n);
          if (i > 0) {
            // Sorted and distinct.
            ASSERT_LT(values[i - 1], values[i]);
          }
        }
      }
    }
  }
}

TEST_F(RandomUtilTest, FloydSampleUniformity) {
  std::mt19937_64 rng(SeedRandom());
  constexpr uint64_t kN = 10;
  constexpr uint64_t kK = 3;
  constexpr int kNumTrials = 200000;
  // Every value and every pair of values should be picked equally often: k/n and
  // k(k-1)/(n(n-1)) of the trials.
  std::vector<int> value_counts(kN);
  std::vector<int> pair_counts(kN * kN);
  for (int trial = 0; trial < kNumTrials; ++trial) {
    const auto values = FloydSample(kN, kK, &rng);
    for (auto value : values) {
      ++value_counts[value];
    }
    for (size_t i = 0; i < values.size(); ++i) {
      for (size_t j = i + 1; j < values.size(); ++j) {
        ++pair_counts[values[i] * kN + values[j]];
      }
    }
  }
  const double expected_value_count = 1.0 * kNumTrials * kK / kN;
  for (uint64_t value = 0; value < kN; ++value) {
    // About 9 standard deviations at these sizes.
    ASSERT_NEAR(value_counts[value], expected_value_count, 0.03 * expected_value_count)
        << "value: " << value;
  }
  const double expected_pair_count = 1.0 * kNumTrials * kK * (kK - 1) / (kN * (kN - 1));
  for (uint64_t first = 0; first < kN; ++first) {
    for (uint64_t second = first + 1; second < kN; ++second) {
      ASSERT_NEAR(pair_counts[first * kN + second], expected_pair_count, 0.1 * expected_pair_count)
          << "pair: " << first << ", " << second;
    }
  }
}

} // namespace yb
