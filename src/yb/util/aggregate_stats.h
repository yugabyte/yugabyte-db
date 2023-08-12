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

namespace yb {

// Tracks min/max/sum/count.
// This class is thread-safe.
class AggregateStats {
 public:
  AggregateStats();

  // Copy-construct a (non-consistent) snapshot of other.
  explicit AggregateStats(const AggregateStats& other);

  // Record new data.
  void Increment(int64_t value) { IncrementBy(value, 1); }
  void IncrementBy(int64_t value, int64_t count);

  // Sum of all events recorded.
  int64_t TotalSum() const { return total_sum_.Load() + current_sum_.Load(); }

  // Count of all events recorded.
  int64_t TotalCount() const { return total_count_.Load() + current_count_.Load(); }

  // Sum of all events recorded since last Reset. Resets to 0 after
  // Reset().
  int64_t CurrentSum() const { return current_sum_.Load(); }

  // Count of all events recorded since last Reset. Resets to 0 after
  // Reset().
  int64_t CurrentCount() const { return current_count_.Load(); }

  // Get the minimum value,
  int64_t MinValue() const;

  // Get the maximum value,
  int64_t MaxValue() const;

  // Get the mean value of all recorded values.
  double MeanValue() const;

  // Resets min/mean/max. Preserves the values for TotalSum and TotalCount.
  void Reset();

  size_t DynamicMemoryUsage() const { return sizeof(*this); }

 private:
  // Non-resetting sum and counts.
  AtomicInt<int64_t> total_sum_;
  AtomicInt<int64_t> total_count_;
  // Resetting values
  AtomicInt<int64_t> current_sum_;
  AtomicInt<int64_t> current_count_;
  AtomicInt<int64_t> min_value_;
  AtomicInt<int64_t> max_value_;

  AggregateStats& operator=(const AggregateStats& other) = delete;
};

} // namespace yb
