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

#include <gtest/gtest.h>

#include "kudu/cfile/cfile_writer.h"
#include "kudu/cfile/index_btree.h"
#include "kudu/gutil/endian.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/status.h"
#include "kudu/util/test_macros.h"

namespace kudu { namespace cfile {

Status SearchInReaderString(const IndexBlockReader &reader,
                            string search_key,
                            BlockPointer *ptr, Slice *match) {

  static faststring dst;

  gscoped_ptr<IndexBlockIterator> iter(reader.NewIterator());
  dst.clear();
  KeyEncoderTraits<BINARY, faststring>::Encode(search_key, &dst);
  Status s = iter->SeekAtOrBefore(Slice(dst));
  RETURN_NOT_OK(s);

  *ptr = iter->GetCurrentBlockPointer();
  *match = iter->GetCurrentKey();
  return Status::OK();
}


Status SearchInReaderUint32(const IndexBlockReader &reader,
                            uint32_t search_key,
                            BlockPointer *ptr, Slice *match) {

  static faststring dst;

  gscoped_ptr<IndexBlockIterator> iter(reader.NewIterator());
  dst.clear();
  KeyEncoderTraits<UINT32, faststring>::Encode(search_key, &dst);
  Status s = iter->SeekAtOrBefore(Slice(dst));
  RETURN_NOT_OK(s);

  *ptr = iter->GetCurrentBlockPointer();
  *match = iter->GetCurrentKey();
  return Status::OK();
}

// Expects a Slice containing a big endian encoded int
static uint32_t SliceAsUInt32(const Slice &slice) {
  CHECK_EQ(slice.size(), 4);
  uint32_t val;
  memcpy(&val, slice.data(), slice.size());
  val = BigEndian::FromHost32(val);
  return val;
}

static void AddToIndex(IndexBlockBuilder *idx, uint32_t val,
                       const BlockPointer &block_pointer) {

  static faststring dst;
  dst.clear();
  KeyEncoderTraits<UINT32, faststring>::Encode(val, &dst);
  idx->Add(Slice(dst), block_pointer);
}


// Test IndexBlockBuilder and IndexReader with integers
TEST(TestIndexBuilder, TestIndexWithInts) {

  // Encode an index block.
  WriterOptions opts;
  IndexBlockBuilder idx(&opts, true);

  const int EXPECTED_NUM_ENTRIES = 4;

  uint32_t i;

  i = 10;
  AddToIndex(&idx, i, BlockPointer(90010, 64 * 1024));

  i = 20;
  AddToIndex(&idx, i, BlockPointer(90020, 64 * 1024));

  i = 30;
  AddToIndex(&idx, i, BlockPointer(90030, 64 * 1024));

  i = 40;
  AddToIndex(&idx, i, BlockPointer(90040, 64 * 1024));

  size_t est_size = idx.EstimateEncodedSize();
  Slice s = idx.Finish();

  // Estimated size should be between 75-100%
  // of actual size.
  EXPECT_LT(s.size(), est_size);
  EXPECT_GT(s.size(), est_size * 3 /4);

  // Open the encoded block in a reader.
  IndexBlockReader reader;
  ASSERT_OK(reader.Parse(s));

  // Should have all the entries we inserted.
  ASSERT_EQ(EXPECTED_NUM_ENTRIES, static_cast<int>(reader.Count()));

  // Search for a value prior to first entry
  BlockPointer ptr;
  Slice match;
  Status status = SearchInReaderUint32(reader, 0, &ptr, &match);
  EXPECT_TRUE(status.IsNotFound());

  // Search for a value equal to first entry
  status = SearchInReaderUint32(reader, 10, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90010, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(10, SliceAsUInt32(match));

  // Search for a value between 1st and 2nd entries.
  // Should return 1st.
  status = SearchInReaderUint32(reader, 15, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90010, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(10, SliceAsUInt32(match));

  // Search for a value equal to 2nd
  // Should return 2nd.
  status = SearchInReaderUint32(reader, 20, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90020, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(20, SliceAsUInt32(match));

  // Between 2nd and 3rd.
  // Should return 2nd
  status = SearchInReaderUint32(reader, 25, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90020, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(20, SliceAsUInt32(match));

  // Equal 3rd
  status = SearchInReaderUint32(reader, 30, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90030, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(30, SliceAsUInt32(match));

  // Between 3rd and 4th
  status = SearchInReaderUint32(reader, 35, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90030, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(30, SliceAsUInt32(match));

  // Equal 4th (last)
  status = SearchInReaderUint32(reader, 40, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90040, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(40, SliceAsUInt32(match));

  // Greater than 4th (last)
  status = SearchInReaderUint32(reader, 45, &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90040, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ(40, SliceAsUInt32(match));

  idx.Reset();
}

TEST(TestIndexBlock, TestIndexBlockWithStrings) {
  WriterOptions opts;
  IndexBlockBuilder idx(&opts, true);

  // Insert data for "hello-10" through "hello-40" by 10s
  const int EXPECTED_NUM_ENTRIES = 4;
  char data[20];
  for (int i = 1; i <= EXPECTED_NUM_ENTRIES; i++) {
    int len = snprintf(data, sizeof(data), "hello-%d", i * 10);
    Slice s(data, len);

    idx.Add(s, BlockPointer(90000 + i*10, 64 * 1024));
  }
  size_t est_size = idx.EstimateEncodedSize();
  Slice s = idx.Finish();

  // Estimated size should be between 75-100%
  // of actual size.
  EXPECT_LT(s.size(), est_size);
  EXPECT_GT(s.size(), est_size * 3 /4);

  VLOG(1) << kudu::HexDump(s);

  // Open the encoded block in a reader.
  IndexBlockReader reader;
  ASSERT_OK(reader.Parse(s));

  // Should have all the entries we inserted.
  ASSERT_EQ(EXPECTED_NUM_ENTRIES, static_cast<int>(reader.Count()));

  // Search for a value prior to first entry
  BlockPointer ptr;
  Slice match;
  Status status = SearchInReaderString(reader, "hello", &ptr, &match);
  EXPECT_TRUE(status.IsNotFound());

  // Search for a value equal to first entry
  status = SearchInReaderString(reader, "hello-10", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90010, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-10", match);

  // Search for a value between 1st and 2nd entries.
  // Should return 1st.
  status = SearchInReaderString(reader, "hello-15", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90010, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-10", match);

  // Search for a value equal to 2nd
  // Should return 2nd.
  status = SearchInReaderString(reader, "hello-20", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90020, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-20", match);

  // Between 2nd and 3rd.
  // Should return 2nd
  status = SearchInReaderString(reader, "hello-25", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90020, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-20", match);

  // Equal 3rd
  status = SearchInReaderString(reader, "hello-30", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90030, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-30", match);

  // Between 3rd and 4th
  status = SearchInReaderString(reader, "hello-35", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90030, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-30", match);

  // Equal 4th (last)
  status = SearchInReaderString(reader, "hello-40", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90040, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-40", match);

  // Greater than 4th (last)
  status = SearchInReaderString(reader, "hello-45", &ptr, &match);
  ASSERT_OK(status);
  EXPECT_EQ(90040, static_cast<int>(ptr.offset()));
  EXPECT_EQ(64 * 1024, static_cast<int>(ptr.size()));
  EXPECT_EQ("hello-40", match);
}

// Test seeking around using the IndexBlockIterator class
TEST(TestIndexBlock, TestIterator) {
  // Encode an index block with 1000 entries.
  WriterOptions opts;
  IndexBlockBuilder idx(&opts, true);

  for (int i = 0; i < 1000; i++) {
    uint32_t key = i * 10;
    AddToIndex(&idx, key, BlockPointer(100000 + i, 64 * 1024));
  }

  Slice s = idx.Finish();

  IndexBlockReader reader;
  ASSERT_OK(reader.Parse(s));
  gscoped_ptr<IndexBlockIterator> iter(reader.NewIterator());
  ASSERT_OK(iter->SeekToIndex(0));
  ASSERT_EQ(0U, SliceAsUInt32(iter->GetCurrentKey()));
  ASSERT_EQ(100000U, iter->GetCurrentBlockPointer().offset());

  ASSERT_OK(iter->SeekToIndex(50));
  ASSERT_EQ(500U, SliceAsUInt32(iter->GetCurrentKey()));
  ASSERT_EQ(100050U, iter->GetCurrentBlockPointer().offset());

  ASSERT_TRUE(iter->HasNext());
  ASSERT_OK(iter->Next());
  ASSERT_EQ(510U, SliceAsUInt32(iter->GetCurrentKey()));
  ASSERT_EQ(100051U, iter->GetCurrentBlockPointer().offset());

  ASSERT_OK(iter->SeekToIndex(999));
  ASSERT_EQ(9990U, SliceAsUInt32(iter->GetCurrentKey()));
  ASSERT_EQ(100999U, iter->GetCurrentBlockPointer().offset());
  ASSERT_FALSE(iter->HasNext());
  ASSERT_TRUE(iter->Next().IsNotFound());

  ASSERT_OK(iter->SeekToIndex(0));
  ASSERT_EQ(0U, SliceAsUInt32(iter->GetCurrentKey()));
  ASSERT_EQ(100000U, iter->GetCurrentBlockPointer().offset());
  ASSERT_TRUE(iter->HasNext());
}

} // namespace cfile
} // namespace kudu
