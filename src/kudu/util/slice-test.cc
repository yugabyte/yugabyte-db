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

#include "kudu/util/slice.h"

#include <gtest/gtest.h>

#include "kudu/gutil/map-util.h"

using std::string;

namespace kudu {

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

} // namespace kudu
