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

#include <gtest/gtest.h>
#include <vector>

#include "kudu/gutil/strings/join.h"
#include "kudu/util/bitmap.h"

namespace kudu {

static int ReadBackBitmap(uint8_t *bm, size_t bits,
                           std::vector<size_t> *result) {
  int iters = 0;
  for (TrueBitIterator iter(bm, bits);
       !iter.done();
       ++iter) {
    size_t val = *iter;
    result->push_back(val);

    iters++;
  }
  return iters;
}

TEST(TestBitMap, TestIteration) {
  uint8_t bm[8];
  memset(bm, 0, sizeof(bm));
  BitmapSet(bm, 0);
  BitmapSet(bm, 8);
  BitmapSet(bm, 31);
  BitmapSet(bm, 32);
  BitmapSet(bm, 33);
  BitmapSet(bm, 63);

  EXPECT_EQ("   0: 10000000 10000000 00000000 00000001 11000000 00000000 00000000 00000001 \n",
            BitmapToString(bm, sizeof(bm) * 8));

  std::vector<size_t> read_back;

  int iters = ReadBackBitmap(bm, sizeof(bm)*8, &read_back);
  ASSERT_EQ(6, iters);
  ASSERT_EQ("0,8,31,32,33,63", JoinElements(read_back, ","));
}


TEST(TestBitMap, TestIteration2) {
  uint8_t bm[1];
  memset(bm, 0, sizeof(bm));
  BitmapSet(bm, 1);

  std::vector<size_t> read_back;

  int iters = ReadBackBitmap(bm, 3, &read_back);
  ASSERT_EQ(1, iters);
  ASSERT_EQ("1", JoinElements(read_back, ","));
}

TEST(TestBitmap, TestSetAndTestBits) {
  uint8_t bm[1];
  memset(bm, 0, sizeof(bm));

  size_t num_bits = sizeof(bm) * 8;
  for (size_t i = 0; i < num_bits; i++) {
    ASSERT_FALSE(BitmapTest(bm, i));

    BitmapSet(bm, i);
    ASSERT_TRUE(BitmapTest(bm, i));

    BitmapClear(bm, i);
    ASSERT_FALSE(BitmapTest(bm, i));

    BitmapChange(bm, i, true);
    ASSERT_TRUE(BitmapTest(bm, i));

    BitmapChange(bm, i, false);
    ASSERT_FALSE(BitmapTest(bm, i));
  }

  // Set the other bit: 01010101
  for (size_t i = 0; i < num_bits; ++i) {
    ASSERT_FALSE(BitmapTest(bm, i));
    if (i & 1) BitmapSet(bm, i);
  }

  // Check and Clear the other bit: 0000000
  for (size_t i = 0; i < num_bits; ++i) {
    ASSERT_EQ(!!(i & 1), BitmapTest(bm, i));
    if (i & 1) BitmapClear(bm, i);
  }

  // Check if bits are zero and change the other to one
  for (size_t i = 0; i < num_bits; ++i) {
    ASSERT_FALSE(BitmapTest(bm, i));
    BitmapChange(bm, i, i & 1);
  }

  // Check the bits change them again
  for (size_t i = 0; i < num_bits; ++i) {
    ASSERT_EQ(!!(i & 1), BitmapTest(bm, i));
    BitmapChange(bm, i, !(i & 1));
  }

  // Check the last setup
  for (size_t i = 0; i < num_bits; ++i) {
    ASSERT_EQ(!(i & 1), BitmapTest(bm, i));
  }
}

TEST(TestBitMap, TestBulkSetAndTestBits) {
  uint8_t bm[16];
  size_t total_size = sizeof(bm) * 8;

  // Test Bulk change bits and test bits
  for (int i = 0; i < 4; ++i) {
    bool value = i & 1;
    size_t num_bits = total_size;
    while (num_bits > 0) {
      for (size_t offset = 0; offset < num_bits; ++offset) {
        BitmapChangeBits(bm, 0, total_size, !value);
        BitmapChangeBits(bm, offset, num_bits - offset, value);

        ASSERT_EQ(value, BitMapIsAllSet(bm, offset, num_bits));
        ASSERT_EQ(!value, BitmapIsAllZero(bm, offset, num_bits));

        if (offset > 1) {
          ASSERT_EQ(value, BitmapIsAllZero(bm, 0, offset - 1));
          ASSERT_EQ(!value, BitMapIsAllSet(bm, 0, offset - 1));
        }

        if ((offset + num_bits) < total_size) {
          ASSERT_EQ(value, BitmapIsAllZero(bm, num_bits, total_size));
          ASSERT_EQ(!value, BitMapIsAllSet(bm, num_bits, total_size));
        }
      }
      num_bits--;
    }
  }
}

TEST(TestBitMap, TestFindBit) {
  uint8_t bm[16];

  size_t num_bits = sizeof(bm) * 8;
  BitmapChangeBits(bm, 0, num_bits, false);
  while (num_bits > 0) {
    for (size_t offset = 0; offset < num_bits; ++offset) {
      size_t idx;
      ASSERT_FALSE(BitmapFindFirstSet(bm, offset, num_bits, &idx));
      ASSERT_TRUE(BitmapFindFirstZero(bm, offset, num_bits, &idx));
      ASSERT_EQ(idx, offset);
    }
    num_bits--;
  }

  num_bits = sizeof(bm) * 8;
  for (int i = 0; i < num_bits; ++i) {
    BitmapChange(bm, i, i & 3);
  }

  while (num_bits--) {
    for (size_t offset = 0; offset < num_bits; ++offset) {
      size_t idx;

      // Find a set bit
      bool res = BitmapFindFirstSet(bm, offset, num_bits, &idx);
      size_t expected_set_idx = (offset + !(offset & 3));
      bool expect_set_found = (expected_set_idx < num_bits);
      ASSERT_EQ(expect_set_found, res);
      if (expect_set_found) ASSERT_EQ(expected_set_idx, idx);

      // Find a zero bit
      res = BitmapFindFirstZero(bm, offset, num_bits, &idx);
      size_t expected_zero_idx = offset + ((offset & 3) ? (4 - (offset & 3)) : 0);
      bool expect_zero_found = (expected_zero_idx < num_bits);
      ASSERT_EQ(expect_zero_found, res);
      if (expect_zero_found) ASSERT_EQ(expected_zero_idx, idx);
    }
  }
}

TEST(TestBitMap, TestBitmapIteration) {
  uint8_t bm[8];
  memset(bm, 0, sizeof(bm));
  BitmapSet(bm, 0);
  BitmapSet(bm, 8);
  BitmapSet(bm, 31);
  BitmapSet(bm, 32);
  BitmapSet(bm, 33);
  BitmapSet(bm, 63);

  BitmapIterator biter(bm, sizeof(bm) * 8);

  size_t i = 0;
  size_t size;
  bool value = false;
  bool expected_value = true;
  size_t expected_sizes[] = {1, 7, 1, 22, 3, 29, 1, 0};
  while ((size = biter.Next(&value)) > 0) {
    ASSERT_LT(i, 8);
    ASSERT_EQ(expected_value, value);
    ASSERT_EQ(expected_sizes[i], size);
    expected_value = !expected_value;
    i++;
  }
  ASSERT_EQ(expected_sizes[i], size);
}

} // namespace kudu
