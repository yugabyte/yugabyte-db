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

// This unit test belongs in gutil, but it depends on test_main which is
// part of util.
#include "yb/gutil/map-util.h"


#include <gtest/gtest.h>

using std::map;
using std::string;
using std::unique_ptr;

namespace yb {

TEST(FloorTest, TestMapUtil) {
  map<int, int> my_map;

  ASSERT_EQ(nullptr, FindFloorOrNull(my_map, 5));

  my_map[5] = 5;
  ASSERT_EQ(5, *FindFloorOrNull(my_map, 6));
  ASSERT_EQ(5, *FindFloorOrNull(my_map, 5));
  ASSERT_EQ(nullptr, FindFloorOrNull(my_map, 4));

  my_map[1] = 1;
  ASSERT_EQ(5, *FindFloorOrNull(my_map, 6));
  ASSERT_EQ(5, *FindFloorOrNull(my_map, 5));
  ASSERT_EQ(1, *FindFloorOrNull(my_map, 4));
  ASSERT_EQ(1, *FindFloorOrNull(my_map, 1));
  ASSERT_EQ(nullptr, FindFloorOrNull(my_map, 0));

}

TEST(EmplaceTest, TestEmplace) {
  // Map with move-only value type.
  map<string, unique_ptr<string>> my_map;
  unique_ptr<string> val(new string("foo"));
  ASSERT_TRUE(EmplaceIfNotPresent(&my_map, "k", std::move(val)));
  ASSERT_TRUE(ContainsKey(my_map, "k"));
  ASSERT_FALSE(EmplaceIfNotPresent(&my_map, "k", nullptr))
      << "Should return false for already-present";
}

} // namespace yb
