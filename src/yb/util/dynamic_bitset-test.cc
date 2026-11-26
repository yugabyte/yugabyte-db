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

#include <algorithm>
#include <iterator>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/dynamic_bitset.h"
#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

namespace yb {

namespace {

constexpr size_t kNoSize = std::numeric_limits<size_t>::max();
constexpr size_t kInline = DynamicBitSet::kInlineCapacity;

DynamicBitSet MakeSet(size_t size, std::initializer_list<size_t> positions) {
  DynamicBitSet result;
  result.resize(size);
  for (auto pos : positions) {
    result.set(pos);
  }
  return result;
}

// Checks that the set has `size` bits, of which exactly `positions` are set.
void CheckBits(const DynamicBitSet& set, size_t size, const std::vector<size_t>& positions) {
  ASSERT_EQ(set.size(), size);
  std::vector<bool> expected(size);
  for (auto pos : positions) {
    expected[pos] = true;
  }
  for (size_t pos = 0; pos != size; ++pos) {
    ASSERT_EQ(set.test(pos), expected[pos]) << "pos: " << pos << ", set: " << set.ToString();
  }
  ASSERT_EQ(set.any(), !positions.empty());
}

} // namespace

class DynamicBitSetTest : public YBTest {
 protected:
  static bool IsDynamic(const DynamicBitSet& set) {
    return set.IsDynamic();
  }

