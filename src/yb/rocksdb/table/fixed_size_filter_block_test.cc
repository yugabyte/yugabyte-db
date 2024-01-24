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

#include <gtest/gtest.h>

#include "yb/rocksdb/table/fixed_size_filter_block.h"

#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/util/hash.h"
#include "yb/rocksdb/env.h"

#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

class FixedSizeFilterTest : public RocksDBTest {
 public:
  BlockBasedTableOptions table_options_;

  FixedSizeFilterTest() {
    table_options_.filter_policy.reset(NewFixedSizeFilterPolicy(
        FilterPolicy::kDefaultFixedSizeFilterBits, FilterPolicy::kDefaultFixedSizeFilterErrorRate,
        nullptr));
  }
};

TEST_F(FixedSizeFilterTest, SingleChunk) {
  FixedSizeFilterBlockBuilder builder(nullptr, table_options_);
  builder.StartBlock(0);
  builder.Add("foo");
  builder.Add("bar");
  builder.Add("box");
  builder.Add("hello");

  BlockContents block(builder.Finish(), false, kNoCompression);
  FixedSizeFilterBlockReader reader(nullptr, table_options_, true, std::move(block));
  ASSERT_TRUE(reader.KeyMayMatch("foo"));
  ASSERT_TRUE(reader.KeyMayMatch("bar"));
  ASSERT_TRUE(reader.KeyMayMatch("box"));
  ASSERT_TRUE(reader.KeyMayMatch("hello"));
  ASSERT_TRUE(!reader.KeyMayMatch("missing"));
  ASSERT_TRUE(!reader.KeyMayMatch("other"));
}

TEST_F(FixedSizeFilterTest, MultipleChunks) {
  FixedSizeFilterBlockBuilder builder(nullptr, table_options_);

  // First block
  builder.StartBlock(0);
  builder.Add("a1");
  builder.Add("b1");
  builder.Add("c1");
  builder.Add("foo");
  builder.Add("bar");

  BlockContents block1(builder.Finish(), false, kNoCompression);

  // Second block
  builder.StartBlock(0);
  builder.Add("a2");
  builder.Add("b2");
  builder.Add("c2");
  builder.Add("foo");
  builder.Add("bar");

  BlockContents block2(builder.Finish(), false, kNoCompression);

  // Third block (empty block)
  builder.StartBlock(0);
  BlockContents block3(builder.Finish(), false, kNoCompression);

  // Check first block
  FixedSizeFilterBlockReader reader1(nullptr, table_options_, true, std::move(block1));
  ASSERT_TRUE(reader1.KeyMayMatch("a1"));
  ASSERT_TRUE(reader1.KeyMayMatch("b1"));
  ASSERT_TRUE(reader1.KeyMayMatch("c1"));
  ASSERT_TRUE(reader1.KeyMayMatch("foo"));
  ASSERT_TRUE(reader1.KeyMayMatch("bar"));
  ASSERT_TRUE(!reader1.KeyMayMatch("a2"));
  ASSERT_TRUE(!reader1.KeyMayMatch("b2"));
  ASSERT_TRUE(!reader1.KeyMayMatch("c2"));
  ASSERT_TRUE(!reader1.KeyMayMatch("missing"));
  ASSERT_TRUE(!reader1.KeyMayMatch("other"));

  // Check second block
  FixedSizeFilterBlockReader reader2(nullptr, table_options_, true, std::move(block2));
  ASSERT_TRUE(reader2.KeyMayMatch("a2"));
  ASSERT_TRUE(reader2.KeyMayMatch("b2"));
  ASSERT_TRUE(reader2.KeyMayMatch("c2"));
  ASSERT_TRUE(reader2.KeyMayMatch("foo"));
  ASSERT_TRUE(reader2.KeyMayMatch("bar"));
  ASSERT_TRUE(!reader2.KeyMayMatch("a1"));
  ASSERT_TRUE(!reader2.KeyMayMatch("b1"));
  ASSERT_TRUE(!reader2.KeyMayMatch("c1"));
  ASSERT_TRUE(!reader2.KeyMayMatch("missing"));
  ASSERT_TRUE(!reader2.KeyMayMatch("other"));

  // Check third block
  FixedSizeFilterBlockReader reader3(nullptr, table_options_, true, std::move(block3));
  ASSERT_TRUE(!reader3.KeyMayMatch("foo"));
  ASSERT_TRUE(!reader3.KeyMayMatch("bar"));
  ASSERT_TRUE(!reader3.KeyMayMatch("a1"));
  ASSERT_TRUE(!reader3.KeyMayMatch("b1"));
  ASSERT_TRUE(!reader3.KeyMayMatch("c1"));
  ASSERT_TRUE(!reader3.KeyMayMatch("a2"));
  ASSERT_TRUE(!reader3.KeyMayMatch("b2"));
  ASSERT_TRUE(!reader3.KeyMayMatch("c2"));
  ASSERT_TRUE(!reader3.KeyMayMatch("missing"));
  ASSERT_TRUE(!reader3.KeyMayMatch("other"));
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
