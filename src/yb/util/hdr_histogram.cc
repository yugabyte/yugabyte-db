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
#include "yb/util/hdr_histogram.h"

#include <math.h>

#include <limits>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/bits.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/status.h"

using base::subtle::Atomic64;
using base::subtle::NoBarrier_AtomicIncrement;
using base::subtle::NoBarrier_Store;
using base::subtle::NoBarrier_Load;
using base::subtle::NoBarrier_CompareAndSwap;
using strings::Substitute;
using std::endl;

namespace yb {

HdrHistogram::HdrHistogram(uint64_t highest_trackable_value, int num_significant_digits)
  : highest_trackable_value_(highest_trackable_value),
    num_significant_digits_(num_significant_digits),
    counts_array_length_(0),
    bucket_count_(0),
    sub_bucket_count_(0),
    sub_bucket_half_count_magnitude_(0),
    sub_bucket_half_count_(0),
    sub_bucket_mask_(0),
    total_count_(0),
    total_sum_(0),
    current_count_(0),
    current_sum_(0),
    min_value_(std::numeric_limits<Atomic64>::max()),
    max_value_(0),
    counts_(nullptr) {
  Init();
}

HdrHistogram::HdrHistogram(const HdrHistogram& other)
  : highest_trackable_value_(other.highest_trackable_value_),
    num_significant_digits_(other.num_significant_digits_),
    counts_array_length_(0),
    bucket_count_(0),
    sub_bucket_count_(0),
    sub_bucket_half_count_magnitude_(0),
    sub_bucket_half_count_(0),
    sub_bucket_mask_(0),
    total_count_(0),
    total_sum_(0),
    current_count_(0),
    current_sum_(0),
    min_value_(std::numeric_limits<Atomic64>::max()),
    max_value_(0),
    counts_(nullptr) {
  Init();

  // Not a consistent snapshot but we try to roughly keep it close.
  // Copy the sum and min first.
  NoBarrier_Store(&total_sum_, NoBarrier_Load(&other.total_sum_));
  NoBarrier_Store(&current_sum_, NoBarrier_Load(&other.current_sum_));
  NoBarrier_Store(&min_value_, NoBarrier_Load(&other.min_value_));

  uint64_t total_copied_count = 0;
  // Copy the counts in order of ascending magnitude.
  for (int i = 0; i < counts_array_length_; i++) {
    uint64_t count = NoBarrier_Load(&other.counts_[i]);
    NoBarrier_Store(&counts_[i], count);
    total_copied_count += count;
  }
  // Copy the max observed value last.
  NoBarrier_Store(&max_value_, NoBarrier_Load(&other.max_value_));
  // We must ensure the total is consistent with the copied counts.
  NoBarrier_Store(&total_count_, NoBarrier_Load(&other.total_count_));
  NoBarrier_Store(&current_count_, total_copied_count);
}

void HdrHistogram::Reset() {
  for (int i = 0; i < counts_array_length_; i++) {
    NoBarrier_Store(&counts_[i], 0);
  }
  NoBarrier_Store(&current_count_, 0);
  NoBarrier_Store(&current_sum_, 0);

  NoBarrier_Store(&min_value_, std::numeric_limits<Atomic64>::max());
  NoBarrier_Store(&max_value_, 0);
}

bool HdrHistogram::IsValidHighestTrackableValue(uint64_t highest_trackable_value) {
  return highest_trackable_value >= kMinHighestTrackableValue;
}

bool HdrHistogram::IsValidNumSignificantDigits(int num_significant_digits) {
  return num_significant_digits >= kMinValidNumSignificantDigits &&
         num_significant_digits <= kMaxValidNumSignificantDigits;
}

void HdrHistogram::Init() {
  // Verify parameter validity
  CHECK(IsValidHighestTrackableValue(highest_trackable_value_)) <<
      Substitute("highest_trackable_value must be >= $0", kMinHighestTrackableValue);
  CHECK(IsValidNumSignificantDigits(num_significant_digits_)) <<
      Substitute("num_significant_digits must be between $0 and $1",
          kMinValidNumSignificantDigits, kMaxValidNumSignificantDigits);

  uint32_t largest_value_with_single_unit_resolution =
      2 * static_cast<uint32_t>(pow(10.0, num_significant_digits_));

  // We need to maintain power-of-two sub_bucket_count_ (for clean direct
  // indexing) that is large enough to provide unit resolution to at least
  // largest_value_with_single_unit_resolution. So figure out
  // largest_value_with_single_unit_resolution's nearest power-of-two
  // (rounded up), and use that:

  // The sub-buckets take care of the precision.
  // Each sub-bucket is sized to have enough bits for the requested
  // 10^precision accuracy.
  int sub_bucket_count_magnitude =
      Bits::Log2Ceiling(largest_value_with_single_unit_resolution);
  sub_bucket_half_count_magnitude_ =
      (sub_bucket_count_magnitude >= 1) ? sub_bucket_count_magnitude - 1 : 0;

  // sub_bucket_count_ is approx. 10^num_sig_digits (as a power of 2)
  sub_bucket_count_ = pow(2.0, sub_bucket_half_count_magnitude_ + 1);
  sub_bucket_mask_ = sub_bucket_count_ - 1;
  sub_bucket_half_count_ = sub_bucket_count_ / 2;

  // The buckets take care of the magnitude.
  // Determine exponent range needed to support the trackable value with no
  // overflow:
  uint64_t trackable_value = sub_bucket_count_ - 1;
  int buckets_needed = 1;
  while (trackable_value < highest_trackable_value_) {
    trackable_value <<= 1;
    buckets_needed++;
  }
  bucket_count_ = buckets_needed;

  counts_array_length_ = (bucket_count_ + 1) * sub_bucket_half_count_;
  counts_.reset(new Atomic64[counts_array_length_]());  // value-initialized
}

void HdrHistogram::Increment(int64_t value) {
  IncrementBy(value, 1);
}

void HdrHistogram::IncrementBy(int64_t value, int64_t count) {
  DCHECK_GE(value, 0);
  DCHECK_GE(count, 0);

  // Dissect the value into bucket and sub-bucket parts, and derive index into
  // counts array:
  int bucket_index = BucketIndex(value);
  int sub_bucket_index = SubBucketIndex(value, bucket_index);
  int counts_index = CountsArrayIndex(bucket_index, sub_bucket_index);

  // Increment bucket, total, and sum.
  NoBarrier_AtomicIncrement(&counts_[counts_index], count);
  NoBarrier_AtomicIncrement(&total_count_, count);
  NoBarrier_AtomicIncrement(&current_count_, count);
  NoBarrier_AtomicIncrement(&total_sum_, value * count);
  NoBarrier_AtomicIncrement(&current_sum_, value * count);

  // Update min, if needed.
  {
    Atomic64 min_val;
    while (PREDICT_FALSE(value < (min_val = MinValue()))) {
      Atomic64 old_val = NoBarrier_CompareAndSwap(&min_value_, min_val, value);
      if (PREDICT_TRUE(old_val == min_val)) break; // CAS success.
    }
  }

  // Update max, if needed.
  {
    Atomic64 max_val;
    while (PREDICT_FALSE(value > (max_val = MaxValue()))) {
      Atomic64 old_val = NoBarrier_CompareAndSwap(&max_value_, max_val, value);
      if (PREDICT_TRUE(old_val == max_val)) break; // CAS success.
    }
  }
}

void HdrHistogram::IncrementWithExpectedInterval(int64_t value,
                                                 int64_t expected_interval_between_samples) {
  Increment(value);
  if (expected_interval_between_samples <= 0) {
    return;
  }
  for (int64_t missing_value = value - expected_interval_between_samples;
      missing_value >= expected_interval_between_samples;
      missing_value -= expected_interval_between_samples) {
    Increment(missing_value);
  }
}

////////////////////////////////////

int HdrHistogram::BucketIndex(uint64_t value) const {
  if (PREDICT_FALSE(value > highest_trackable_value_)) {
    value = highest_trackable_value_;
  }
  // Here we are calculating the power-of-2 magnitude of the value with a
  // correction for precision in the first bucket.
  // Smallest power of 2 containing value.
  int pow2ceiling = Bits::Log2Ceiling64(value | sub_bucket_mask_);
  return pow2ceiling - (sub_bucket_half_count_magnitude_ + 1);
}

int HdrHistogram::SubBucketIndex(uint64_t value, int bucket_index) const {
  if (PREDICT_FALSE(value > highest_trackable_value_)) {
    value = highest_trackable_value_;
  }
  // We hack off the magnitude and are left with only the relevant precision
  // portion, which gives us a direct index into the sub-bucket. TODO: Right??
  return static_cast<int>(value >> bucket_index);
}

int HdrHistogram::CountsArrayIndex(int bucket_index, int sub_bucket_index) const {
  DCHECK(sub_bucket_index < sub_bucket_count_);
  DCHECK(bucket_index < bucket_count_);
  DCHECK(bucket_index == 0 || (sub_bucket_index >= sub_bucket_half_count_));
  // Calculate the index for the first entry in the bucket:
  // (The following is the equivalent of ((bucket_index + 1) * sub_bucket_half_count_) ):
  int bucket_base_index = (bucket_index + 1) << sub_bucket_half_count_magnitude_;
  // Calculate the offset in the bucket:
  int offset_in_bucket = sub_bucket_index - sub_bucket_half_count_;
  return bucket_base_index + offset_in_bucket;
}

uint64_t HdrHistogram::CountAt(int bucket_index, int sub_bucket_index) const {
  return counts_[CountsArrayIndex(bucket_index, sub_bucket_index)];
}

uint64_t HdrHistogram::CountInBucketForValue(uint64_t value) const {
  int bucket_index = BucketIndex(value);
  int sub_bucket_index = SubBucketIndex(value, bucket_index);
  return CountAt(bucket_index, sub_bucket_index);
}

uint64_t HdrHistogram::ValueFromIndex(int bucket_index, int sub_bucket_index) {
  return static_cast<uint64_t>(sub_bucket_index) << bucket_index;
}

////////////////////////////////////

uint64_t HdrHistogram::SizeOfEquivalentValueRange(uint64_t value) const {
  int bucket_index = BucketIndex(value);
  int sub_bucket_index = SubBucketIndex(value, bucket_index);
  uint64_t distance_to_next_value =
    (1 << ((sub_bucket_index >= sub_bucket_count_) ? (bucket_index + 1) : bucket_index));
  return distance_to_next_value;
}

uint64_t HdrHistogram::LowestEquivalentValue(uint64_t value) const {
  int bucket_index = BucketIndex(value);
  int sub_bucket_index = SubBucketIndex(value, bucket_index);
  uint64_t this_value_base_level = ValueFromIndex(bucket_index, sub_bucket_index);
  return this_value_base_level;
}

uint64_t HdrHistogram::HighestEquivalentValue(uint64_t value) const {
  return NextNonEquivalentValue(value) - 1;
}

uint64_t HdrHistogram::MedianEquivalentValue(uint64_t value) const {
  return (LowestEquivalentValue(value) + (SizeOfEquivalentValueRange(value) >> 1));
}

uint64_t HdrHistogram::NextNonEquivalentValue(uint64_t value) const {
  return LowestEquivalentValue(value) + SizeOfEquivalentValueRange(value);
}

bool HdrHistogram::ValuesAreEquivalent(uint64_t value1, uint64_t value2) const {
  return (LowestEquivalentValue(value1) == LowestEquivalentValue(value2));
}

uint64_t HdrHistogram::MinValue() const {
  if (PREDICT_FALSE(CurrentCount() == 0)) {
    return 0;
  }
  return NoBarrier_Load(&min_value_);
}

uint64_t HdrHistogram::MaxValue() const {
  if (PREDICT_FALSE(CurrentCount() == 0)) {
    return 0;
  }
  return NoBarrier_Load(&max_value_);
}

double HdrHistogram::MeanValue() const {
  uint64_t count = CurrentCount();
  if (PREDICT_FALSE(count == 0)) return 0.0;
  return static_cast<double>(CurrentSum()) / count;
}

uint64_t HdrHistogram::ValueAtPercentile(double percentile) const {
  uint64_t count = CurrentCount();
  if (PREDICT_FALSE(count == 0)) return 0;

  double requested_percentile = std::min(percentile, 100.0); // Truncate down to 100%
  uint64_t count_at_percentile =
    static_cast<uint64_t>(((requested_percentile / 100.0) * count) + 0.5); // Round
  // Make sure we at least reach the first recorded entry
  count_at_percentile = std::max(count_at_percentile, static_cast<uint64_t>(1));

  uint64_t total_to_current_iJ = 0;
  for (int i = 0; i < bucket_count_; i++) {
    int j = (i == 0) ? 0 : (sub_bucket_count_ / 2);
    for (; j < sub_bucket_count_; j++) {
      total_to_current_iJ += CountAt(i, j);
      if (total_to_current_iJ >= count_at_percentile) {
        uint64_t valueAtIndex = ValueFromIndex(i, j);
        return valueAtIndex;
      }
    }
  }

  LOG(DFATAL) << "Fell through while iterating, likely concurrent modification of histogram";
  return 0;
}

void HdrHistogram::DumpHumanReadable(std::ostream* out) const {
  *out << "Total Count: " << TotalCount() << endl;
  *out << "Mean: " << MeanValue() << endl;
  *out << "Percentiles:" << endl;
  *out << "CountInBuckets: " << CurrentCount() << endl;
  *out << "   0%  (min) = " << MinValue() << endl;
  *out << "  25%        = " << ValueAtPercentile(25) << endl;
  *out << "  50%  (med) = " << ValueAtPercentile(50) << endl;
  *out << "  75%        = " << ValueAtPercentile(75) << endl;
  *out << "  95%        = " << ValueAtPercentile(95) << endl;
  *out << "  99%        = " << ValueAtPercentile(99) << endl;
  *out << "  99.9%      = " << ValueAtPercentile(99.9) << endl;
  *out << "  99.99%     = " << ValueAtPercentile(99.99) << endl;
  *out << "  100% (max) = " << MaxValue() << endl;
  if (MaxValue() >= highest_trackable_value()) {
    *out << "*NOTE: some values were greater than highest trackable value" << endl;
  }
}

size_t HdrHistogram::DynamicMemoryUsage() const {
  return sizeof(*this) + sizeof(Atomic64) * counts_array_length_;
}

///////////////////////////////////////////////////////////////////////
// AbstractHistogramIterator
///////////////////////////////////////////////////////////////////////

AbstractHistogramIterator::AbstractHistogramIterator(const HdrHistogram* histogram)
  : histogram_(CHECK_NOTNULL(histogram)),
    cur_iter_val_(),
    histogram_total_count_(histogram_->CurrentCount()),
    current_bucket_index_(0),
    current_sub_bucket_index_(0),
    current_value_at_index_(0),
    next_bucket_index_(0),
    next_sub_bucket_index_(1),
    next_value_at_index_(1),
    prev_value_iterated_to_(0),
    total_count_to_prev_index_(0),
    total_count_to_current_index_(0),
    total_value_to_current_index_(0),
    count_at_this_value_(0),
    fresh_sub_bucket_(true) {
}

bool AbstractHistogramIterator::HasNext() const {
  return total_count_to_current_index_ < histogram_total_count_;
}

Status AbstractHistogramIterator::Next(HistogramIterationValue* value) {
  if (histogram_->CurrentCount() != histogram_total_count_) {
    return STATUS(IllegalState, "Concurrently modified histogram while traversing it");
  }

  // Move through the sub buckets and buckets until we hit the next reporting level:
  while (!ExhaustedSubBuckets()) {
    count_at_this_value_ =
        histogram_->CountAt(current_bucket_index_, current_sub_bucket_index_);
    if (fresh_sub_bucket_) { // Don't add unless we've incremented since last bucket...
      total_count_to_current_index_ += count_at_this_value_;
      total_value_to_current_index_ +=
        count_at_this_value_ * histogram_->MedianEquivalentValue(current_value_at_index_);
      fresh_sub_bucket_ = false;
    }
    if (ReachedIterationLevel()) {
      uint64_t value_iterated_to = ValueIteratedTo();

      // Update iterator value.
      cur_iter_val_.value_iterated_to = value_iterated_to;
      cur_iter_val_.value_iterated_from = prev_value_iterated_to_;
      cur_iter_val_.count_at_value_iterated_to = count_at_this_value_;
      cur_iter_val_.count_added_in_this_iteration_step =
          (total_count_to_current_index_ - total_count_to_prev_index_);
      cur_iter_val_.total_count_to_this_value = total_count_to_current_index_;
      cur_iter_val_.total_value_to_this_value = total_value_to_current_index_;
      cur_iter_val_.percentile =
          ((100.0 * total_count_to_current_index_) / histogram_total_count_);
      cur_iter_val_.percentile_level_iterated_to = PercentileIteratedTo();

      prev_value_iterated_to_ = value_iterated_to;
      total_count_to_prev_index_ = total_count_to_current_index_;
      // Move the next percentile reporting level forward.
      IncrementIterationLevel();

      *value = cur_iter_val_;
      return Status::OK();
    }
    IncrementSubBucket();
  }
  return STATUS(IllegalState, "Histogram array index out of bounds while traversing");
}

double AbstractHistogramIterator::PercentileIteratedTo() const {
  return (100.0 * static_cast<double>(total_count_to_current_index_)) / histogram_total_count_;
}

double AbstractHistogramIterator::PercentileIteratedFrom() const {
  return (100.0 * static_cast<double>(total_count_to_prev_index_)) / histogram_total_count_;
}

uint64_t AbstractHistogramIterator::ValueIteratedTo() const {
  return histogram_->HighestEquivalentValue(current_value_at_index_);
}

bool AbstractHistogramIterator::ExhaustedSubBuckets() const {
  return (current_bucket_index_ >= histogram_->bucket_count_);
}

void AbstractHistogramIterator::IncrementSubBucket() {
  fresh_sub_bucket_ = true;
  // Take on the next index:
  current_bucket_index_ = next_bucket_index_;
  current_sub_bucket_index_ = next_sub_bucket_index_;
  current_value_at_index_ = next_value_at_index_;
  // Figure out the next next index:
  next_sub_bucket_index_++;
  if (next_sub_bucket_index_ >= histogram_->sub_bucket_count_) {
    next_sub_bucket_index_ = histogram_->sub_bucket_half_count_;
    next_bucket_index_++;
  }
  next_value_at_index_ = HdrHistogram::ValueFromIndex(next_bucket_index_, next_sub_bucket_index_);
}

///////////////////////////////////////////////////////////////////////
// RecordedValuesIterator
///////////////////////////////////////////////////////////////////////

RecordedValuesIterator::RecordedValuesIterator(const HdrHistogram* histogram)
  : AbstractHistogramIterator(histogram),
    visited_sub_bucket_index_(-1),
    visited_bucket_index_(-1) {
}

void RecordedValuesIterator::IncrementIterationLevel() {
  visited_sub_bucket_index_ = current_sub_bucket_index_;
  visited_bucket_index_ = current_bucket_index_;
}

bool RecordedValuesIterator::ReachedIterationLevel() const {
  uint64_t current_ij_count =
      histogram_->CountAt(current_bucket_index_, current_sub_bucket_index_);
  return current_ij_count != 0 &&
      ((visited_sub_bucket_index_ != current_sub_bucket_index_) ||
       (visited_bucket_index_ != current_bucket_index_));
}

///////////////////////////////////////////////////////////////////////
// PercentileIterator
///////////////////////////////////////////////////////////////////////

PercentileIterator::PercentileIterator(const HdrHistogram* histogram,
                                       int percentile_ticks_per_half_distance)
  : AbstractHistogramIterator(histogram),
    percentile_ticks_per_half_distance_(percentile_ticks_per_half_distance),
    percentile_level_to_iterate_to_(0.0),
    percentile_level_to_iterate_from_(0.0),
    reached_last_recorded_value_(false) {
}

bool PercentileIterator::HasNext() const {
  if (AbstractHistogramIterator::HasNext()) {
    return true;
  }
  // We want one additional last step to 100%
  if (!reached_last_recorded_value_ && (histogram_total_count_ > 0)) {
    const_cast<PercentileIterator*>(this)->percentile_level_to_iterate_to_ = 100.0;
    const_cast<PercentileIterator*>(this)->reached_last_recorded_value_ = true;
    return true;
  }
  return false;
}

double PercentileIterator::PercentileIteratedTo() const {
  return percentile_level_to_iterate_to_;
}


double PercentileIterator::PercentileIteratedFrom() const {
  return percentile_level_to_iterate_from_;
}

void PercentileIterator::IncrementIterationLevel() {
  percentile_level_to_iterate_from_ = percentile_level_to_iterate_to_;
  // TODO: Can this expression be simplified?
  uint64_t percentile_reporting_ticks = percentile_ticks_per_half_distance_ *
    static_cast<uint64_t>(pow(2.0,
          static_cast<int>(log(100.0 / (100.0 - (percentile_level_to_iterate_to_))) / log(2)) + 1));
  percentile_level_to_iterate_to_ += 100.0 / percentile_reporting_ticks;
}

bool PercentileIterator::ReachedIterationLevel() const {
  if (count_at_this_value_ == 0) return false;
  double current_percentile =
      (100.0 * static_cast<double>(total_count_to_current_index_)) / histogram_total_count_;
  return (current_percentile >= percentile_level_to_iterate_to_);
}

} // namespace yb
