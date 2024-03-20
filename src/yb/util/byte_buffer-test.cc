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
#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

#include "yb/util/byte_buffer.h"
#include "yb/util/random_util.h"

using namespace std::literals;

namespace yb {

using Buffer = ByteBuffer<8>;

TEST(ByteBufferTest, Assign) {
  std::string str = "source_string"s;
  Buffer buffer(str);
  Buffer copy(buffer);
  ASSERT_EQ(buffer, copy);
  copy = buffer;
  Buffer moved(std::move(buffer));
  ASSERT_TRUE(buffer.empty()); // NOLINT(bugprone-use-after-move)
  ASSERT_EQ(moved, copy);
  moved = std::move(copy);
  ASSERT_TRUE(copy.empty()); // NOLINT(bugprone-use-after-move)
  buffer.Assign(str);
  ASSERT_EQ(buffer, moved);
}

TEST(ByteBufferTest, Append) {
  constexpr int kStringLen = 128;
  constexpr int kIterations = 1000;
  std::string str = RandomHumanReadableString(kStringLen);
  for (int i = kIterations; i-- > 0;) {
    Buffer buffer;
    while (buffer.size() < str.size()) {
      size_t len = RandomUniformInt<size_t>(0, str.size() - buffer.size());
      buffer.append(Slice(str.c_str() + buffer.size(), len));
      ASSERT_EQ(buffer.AsSlice(), Slice(str).Prefix(buffer.size()));
      if (len == 0) {
        buffer.PushBack(str[buffer.size()]);
        ASSERT_EQ(buffer.AsSlice(), Slice(str).Prefix(buffer.size()));
      }
    }
  }
}

template <class Map>
void TestMap() {
  std::vector<std::pair<std::string, int>> entries = {
      { "one"s, 1 },
      { "two"s, 2 },
      { "three"s, 3 },
  };

  Map m;
  for (const auto& p : entries) {
    ASSERT_TRUE(m.emplace(Buffer(p.first), p.second).second);
  }

  for (const auto& p : entries) {
    ASSERT_EQ(m[Buffer(p.first)], p.second);
  }
}

TEST(ByteBufferTest, Map) {
  TestMap<std::map<Buffer, int>>();
  TestMap<std::unordered_map<Buffer, int, ByteBufferHash>>();
}

} // namespace yb
