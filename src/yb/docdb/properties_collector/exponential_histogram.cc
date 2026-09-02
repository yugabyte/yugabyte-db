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

#include <bit>
#include <limits>

#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

namespace yb::docdb {

size_t ExponentialHistogram::BucketIndex(uint64_t value) {
  if (value <= kExactMax) {
    return value == 0 ? 0 : value - 1;
  }
  if (value >= (1ULL << kCeilingLog2)) {
    return kNumBuckets - 1;
  }
  // Which power-of-two range: floor(log2(value)), i.e. the position of the top bit.
  const int range_log2 = std::bit_width(value) - 1;
  // Which of the 8 equal sub-buckets: the 3 bits below the top bit.
  const uint64_t sub_bucket = (value >> (range_log2 - kScale)) - (1ULL << kScale);
  return kExactMax + ((range_log2 - kMinRangeLog2) << kScale) + sub_bucket;
}

uint64_t ExponentialHistogram::BucketLowerBound(size_t index) {
  if (index < kExactMax) {
    return index + 1;
  }
  if (index >= kNumBuckets - 1) {
    return 1ULL << kCeilingLog2;
  }
  const size_t k = index - kExactMax;
  const int range_log2 = kMinRangeLog2 + static_cast<int>(k >> kScale);
  const uint64_t sub_bucket = k & (kSubBucketsPerRange - 1);
  const uint64_t lower = (1ULL << range_log2) + (sub_bucket << (range_log2 - kScale));
  // The first split sub-bucket nominally starts at 16, but 16 itself belongs to the exact region.
  return std::max<uint64_t>(lower, kExactMax + 1);
}

uint64_t ExponentialHistogram::BucketUpperBound(size_t index) {
  if (index >= kNumBuckets - 1) {
    return std::numeric_limits<uint64_t>::max();
  }
  return BucketLowerBound(index + 1);
}

void ExponentialHistogram::Add(uint64_t value, uint64_t weight) {
  counts_[BucketIndex(value)] += weight;
}

void ExponentialHistogram::Merge(const ExponentialHistogram& other) {
  for (size_t i = 0; i < kNumBuckets; ++i) {
    counts_[i] += other.counts_[i];
  }
}

uint64_t ExponentialHistogram::TotalWeight() const {
  uint64_t total = 0;
  for (auto count : counts_) {
    total += count;
  }
  return total;
}

bool ExponentialHistogram::Empty() const {
  for (auto count : counts_) {
    if (count != 0) {
      return false;
    }
  }
  return true;
}

uint64_t ExponentialHistogram::QuantileLowerBound(double q) const {
  const auto total = TotalWeight();
  if (total == 0) {
    return 0;
  }
  q = std::clamp(q, 0.0, 1.0);
  const auto target = static_cast<uint64_t>(std::ceil(q * static_cast<double>(total)));
  uint64_t cumulative = 0;
  for (size_t i = 0; i < kNumBuckets; ++i) {
    cumulative += counts_[i];
    if (cumulative >= target && cumulative > 0) {
      return BucketLowerBound(i);
    }
  }
  return BucketLowerBound(kNumBuckets - 1);
}

std::string ExponentialHistogram::Serialize() const {
  std::string result = kScaleTag;
  bool first = true;
  for (size_t i = 0; i < kNumBuckets; ++i) {
    if (counts_[i] == 0) {
      continue;
    }
    if (!first) {
      result += ',';
    }
    first = false;
    result += std::to_string(i);
    result += ':';
    result += std::to_string(counts_[i]);
  }
  return result;
}

Result<ExponentialHistogram> ExponentialHistogram::Parse(Slice serialized) {
  if (!serialized.starts_with(kScaleTag)) {
    return STATUS_FORMAT(
        NotSupported, "Unsupported histogram scale tag in '$0'", serialized.ToDebugString());
  }
  serialized.remove_prefix(strlen(kScaleTag));
  ExponentialHistogram result;
  if (serialized.empty()) {
    return result;
  }
  for (const auto& pair : StringSplit(serialized.ToBuffer(), ',')) {
    const auto colon = pair.find(':');
    if (colon == std::string::npos) {
      return STATUS_FORMAT(Corruption, "Malformed histogram bucket '$0'", pair);
    }
    size_t index = 0;
    uint64_t count = 0;
    try {
      index = std::stoull(pair.substr(0, colon));
      count = std::stoull(pair.substr(colon + 1));
    } catch (const std::exception& e) {
      return STATUS_FORMAT(Corruption, "Malformed histogram bucket '$0': $1", pair, e.what());
    }
    if (index >= kNumBuckets) {
      return STATUS_FORMAT(Corruption, "Histogram bucket index out of range: $0", index);
    }
    result.counts_[index] += count;
  }
  return result;
}

}  // namespace yb::docdb
