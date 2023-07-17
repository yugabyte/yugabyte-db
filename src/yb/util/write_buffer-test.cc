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

#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
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

}  // namespace yb
