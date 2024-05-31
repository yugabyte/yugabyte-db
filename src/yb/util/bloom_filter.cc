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
#include <math.h>

#include "yb/util/logging.h"

#include "yb/util/bloom_filter.h"
#include "yb/util/bitmap.h"

namespace yb {

static double kNaturalLog2 = 0.69314;

static int ComputeOptimalHashCount(size_t n_bits, size_t elems) {
  int n_hashes = n_bits * kNaturalLog2 / elems;
  if (n_hashes < 1) n_hashes = 1;
  return n_hashes;
}

BloomFilterSizing BloomFilterSizing::ByCountAndFPRate(
  size_t expected_count, double fp_rate) {
  CHECK_GT(fp_rate, 0);
  CHECK_LT(fp_rate, 1);

  double n_bits = -static_cast<double>(expected_count) * log(fp_rate)
    / kNaturalLog2 / kNaturalLog2;
  int n_bytes = static_cast<int>(ceil(n_bits / 8));
  CHECK_GT(n_bytes, 0)
    << "expected_count: " << expected_count
    << " fp_rate: " << fp_rate;
  return BloomFilterSizing(n_bytes, expected_count);
}

BloomFilterSizing BloomFilterSizing::BySizeAndFPRate(size_t n_bytes, double fp_rate) {
  size_t n_bits = n_bytes * 8;
  double expected_elems = -static_cast<double>(n_bits) * kNaturalLog2 * kNaturalLog2 /
    log(fp_rate);
  DCHECK_GT(expected_elems, 1);
  return BloomFilterSizing(n_bytes, (size_t)ceil(expected_elems));
}


BloomFilterBuilder::BloomFilterBuilder(const BloomFilterSizing &sizing)
  : n_bits_(sizing.n_bytes() * 8),
    bitmap_(new uint8_t[sizing.n_bytes()]),
    n_hashes_(ComputeOptimalHashCount(n_bits_, sizing.expected_count())),
    expected_count_(sizing.expected_count()),
    n_inserted_(0) {
  Clear();
}

void BloomFilterBuilder::Clear() {
  memset(&bitmap_[0], 0, n_bytes());
  n_inserted_ = 0;
}

double BloomFilterBuilder::false_positive_rate() const {
  CHECK_NE(expected_count_, 0)
    << "expected_count_ not initialized: can't call this function on "
    << "a BloomFilter initialized from external data";

  return pow(1 - exp(-static_cast<double>(n_hashes_) * expected_count_ / n_bits_), n_hashes_);
}

BloomFilter::BloomFilter(const Slice &data, size_t n_hashes)
  : n_bits_(data.size() * 8),
    bitmap_(reinterpret_cast<const uint8_t *>(data.data())),
    n_hashes_(n_hashes)
{}



} // namespace yb
