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

#include <vector>

#include <gtest/gtest.h>

#include "yb/common/id_mapping.h"
#include "yb/util/random.h"
#include "yb/util/test_util.h"

namespace yb {
// Basic unit test for IdMapping.
TEST(TestIdMapping, TestSimple) {
  IdMapping m;
  ASSERT_EQ(-1, m.get(1));
  m.set(1, 10);
  m.set(2, 20);
  m.set(3, 30);
  ASSERT_EQ(10, m.get(1));
  ASSERT_EQ(20, m.get(2));
  ASSERT_EQ(30, m.get(3));
}

// Insert enough entries in the mapping so that it is forced to rehash
// itself.
TEST(TestIdMapping, TestRehash) {
  IdMapping m;

  for (int i = 0; i < 1000; i++) {
    m.set(i, i * 10);
  }
  for (int i = 0; i < 1000; i++) {
    ASSERT_EQ(i * 10, m.get(i));
  }
}

// Generate a random sequence of keys.
TEST(TestIdMapping, TestRandom) {
  Random r(SeedRandom());
  IdMapping m;
  std::vector<int> picked;
  for (int i = 0; i < 1000; i++) {
    int32_t k = r.Next32();
    m.set(k, i);
    picked.push_back(k);
  }

  for (size_t i = 0; i < picked.size(); i++) {
    ASSERT_EQ(i, m.get(picked[i]));
  }
}

// Regression test for a particular bad sequence of inserts
// that caused a crash on a previous implementation.
//
// The particular issue here is that we have many inserts
// which have the same key modulo the initial capacity.
TEST(TestIdMapping, TestBadSequence) {
  IdMapping m;
  m.set(0, 0);
  m.set(4, 0);
  m.set(128, 0); // 0 modulo 64 and 128
  m.set(129, 0); // 1
  m.set(130, 0); // 2
  m.set(131, 0); // 3
}

TEST(TestIdMapping, TestReinsert) {
  IdMapping m;
  m.set(0, 0);
  ASSERT_DEATH({
    m.set(0, 1);
  },
  "Cannot insert duplicate keys");
}

TEST(TestIdMapping, TestEquality) {
  IdMapping m1;
  m1.set(123, 456);
  IdMapping m2(m1);
  ASSERT_EQ(m1, m2);
  m2.set(321, 456);
  ASSERT_NE(m1, m2);
  m1.set(321, 654);
  ASSERT_NE(m1, m2);
  m1.clear();
  m1.set(123, 456);
  m1.set(321, 456);
  ASSERT_EQ(m1, m2);

  // Capacity shouldn't matter
  m1.clear();
  for (int i = 0; i < 100000; ++i) {
    m1.set(i, i);
  }
  m1.clear();
  m1.set(123, 456);
  m1.set(321, 456);
  ASSERT_EQ(m1, m2);
  m2.set(100, 200);
  ASSERT_NE(m1, m2);
}

} // namespace yb
