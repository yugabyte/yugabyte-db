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
#pragma once

// C++ (TR1) port of HdrHistogram.
// Original java implementation: http://giltene.github.io/HdrHistogram/
//
// A High Dynamic Range (HDR) Histogram
//
// HdrHistogram supports the recording and analyzing sampled data value counts
// across a configurable integer value range with configurable value precision
// within the range. Value precision is expressed as the number of significant
// digits in the value recording, and provides control over value quantization
// behavior across the value range and the subsequent value resolution at any
// given level.
//
// For example, a Histogram could be configured to track the counts of observed
// integer values between 0 and 3,600,000,000 while maintaining a value
// precision of 3 significant digits across that range. Value quantization
// within the range will thus be no larger than 1/1,000th (or 0.1%) of any
// value. This example Histogram could be used to track and analyze the counts
// of observed response times ranging between 1 microsecond and 1 hour in
// magnitude, while maintaining a value resolution of 1 microsecond up to 1
// millisecond, a resolution of 1 millisecond (or better) up to one second, and
// a resolution of 1 second (or better) up to 1,000 seconds. At it's maximum
// tracked value (1 hour), it would still maintain a resolution of 3.6 seconds
// (or better).

#include <iosfwd>
#include <memory>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"

namespace yb {

class AbstractHistogramIterator;
class Status;
class RecordedValuesIterator;

// This implementation allows you to specify a range and accuracy (significant
// digits) to support in an instance of a histogram. The class takes care of
// the rest. At this time, only uint64_t values are supported.
//
// An HdrHistogram consists of a set of buckets (which bucket the magnitude of
// a value stored), and a set of sub-buckets (which implement the tunable
// precision of the storage). So if you specify 3 significant digits of
// precision, then you will get about 10^3 sub-buckets (as a power of 2) for
// each level of magnitude. Magnitude buckets are tracked in powers of 2.
//
// This class is thread-safe.
class HdrHistogram {
 public:
  // Specify the highest trackable value so that the class has a bound on the
  // number of buckets, and # of significant digits (in decimal) so that the
  // class can determine the granularity of those buckets.
  HdrHistogram(uint64_t highest_trackable_value, int num_significant_digits);

  // Copy-construct a (non-consistent) snapshot of other.
  explicit HdrHistogram(const HdrHistogram& other);

  // Validate your params before trying to construct the object.
  static bool IsValidHighestTrackableValue(uint64_t highest_trackable_value);
  static bool IsValidNumSignificantDigits(int num_significant_digits);

  // Record new data.
  void Increment(int64_t value);
  void IncrementBy(int64_t value, int64_t count);

  // Record new data, correcting for "coordinated omission".
  //
  // See https://groups.google.com/d/msg/mechanical-sympathy/icNZJejUHfE/BfDekfBEs_sJ
  // for more details.
  void IncrementWithExpectedInterval(int64_t value,
                                     int64_t expected_interval_between_samples);

  // Fetch configuration params.
  uint64_t highest_trackable_value() const { return highest_trackable_value_; }
  int num_significant_digits() const { return num_significant_digits_; }

  // Get indexes into histogram based on value.
  int BucketIndex(uint64_t value) const;
  int SubBucketIndex(uint64_t value, int bucket_index) const;

  // Count of all events recorded.
  uint64_t TotalCount() const { return base::subtle::NoBarrier_Load(&total_count_); }

  // Count of all events recorded since last Reset. Resets to 0 after
  // Reset().
  uint64_t CurrentCount() const {
    return base::subtle::NoBarrier_Load(&current_count_);
  }

  // Sum of all events recorded.
  uint64_t TotalSum() const { return base::subtle::NoBarrier_Load(&total_sum_); }

  // Sum of all events recorded since last Reset. Resets to 0 after
  // Reset().
  uint64_t CurrentSum() const {
    return base::subtle::NoBarrier_Load(&current_sum_);
  }

  // Return number of items at index.
  uint64_t CountAt(int bucket_index, int sub_bucket_index) const;

  // Return count of values in bucket with values equivalent to value.
  uint64_t CountInBucketForValue(uint64_t) const;

