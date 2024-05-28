//
// Copyright (c) YugaByte, Inc.
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

#include <gtest/gtest.h>

#include "yb/util/proto_container_test.pb.h"
#include "yb/util/random.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/uint_set.h"

namespace yb {

constexpr uint32_t kNumRandomToVerify = 10000;
class UnsignedIntSetTest : public YBTest {
 protected:
  Status SetRange(uint32_t lo, uint32_t hi) {
    RETURN_NOT_OK(set_.SetRange(lo, hi));
    for (auto i = lo; i <= hi; ++i) {
      state_.insert(i);
    }
    max_ = std::max(max_, hi);
    return Status::OK();
  }

  uint32_t GetMaxIndexToCheck() const {
    constexpr uint32_t kBufferOverMaxToCheck = 100;
    return max_ + kBufferOverMaxToCheck;
  }

  void VerifyState() const {
    EXPECT_EQ(state_.empty(), set_.IsEmpty());
    for (uint32_t i = 0; i <= GetMaxIndexToCheck(); ++i) {
      EXPECT_EQ(set_.Test(i), state_.find(i) != state_.end()) << i;
    }
  }

  void RandomVerifyState() const {
    EXPECT_EQ(state_.empty(), set_.IsEmpty());
    for (const auto& elem : state_) {
      EXPECT_TRUE(set_.Test(elem));
    }

    Random rng(29203);
    for (size_t i = 0; i < kNumRandomToVerify; ++i) {
      auto random_idx = rng.Next32();
      EXPECT_EQ(set_.Test(random_idx), state_.find(random_idx) != state_.end()) << i;
    }
  }

  UnsignedIntSet<uint32_t> set_;
  std::set<uint32_t> state_;
  uint32_t max_ = 0;
};

TEST_F(UnsignedIntSetTest, BasicSet) {
  ASSERT_OK(SetRange(10, 21));
  ASSERT_OK(SetRange(24, 29));
  VerifyState();
}

TEST_F(UnsignedIntSetTest, JoinRanges) {
  ASSERT_OK(SetRange(10, 21));
  ASSERT_OK(SetRange(22, 29));
  VerifyState();
}

TEST_F(UnsignedIntSetTest, SingleElemRange) {
  ASSERT_OK(SetRange(10, 10));
  VerifyState();
}

TEST_F(UnsignedIntSetTest, OverlappingRange) {
  ASSERT_OK(SetRange(10, 21));
  ASSERT_OK(SetRange(15, 25));
  VerifyState();
}

TEST_F(UnsignedIntSetTest, Contains) {
  UnsignedIntSet<uint32_t> other;
  ASSERT_TRUE(set_.Contains(other));

  ASSERT_OK(SetRange(1, 4));
  ASSERT_TRUE(set_.Contains(other));
  ASSERT_FALSE(other.Contains(set_));

  ASSERT_OK(other.SetRange(1, 1));
  ASSERT_TRUE(set_.Contains(other));
  ASSERT_OK(other.SetRange(2, 3));
  ASSERT_TRUE(set_.Contains(other));
  ASSERT_OK(other.SetRange(5, 5));
  ASSERT_FALSE(set_.Contains(other));
  ASSERT_FALSE(other.Contains(set_));
  ASSERT_OK(other.SetRange(4, 4));
  ASSERT_FALSE(set_.Contains(other));
  ASSERT_TRUE(other.Contains(set_));

  ASSERT_OK(SetRange(6, 8));
  ASSERT_OK(other.SetRange(7, 8));
  ASSERT_FALSE(set_.Contains(other));
  ASSERT_FALSE(other.Contains(set_));
  ASSERT_OK(SetRange(5, 5));
  ASSERT_TRUE(set_.Contains(other));
  ASSERT_FALSE(other.Contains(set_));
}

TEST_F(UnsignedIntSetTest, Hash) {
  std::unordered_set<UnsignedIntSet<uint32_t>> test_set;
  ASSERT_OK(SetRange(1, 2));
  ASSERT_EQ(test_set.find(set_), test_set.end());
  ASSERT_TRUE(test_set.emplace(set_).second);
  ASSERT_FALSE(test_set.emplace(set_).second);
  ASSERT_NE(test_set.find(set_), test_set.end());
  // test_set contains { [(1, 2)] }
  ASSERT_EQ(test_set.size(), 1);

  // set_ becomes - [(1, 2), (4, 4)]
  ASSERT_OK(SetRange(4, 4));
  ASSERT_EQ(test_set.find(set_), test_set.end());

  UnsignedIntSet<uint32_t> tmp;
  ASSERT_OK(tmp.SetRange(3, 4));
  ASSERT_TRUE(test_set.emplace(tmp).second);
  // test_set contains { [(1, 2)], [(3, 4)]}
  ASSERT_EQ(test_set.size(), 2);

  // set_ becomes - [(1, 4)]
  ASSERT_OK(SetRange(3, 3));
  ASSERT_TRUE(test_set.emplace(set_).second);
  // test_set contains { [(1, 2)], [(3, 4)], [(1, 4)]}
  ASSERT_EQ(test_set.size(), 3);

  // tmp becomes - [(1, 1), (3, 4)]
  ASSERT_OK(tmp.SetRange(1, 1));
  ASSERT_TRUE(test_set.emplace(tmp).second);
  // test_set contains { [(1, 2)], [(3, 4)], [(1, 4)], [(1, 1), (3, 4)]}
  ASSERT_EQ(test_set.size(), 4);

  // tmp becomes - [(1, 4)]
  ASSERT_OK(tmp.SetRange(2, 2));
  ASSERT_NE(test_set.find(tmp), test_set.end());
  ASSERT_OK(tmp.SetRange(1, 3));
  ASSERT_NE(test_set.find(tmp), test_set.end());

  tmp = UnsignedIntSet<uint32_t>();
  ASSERT_OK(tmp.SetRange(1, 4));
  ASSERT_NE(test_set.find(set_), test_set.end());
  test_set.erase(tmp);
  ASSERT_EQ(test_set.find(set_), test_set.end());
}

class UnsignedIntSetEncodeDecodeTest : public UnsignedIntSetTest {
 protected:
  void VerifyCopy() const {
    auto copy = ASSERT_RESULT(GetCopy());

    for (uint32_t i = 0; i <= GetMaxIndexToCheck(); ++i) {
      EXPECT_EQ(set_.Test(i), copy.Test(i));
    }
  }

