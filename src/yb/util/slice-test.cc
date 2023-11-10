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
#include "yb/util/tostring.h"

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
  ASSERT_EQ(expected, lhs.Less(std::forward<Args>(rhs)...))
       << lhs.ToBuffer() << " vs " << AsString(std::tuple<Args...>(std::forward<Args>(rhs)...));
  ++made_checks;
}

template <size_t prefix>
struct TestLessHelper<prefix, prefix, /* prefix_eq_len */ true> {
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
  template <class... Args>
  static void Apply(const Slice& lhs, Args&&... rhs) {
    CheckLess(true, lhs, std::forward<Args>(rhs)...);

    TestLessIteration<prefix, len, prefix + 1, (prefix < len)>::Apply(
        lhs, std::forward<Args>(rhs)...);
  }
};

template <size_t prefix, size_t len, class... Args>
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

} // namespace yb
