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

#include "kudu/util/test_util.h"

#include "kudu/util/hash_util.h"

namespace kudu {

// Test Murmur2 Hash64 returns the expected values for inputs. These tests are
// duplicated on the Java side to ensure that hash computations are stable
// across both platforms.
TEST(HashUtilTest, TestMurmur2Hash64) {
  uint64_t hash;

  hash = HashUtil::MurmurHash2_64("ab", 2, 0);
  ASSERT_EQ(7115271465109541368, hash);

  hash = HashUtil::MurmurHash2_64("abcdefg", 7, 0);
  ASSERT_EQ(2601573339036254301, hash);

  hash = HashUtil::MurmurHash2_64("quick brown fox", 15, 42);
  ASSERT_EQ(3575930248840144026, hash);
}

} // namespace kudu
