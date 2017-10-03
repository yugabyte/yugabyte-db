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
#include <boost/scoped_array.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>
#include <stdlib.h>

#include "kudu/cfile/cfile-test-base.h"
#include "kudu/cfile/cfile_reader.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/cfile/cfile.pb.h"
#include "kudu/cfile/compression_codec.h"
#include "kudu/cfile/index_block.h"
#include "kudu/cfile/index_btree.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"
#include "kudu/util/status.h"

namespace kudu {
namespace cfile {

static void TestCompressionCodec(CompressionType compression) {
  const int kInputSize = 64;

  const CompressionCodec* codec;
  uint8_t ibuffer[kInputSize];
  uint8_t ubuffer[kInputSize];
  size_t compressed;

  // Fill the test input buffer
  memset(ibuffer, 'Z', kInputSize);

  // Get the specified compression codec
  ASSERT_OK(GetCompressionCodec(compression, &codec));

  // Allocate the compression buffer
  size_t max_compressed = codec->MaxCompressedLength(kInputSize);
  ASSERT_LT(max_compressed, (kInputSize * 2));
  gscoped_array<uint8_t> cbuffer(new uint8_t[max_compressed]);

  // Compress and uncompress
  ASSERT_OK(codec->Compress(Slice(ibuffer, kInputSize), cbuffer.get(), &compressed));
  ASSERT_OK(codec->Uncompress(Slice(cbuffer.get(), compressed), ubuffer, kInputSize));
  ASSERT_EQ(0, memcmp(ibuffer, ubuffer, kInputSize));

  // Compress slices and uncompress
  vector<Slice> v;
  v.push_back(Slice(ibuffer, 1));
  for (int i = 1; i <= kInputSize; i += 7)
    v.push_back(Slice(ibuffer + i, 7));
  ASSERT_OK(codec->Compress(Slice(ibuffer, kInputSize), cbuffer.get(), &compressed));
  ASSERT_OK(codec->Uncompress(Slice(cbuffer.get(), compressed), ubuffer, kInputSize));
  ASSERT_EQ(0, memcmp(ibuffer, ubuffer, kInputSize));
}

class TestCompression : public CFileTestBase {
 protected:
  void TestReadWriteCompressed(CompressionType compression) {
    const size_t nrows = 10000;
    BlockId block_id;
    size_t rdrows;

    {
      StringDataGenerator<false> string_gen("hello %04d");
      WriteTestFile(&string_gen, PREFIX_ENCODING, compression, nrows,
                    WRITE_VALIDX, &block_id);

      TimeReadFile(fs_manager_.get(), block_id, &rdrows);
      ASSERT_EQ(nrows, rdrows);
    }

    {
      UInt32DataGenerator<false> int_gen;
      WriteTestFile(&int_gen, GROUP_VARINT, compression, nrows,
                    NO_FLAGS, &block_id);
      TimeReadFile(fs_manager_.get(), block_id, &rdrows);
      ASSERT_EQ(nrows, rdrows);
    }
  }
};

TEST_F(TestCompression, TestNoCompressionCodec) {
  const CompressionCodec* codec;
  ASSERT_OK(GetCompressionCodec(NO_COMPRESSION, &codec));
  ASSERT_EQ(nullptr, codec);
}

TEST_F(TestCompression, TestSnappyCompressionCodec) {
  TestCompressionCodec(SNAPPY);
}

TEST_F(TestCompression, TestLz4CompressionCodec) {
  TestCompressionCodec(LZ4);
}

TEST_F(TestCompression, TestZlibCompressionCodec) {
  TestCompressionCodec(ZLIB);
}

TEST_F(TestCompression, TestCFileNoCompressionReadWrite) {
  TestReadWriteCompressed(NO_COMPRESSION);
}

TEST_F(TestCompression, TestCFileSnappyReadWrite) {
  TestReadWriteCompressed(SNAPPY);
}

TEST_F(TestCompression, TestCFileLZ4ReadWrite) {
  TestReadWriteCompressed(SNAPPY);
}

TEST_F(TestCompression, TestCFileZlibReadWrite) {
  TestReadWriteCompressed(ZLIB);
}

} // namespace cfile
} // namespace kudu
