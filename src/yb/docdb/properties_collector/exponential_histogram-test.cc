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

#include "yb/docdb/properties_collector/exponential_histogram.h"

#include <random>

#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

namespace yb::docdb {

using Hist = ExponentialHistogram;

class ExponentialHistogramTest : public YBTest {};

TEST_F(ExponentialHistogramTest, SpotIndices) {
  EXPECT_EQ(Hist::kNumBuckets, 145);
  EXPECT_EQ(Hist::BucketIndex(0), 0);
  EXPECT_EQ(Hist::BucketIndex(1), 0);
  EXPECT_EQ(Hist::BucketIndex(2), 1);
  EXPECT_EQ(Hist::BucketIndex(16), 15);
  EXPECT_EQ(Hist::BucketIndex(17), 16);
  EXPECT_EQ(Hist::BucketIndex(18), 17);
  EXPECT_EQ(Hist::BucketIndex(19), 17);
  EXPECT_EQ(Hist::BucketIndex(31), 23);
  EXPECT_EQ(Hist::BucketIndex(32), 24);
  EXPECT_EQ(Hist::BucketIndex(63), 31);
  EXPECT_EQ(Hist::BucketIndex(64), 32);
  // The incident row: bucket [73728, 81920), ~11% wide.
  EXPECT_EQ(Hist::BucketIndex(80624), 113);
  EXPECT_EQ(Hist::BucketLowerBound(113), 73728);
  EXPECT_EQ(Hist::BucketUpperBound(113), 81920);
  EXPECT_EQ(Hist::BucketIndex((1u << 20) - 1), 143);
  EXPECT_EQ(Hist::BucketIndex(1u << 20), 144);
  EXPECT_EQ(Hist::BucketIndex(std::numeric_limits<uint64_t>::max()), 144);
}

TEST_F(ExponentialHistogramTest, BoundsAreConsistentWithIndex) {
  // Every bucket's bounds contain exactly the values that map to it.
  for (size_t i = 0; i < Hist::kNumBuckets; ++i) {
    const auto lower = Hist::BucketLowerBound(i);
    ASSERT_EQ(Hist::BucketIndex(lower), i) << "bucket " << i << " lower " << lower;
    if (i + 1 < Hist::kNumBuckets) {
      const auto upper = Hist::BucketUpperBound(i);
      ASSERT_GT(upper, lower) << "bucket " << i;
      ASSERT_EQ(Hist::BucketIndex(upper - 1), i) << "bucket " << i << " upper " << upper;
      ASSERT_EQ(Hist::BucketIndex(upper), i + 1) << "bucket " << i << " upper " << upper;
    }
  }
  // Bucket edges are within 12.5% of each other above the exact region.
  for (size_t i = Hist::kExactMax; i + 1 < Hist::kNumBuckets; ++i) {
    const auto lower = Hist::BucketLowerBound(i);
    const auto upper = Hist::BucketUpperBound(i);
    ASSERT_LE(static_cast<double>(upper - lower) / static_cast<double>(lower), 0.125 + 1e-9)
        << "bucket " << i;
  }
  // Exhaustive: every value from 1 to just past the ceiling lands within its bucket's bounds.
  for (uint64_t v = 1; v <= (1u << 20) + 5; ++v) {
    const auto i = Hist::BucketIndex(v);
    ASSERT_LE(Hist::BucketLowerBound(i), v);
    ASSERT_LT(v, Hist::BucketUpperBound(i));
  }
}

TEST_F(ExponentialHistogramTest, DownscaleToPowersOfTwoIsExact) {
  // Summing the 8 sub-buckets of a power-of-two range must equal a direct count of values in that
  // range: the scale tag can be lowered without loss.
  std::mt19937_64 rng(42);
  std::vector<uint64_t> values;
  Hist hist;
  for (int i = 0; i < 100000; ++i) {
    // Log-uniform over [1, 2^21) so every range gets samples.
    const auto v = static_cast<uint64_t>(std::exp2(std::uniform_real_distribution<>(0, 21)(rng)));
    values.push_back(v);
    hist.Add(v);
  }
  for (int range_log2 = Hist::kMinRangeLog2; range_log2 < Hist::kCeilingLog2; ++range_log2) {
    const uint64_t lo = 1ULL << range_log2;
    const uint64_t hi = 1ULL << (range_log2 + 1);
    uint64_t direct = 0;
    for (auto v : values) {
      // Value 16 sits in the exact region, not in the first split range.
      if (v >= std::max<uint64_t>(lo, Hist::kExactMax + 1) && v < hi) {
        ++direct;
      }
    }
    uint64_t from_buckets = 0;
    const size_t first = Hist::kExactMax + (range_log2 - Hist::kMinRangeLog2) * 8;
    for (size_t i = first; i < first + 8; ++i) {
      from_buckets += hist.bucket(i);
    }
    ASSERT_EQ(from_buckets, direct) << "range [" << lo << ", " << hi << ")";
  }
}

TEST_F(ExponentialHistogramTest, MergeIsBucketwiseAdd) {
  Hist a, b, both;
  for (uint64_t v : {1, 1, 5, 17, 300, 80624}) {
    a.Add(v);
    both.Add(v);
  }
  for (uint64_t v : {2, 17, 17, 1 << 20}) {
    b.Add(v, 3);
    both.Add(v, 3);
  }
  a.Merge(b);
  EXPECT_EQ(a, both);
  EXPECT_EQ(a.TotalWeight(), 6 + 4 * 3);
}

TEST_F(ExponentialHistogramTest, SerializeRoundTrip) {
  Hist hist;
  EXPECT_EQ(hist.Serialize(), "s3;");
  EXPECT_TRUE(hist.Empty());
  hist.Add(1, 12);
  hist.Add(18, 3);
  hist.Add(1u << 20, 1);
  EXPECT_EQ(hist.Serialize(), "s3;0:12,17:3,144:1");
  const auto parsed = ASSERT_RESULT(Hist::Parse(hist.Serialize()));
  EXPECT_EQ(parsed, hist);
  EXPECT_EQ(ASSERT_RESULT(Hist::Parse("s3;")), Hist());

  EXPECT_NOK(Hist::Parse("s2;0:1"));
  EXPECT_NOK(Hist::Parse("s3;145:1"));
  EXPECT_NOK(Hist::Parse("s3;0"));
  EXPECT_NOK(Hist::Parse("s3;x:1"));
}

TEST_F(ExponentialHistogramTest, Quantile) {
  Hist hist;
  EXPECT_EQ(hist.QuantileLowerBound(0.5), 0);
  for (uint64_t v = 1; v <= 100; ++v) {
    hist.Add(v);
  }
  EXPECT_EQ(hist.QuantileLowerBound(0.0), 1);
  EXPECT_EQ(hist.QuantileLowerBound(0.05), 5);
  // p50 = 50 lies in bucket [48, 52).
  EXPECT_EQ(hist.QuantileLowerBound(0.5), 48);
  // p99 = 99 lies in bucket [96, 104).
  EXPECT_EQ(hist.QuantileLowerBound(0.99), 96);
  EXPECT_EQ(hist.QuantileLowerBound(1.0), 96);
}

}  // namespace yb::docdb
