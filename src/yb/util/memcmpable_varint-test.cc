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
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/util/hexdump.h"
#include "yb/util/memcmpable_varint.h"
#include "yb/util/random.h"
#include "yb/util/stopwatch.h" // Required in NDEBUG mode
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::pair;
using std::make_pair;
using std::vector;

namespace yb {

class TestMemcmpableVarint : public YBTest {
 protected:
  TestMemcmpableVarint() : random_(SeedRandom()) {}

  // Random number generator that generates different length integers
  // with equal probability -- i.e it is equally as likely to generate
  // a number with 8 bits as it is to generate one with 64 bits.
  // This is useful for testing varint implementations, where a uniform
  // random is skewed towards generating longer integers.
  uint64_t Rand64WithRandomBitLength() {
    return random_.Next64() >> random_.Uniform(64);
  }

  Random random_;
};

static void DoRoundTripTest(uint64_t to_encode) {
  static faststring buf;
  buf.clear();
  PutMemcmpableVarint64(&buf, to_encode);

  uint64_t decoded;
  Slice slice(buf);
  auto status = GetMemcmpableVarint64(&slice, &decoded);
  ASSERT_OK(status);
  ASSERT_EQ(to_encode, decoded);
  ASSERT_TRUE(slice.empty());
}


TEST_F(TestMemcmpableVarint, TestRoundTrip) {
  // Test the first 100K integers
  // (exercises the special cases for <= 67823 in the code)
  for (int i = 0; i < 100000; i++) {
    DoRoundTripTest(i);
  }

  // Test a bunch of random integers (which are likely to be many bytes)
  for (int i = 0; i < 100000; i++) {
    DoRoundTripTest(random_.Next64());
  }
}


// Test that a composite key can be made up of multiple memcmpable
// varints strung together, and that the resulting key compares the
// same as the original pair of integers (i.e left-to-right).
TEST_F(TestMemcmpableVarint, TestCompositeKeys) {
  faststring buf1;
  faststring buf2;

  const int n_trials = 1000;

  for (int i = 0; i < n_trials; i++) {
    buf1.clear();
    buf2.clear();

    pair<uint64_t, uint64_t> p1 =
      make_pair(Rand64WithRandomBitLength(), Rand64WithRandomBitLength());
    PutMemcmpableVarint64(&buf1, p1.first);
    PutMemcmpableVarint64(&buf1, p1.second);

    pair<uint64_t, uint64_t> p2 =
      make_pair(Rand64WithRandomBitLength(), Rand64WithRandomBitLength());
    PutMemcmpableVarint64(&buf2, p2.first);
    PutMemcmpableVarint64(&buf2, p2.second);

    SCOPED_TRACE(testing::Message() << p1 << "\n" << HexDump(Slice(buf1))
                 << "  vs\n" << p2 << "\n" << HexDump(Slice(buf2)));
    if (p1 < p2) {
      ASSERT_LT(Slice(buf1).compare(Slice(buf2)), 0);
    } else if (p1 > p2) {
      ASSERT_GT(Slice(buf1).compare(Slice(buf2)), 0);
    } else {
      ASSERT_EQ(Slice(buf1).compare(Slice(buf2)), 0);
    }
  }
}

// Similar to the above test, but instead of being randomized, specifically
// tests "interesting" values -- i.e values around the boundaries of where
// the encoding changes its number of bytes.
TEST_F(TestMemcmpableVarint, TestInterestingCompositeKeys) {
  vector<uint64_t> interesting_values = { 0, 1, 240, // 1 byte
                                          241, 2000, 2287, // 2 bytes
                                          2288, 40000, 67823, // 3 bytes
                                          67824, 1ULL << 23, (1ULL << 24) - 1, // 4 bytes
                                          1ULL << 24, 1ULL << 30, (1ULL << 32) - 1 }; // 5 bytes

  faststring buf1;
  faststring buf2;

  for (uint64_t v1 : interesting_values) {
    for (uint64_t v2 : interesting_values) {
      buf1.clear();
      pair<uint64_t, uint64_t> p1 = make_pair(v1, v2);
      PutMemcmpableVarint64(&buf1, p1.first);
      PutMemcmpableVarint64(&buf1, p1.second);

      for (uint64_t v3 : interesting_values) {
        for (uint64_t v4 : interesting_values) {
          buf2.clear();
          pair<uint64_t, uint64_t> p2 = make_pair(v3, v4);
          PutMemcmpableVarint64(&buf2, p2.first);
          PutMemcmpableVarint64(&buf2, p2.second);

          SCOPED_TRACE(testing::Message() << p1 << "\n" << HexDump(Slice(buf1))
                       << "  vs\n" << p2 << "\n" << HexDump(Slice(buf2)));
          if (p1 < p2) {
            ASSERT_LT(Slice(buf1).compare(Slice(buf2)), 0);
          } else if (p1 > p2) {
            ASSERT_GT(Slice(buf1).compare(Slice(buf2)), 0);
          } else {
            ASSERT_EQ(Slice(buf1).compare(Slice(buf2)), 0);
          }
        }
      }
    }
  }
}

////////////////////////////////////////////////////////////
// Benchmarks
////////////////////////////////////////////////////////////

#ifdef NDEBUG
TEST_F(TestMemcmpableVarint, BenchmarkEncode) {
  faststring buf;

  int sum_sizes = 0; // need to do something with results to force evaluation

  LOG_TIMING(INFO, "Encoding integers") {
    for (int trial = 0; trial < 100; trial++) {
      for (uint64_t i = 0; i < 1000000; i++) {
        buf.clear();
        PutMemcmpableVarint64(&buf, i);
        sum_sizes += buf.size();
      }
    }
  }
  ASSERT_GT(sum_sizes, 1); // use 'sum_sizes' to avoid optimizing it out.
}

TEST_F(TestMemcmpableVarint, BenchmarkDecode) {
  faststring buf;

  // Encode 1M integers into the buffer
  for (uint64_t i = 0; i < 1000000; i++) {
    PutMemcmpableVarint64(&buf, i);
  }

  // Decode the whole buffer 100 times.
  LOG_TIMING(INFO, "Decoding integers") {
    uint64_t sum_vals = 0;
    for (int trial = 0; trial < 100; trial++) {
      Slice s(buf);
      while (!s.empty()) {
        uint64_t decoded;
        CHECK(GetMemcmpableVarint64(&s, &decoded).ok());
        sum_vals += decoded;
      }
    }
    ASSERT_GT(sum_vals, 1); // use 'sum_vals' to avoid optimizing it out.
  }
}

#endif

} // namespace yb
