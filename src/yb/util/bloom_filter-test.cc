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
#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/util/bloom_filter.h"

namespace yb {

static const int kRandomSeed = 0xdeadbeef;

static void AddRandomKeys(int random_seed, int n_keys, BloomFilterBuilder *bf) {
  srandom(random_seed);
  for (int i = 0; i < n_keys; i++) {
    uint64_t key = random();
    Slice key_slice(reinterpret_cast<const uint8_t *>(&key), sizeof(key));
    BloomKeyProbe probe(key_slice);
    bf->AddKey(probe);
  }
}

static void CheckRandomKeys(int random_seed, int n_keys, const BloomFilter &bf) {
  srandom(random_seed);
  for (int i = 0; i < n_keys; i++) {
    uint64_t key = random();
    Slice key_slice(reinterpret_cast<const uint8_t *>(&key), sizeof(key));
    BloomKeyProbe probe(key_slice);
    ASSERT_TRUE(bf.MayContainKey(probe));
  }
}

TEST(TestBloomFilter, TestInsertAndProbe) {
  int n_keys = 2000;
  BloomFilterBuilder bfb(
    BloomFilterSizing::ByCountAndFPRate(n_keys, 0.01));

  // Check that the desired false positive rate is achieved.
  double expected_fp_rate = bfb.false_positive_rate();
  ASSERT_NEAR(expected_fp_rate, 0.01, 0.002);

  // 1% FP rate should need about 9 bits per key
  ASSERT_EQ(9, bfb.n_bits() / n_keys);

  // Enter n_keys random keys into the bloom filter
  AddRandomKeys(kRandomSeed, n_keys, &bfb);

  // Verify that the keys we inserted all return true when queried.
  BloomFilter bf(bfb.slice(), bfb.n_hashes());
  CheckRandomKeys(kRandomSeed, n_keys, bf);

  // Query a bunch of other keys, and verify the false positive rate
  // is within reasonable bounds.
  uint32_t num_queries = 100000;
  uint32_t num_positives = 0;
  for (uint32_t i = 0; i < num_queries; i++) {
    uint64_t key = random();
    Slice key_slice(reinterpret_cast<const uint8_t *>(&key), sizeof(key));
    BloomKeyProbe probe(key_slice);
    if (bf.MayContainKey(probe)) {
      num_positives++;
    }
  }

  double fp_rate = static_cast<double>(num_positives) / static_cast<double>(num_queries);
  LOG(INFO) << "FP rate: " << fp_rate << " (" << num_positives << "/" << num_queries << ")";
  LOG(INFO) << "Expected FP rate: " << expected_fp_rate;

  // Actual FP rate should be within 20% of the estimated FP rate
  ASSERT_NEAR(fp_rate, expected_fp_rate, 0.20*expected_fp_rate);
}

} // namespace yb
