//
// Copyright (c) YugaByte, Inc.
//

#include <gtest/gtest.h>

#include "yb/rpc/growable_buffer.h"

#include "yb/util/test_util.h"

namespace yb {
namespace rpc {

class GrowableBufferTest : public YBTest {
};

const size_t kInitialSize = 0x100;
const size_t kSizeLimit = 0x1000;

TEST_F(GrowableBufferTest, TestLimit) {
  GrowableBuffer buffer(kInitialSize, kSizeLimit);

  ASSERT_EQ(buffer.capacity_left(), kInitialSize);
  unsigned int seed = SeedRandom();
  while (buffer.size() != buffer.limit()) {
    size_t extra_space = rand_r(&seed) % (kSizeLimit * 2);
    auto status = buffer.EnsureFreeSpace(extra_space);
    ASSERT_EQ(status.ok(), buffer.size() + extra_space <= buffer.limit());
    buffer.DataAppended(1);
  }

  ASSERT_EQ(buffer.capacity_left(), 0);
}

TEST_F(GrowableBufferTest, TestPrepareRead) {
  GrowableBuffer buffer(kInitialSize, kSizeLimit);

  unsigned int seed = SeedRandom();

  while (buffer.size() != buffer.limit()) {
    auto status = buffer.PrepareRead();
    ASSERT_OK(status);
    size_t step = 1 + rand_r(&seed) % buffer.capacity_left();
    buffer.DataAppended(step);
  }

  ASSERT_EQ(buffer.capacity_left(), 0);
}

TEST_F(GrowableBufferTest, TestConsume) {
  GrowableBuffer buffer(kInitialSize, kSizeLimit);

  int counter = 0;

  unsigned int seed = SeedRandom();
  size_t consumed = 0;

  for (auto i = 10000; i--;) {
    size_t step = 1 + rand_r(&seed) % (buffer.limit() - buffer.size());
    ASSERT_OK(buffer.EnsureFreeSpace(step));
    for (int j = 0; j != step; ++j)
      buffer.write_position()[j] = static_cast<uint8_t>(counter++);
    buffer.DataAppended(step);
    ASSERT_EQ(consumed + buffer.size(), counter);
    size_t consume_size = 1 + rand_r(&seed) % buffer.size();
    buffer.Consume(consume_size);
    consumed += consume_size;
    ASSERT_EQ(consumed + buffer.size(), counter);
    for (int j = 0; j != buffer.size(); ++j) {
      ASSERT_EQ(buffer.begin()[j], static_cast<uint8_t>(consumed + j));
    }
  }
}

} // namespace rpc
} // namespace yb