  // Return representative value based on index.
  static uint64_t ValueFromIndex(int bucket_index, int sub_bucket_index);

  // Get the size (in value units) of the range of values that are equivalent
  // to the given value within the histogram's resolution. Where "equivalent"
  // means that value samples recorded for any two equivalent values are
  // counted in a common total count.
  uint64_t SizeOfEquivalentValueRange(uint64_t value) const;

  // Get the lowest value that is equivalent to the given value within the
  // histogram's resolution. Where "equivalent" means that value samples
  // recorded for any two equivalent values are counted in a common total
  // count.
  uint64_t LowestEquivalentValue(uint64_t value) const;

  // Get the highest value that is equivalent to the given value within the
  // histogram's resolution.
  uint64_t HighestEquivalentValue(uint64_t value) const;

  // Get a value that lies in the middle (rounded up) of the range of values
  // equivalent the given value.
  uint64_t MedianEquivalentValue(uint64_t value) const;

  // Get the next value that is not equivalent to the given value within the
  // histogram's resolution.
  uint64_t NextNonEquivalentValue(uint64_t value) const;

  // Determine if two values are equivalent with the histogram's resolution.
  bool ValuesAreEquivalent(uint64_t value1, uint64_t value2) const;

  // Get the exact minimum value (may lie outside the histogram).
  uint64_t MinValue() const;

  // Get the exact maximum value (may lie outside the histogram).
  uint64_t MaxValue() const;

  // Get the exact mean value of all recorded values in the histogram.
  double MeanValue() const;

  // Get the value at a given percentile.
  // This is a percentile in percents, i.e. 99.99 percentile.
  uint64_t ValueAtPercentile(double percentile) const;

  // Resets the counts_ array to reset the percentiles information.
  // Preserves the values for TotalSum and TotalCount.
  void Reset();

  // Get the percentile at a given value
  // TODO: implement
  // double PercentileAtOrBelowValue(uint64_t value) const;

  // Get the count of recorded values within a range of value levels.
  // (inclusive to within the histogram's resolution)
  // TODO: implement
  // uint64_t CountBetweenValues(uint64_t low_value, uint64_t high_value) const;

  // Dump a formatted, multiline string describing this histogram to 'out'.
  void DumpHumanReadable(std::ostream* out) const;

  size_t DynamicMemoryUsage() const;

 private:
  friend class AbstractHistogramIterator;

  static const uint64_t kMinHighestTrackableValue = 2;
  static const int kMinValidNumSignificantDigits = 1;
  static const int kMaxValidNumSignificantDigits = 5;

  void Init();
  int CountsArrayIndex(int bucket_index, int sub_bucket_index) const;

  uint64_t highest_trackable_value_;
  int num_significant_digits_;
  int counts_array_length_;
  int bucket_count_;
  int sub_bucket_count_;

  // "Hot" fields in the write path.
  uint8_t sub_bucket_half_count_magnitude_;
  int sub_bucket_half_count_;
  uint32_t sub_bucket_mask_;

  // Also hot.
  // Non-resetting sum and counts.
  base::subtle::Atomic64 total_count_;
  base::subtle::Atomic64 total_sum_;
  // Resetting values
  base::subtle::Atomic64 current_count_;
  base::subtle::Atomic64 current_sum_;
  base::subtle::Atomic64 min_value_;
  base::subtle::Atomic64 max_value_;
  std::unique_ptr<base::subtle::Atomic64[]> counts_;

  HdrHistogram& operator=(const HdrHistogram& other); // Disable assignment operator.
};

// Value returned from iterators.
struct HistogramIterationValue {
  HistogramIterationValue()
    : value_iterated_to(0),
      value_iterated_from(0),
      count_at_value_iterated_to(0),
      count_added_in_this_iteration_step(0),
      total_count_to_this_value(0),
      total_value_to_this_value(0),
      percentile(0.0),
      percentile_level_iterated_to(0.0) {
  }

