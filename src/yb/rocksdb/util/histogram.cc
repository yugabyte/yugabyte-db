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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/util/histogram.h"

#include <math.h>

#include "yb/gutil/port.h"

namespace rocksdb {

HistogramBucketMapper::HistogramBucketMapper()
    :
      // Add newer bucket index here.
      // Should be always added in sorted order.
      // If you change this, you also need to change the size of array buckets_ and the initial
      // value of min_ in HistogramImpl.
      bucketValues_(
          {1,         2,         3,         4,         5,         6,
           7,         8,         9,         10,        12,        14,
           16,        18,        20,        25,        30,        35,
           40,        45,        50,        60,        70,        80,
           90,        100,       120,       140,       160,       180,
           200,       250,       300,       350,       400,       450,
           500,       600,       700,       800,       900,       1000,
           1200,      1400,      1600,      1800,      2000,      2500,
           3000,      3500,      4000,      4500,      5000,      6000,
           7000,      8000,      9000,      10000,     12000,     14000,
           16000,     18000,     20000,     25000,     30000,     35000,
           40000,     45000,     50000,     60000,     70000,     80000,
           90000,     100000,    120000,    140000,    160000,    180000,
           200000,    250000,    300000,    350000,    400000,    450000,
           500000,    600000,    700000,    800000,    900000,    1000000,
           1200000,   1400000,   1600000,   1800000,   2000000,   2500000,
           3000000,   3500000,   4000000,   4500000,   5000000,   6000000,
           7000000,   8000000,   9000000,   10000000,  12000000,  14000000,
           16000000,  18000000,  20000000,  25000000,  30000000,  35000000,
           40000000,  45000000,  50000000,  60000000,  70000000,  80000000,
           90000000,  100000000, 120000000, 140000000, 160000000, 180000000,
           200000000, 250000000, 300000000, 350000000, 400000000, 450000000,
           500000000, 600000000, 700000000, 800000000, 900000000, 1000000000}),
      maxBucketValue_(bucketValues_.back()),
      minBucketValue_(bucketValues_.front()) {
  assert(kHistogramNumBuckets == BucketCount());
  for (size_t i =0; i < bucketValues_.size(); ++i) {
    valueIndexMap_[bucketValues_[i]] = i;
  }
}

size_t HistogramBucketMapper::IndexForValue(const uint64_t value) const {
  if (value >= maxBucketValue_) {
    return bucketValues_.size() - 1;
  } else if ( value >= minBucketValue_ ) {
    std::map<uint64_t, uint64_t>::const_iterator lowerBound =
      valueIndexMap_.lower_bound(value);
    if (lowerBound != valueIndexMap_.end()) {
      return static_cast<size_t>(lowerBound->second);
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}

namespace {
  const HistogramBucketMapper bucketMapper;
}

void HistogramImpl::Clear() {
  set_min(static_cast<double>(bucketMapper.LastValue()));
  set_max(0);
  num_ = 0;
  sum_ = 0;
  sum_squares_ = 0;
  for (int i = 0; i < kHistogramNumBuckets; ++i) {
    set_bucket(i, 0);
  }
}

bool HistogramImpl::Empty() { return num_ == 0; }

void HistogramImpl::Add(uint64_t value) {
  update_min(value);
  update_max(value);

  add_to_num(1);
  add_to_sum(value);
  add_to_sum_squares(static_cast<double>(value) * value);

  // TODO: maybe we should use int for IndexForValue's return value?
  // There are only 200 or so buckets.
  add_to_bucket(static_cast<int>(bucketMapper.IndexForValue(value)), 1);
}

void HistogramImpl::Merge(const HistogramImpl& other) {
  update_min(other.min());
  update_max(other.max());
  add_to_num(other.num_);
  add_to_sum(other.sum_);
  add_to_sum_squares(other.sum_squares_);
  for (unsigned int b = 0; b < bucketMapper.BucketCount(); b++) {
    add_to_bucket(b, other.bucket(b));
  }
}

double HistogramImpl::Median() const {
  return Percentile(50.0);
}

double HistogramImpl::Percentile(double p) const {
  double threshold = num_ * (p / 100.0);
  double sum = 0;
  const double current_min = min();
  const double current_max = max();
  for (unsigned int b = 0; b < bucketMapper.BucketCount(); b++) {
    sum += buckets_[b];
    if (sum >= threshold) {
      // Scale linearly within this bucket
      double left_point =
        static_cast<double>((b == 0) ? 0 : bucketMapper.BucketLimit(b-1));
      double right_point =
        static_cast<double>(bucketMapper.BucketLimit(b));
      double left_sum = sum - bucket(b);
      double right_sum = sum;
      double pos = 0;
      double right_left_diff = right_sum - left_sum;
      if (right_left_diff != 0) {
       pos = (threshold - left_sum) / (right_sum - left_sum);
      }
      double r = left_point + (right_point - left_point) * pos;
      if (r < current_min) r = current_min;
      if (r > current_max) r = current_max;
      return r;
    }
  }
  return current_max;
}

double HistogramImpl::Average() const {
  if (num_ == 0.0) return 0;
  return sum_ / num_;
}

double HistogramImpl::StandardDeviation() const {
  if (num_ == 0.0) return 0;
  double variance = (sum_squares_ * num_ - sum_ * sum_) / (num_ * num_);
  return sqrt(variance);
}

std::string HistogramImpl::ToString() const {
  std::string r;
  char buf[200];
  auto current_num = num();
  snprintf(buf, sizeof(buf),
           "Count: %.0f  Average: %.4f  StdDev: %.2f\n",
           static_cast<double>(current_num), Average(), StandardDeviation());
  r.append(buf);
  snprintf(buf, sizeof(buf),
           "Min: %.4f  Median: %.4f  Max: %.4f\n",
           (current_num == 0.0 ? 0.0 : min()), Median(), max());
  r.append(buf);
  snprintf(buf, sizeof(buf),
           "Percentiles: "
           "P50: %.2f P75: %.2f P99: %.2f P99.9: %.2f P99.99: %.2f\n",
           Percentile(50), Percentile(75), Percentile(99), Percentile(99.9),
           Percentile(99.99));
  r.append(buf);
  r.append("------------------------------------------------------\n");
  const double mult = 100.0 / current_num;
  double sum = 0;
  for (unsigned int b = 0; b < bucketMapper.BucketCount(); b++) {
    const auto bucket_value = bucket(b);
    if (bucket_value <= 0) continue;
    sum += bucket_value;
    snprintf(buf, sizeof(buf),
             "[ %7" PRIu64 ", %7" PRIu64 " ) %8" PRIu64 " %7.3f%% %7.3f%% ",
             b == 0 ? 0 : bucketMapper.BucketLimit(b - 1),  // left
             bucketMapper.BucketLimit(b),                   // right
             bucket_value,         // count
             mult * bucket_value,  // percentage
             mult * sum);          // cumulative percentage
    r.append(buf);

    // Add hash marks based on percentage; 20 marks for 100%.
    int marks = static_cast<int>(20*(bucket_value / current_num) + 0.5);
    r.append(marks, '#');
    r.push_back('\n');
  }
  return r;
}

void HistogramImpl::Data(HistogramData * const data) const {
  assert(data);
  data->count = num_;
  data->sum = sum_;
  data->min = min();
  data->max = max();
  data->average = Average();
}

} // namespace rocksdb