  static size_t Capacity(const DynamicBitSet& set) {
    return set.capacity();
  }
};

TEST_F(DynamicBitSetTest, DefaultIsVirtualNone) {
  DynamicBitSet set;
  ASSERT_EQ(set.size(), kNoSize);
  ASSERT_FALSE(set.any());
  ASSERT_FALSE(set.test(0));
  ASSERT_FALSE(set.test(kInline));
  ASSERT_FALSE(set.test(kNoSize - 1));
  ASSERT_FALSE(IsDynamic(set));
  ASSERT_EQ(set.ToString(), "<none>");
}

TEST_F(DynamicBitSetTest, AllIsVirtual) {
  auto set = DynamicBitSet::All();
  ASSERT_EQ(set.size(), kNoSize);
  ASSERT_TRUE(set.any());
  ASSERT_TRUE(set.test(0));
  ASSERT_TRUE(set.test(kInline));
  ASSERT_TRUE(set.test(kNoSize - 1));
  ASSERT_FALSE(IsDynamic(set));
  ASSERT_EQ(set.ToString(), "<all>");
}

TEST_F(DynamicBitSetTest, ResizeBoundsInline) {
  DynamicBitSet set;
  set.resize(0);
  ASSERT_NO_FATALS(CheckBits(set, 0, {}));
  ASSERT_EQ(set.ToString(), "");

  set.resize(4);
  ASSERT_FALSE(IsDynamic(set));
  ASSERT_NO_FATALS(CheckBits(set, 4, {}));

  set.set(3);
  set.set(0);
  ASSERT_EQ(set.ToString(), "1001");
  ASSERT_NO_FATALS(CheckBits(set, 4, {0, 3}));

  // Growing keeps the bits and adds unset ones.
  set.resize(11);
  ASSERT_FALSE(IsDynamic(set));
  ASSERT_NO_FATALS(CheckBits(set, 11, {0, 3}));
}

TEST_F(DynamicBitSetTest, InlineCapacityBoundary) {
  auto set = MakeSet(kInline, {0, kInline - 1});
  ASSERT_FALSE(IsDynamic(set));
  ASSERT_NO_FATALS(CheckBits(set, kInline, {0, kInline - 1}));

  // One bit past the inline word moves the bits to the heap.
  set.resize(kInline + 1);
  ASSERT_TRUE(IsDynamic(set));
  ASSERT_NO_FATALS(CheckBits(set, kInline + 1, {0, kInline - 1}));

  set.set(kInline);
  ASSERT_NO_FATALS(CheckBits(set, kInline + 1, {0, kInline - 1, kInline}));
}

// resize() of a virtual set gives every bit its uniform value, inline and on the heap.
TEST_F(DynamicBitSetTest, ResizeVirtualKeepsValue) {
  for (size_t size : {size_t{0}, size_t{1}, size_t{10}, kInline - 1, kInline, kInline + 1,
                      2 * kInline, 2 * kInline + 3}) {
    SCOPED_TRACE(Format("size: $0", size));

    DynamicBitSet none;
    none.resize(size);
    ASSERT_EQ(IsDynamic(none), size > kInline);
    ASSERT_NO_FATALS(CheckBits(none, size, {}));

    auto all = DynamicBitSet::All();
    all.resize(size);
    ASSERT_EQ(IsDynamic(all), size > kInline);
    std::vector<size_t> every_bit(size);
    std::iota(every_bit.begin(), every_bit.end(), 0);
    ASSERT_NO_FATALS(CheckBits(all, size, every_bit));
    ASSERT_EQ(all.ToString(), std::string(size, '1'));
  }
}

// The bits of the last word above the size stay zero, so growing again exposes unset bits.
TEST_F(DynamicBitSetTest, ResizeAllClearsTail) {
  for (auto [first, second] : std::vector<std::pair<size_t, size_t>>{
           {0, 10}, {10, kInline}, {10, 2 * kInline}, {kInline + 1, 2 * kInline},
           {kInline + 1, 4 * kInline}, {2 * kInline, 2 * kInline + 1}}) {
    SCOPED_TRACE(Format("first: $0, second: $1", first, second));
    auto set = DynamicBitSet::All();
    set.resize(first);
    set.resize(second);
    std::vector<size_t> first_bits(first);
    std::iota(first_bits.begin(), first_bits.end(), 0);
    ASSERT_NO_FATALS(CheckBits(set, second, first_bits));
  }
}

TEST_F(DynamicBitSetTest, ResizeFromVirtualToDynamic) {
  auto set = MakeSet(3 * kInline + 5, {3 * kInline + 4});
  ASSERT_TRUE(IsDynamic(set));
  ASSERT_NO_FATALS(CheckBits(set, 3 * kInline + 5, {3 * kInline + 4}));
}

TEST_F(DynamicBitSetTest, GrowsAcrossBlocks) {
  // Within the last block, then into new ones; every step keeps the bits set so far.
  const std::vector<size_t> positions = {
      1, kInline, kInline + 5, 2 * kInline - 1, 2 * kInline, 5 * kInline + 7};
  DynamicBitSet set;
  std::vector<size_t> set_so_far;
  for (auto pos : positions) {
    set.resize(pos + 1);
    ASSERT_NO_FATALS(CheckBits(set, pos + 1, set_so_far));
    set.set(pos);
    set_so_far.push_back(pos);
    ASSERT_NO_FATALS(CheckBits(set, pos + 1, set_so_far));
  }
  ASSERT_EQ(set.ToString().size(), 5 * kInline + 8);
}

// Shrinking clears the dropped bits, so growing back gets them unset.
TEST_F(DynamicBitSetTest, ShrinkInline) {
  auto set = MakeSet(40, {1, 9, 10, 39});
  set.resize(10);
  ASSERT_FALSE(IsDynamic(set));
  ASSERT_NO_FATALS(CheckBits(set, 10, {1, 9}));

  set.resize(kInline);
  ASSERT_NO_FATALS(CheckBits(set, kInline, {1, 9}));

  set.resize(0);
  ASSERT_NO_FATALS(CheckBits(set, 0, {}));
  set.resize(5);
  ASSERT_NO_FATALS(CheckBits(set, 5, {}));
}

// Shrinking dynamic storage keeps the allocation, also below kInlineCapacity, and growing back
// within it neither reallocates nor exposes the dropped bits.
TEST_F(DynamicBitSetTest, ShrinkDynamicKeepsAllocation) {
  const auto size = 3 * kInline + 5;
  auto set = MakeSet(size, {1, kInline + 6, 2 * kInline + 22, size - 1});
  const auto capacity = Capacity(set);
  ASSERT_EQ(capacity, 4 * kInline);

  set.resize(kInline + 36);
  ASSERT_TRUE(IsDynamic(set));
  ASSERT_EQ(Capacity(set), capacity);
  ASSERT_NO_FATALS(CheckBits(set, kInline + 36, {1, kInline + 6}));

  set.resize(10);
  ASSERT_TRUE(IsDynamic(set));
  ASSERT_EQ(Capacity(set), capacity);
  ASSERT_NO_FATALS(CheckBits(set, 10, {1}));

  set.resize(0);
  ASSERT_TRUE(IsDynamic(set));
  ASSERT_NO_FATALS(CheckBits(set, 0, {}));

  set.resize(capacity);
  ASSERT_EQ(Capacity(set), capacity);
  ASSERT_NO_FATALS(CheckBits(set, capacity, {}));
}

// Growing past the capacity of shrunk dynamic storage reallocates and keeps the remaining bits.
TEST_F(DynamicBitSetTest, GrowPastCapacityAfterShrink) {
  auto set = MakeSet(2 * kInline, {3, kInline + 1, 2 * kInline - 1});
  set.resize(kInline + 2);
  set.resize(5 * kInline + 1);
  ASSERT_EQ(Capacity(set), 6 * kInline);
  ASSERT_NO_FATALS(CheckBits(set, 5 * kInline + 1, {3, kInline + 1}));
}

// A copy of shrunk dynamic storage keeps its bits and its capacity.
TEST_F(DynamicBitSetTest, CopyShrunkDynamic) {
  auto set = MakeSet(3 * kInline, {2, 2 * kInline});
  set.resize(20);
  DynamicBitSet copy(set);
  ASSERT_TRUE(IsDynamic(copy));
  ASSERT_EQ(Capacity(copy), Capacity(set));
  ASSERT_NO_FATALS(CheckBits(copy, 20, {2}));
  copy.resize(3 * kInline);
  ASSERT_NO_FATALS(CheckBits(copy, 3 * kInline, {2}));
}

TEST_F(DynamicBitSetTest, ResizeToSameSize) {
  auto set = MakeSet(2 * kInline, {kInline + 1});
  set.resize(2 * kInline);
  ASSERT_NO_FATALS(CheckBits(set, 2 * kInline, {kInline + 1}));
}

TEST_F(DynamicBitSetTest, AnySeesBitsInLaterBlocks) {
  // Only the last of the four blocks has a bit.
  auto set = MakeSet(3 * kInline + 1, {3 * kInline});
  ASSERT_TRUE(set.any());
  ASSERT_FALSE(set.test(0));
  ASSERT_FALSE(set.test(kInline));
}

TEST_F(DynamicBitSetTest, CopyAndMove) {
  struct Case {
    DynamicBitSet set;
    size_t size;
    std::vector<size_t> positions;
  };
  const std::vector<Case> cases = {
      {MakeSet(0, {}), 0, {}},
      {MakeSet(8, {2, 7}), 8, {2, 7}},
      {MakeSet(4 * kInline + 1, {2, kInline + 3, 4 * kInline}), 4 * kInline + 1,
       {2, kInline + 3, 4 * kInline}},
  };

  for (const auto& [original, size, positions] : cases) {
    SCOPED_TRACE(original.ToString());

    // A copy is independent of its source.
    DynamicBitSet copy(original);
    ASSERT_NO_FATALS(CheckBits(copy, size, positions));
    copy.resize(5 * kInline + 1);
    copy.set(5 * kInline);
    ASSERT_NO_FATALS(CheckBits(original, size, positions));

    DynamicBitSet assigned = MakeSet(3 * kInline + 1, {1, 3 * kInline});
    assigned = original;
    ASSERT_NO_FATALS(CheckBits(assigned, size, positions));

    // A move takes the bits and leaves the source virtual and empty.
    DynamicBitSet moved(std::move(assigned));
    ASSERT_NO_FATALS(CheckBits(moved, size, positions));
    ASSERT_EQ(assigned.size(), kNoSize); // NOLINT(bugprone-use-after-move)
    ASSERT_FALSE(assigned.any());

    DynamicBitSet move_assigned = MakeSet(2 * kInline + 1, {2 * kInline});
    move_assigned = std::move(moved);
    ASSERT_NO_FATALS(CheckBits(move_assigned, size, positions));
    ASSERT_EQ(moved.size(), kNoSize); // NOLINT(bugprone-use-after-move)
  }

  auto all = DynamicBitSet::All();
  DynamicBitSet none;
  none = all;
  ASSERT_TRUE(none.test(5 * kInline));
  ASSERT_EQ(none.ToString(), "<all>");
}

TEST_F(DynamicBitSetTest, Randomized) {
  constexpr size_t kMaxSize = 8 * kInline;
  constexpr int kIterations = 200;
  for (int iteration = 0; iteration != kIterations; ++iteration) {
    SCOPED_TRACE(Format("iteration: $0", iteration));
    // The first resize() of a virtual set fills the bits with its value; later ones grow or shrink,
    // as std::vector, adding unset bits.
    const bool start_all = RandomUniformBool();
    auto set = start_all ? DynamicBitSet::All() : DynamicBitSet();
    std::vector<bool> expected;
    size_t max_size = 0;
    const auto num_steps = RandomUniformInt<size_t>(1, 20);
    for (size_t step = 0; step != num_steps; ++step) {
      const auto new_size = RandomUniformInt<size_t>(0, kMaxSize);
      set.resize(new_size);
      expected.resize(new_size, step == 0 && start_all);
      max_size = std::max(max_size, new_size);
      if (new_size != 0) {
        const auto pos = RandomUniformInt<size_t>(0, new_size - 1);
        set.set(pos);
        expected[pos] = true;
      }

      ASSERT_EQ(set.size(), expected.size());
      // Dynamic once it has been larger than kInlineCapacity; shrinking keeps it dynamic.
      ASSERT_EQ(IsDynamic(set), max_size > kInline);
      for (size_t pos = 0; pos != expected.size(); ++pos) {
        ASSERT_EQ(set.test(pos), expected[pos]) << "pos: " << pos;
      }
    }

    DynamicBitSet copy(set);
    for (size_t pos = 0; pos != expected.size(); ++pos) {
      ASSERT_EQ(copy.test(pos), expected[pos]) << "pos: " << pos;
    }
  }
}

TEST_F(DynamicBitSetTest, OutOfRangeAccessDies) {
  auto inline_set = MakeSet(4, {1});
  ASSERT_DEATH(static_cast<void>(inline_set.test(4)), "Check failed");
  ASSERT_DEATH(inline_set.set(4), "Check failed");

  auto dynamic_set = MakeSet(kInline + 1, {kInline});
  ASSERT_DEATH(static_cast<void>(dynamic_set.test(kInline + 1)), "Check failed");
  ASSERT_DEATH(dynamic_set.set(2 * kInline), "Check failed");
}

TEST_F(DynamicBitSetTest, InvalidMutationsDie) {
  DynamicBitSet none;
  ASSERT_DEATH(none.set(0), "resize\\(\\) a DynamicBitSet before setting its bits");

  auto all = DynamicBitSet::All();
  ASSERT_DEATH(all.set(0), "resize\\(\\) a DynamicBitSet before setting its bits");

}

// Swapping exchanges the representations as well as the bits.
TEST_F(DynamicBitSetTest, Swap) {
  auto inline_set = MakeSet(10, {3});
  auto dynamic_set = MakeSet(2 * kInline + 1, {kInline, 2 * kInline});

  inline_set.swap(dynamic_set);
  ASSERT_TRUE(IsDynamic(inline_set));
  ASSERT_NO_FATALS(CheckBits(inline_set, 2 * kInline + 1, {kInline, 2 * kInline}));
  ASSERT_FALSE(IsDynamic(dynamic_set));
  ASSERT_NO_FATALS(CheckBits(dynamic_set, 10, {3}));

  DynamicBitSet none;
  none.swap(inline_set);
  ASSERT_NO_FATALS(CheckBits(none, 2 * kInline + 1, {kInline, 2 * kInline}));
  ASSERT_EQ(inline_set.size(), kNoSize);
  ASSERT_FALSE(inline_set.any());
}

// Assigning a set to itself, by copy or by move, keeps its bits and its storage.
TEST_F(DynamicBitSetTest, SelfAssignment) {
  for (auto [size, positions] : std::vector<std::pair<size_t, std::vector<size_t>>>{
           {10, {3}}, {2 * kInline + 1, {kInline, 2 * kInline}}}) {
    SCOPED_TRACE(Format("size: $0", size));
    DynamicBitSet set;
    set.resize(size);
    for (auto pos : positions) {
      set.set(pos);
    }

    // Through a reference, so that the compiler does not reject the self-assignment.
    DynamicBitSet& alias = set;
    set = alias;
    ASSERT_NO_FATALS(CheckBits(set, size, positions));
    set = std::move(alias);
    ASSERT_NO_FATALS(CheckBits(set, size, positions));
  }
}

TEST_F(DynamicBitSetTest, ResizePastMaxCapacityDies) {
  DynamicBitSet none;
  ASSERT_DEATH(none.resize(DynamicBitSet::kMaxCapacity + 1), "exceeds kMaxCapacity");

  auto bounded = MakeSet(10, {});
  ASSERT_DEATH(bounded.resize(DynamicBitSet::kMaxCapacity + 1), "exceeds kMaxCapacity");
}

// Shrinking dynamic storage to a block boundary clears every block from the boundary on.
TEST_F(DynamicBitSetTest, ShrinkDynamicToBlockBoundary) {
  const auto size = 3 * kInline;
  const std::vector<size_t> positions = {0, kInline - 1, kInline, 2 * kInline - 1, 2 * kInline,
                                         size - 1};
  for (auto boundary : {kInline, 2 * kInline}) {
    SCOPED_TRACE(Format("boundary: $0", boundary));
    DynamicBitSet set;
    set.resize(size);
    for (auto pos : positions) {
      set.set(pos);
    }
    std::vector<size_t> kept;
    std::ranges::copy_if(positions, std::back_inserter(kept), [boundary](auto pos) {
      return pos < boundary;
    });

    set.resize(boundary);
    ASSERT_NO_FATALS(CheckBits(set, boundary, kept));
    set.resize(size);
    ASSERT_NO_FATALS(CheckBits(set, size, kept));
  }
}

// A moved-from set is virtual and empty, and can be sized and used again.
TEST_F(DynamicBitSetTest, ReuseAfterMove) {
  auto set = MakeSet(2 * kInline + 1, {5, 2 * kInline});
  DynamicBitSet moved(std::move(set));
  set.resize(3); // NOLINT(bugprone-use-after-move)
  set.set(1);
  ASSERT_NO_FATALS(CheckBits(set, 3, {1}));
  ASSERT_NO_FATALS(CheckBits(moved, 2 * kInline + 1, {5, 2 * kInline}));

  DynamicBitSet assigned;
  assigned = std::move(moved);
  moved.resize(kInline + 1); // NOLINT(bugprone-use-after-move)
  moved.set(kInline);
  ASSERT_TRUE(IsDynamic(moved));
  ASSERT_NO_FATALS(CheckBits(moved, kInline + 1, {kInline}));
  ASSERT_NO_FATALS(CheckBits(assigned, 2 * kInline + 1, {5, 2 * kInline}));
}

// Bits print most significant first, across the boundary between blocks.
TEST_F(DynamicBitSetTest, ToStringDynamic) {
  auto set = MakeSet(kInline + 3, {0, kInline - 1, kInline + 1});
  ASSERT_EQ(set.ToString(), "010" "1" + std::string(kInline - 2, '0') + "1");
}

}  // namespace yb
