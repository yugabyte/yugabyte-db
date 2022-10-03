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
//

#include <gtest/gtest.h>

#include "yb/util/logging.h"
#include "yb/util/numbered_deque.h"
#include "yb/util/test_macros.h"

namespace yb {

namespace {

struct Element {
  Element() {}
  explicit Element(int _value) : value(_value) {}

  int value;
};

Status Check(const NumberedDeque<int, Element>& deque, const int first_seq_num) {
  int seq_num = first_seq_num;
  int idx = 0;
  for (const Element& elem : deque) {
    VLOG_WITH_FUNC(1) << "value: " << elem.value;
    SCHECK_EQ(
        elem.value, seq_num, IllegalState,
        Format("unexpected $0-th (0-based) element value (first_seq_num: $1)", idx, first_seq_num));
    const Element& elem_by_seq_num = VERIFY_RESULT(deque.Get(seq_num));
    SCHECK_EQ(
        seq_num, elem_by_seq_num.value, IllegalState,
        "NumberedDeque::Get: sequence number vs value mismatch");
    ++idx;
    ++seq_num;
  }

  seq_num = first_seq_num;
  idx = 0;
  for (auto it = deque.cbegin(); it != deque.cend(); ++it) {
    SCHECK_EQ(
        it->value, seq_num, IllegalState,
        Format("unexpected $0-th (0-based) element value (first_seq_num: $1)", idx, first_seq_num));
    ++seq_num;
    ++idx;
  }

  const Element& back = VERIFY_RESULT(deque.back());
  seq_num = back.value;
  idx = 0;
  for (auto it = deque.rbegin(); it != deque.rend(); ++it) {
    SCHECK_EQ(
        it->value, seq_num, IllegalState,
        Format(
            "unexpected $0-th (0-based, reverse) element value (first_seq_num: $1)", idx,
            first_seq_num));
    --seq_num;
    ++idx;
  }

  return Status::OK();
}

} // namespace

TEST(NumberedDequeTest, Basic) {
  NumberedDeque<int, Element> deque;

  constexpr auto kInitialNum = 111;
  constexpr auto kMaxDist = 100;

  // Run 2 iterations to make sure deque works fine after clear.
  for (int iter = 0; iter < 2; ++iter) {
    ASSERT_TRUE(deque.empty());

    ASSERT_OK(deque.push_back(kInitialNum, Element(kInitialNum)));
    ASSERT_FALSE(deque.empty());
    for (int i = 1; i < kMaxDist; ++i) {
      auto seq_num = kInitialNum + i;

      VLOG(1) << "push_back, i: " << i << ", seq_num: " << seq_num;
      ASSERT_OK(deque.push_back(seq_num, Element(seq_num)));
      ASSERT_OK(Check(deque, kInitialNum - i + 1));
      ASSERT_OK(deque.pop_back());
      ASSERT_OK(Check(deque, kInitialNum - i + 1));
      ASSERT_OK(deque.push_back(seq_num, Element(seq_num)));
      ASSERT_OK(Check(deque, kInitialNum - i + 1));

      seq_num = kInitialNum - i;
      VLOG(1) << "push_front, i: " << i << ", seq_num: " << seq_num;
      ASSERT_OK(deque.push_front(seq_num, Element(seq_num)));
      ASSERT_OK(Check(deque, seq_num));
      ASSERT_OK(deque.pop_front());
      ASSERT_OK(Check(deque, seq_num + 1));
      ASSERT_OK(deque.push_front(seq_num, Element(seq_num)));
      ASSERT_OK(Check(deque, seq_num));
    }

    ASSERT_EQ(deque.size(), kMaxDist * 2 - 1);

    const Element& first = ASSERT_RESULT(deque.front());
    const auto first_seq_num = first.value;

    // Truncate to bigger size is a no-op.
    for (int add_size = 0; add_size < 2; ++add_size) {
      deque.truncate(deque.size() + add_size);
      ASSERT_EQ(deque.size(), kMaxDist * 2 - 1);
      ASSERT_OK(Check(deque, first_seq_num));
    }

    // Truncate to smaller size.
    if (iter == 0) {
      deque.truncate(kMaxDist);
    } else {
      deque.truncate(deque.begin() + kMaxDist);
    }
    ASSERT_EQ(deque.size(), kMaxDist);
    ASSERT_OK(Check(deque, first_seq_num));

    // Make sure gaps in sequence numbers are not allowed.
    const Element& front = ASSERT_RESULT(deque.front());
    ASSERT_TRUE(deque.push_front(front.value, front).IsInvalidArgument());
    const Element& back = ASSERT_RESULT(deque.back());
    ASSERT_TRUE(deque.push_back(back.value, back).IsInvalidArgument());

    deque.clear();
    ASSERT_EQ(deque.size(), 0);
  }
}

TEST(NumberedDequeTest, Underflow) {
  NumberedDeque<uint32_t, Element> deque;
  ASSERT_OK(deque.push_back(0, Element(0)));
  ASSERT_TRUE(deque.push_front(-1, Element(1)).IsIllegalState());
}

} // namespace yb
