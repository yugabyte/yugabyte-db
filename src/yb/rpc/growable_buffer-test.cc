//
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
//

#include <gtest/gtest.h>

#include "yb/rpc/growable_buffer.h"

#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace rpc {

constexpr size_t kBlockSize = 0x100;
constexpr size_t kSizeLimit = 0x1000;

class GrowableBufferTest : public YBTest {
 protected:
  GrowableBufferAllocator allocator_{kBlockSize, MemTrackerPtr()};
};

TEST_F(GrowableBufferTest, TestLimit) {
  GrowableBuffer buffer(&allocator_, kSizeLimit);

  ASSERT_EQ(buffer.capacity_left(), kBlockSize);
  for (;;) {
    auto result = buffer.PrepareAppend();
    ASSERT_EQ(result.ok(), buffer.size() < buffer.limit())
        << "Status: " << (result.ok() ? Status::OK() : result.status());
    if (!result.ok()) {
      break;
    }
    buffer.DataAppended(1);
  }

  ASSERT_EQ(buffer.capacity_left(), 0);
}

TEST_F(GrowableBufferTest, TestPrepareRead) {
  GrowableBuffer buffer(&allocator_, kSizeLimit);

  unsigned int seed = SeedRandom();

  while (buffer.size() != buffer.limit()) {
    auto status = buffer.PrepareAppend();
    ASSERT_OK(status);
    size_t step = 1 + rand_r(&seed) % buffer.capacity_left();
    buffer.DataAppended(step);
  }

  ASSERT_EQ(buffer.capacity_left(), 0);
}

TEST_F(GrowableBufferTest, TestConsume) {
  GrowableBuffer buffer(&allocator_, kSizeLimit);

  int counter = 0;

  unsigned int seed = SeedRandom();
  size_t consumed = 0;

  for (auto i = 10000; i--;) {
    size_t step = 1 + rand_r(&seed) % (buffer.limit() - buffer.size());
    size_t appended = 0;
    {
      auto iov = ASSERT_RESULT(buffer.PrepareAppend());
      size_t idx = 0;
      auto* data = static_cast<uint8_t*>(iov[0].iov_base);
      int start = 0;
      for (size_t j = 0; j != step; ++j) {
        if (j - start >= iov[idx].iov_len) {
          start += iov[idx].iov_len;
          ++idx;
          if (idx >= iov.size()) {
            break;
          }
          data = static_cast<uint8_t*>(iov[idx].iov_base);
        }
        data[j - start] = static_cast<uint8_t>(counter++);
        ++appended;
      }
    }
    buffer.DataAppended(appended);
    ASSERT_EQ(consumed + buffer.size(), counter);
    size_t consume_size = 1 + rand_r(&seed) % buffer.size();
    buffer.Consume(consume_size, Slice());
    consumed += consume_size;
    ASSERT_EQ(consumed + buffer.size(), counter);
    auto iovs = buffer.AppendedVecs();
    auto value = consumed;
    for (const auto& iov : iovs) {
      const auto* data = static_cast<const uint8_t*>(iov.iov_base);
      for (size_t j = 0; j != iov.iov_len; ++j) {
        ASSERT_EQ(data[j], static_cast<uint8_t>(value++));
      }
    }
  }
}

} // namespace rpc
} // namespace yb
