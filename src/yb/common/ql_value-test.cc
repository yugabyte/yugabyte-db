// Copyright (c) YugabyteDB, Inc.
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

#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/ql_value.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/uuid.h"

namespace yb {

class QLValueTest : public YBTest {
};

namespace {

// Compares two UUIDs the way they are ordered inside a frozen set.
bool LessInFrozenCollection(const Uuid& lhs, const Uuid& rhs) {
  QLValuePB lhs_pb, rhs_pb;
  QLValue::set_uuid_value(lhs, &lhs_pb);
  QLValue::set_uuid_value(rhs, &rhs_pb);
  return FrozenCollectionLess()(lhs_pb, rhs_pb);
}

} // namespace

// Versions 0 and 6..15 sort before v1..v5 and compare by raw bytes among themselves.
TEST_F(QLValueTest, FrozenCollectionUuidOrder) {
  auto uuid_v4 = ASSERT_RESULT(Uuid::FromString("9e107d9d-372b-4b6a-8d0a-2f5c7c8e1a3b"));
  auto uuid_v7 = ASSERT_RESULT(Uuid::FromString("0b9a2f3c-5d4e-7f60-9a1b-2c3d4e5f6a7b"));
  auto uuid_v6 = ASSERT_RESULT(Uuid::FromString("f1111111-1111-6111-8111-111111111111"));
  auto uuid_v8 = ASSERT_RESULT(Uuid::FromString("01111111-1111-8111-8111-111111111111"));
  auto uuid_v0 = ASSERT_RESULT(Uuid::FromString("f2222222-2222-0222-8222-222222222222"));
  auto uuid_v1 = ASSERT_RESULT(Uuid::FromString("01111111-1111-1111-8111-111111111111"));
  EXPECT_TRUE(LessInFrozenCollection(uuid_v7, uuid_v4));
  EXPECT_FALSE(LessInFrozenCollection(uuid_v4, uuid_v7));
  EXPECT_TRUE(LessInFrozenCollection(uuid_v8, uuid_v6));
  EXPECT_FALSE(LessInFrozenCollection(uuid_v6, uuid_v8));
  EXPECT_TRUE(LessInFrozenCollection(uuid_v7, uuid_v6));
  EXPECT_TRUE(LessInFrozenCollection(uuid_v8, uuid_v7));
  EXPECT_TRUE(LessInFrozenCollection(uuid_v0, uuid_v1));
  EXPECT_TRUE(LessInFrozenCollection(uuid_v8, uuid_v0));
  EXPECT_TRUE(LessInFrozenCollection(uuid_v6, uuid_v0));
}

// Compares the order with operator< from before #28566 (git show f186cd90560^:src/yb/util/uuid.cc)
// on random UUIDs of every version.
TEST_F(QLValueTest, FrozenCollectionUuidOrderMatchesOldOperatorLess) {
  // With boost 1.81, version() returned 1..5 as is and -1 for every other version.
  auto old_less = [](const Uuid& lhs, const Uuid& rhs) {
    auto rank = [](const Uuid& uuid) {
      const auto version = static_cast<int>(uuid.version_raw());
      return version >= 1 && version <= 5 ? version : -1;
    };
    if (rank(lhs) != rank(rhs)) {
      return rank(lhs) < rank(rhs);
    }
    // v1 compares the timestamp: bytes 6, 7, 4, 5, 0..3. Others compare bytes 0..7. Both then
    // compare bytes 8..15.
    std::vector<size_t> order = rank(lhs) == 1
        ? std::vector<size_t>{6, 7, 4, 5, 0, 1, 2, 3}
        : std::vector<size_t>{0, 1, 2, 3, 4, 5, 6, 7};
    for (size_t i = Uuid::kUuidMsbSize; i < kUuidSize; ++i) {
      order.push_back(i);
    }
    for (auto i : order) {
      if (lhs.data()[i] != rhs.data()[i]) {
        return lhs.data()[i] < rhs.data()[i];
      }
    }
    return false;
  };

  std::mt19937_64 rng(42);
  std::vector<Uuid> uuids;
  for (int version = 0; version < 16; ++version) {
    for (int k = 0; k < 50; ++k) {
      uint8_t bytes[kUuidSize];
      for (auto& b : bytes) {
        b = static_cast<uint8_t>(rng());
      }
      if (k < 10) {
        // Equal leading bytes, to reach the tie-breaks.
        memset(bytes, 0x11, 6);
      }
      bytes[Uuid::kVersionByte] = (bytes[Uuid::kVersionByte] & 0x0F) | (version << 4);
      uuids.push_back(ASSERT_RESULT(Uuid::FromSlice(Slice(bytes, sizeof(bytes)))));
    }
  }
  for (const auto& lhs : uuids) {
    for (const auto& rhs : uuids) {
      ASSERT_EQ(old_less(lhs, rhs), LessInFrozenCollection(lhs, rhs))
          << lhs.ToString() << " vs " << rhs.ToString();
    }
  }
}

} // namespace yb
