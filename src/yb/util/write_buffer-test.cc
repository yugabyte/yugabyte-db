// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/enums.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/write_buffer.h"

namespace yb {

template <class F>
void TestPerformance(const F& f) {
  for (int i = 0; i != 5; ++i) {
    WriteBuffer write_buffer(16_KB);
    auto start = MonoTime::Now();
    for (int row = 0; row != 1000000; ++row) {
      for (int col = 0; col != 10; ++col) {
        f(row, col, &write_buffer);
      }
    }
    auto finish = MonoTime::Now();
    LOG(INFO) << i << ") passed: " << finish - start;
  }
}

TEST(WriteBufferTest, AppendWithPrefixPerformance) {
  TestPerformance([](int row, int col, WriteBuffer* write_buffer) {
    uint64_t value = row ^ col;
    write_buffer->AppendWithPrefix(0, pointer_cast<const char*>(&value), sizeof(value));
  });
}

TEST(WriteBufferTest, AppendPerformance) {
  TestPerformance([](int row, int col, WriteBuffer* write_buffer) {
    uint64_t value = row ^ col;
    write_buffer->Append(pointer_cast<const char*>(&value), sizeof(value));
  });
}

TEST(WriteBufferTest, MoveCtor) {
  std::string text("Hello");
  WriteBuffer original(0x10);
  original.Append(text.c_str(), text.size());
  alignas(alignof(WriteBuffer)) char buffer[sizeof(WriteBuffer)];
  std::fill_n(buffer, sizeof (buffer), 0x40);
  auto* moved = new (buffer) WriteBuffer(std::move(original));
  ASSERT_EQ(moved->ToBuffer(), text);
  moved->~WriteBuffer();
}

TEST(WriteBufferTest, Truncate) {
  std::string text1("First test record");
  std::string text2("Second test record");
  std::string text3("Third, a little bit longer test record");
  std::string text4("!");
  WriteBuffer buffer(0x10);
  auto empty_position = buffer.Position();
  auto empty_size = buffer.size();
  buffer.Append(Slice(text1));
  auto first_position = buffer.Position();
  auto first_size = buffer.size();
  buffer.Append(Slice(text2));
  buffer.Append(Slice(text3));
  buffer.Append(Slice(text4));
  auto second_position = buffer.Position();
  auto second_size = buffer.size();
  auto full_text = buffer.ToBuffer();
  LOG(INFO) << "Truncate to second position (no-op)";
  ASSERT_OK(buffer.Truncate(second_position));
  ASSERT_EQ(second_size, buffer.size());
  ASSERT_EQ(buffer.ToBuffer(), full_text);
  LOG(INFO) << "Truncate to first position";
  ASSERT_OK(buffer.Truncate(first_position));
  ASSERT_EQ(first_size, buffer.size());
  ASSERT_EQ(buffer.ToBuffer(), text1);
#ifdef NDEBUG
  // second position no longer valid
  LOG(INFO) << "Truncate to second position (invalid)";
  ASSERT_NOK(buffer.Truncate(second_position));
#endif
  // ensure buffer works properly after truncate
  buffer.Append(Slice(text2));
  buffer.Append(Slice(text3));
  buffer.Append(Slice(text4));
  ASSERT_EQ(buffer.ToBuffer(), full_text);
  ASSERT_EQ(second_size, buffer.size());
  LOG(INFO) << "Truncate to empty position";
  ASSERT_OK(buffer.Truncate(empty_position));
  ASSERT_EQ(empty_size, buffer.size());
  ASSERT_EQ(buffer.ToBuffer(), "");
}

TEST(WriteBufferTest, RandomTruncate) {
  constexpr int kStrMinLen = 2;
  constexpr int kStrMaxLen = 256;
  constexpr int kIterations = 100;
  constexpr int kTruncateProbability = 10;
  constexpr int kStorePosProbability = 20;
  WriteBuffer buffer(0x10);
  std::optional<WriteBufferPos> last_pos;
  std::mt19937_64 rng(12345);
  std::string expected;
  std::string saved_expected;
  int append_count = 0;
  int store_pos_count = 0;
  int truncate_count = 0;
  for (int i = 0; i < kIterations; ++i) {
    auto random = RandomUniformInt(0, 99, &rng);
    if (random < kTruncateProbability) {
      if (last_pos) {
        ++truncate_count;
        ASSERT_OK(buffer.Truncate(*last_pos));
        expected = saved_expected;
      }
    } else if (random < kTruncateProbability + kStorePosProbability) {
      ++store_pos_count;
      last_pos = buffer.Position();
      saved_expected = expected;
    } else {
      ++append_count;
      std::string str = RandomString(RandomUniformInt(kStrMinLen, kStrMaxLen, &rng), &rng);
      buffer.Append(Slice(str));
      expected += str;
    }
    ASSERT_EQ(buffer.ToBuffer(), expected);
  }
  LOG(INFO) << "Total appends: " << append_count
            << " store positions: " << store_pos_count
            << " truncates: " << truncate_count;
}

}  // namespace yb
