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

#pragma once
#include "yb/rocksdb/statistics.h"

#include <atomic>
#include <cassert>
#include <string>
#include <vector>
#include <map>

#include <string.h>

namespace rocksdb {

static constexpr int kHistogramNumBuckets = 138;

class HistogramBucketMapper {
 public:

  HistogramBucketMapper();

  // converts a value to the bucket index.
  size_t IndexForValue(const uint64_t value) const;
  // number of buckets required.

  size_t BucketCount() const {
    return bucketValues_.size();
  }

  uint64_t LastValue() const {
    return maxBucketValue_;
  }

  uint64_t FirstValue() const {
    return minBucketValue_;
  }

  uint64_t BucketLimit(const size_t bucketNumber) const {
    assert(bucketNumber < BucketCount());
    return bucketValues_[bucketNumber];
  }

 private:
  const std::vector<uint64_t> bucketValues_;
  const uint64_t maxBucketValue_;
  const uint64_t minBucketValue_;
  std::map<uint64_t, uint64_t> valueIndexMap_;

};

class HistogramImpl {
 public:
  HistogramImpl() {
    // Original comment from RocksDB: "this is BucketMapper:LastValue()".
    // TODO: is there a cleaner way to set it here?
    set_min(1000000000);
    set_max(0);
    set_num(0);
    set_sum(0);
    set_sum_squares(0);
    for (int i = 0; i < kHistogramNumBuckets; ++i) {
      set_bucket(i, 0);
    }
  }

  HistogramImpl(const HistogramImpl& other) {
    set_min(other.min());
    set_max(other.max());
    set_num(other.num());
    set_sum(other.sum());
    set_sum_squares(other.sum_squares());
    for (int i = 0; i < kHistogramNumBuckets; ++i) {
      set_bucket(i, other.bucket(i));
    }
  }

  virtual void Clear();
  virtual bool Empty();
  virtual void Add(uint64_t value);
  void Merge(const HistogramImpl& other);

  virtual std::string ToString() const;

  virtual double Median() const;
  virtual double Percentile(double p) const;
  virtual double Average() const;
  virtual double StandardDeviation() const;
  virtual void Data(HistogramData * const data) const;

  virtual ~HistogramImpl() {}

 private:
  std::atomic<double> min_;
  std::atomic<double> max_;
  std::atomic<uint64_t> num_;
  std::atomic<double> sum_;
  std::atomic<double> sum_squares_;
  std::atomic<uint64_t> buckets_[kHistogramNumBuckets];

  // We don't use any synchronization because that's what RocksDB has been doing, and we need to
  // measure the performance impact before using a stronger memory order.
  static constexpr auto kMemoryOrder = std::memory_order_relaxed;

  double min() const { return min_.load(kMemoryOrder); }

  void set_min(double new_min) { min_.store(new_min, kMemoryOrder); }

  void update_min(double value) {
    double previous_min = min();
    if (value < previous_min) {
      // This has a race condition. We need to do a compare-and-swap loop here to if we want a
      // precise statistic.
      set_min(value);
    }
  }

  double max() const { return max_.load(kMemoryOrder); }

  void set_max(double new_max) { max_.store(new_max, kMemoryOrder); }

  void update_max(double value) {
    double previous_max = max();
    if (value > previous_max) {
      set_max(value);
    }
  }

  uint64_t num() const { return num_.load(kMemoryOrder); }
  void add_to_num(uint64_t delta) { num_.fetch_add(delta, kMemoryOrder); }
  void set_num(int64_t new_num) { num_.store(new_num, kMemoryOrder); }

  double sum() const { return sum_.load(kMemoryOrder); }
  void set_sum(double value) { sum_.store(value, kMemoryOrder); }

  void add_to_sum(double delta) {
    sum_.store(sum_.load(kMemoryOrder) + delta, kMemoryOrder);
  }

  void add_to_sum_squares(double delta) {
    sum_squares_.store(sum_squares_.load(kMemoryOrder) + delta, kMemoryOrder);
  }

  double sum_squares() const { return sum_squares_.load(kMemoryOrder); }
  void set_sum_squares(double value) { sum_squares_.store(value, kMemoryOrder); }

  uint64_t bucket(int b) const { return buckets_[b].load(kMemoryOrder); }
  void add_to_bucket(int b, uint64_t delta) {
    buckets_[b].fetch_add(delta, kMemoryOrder);
  }
  void set_bucket(int b, uint64_t value) {
    buckets_[b].store(value, kMemoryOrder);
  }


};

}  // namespace rocksdb
