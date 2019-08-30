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

#include <queue>

#include <gtest/gtest.h>

#include "yb/util/priority_queue.h"
#include "yb/util/random_util.h"

namespace yb {

TEST(PriorityQueueTest, Simple) {
  PriorityQueue<int> pq;
  ASSERT_TRUE(pq.empty());
  pq.Push(3);
  ASSERT_FALSE(pq.empty());
  pq.Push(5);
  pq.Push(2);
  pq.Push(1);
  pq.Push(4);

  ASSERT_EQ(pq.Pop(), 5);
  ASSERT_EQ(pq.Pop(), 4);
  ASSERT_EQ(pq.Pop(), 3);
  ASSERT_EQ(pq.Pop(), 2);
  ASSERT_EQ(pq.Pop(), 1);
  ASSERT_TRUE(pq.empty());
}

TEST(PriorityQueueTest, Swap) {
  PriorityQueue<int> pq;
  pq.Push(3);
  pq.Push(2);
  pq.Push(1);

  std::vector<int> vector = {6, 5, 4};

  pq.Swap(&vector);
  ASSERT_EQ(vector.size(), 3);
  ASSERT_EQ(vector[0], 3);

  ASSERT_EQ(pq.Pop(), 6);
  ASSERT_EQ(pq.Pop(), 5);
  ASSERT_EQ(pq.Pop(), 4);
}

TEST(PriorityQueueTest, RemoveIf) {
  PriorityQueue<int> pq;
  for (int i = 1; i <= 10; ++i) {
    pq.Push(i);
  }
  pq.RemoveIf([](int* v) { return (*v & 1) == 0; });

  ASSERT_EQ(pq.Pop(), 9);
  ASSERT_EQ(pq.Pop(), 7);
  ASSERT_EQ(pq.Pop(), 5);
  ASSERT_EQ(pq.Pop(), 3);
  ASSERT_EQ(pq.Pop(), 1);
}

TEST(PriorityQueueTest, RemoveNone) {
  PriorityQueue<int> pq;
  for (int i = 1; i <= 5; ++i) {
    pq.Push(i);
  }
  pq.RemoveIf([](int* v) { return false; });

  ASSERT_EQ(pq.Pop(), 5);
  ASSERT_EQ(pq.Pop(), 4);
  ASSERT_EQ(pq.Pop(), 3);
  ASSERT_EQ(pq.Pop(), 2);
  ASSERT_EQ(pq.Pop(), 1);
}

TEST(PriorityQueueTest, RemoveAll) {
  PriorityQueue<int> pq;
  for (int i = 1; i <= 5; ++i) {
    pq.Push(i);
  }
  pq.RemoveIf([](int* v) { return true; });

  ASSERT_TRUE(pq.empty());
}

TEST(PriorityQueueTest, Random) {
  PriorityQueue<int> pq;
  std::priority_queue<int> std_pq;

  for (int i = 0; i != 1000; ++i) {
    ASSERT_EQ(pq.empty(), std_pq.empty());
    if (pq.empty() || RandomUniformInt(0, 1) == 0) {
      int value = RandomUniformInt<int>(-1000, 1000);
      pq.Push(value);
      std_pq.push(value);
    } else {
      ASSERT_EQ(pq.Pop(), std_pq.top());
      std_pq.pop();
    }
  }
}

} // namespace yb
