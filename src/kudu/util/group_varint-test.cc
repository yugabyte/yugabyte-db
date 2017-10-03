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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <vector>

#include "kudu/util/group_varint-inl.h"
#include "kudu/util/stopwatch.h"

namespace kudu {
namespace coding {

extern void DumpSSETable();

// Encodes the given four ints as group-varint, then
// decodes and ensures the result is the same.
static void DoTestRoundTripGVI32(
  uint32_t a, uint32_t b, uint32_t c, uint32_t d,
  bool use_sse = false) {
  faststring buf;
  AppendGroupVarInt32(&buf, a, b, c, d);

  int real_size = buf.size();

  // The implementations actually read past the group varint,
  // so append some extra padding data to ensure that it's not reading
  // uninitialized memory. The SSE implementation uses 128-bit reads
  // and the non-SSE one uses 32-bit reads.
  buf.append(string('x', use_sse ? 16 : 4));

  uint32_t ret[4];

  const uint8_t *end;

  if (use_sse) {
    end = DecodeGroupVarInt32_SSE(
      buf.data(), &ret[0], &ret[1], &ret[2], &ret[3]);
  } else {
    end = DecodeGroupVarInt32(
      buf.data(), &ret[0], &ret[1], &ret[2], &ret[3]);
  }

  ASSERT_EQ(a, ret[0]);
  ASSERT_EQ(b, ret[1]);
  ASSERT_EQ(c, ret[2]);
  ASSERT_EQ(d, ret[3]);
  ASSERT_EQ(end, buf.data() + real_size);
}


TEST(TestGroupVarInt, TestSSETable) {
  DumpSSETable();
  faststring buf;
  AppendGroupVarInt32(&buf, 0, 0, 0, 0);
  DoTestRoundTripGVI32(0, 0, 0, 0, true);
  DoTestRoundTripGVI32(1, 2, 3, 4, true);
  DoTestRoundTripGVI32(1, 2000, 3, 200000, true);
}

TEST(TestGroupVarInt, TestGroupVarInt) {
  faststring buf;
  AppendGroupVarInt32(&buf, 0, 0, 0, 0);
  ASSERT_EQ(5UL, buf.size());
  ASSERT_EQ(0, memcmp("\x00\x00\x00\x00\x00", buf.data(), 5));
  buf.clear();

  // All 1-byte
  AppendGroupVarInt32(&buf, 1, 2, 3, 254);
  ASSERT_EQ(5UL, buf.size());
  ASSERT_EQ(0, memcmp("\x00\x01\x02\x03\xfe", buf.data(), 5));
  buf.clear();

  // Mixed 1-byte and 2-byte
  AppendGroupVarInt32(&buf, 256, 2, 3, 65535);
  ASSERT_EQ(7UL, buf.size());
  ASSERT_EQ(BOOST_BINARY(01 00 00 01), buf.at(0));
  ASSERT_EQ(256, *reinterpret_cast<const uint16_t *>(&buf[1]));
  ASSERT_EQ(2, *reinterpret_cast<const uint8_t *>(&buf[3]));
  ASSERT_EQ(3, *reinterpret_cast<const uint8_t *>(&buf[4]));
  ASSERT_EQ(65535, *reinterpret_cast<const uint16_t *>(&buf[5]));
}


// Round-trip encode/decodes using group varint
TEST(TestGroupVarInt, TestRoundTrip) {
  // A few simple tests.
  DoTestRoundTripGVI32(0, 0, 0, 0);
  DoTestRoundTripGVI32(1, 2, 3, 4);
  DoTestRoundTripGVI32(1, 2000, 3, 200000);

  // Then a randomized test.
  for (int i = 0; i < 10000; i++) {
    DoTestRoundTripGVI32(random(), random(), random(), random());
  }
}

#ifdef NDEBUG
TEST(TestGroupVarInt, EncodingBenchmark) {
  int n_ints = 1000000;

  std::vector<uint32_t> ints;
  ints.reserve(n_ints);
  for (int i = 0; i < n_ints; i++) {
    ints.push_back(i);
  }

  faststring s;
  // conservative reservation
  s.reserve(ints.size() * 4);

  LOG_TIMING(INFO, "Benchmark") {
    for (int i = 0; i < 100; i++) {
      s.clear();
      AppendGroupVarInt32Sequence(&s, 0, &ints[0], n_ints);
    }
  }
}
#endif
} // namespace coding
} // namespace kudu
