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

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "yb/util/result.h"
#include "yb/util/slice.h"

namespace yb::docdb {

// A fixed-layout histogram of positive integers (chain lengths, stretch lengths, byte counts) for
// SST properties. See properties_collector/README.md, "Histogram layout", for the design.
//
// The layout is HdrHistogram's LOG-LINEAR one -- the same bucket count as the base-2 exponential
// family at scale 3, but with boundaries linear within each power-of-two range (NOT the geometric
// 2^(k/8) edges of the OTel / Prometheus-native convention):
//   buckets 0..15    values 1..16, one bucket per value (exact);
//   buckets 16..143  values 17..2^20-1, 8 equal-width sub-buckets per power-of-two range;
//   bucket 144       values >= 2^20 (overflow).
// Below the overflow bucket, edges are within 12.5% of each other, so a quantile read from bucket
// bounds is within 12.5% of the true value; a quantile landing in the overflow bucket reads as
// 2^20 with unbounded error. Merging two histograms is bucket-wise addition, exact at any rollup;
// coarsening within the linear family (halving the sub-bucket count) is exact too.
//
// Bucket index = one bit-scan plus a shift and a mask; no floating point. Plain counters, no
// atomics: a histogram is owned by exactly one SST build.
class ExponentialHistogram {
 public:
  static constexpr int kScale = 3;  // 3 mantissa bits -> 8 linear sub-buckets per range.
  static constexpr int kSubBucketsPerRange = 1 << kScale;
  static constexpr uint64_t kExactMax = 16;
  static constexpr int kMinRangeLog2 = 4;   // The first split power-of-two range is [16, 32).
  static constexpr int kCeilingLog2 = 20;   // Values >= 2^20 share the overflow bucket.
  static constexpr size_t kNumBuckets =
      kExactMax + (kCeilingLog2 - kMinRangeLog2) * kSubBucketsPerRange + 1;  // 145

  // Serialized form: the layout tag followed by sparse "index:count" pairs, e.g. "l3;0:12,17:3".
  // "l3" = log-linear with 3 mantissa bits; deliberately not "s3", which would wrongly claim the
  // geometric scale-3 boundary convention.
  static constexpr char kLayoutTag[] = "l3;";

  // Bucket for a value. Values >= 1; 0 is folded into bucket 0.
  static size_t BucketIndex(uint64_t value);
  // Inclusive lower bound of a bucket's value range.
  static uint64_t BucketLowerBound(size_t index);
  // Exclusive upper bound of a bucket's value range; UINT64_MAX for the overflow bucket.
  static uint64_t BucketUpperBound(size_t index);

  void Add(uint64_t value, uint64_t weight = 1);
  void Merge(const ExponentialHistogram& other);

  uint64_t bucket(size_t index) const { return counts_[index]; }
  uint64_t TotalWeight() const;
  bool Empty() const;

  // Lower bound of the bucket in which the cumulative weight first reaches quantile q in [0, 1].
  // Returns 0 for an empty histogram; a quantile in the overflow bucket returns 2^20 regardless of
  // the true magnitude.
  uint64_t QuantileLowerBound(double q) const;

  std::string Serialize() const;
  static Result<ExponentialHistogram> Parse(Slice serialized);

  bool operator==(const ExponentialHistogram& other) const { return counts_ == other.counts_; }

 private:
  std::array<uint64_t, kNumBuckets> counts_{};
};

}  // namespace yb::docdb
