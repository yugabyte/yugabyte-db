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

#include "yb/gutil/map-util.h"

#include "yb/util/random_util.h"
#include "yb/util/slice.h"
#include "yb/util/test_util.h"
#include "yb/util/tostring.h"
#include "yb/util/tsan_util.h"

using std::string;

namespace yb {

// STL map whose keys are Slices.
//
// See sample usage in slice-test.cc.
template <typename T>
struct SliceMap {
  typedef std::map<Slice, T, Slice::Comparator> type;
};

typedef SliceMap<int>::type MySliceMap;

TEST(SliceTest, TestSliceMap) {
  MySliceMap my_map;
  Slice a("a");
  Slice b("b");
  Slice c("c");

  // Insertion is deliberately out-of-order; the map should restore order.
  InsertOrDie(&my_map, c, 3);
  InsertOrDie(&my_map, a, 1);
  InsertOrDie(&my_map, b, 2);

  int expectedValue = 0;
  for (const MySliceMap::value_type& pair : my_map) {
    int data = 'a' + expectedValue++;
    ASSERT_EQ(Slice(reinterpret_cast<uint8_t*>(&data), 1), pair.first);
    ASSERT_EQ(expectedValue, pair.second);
  }

  expectedValue = 0;
  for (auto iter = my_map.begin(); iter != my_map.end(); iter++) {
    int data = 'a' + expectedValue++;
    ASSERT_EQ(Slice(reinterpret_cast<uint8_t*>(&data), 1), iter->first);
    ASSERT_EQ(expectedValue, iter->second);
  }
}

template <size_t prefix, size_t len, bool prefix_eq_len>
struct TestLessHelper;

void CheckLess(bool expected, const Slice& lhs) {
  // Don't have version that accept empty rhs
}

size_t made_checks = 0;

template <class... Args>
void CheckLess(bool expected, const Slice& lhs, Args&&... rhs) {
  static_assert(sizeof...(Args) > 0, "Args must not be empty");
  ASSERT_EQ(expected, lhs.Less(std::forward<Args>(rhs)...))
       << lhs.ToBuffer() << " vs " << AsString(std::tuple<Args...>(std::forward<Args>(rhs)...));
  ++made_checks;
}

template <size_t prefix>
struct TestLessHelper<prefix, prefix, /* prefix_eq_len */ true> {
  // Requires: lhs = rhs
  template <class... Args>
  static void Apply(const Slice& lhs, Args&&... rhs) {
    CheckLess(false, lhs, std::forward<Args>(rhs)...);
  }
};

template <size_t prefix, size_t len, class... Args>
void TestLess(const Slice& lhs, Args&&... rhs);

template <size_t prefix, size_t len, size_t new_prefix, bool new_prefix_le_len>
struct TestLessIteration;

template <size_t prefix, size_t len, size_t new_prefix>
struct TestLessIteration<prefix, len, new_prefix, /* new_prefix_le_len= */ false> {
  template <class... Args>
  static void Apply(const Slice& lhs, Args&&... rhs) {
  }
};

template <size_t prefix, size_t len, size_t new_prefix>
struct TestLessIteration<prefix, len, new_prefix, /* new_prefix_le_len= */ true> {
  template <class... Args>
  static void Apply(const Slice& lhs, Args&&... rhs) {
    TestLess<new_prefix, len>(
        lhs, std::forward<Args>(rhs)..., Slice(lhs.data() + prefix, lhs.data() + new_prefix));
    TestLessIteration<prefix, len, new_prefix + 1, (new_prefix < len)>::Apply(
        lhs, std::forward<Args>(rhs)...);
  }
};

template <size_t prefix, size_t len>
struct TestLessHelper<prefix, len, /* prefix_eq_len= */ false> {
  // Requires: len = lhs.size(), lhs.starts_with(rhs), prefix = rhs.size(), prefix < len
  template <class... Args>
  static void Apply(const Slice& lhs, Args&&... rhs) {
    CheckLess(false, lhs, std::forward<Args>(rhs)...);

    TestLessIteration<prefix, len, prefix + 1, (prefix < len)>::Apply(
        lhs, std::forward<Args>(rhs)...);
  }
};

template <size_t prefix, size_t len, class... Args>
// Requires: len = lhs.size(), lhs.starts_with(rhs), prefix = rhs.size()
void TestLess(const Slice& lhs, Args&&... rhs) {
  char kMinChar = '\x00';
  char kMaxChar = '\xff';
  CheckLess(prefix == len, lhs, std::forward<Args>(rhs)..., Slice(&kMinChar, 1));
  CheckLess(true, lhs, std::forward<Args>(rhs)..., Slice(&kMaxChar, 1));

  TestLessHelper<prefix, len, prefix == len>::Apply(lhs, std::forward<Args>(rhs)...);
}

TEST(SliceTest, Less) {
  constexpr size_t kLen = 10;

  auto random_bytes = RandomHumanReadableString(kLen);

  std::vector<Slice> rhs;
  TestLess<0, kLen>(random_bytes);
  // There are 2^(kLen - 1) ways to split slice into concatenation of slices.
  // So number of ways to get slice and all its non empty prefixes would be
  // sum 2^n, for n from 0 to kLen - 1.
  // So it will be 2^kLen - 1.
  // We use each such combination X 3 times - X, X + kMinChar, X + kMaxChar.
  // And compare with just kMinChar and kMaxChar. So we should get:
  // (2 ^ kLen - 1) * 3 + 2 comparisons, simplified it to (2^kLen) * 3 - 1
  ASSERT_EQ(made_checks, (1ULL << kLen) * 3 - 1);
}

template <class... Args>
void TestLessComponents(
    const bool expected, const size_t component_size, const Slice& lhs, const Slice& rhs0,
    Args&&... rhs) {
  CheckLess(expected, lhs, rhs0, std::forward<Args>(rhs)...);
  NO_PENDING_FATALS();
  if (RandomActWithProbability(0.9)) {
    CheckLess(expected, lhs, rhs0, Slice(""), std::forward<Args>(rhs)...);
    NO_PENDING_FATALS();
  }
  if (rhs0.size() > component_size) {
    CheckLess(
        expected, lhs, rhs0.Prefix(rhs0.size() - component_size), rhs0.Suffix(component_size),
        std::forward<Args>(rhs)...);
  }
}

void TestOrder(const std::string& str1, const std::string& str2, const size_t num_components) {
  const auto s1 = Slice(str1);
  const auto s2 = Slice(str2);
  auto component_size = std::max<size_t>(s2.size() / num_components, 1);
  TestLessComponents(s1.compare(s2) < 0, component_size, s1, s2);
  component_size = std::max<size_t>(s1.size() / num_components, 1);
  TestLessComponents(s2.compare(s1) < 0, component_size, s2, s1);
}

// Test that Slice::Less order is consistent with Slice::compare.
TEST(SliceTest, OrderConsistency) {
  constexpr auto kNumIters = ReleaseVsDebugVsAsanVsTsan(100000, 100000, 50000, 50000);
  constexpr auto kMaxLength = 256;
  constexpr auto kMaxComponents = 10;

  TestOrder("ABC", "", /* num_components = */ kMaxComponents);
  TestOrder("ABCD", "ABC", /* num_components = */ kMaxComponents);
  TestOrder("ABC", "ABC", /* num_components = */ kMaxComponents);

  std::string str1;
  std::string str2;

  for (int iter = 0; iter < kNumIters; ++iter) {
    const auto len1 = RandomUniformInt<size_t>(0, kMaxLength);
    const auto len2 = RandomUniformInt<size_t>(0, kMaxLength);
    str1 = RandomHumanReadableString(len1);
    str2 = RandomHumanReadableString(len2);

    const auto num_components = 1 + kMaxComponents * iter / kNumIters;

    TestOrder(str1, str2, num_components);
    NO_PENDING_FATALS();
  }
  LOG(INFO) << "made_checks: " << made_checks;
}

TEST(SliceTest, DebugStringLength) {
  constexpr auto kNumIters = 1000;
  constexpr auto kSize = 1024;
  for (auto i = 0; i < kNumIters; ++i) {
    const auto bytes = RandomBytes(kSize);
    const auto slice = Slice(bytes.data(), bytes.size());
    const auto max_len = yb::RandomUniformInt(1, kSize);
    // Allow up to 2 hex chars per one byte plus 40 for "abbreviated ..." suffix.
    const auto debug_str = slice.ToDebugString(max_len);
    ASSERT_LE(debug_str.size(), max_len * 2 + 40)
        << " max_len: " << max_len << " debug_str: " << debug_str;

    if (max_len < 100) {
      YB_LOG_EVERY_N(INFO, 10) << debug_str;
    }
  }
}

} // namespace yb
