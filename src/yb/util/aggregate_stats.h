// Copyright (c) YugaByte, Inc.
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

#pragma once

#include "yb/gutil/macros.h"

#include "yb/util/atomic.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(PreserveTotalStats);

// Tracks min/max/sum/count.
// This class is thread-safe.
class AggregateStats {
 public:
  AggregateStats();

  ~AggregateStats();

  // Copy-construct a (non-consistent) snapshot of other.
  explicit AggregateStats(const AggregateStats& other);

  // Record new data.
  void Increment(int64_t value) { IncrementBy(value, 1); }
  void IncrementBy(int64_t value, uint64_t count);

  // Sum of all events recorded.
  int64_t TotalSum() const { return total_sum_.Load() + current_sum_.Load(); }

  // Count of all events recorded.
  uint64_t TotalCount() const { return total_count_.Load() + current_count_.Load(); }

  // Sum of all events recorded since last Reset. Resets to 0 after
  // Reset().
  int64_t CurrentSum() const { return current_sum_.Load(); }

  // Count of all events recorded since last Reset. Resets to 0 after
  // Reset().
  uint64_t CurrentCount() const { return current_count_.Load(); }

  // Get the minimum value,
  int64_t MinValue() const;

  // Get the maximum value,
  int64_t MaxValue() const;

  // Get the mean value of all recorded values.
  double MeanValue() const;

  // Resets min/mean/max. Preserves the values for TotalSum and TotalCount if preserve_total
  // is true.
  void Reset(PreserveTotalStats preserve_total = PreserveTotalStats::kTrue);

  // Add data from another AggregateStats object. other must not be Reset() concurrently.
  void Add(const AggregateStats& other);

  size_t DynamicMemoryUsage() const { return sizeof(*this); }

  Status SetUpPreAggregationForPrometheus(
      std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_total_sum_value,
      std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_total_count_value);

  bool IsPreAggregatedForPrometheus() const;

  // Aggregate total_sum_ and total_count_ from multiple AggregateStats instances.
  // Aggregated value holders are updated whenever total_sum_ and total_count_ are updated.
  class Aggregator {
   public:
    Aggregator() = default;

    Status InitializeValueHolders(
        std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_sum_value_holder,
        std::shared_ptr<AtomicInt<int64_t>> aggregated_prometheus_count_value_holder);

    void SumIncrementBy(int64_t value) {
      if (sum_holder_ != nullptr) {
        sum_holder_->IncrementBy(value);
      }
    }

    void CountIncrementBy(int64_t value) {
      if (count_holder_ != nullptr) {
        count_holder_->IncrementBy(value);
      }
    }

    bool HasValueHolders() const {
      return sum_holder_ != nullptr && count_holder_ != nullptr;
    }

   private:
    // Both sum_holder_ and count_holder_ must either be nullptrs or non-nullptrs.
    std::shared_ptr<AtomicInt<int64_t>> sum_holder_;
    std::shared_ptr<AtomicInt<int64_t>> count_holder_;
  };

 private:
  // Non-resetting sum and counts.
  AtomicInt<int64_t> total_sum_;
  AtomicInt<uint64_t> total_count_;
  // Resetting values
  AtomicInt<int64_t> current_sum_;
  AtomicInt<uint64_t> current_count_;
  AtomicInt<int64_t> min_value_;
  AtomicInt<int64_t> max_value_;

  Aggregator aggregator_;

  AggregateStats& operator=(const AggregateStats& other) = delete;
};

} // namespace yb