  void Reset() {
    value_iterated_to = 0;
    value_iterated_from = 0;
    count_at_value_iterated_to = 0;
    count_added_in_this_iteration_step = 0;
    total_count_to_this_value = 0;
    total_value_to_this_value = 0;
    percentile = 0.0;
    percentile_level_iterated_to = 0.0;
  }

  uint64_t value_iterated_to;
  uint64_t value_iterated_from;
  uint64_t count_at_value_iterated_to;
  uint64_t count_added_in_this_iteration_step;
  uint64_t total_count_to_this_value;
  uint64_t total_value_to_this_value;
  double percentile;
  double percentile_level_iterated_to;
};

// Base class for iterating through histogram values.
//
// The underlying histogram must not be modified or destroyed while this class
// is iterating over it.
//
// This class is not thread-safe.
class AbstractHistogramIterator {
 public:
  // Create iterator with new histogram.
  // The histogram must not be mutated while the iterator is in use.
  explicit AbstractHistogramIterator(const HdrHistogram* histogram);
  virtual ~AbstractHistogramIterator() {
  }

  // Returns true if the iteration has more elements.
  virtual bool HasNext() const;

  // Returns the next element in the iteration.
  Status Next(HistogramIterationValue* value);

  virtual double PercentileIteratedTo() const;
  virtual double PercentileIteratedFrom() const;
  uint64_t ValueIteratedTo() const;

 protected:
  // Implementations must override these methods.
  virtual void IncrementIterationLevel() = 0;
  virtual bool ReachedIterationLevel() const = 0;

  const HdrHistogram* histogram_;
  HistogramIterationValue cur_iter_val_;

  uint64_t histogram_total_count_;

  int current_bucket_index_;
  int current_sub_bucket_index_;
  uint64_t current_value_at_index_;

  int next_bucket_index_;
  int next_sub_bucket_index_;
  uint64_t next_value_at_index_;

  uint64_t prev_value_iterated_to_;
  uint64_t total_count_to_prev_index_;

  uint64_t total_count_to_current_index_;
  uint64_t total_value_to_current_index_;

  uint64_t count_at_this_value_;

 private:
  bool ExhaustedSubBuckets() const;
  void IncrementSubBucket();

  bool fresh_sub_bucket_;

  DISALLOW_COPY_AND_ASSIGN(AbstractHistogramIterator);
};

// Used for iterating through all recorded histogram values using the finest
// granularity steps supported by the underlying representation. The iteration
// steps through all non-zero recorded value counts, and terminates when all
// recorded histogram values are exhausted.
//
// The underlying histogram must not be modified or destroyed while this class
// is iterating over it.
//
// This class is not thread-safe.
class RecordedValuesIterator : public AbstractHistogramIterator {
 public:
  explicit RecordedValuesIterator(const HdrHistogram* histogram);

 protected:
  virtual void IncrementIterationLevel() override;
  virtual bool ReachedIterationLevel() const override;

 private:
  int visited_sub_bucket_index_;
  int visited_bucket_index_;

  DISALLOW_COPY_AND_ASSIGN(RecordedValuesIterator);
};

// Used for iterating through histogram values according to percentile levels.
// The iteration is performed in steps that start at 0% and reduce their
// distance to 100% according to the percentileTicksPerHalfDistance parameter,
// ultimately reaching 100% when all recorded histogram values are exhausted.
//
// The underlying histogram must not be modified or destroyed while this class
// is iterating over it.
//
// This class is not thread-safe.
class PercentileIterator : public AbstractHistogramIterator {
 public:
  // TODO: Explain percentile_ticks_per_half_distance.
  PercentileIterator(const HdrHistogram* histogram,
                     int percentile_ticks_per_half_distance);
  virtual bool HasNext() const override;
  virtual double PercentileIteratedTo() const override;
  virtual double PercentileIteratedFrom() const override;

 protected:
  virtual void IncrementIterationLevel() override;
  virtual bool ReachedIterationLevel() const override;

 private:
  int percentile_ticks_per_half_distance_;
  double percentile_level_to_iterate_to_;
  double percentile_level_to_iterate_from_;
  bool reached_last_recorded_value_;

  DISALLOW_COPY_AND_ASSIGN(PercentileIterator);
};

} // namespace yb