  void RandomVerifyCopy() const {
    auto copy = ASSERT_RESULT(GetCopy());

    EXPECT_EQ(state_.empty(), set_.IsEmpty());
    for (const auto& elem : state_) {
      EXPECT_EQ(set_.Test(elem), copy.Test(elem));
    }

    Random rng(29203);
    for (size_t i = 0; i < kNumRandomToVerify; ++i) {
      auto random_idx = rng.Next32();
      EXPECT_EQ(set_.Test(random_idx), copy.Test(random_idx)) << i;
    }
  }

 private:
  Result<UnsignedIntSet<uint32_t>> GetCopy() const {
    UnsignedIntSetTestPB pb;
    set_.ToPB(pb.mutable_set());
    return UnsignedIntSet<uint32_t>::FromPB(pb.set());
  }
};

TEST_F(UnsignedIntSetEncodeDecodeTest, EncodeDecode) {
  ASSERT_OK(SetRange(10, 21));
  ASSERT_OK(SetRange(24, 29));
  VerifyCopy();
}

TEST_F(UnsignedIntSetEncodeDecodeTest, HasZeroRangeSet) {
  ASSERT_OK(SetRange(0, 10));
  VerifyCopy();
}

TEST_F(UnsignedIntSetEncodeDecodeTest, HasZeroOnlySet) {
  ASSERT_OK(SetRange(0, 0));
  VerifyCopy();
}

TEST_F(UnsignedIntSetEncodeDecodeTest, HasNoneSet) {
  VerifyCopy();
}

TEST_F(UnsignedIntSetEncodeDecodeTest, MultipleSetRanges) {
  ASSERT_OK(SetRange(2, 3));
  ASSERT_OK(SetRange(5, 5));
  VerifyCopy();
}

TEST_F(UnsignedIntSetEncodeDecodeTest, Random) {
  constexpr int kNumIters = 10;
  constexpr int kMinNumIntervals = 10;
  constexpr int kMaxNumIntervals = 100;
  uint16_t kMaxValue = std::numeric_limits<uint16_t>::max();
  Random rng(2813308004);

  for (int i = 0; i < kNumIters; ++i) {
    UnsignedIntSet<uint16_t> set;
    auto num_ranges = kMinNumIntervals + rng.Uniform(kMaxNumIntervals);
    for (size_t range_idx = 0; range_idx < num_ranges; ++range_idx) {
      uint16_t lo = rng.Uniform(kMaxValue - 1);
      uint16_t hi = lo + rng.Uniform(kMaxValue - lo);
      ASSERT_OK(SetRange(lo, hi));
      RandomVerifyState();
      RandomVerifyCopy();
    }
  }
}

} // namespace yb
