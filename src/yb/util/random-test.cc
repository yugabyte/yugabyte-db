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

#include <limits>
#include <unordered_set>

#include "yb/util/random.h"
#include "yb/util/test_util.h"

using std::vector;

namespace yb {

class RandomTest : public YBTest {
 public:
  RandomTest()
      : rng_(SeedRandom()) {
  }

 protected:
  Random rng_;
};

// Tests that after a certain number of invocations of Normal(), the
// actual mean of all samples is within the specified standard
// deviation of the target mean.
TEST_F(RandomTest, TestNormalDist) {
  const double kMean = 5.0;
  const double kStdDev = 0.01;
  const int kNumIters = 100000;

  double sum = 0.0;
  for (int i = 0; i < kNumIters; ++i) {
    sum += rng_.Normal(kMean, kStdDev);
  }

  ASSERT_LE(fabs((sum / static_cast<double>(kNumIters)) - kMean), kStdDev);
}

// Tests that after a large number of invocations of Next32() and Next64(), we
// have flipped all the bits we claim we should have.
//
// This is a regression test for a bug where we were incorrectly bit-shifting
// in Next64().
//
// Note: Our RNG actually only generates 31 bits of randomness for 32 bit
// integers and 62 bits for 64 bit integers. So this test reflects that, and if
// we change the RNG algo this test should also change.
TEST_F(RandomTest, TestUseOfBits) {
  uint32_t ones32 = std::numeric_limits<uint32_t>::max();
  uint32_t zeroes32 = 0;
  uint64_t ones64 = std::numeric_limits<uint64_t>::max();
  uint64_t zeroes64 = 0;

  for (int i = 0; i < 10000000; i++) {
    uint32_t r32 = rng_.Next32();
    ones32 &= r32;
    zeroes32 |= r32;

    uint64_t r64 = rng_.Next64();
    ones64 &= r64;
    zeroes64 |= r64;
  }

  // At the end, we should have flipped 31 and 62 bits, respectively. One
  // detail of the current RNG impl is that Next32() always returns a number
  // with MSB set to 0, and Next64() always returns a number with the first
  // two bits set to zero.
  uint32_t expected_bits_31 = std::numeric_limits<uint32_t>::max() >> 1;
  uint64_t expected_bits_62 = std::numeric_limits<uint64_t>::max() >> 2;

  ASSERT_EQ(0, ones32);
  ASSERT_EQ(expected_bits_31, zeroes32);
  ASSERT_EQ(0, ones64);
  ASSERT_EQ(expected_bits_62, zeroes64);
}

TEST_F(RandomTest, TestResetSeed) {
  rng_.Reset(1);
  uint64_t first = rng_.Next64();
  rng_.Reset(1);
  uint64_t second = rng_.Next64();
  ASSERT_EQ(first, second);
}

TEST_F(RandomTest, TestReservoirSample) {
  // Use a constant seed to avoid flakiness.
  rng_.Reset(12345);

  vector<int> population;
  for (int i = 0; i < 100; i++) {
    population.push_back(i);
  }

  // Run 1000 trials selecting 5 elements.
  vector<int> results;
  vector<int> counts(population.size());
  std::unordered_set<int> avoid;
  for (int trial = 0; trial < 1000; trial++) {
    rng_.ReservoirSample(population, 5, avoid, &results);
    for (int result : results) {
      counts[result]++;
    }
  }

  // We expect each element to be selected
  // 50 times on average, but since it's random, it won't be exact.
  // However, since we use a constant seed, this test won't be flaky.
  for (int count : counts) {
    ASSERT_GE(count, 25);
    ASSERT_LE(count, 75);
  }

  // Run again, but avoid some particular entries.
  avoid.insert(3);
  avoid.insert(10);
  avoid.insert(20);
  counts.assign(100, 0);
  for (int trial = 0; trial < 1000; trial++) {
    rng_.ReservoirSample(population, 5, avoid, &results);
    for (int result : results) {
      counts[result]++;
    }
  }

  // Ensure that we didn't ever pick the avoided elements.
  ASSERT_EQ(0, counts[3]);
  ASSERT_EQ(0, counts[10]);
  ASSERT_EQ(0, counts[20]);
}

TEST_F(RandomTest, TestReservoirSamplePopulationTooSmall) {
  vector<int> population;
  for (int i = 0; i < 10; i++) {
    population.push_back(i);
  }

  vector<int> results;
  std::unordered_set<int> avoid;
  rng_.ReservoirSample(population, 20, avoid, &results);
  ASSERT_EQ(population.size(), results.size());
  ASSERT_EQ(population, results);

  rng_.ReservoirSample(population, 10, avoid, &results);
  ASSERT_EQ(population.size(), results.size());
  ASSERT_EQ(population, results);
}

} // namespace yb
