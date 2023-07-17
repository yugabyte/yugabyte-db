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

#include "yb/util/lru_cache.h"
#include "yb/util/tostring.h"

namespace yb {

TEST(LRUCacheTest, Simple) {
  LRUCache<int> cache(2);
  cache.insert(1);
  cache.insert(2);
  cache.insert(3);
  ASSERT_EQ(AsString(cache), "[3, 2]");
  ASSERT_EQ(0, cache.erase(1));
  ASSERT_EQ(1, cache.erase(3));
  ASSERT_EQ(AsString(cache), "[2]");
}

TEST(LRUCacheTest, Erase) {
  LRUCache<int> cache(5);
  cache.insert(1);
  cache.insert(2);
  cache.insert(3);
  cache.insert(4);
  cache.insert(5);
  auto last = std::prev(cache.end());
  ASSERT_EQ(*last, 1);
  auto end = cache.erase(last);
  ASSERT_EQ(end, cache.end());
  ASSERT_EQ(AsString(cache), "[5, 4, 3, 2]");
  last = std::prev(cache.end());
  auto i = cache.erase(std::next(cache.begin()), std::prev(cache.end()));
  ASSERT_EQ(*i, 2);
  ASSERT_EQ(AsString(cache), "[5, 2]");
}

} // namespace yb
