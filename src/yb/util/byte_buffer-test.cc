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
#include "yb/util/tsan_util.h"

using namespace std::literals;

namespace yb {

using Buffer = ByteBuffer<8>;
using MemTrackedBuffer = MemTrackedByteBuffer<8>;

TEST(ByteBufferTest, Consumption) {
  // Ensure for non-mem-tracked buffer we don't waste memory on consumption field.
  ASSERT_LT(sizeof(Buffer), sizeof(MemTrackedBuffer));

  auto t = MemTracker::CreateTracker("t");
  auto allocated_bytes_prev = GetTCMallocCurrentAllocatedBytes();
  auto consumption_prev = t->consumption();

  auto get_allocated_bytes_delta = [&allocated_bytes_prev] {
    if (IsSanitizer()) {
      // Can't detect properly under sanitizers, so just make test always pass but still run it
      // in order to potentially catch sanitizer issues.
      return int64_t(0);
    }
    const auto old_allocated_bytes_prev = allocated_bytes_prev;
    allocated_bytes_prev = GetTCMallocCurrentAllocatedBytes();
    return allocated_bytes_prev - old_allocated_bytes_prev;
  };
  auto get_consumption_delta = [&consumption_prev, t] {
    if (IsSanitizer()) {
      return int64_t(0);
    }
    const auto old_consumption_prev = consumption_prev;
    consumption_prev = t->consumption();
    return consumption_prev - old_consumption_prev;
  };

  MemTrackedBuffer buffer(t);
  if (!IsSanitizer()) {
    ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());
  }

  buffer.append(std::string(777, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  buffer.append(std::string(555, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  auto buffer2 = std::move(buffer);
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  buffer2.assign(std::string(1234, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  MemTrackedBuffer buffer3(t, std::string(9999, 'x'));
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  auto buffer4 = buffer3;
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  MemTrackedBuffer buffer5(t);
  buffer5 = std::string(5555, 'x');
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  auto buffer6(buffer5);
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());

  std::string str6(10000, 'x');
  // Update allocated bytes to account for str6 memory usage which is not tracked by mem tracker.
  get_allocated_bytes_delta();

  buffer6 = Slice(str6);
  ASSERT_EQ(get_allocated_bytes_delta(), get_consumption_delta());
}

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
