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
#include <glog/logging.h>
#include <stdlib.h>
#include <list>

#include "kudu/cfile/cfile-test-base.h"
#include "kudu/cfile/cfile_reader.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/cfile/cfile.pb.h"
#include "kudu/cfile/index_block.h"
#include "kudu/cfile/index_btree.h"
#include "kudu/common/columnblock.h"
#include "kudu/fs/fs-test-util.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/util/metrics.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/stopwatch.h"

DECLARE_string(block_cache_type);
DECLARE_string(cfile_do_on_finish);

#if defined(__linux__)
DECLARE_string(nvm_cache_path);
DECLARE_bool(nvm_cache_simulate_allocation_failure);
#endif

METRIC_DECLARE_counter(block_cache_hits_caching);

METRIC_DECLARE_entity(server);

using std::shared_ptr;

namespace kudu {
namespace cfile {

using fs::CountingReadableBlock;
using fs::ReadableBlock;
using fs::WritableBlock;

class TestCFile : public CFileTestBase {
 protected:
  template <class DataGeneratorType>
  void TestReadWriteFixedSizeTypes(EncodingType encoding) {
    BlockId block_id;
    DataGeneratorType generator;

    WriteTestFile(&generator, encoding, NO_COMPRESSION, 10000, SMALL_BLOCKSIZE, &block_id);

    gscoped_ptr<ReadableBlock> block;
    ASSERT_OK(fs_manager_->OpenBlock(block_id, &block));
    gscoped_ptr<CFileReader> reader;
    ASSERT_OK(CFileReader::Open(block.Pass(), ReaderOptions(), &reader));

    BlockPointer ptr;

    gscoped_ptr<CFileIterator> iter;
    ASSERT_OK(reader->NewIterator(&iter, CFileReader::CACHE_BLOCK));

    ASSERT_OK(iter->SeekToOrdinal(5000));
    ASSERT_EQ(5000u, iter->GetCurrentOrdinal());

    // Seek to last key exactly, should succeed
    ASSERT_OK(iter->SeekToOrdinal(9999));
    ASSERT_EQ(9999u, iter->GetCurrentOrdinal());

    // Seek to after last key. Should result in not found.
    ASSERT_TRUE(iter->SeekToOrdinal(10000).IsNotFound());

    // Seek to start of file
    ASSERT_OK(iter->SeekToOrdinal(0));
    ASSERT_EQ(0u, iter->GetCurrentOrdinal());

    // Fetch all data.
    ScopedColumnBlock<DataGeneratorType::kDataType> out(10000);
    size_t n = 10000;
    ASSERT_OK(iter->CopyNextValues(&n, &out));
    ASSERT_EQ(10000, n);

    DataGeneratorType data_generator_pre;

    for (int i = 0; i < 10000; i++) {
      if (out[i] != data_generator_pre.BuildTestValue(0,i)) {
        FAIL() << "mismatch at index " << i
               << " expected: " << data_generator_pre.BuildTestValue(0,i)
               << " got: " << out[i];
      }
      out[i] = 0;
    }

    // Fetch all data using small batches of only a few rows.
    // This should catch edge conditions like a batch lining up exactly
    // with the end of a block.
    unsigned int seed = time(nullptr);
    LOG(INFO) << "Using random seed: " << seed;
    srand(seed);
    ASSERT_OK(iter->SeekToOrdinal(0));
    size_t fetched = 0;
    while (fetched < 10000) {
      ColumnBlock advancing_block(out.type_info(), nullptr,
                                  out.data() + (fetched * out.stride()),
                                  out.nrows() - fetched, out.arena());
      ASSERT_TRUE(iter->HasNext());
      size_t batch_size = random() % 5 + 1;
      size_t n = batch_size;
      ASSERT_OK(iter->CopyNextValues(&n, &advancing_block));
      ASSERT_LE(n, batch_size);
      fetched += n;
    }
    ASSERT_FALSE(iter->HasNext());

    DataGeneratorType data_generator_post;

    // Re-verify
    for (int i = 0; i < 10000; i++) {
      if (out[i] != data_generator_post.BuildTestValue(0,i)) {
        FAIL() << "mismatch at index " << i
               << " expected: " << data_generator_post.BuildTestValue(0,i)
               << " got: " << out[i];
      }
      out[i] = 0;
    }

    TimeReadFile(fs_manager_.get(), block_id, &n);
    ASSERT_EQ(10000, n);
  }

  template <class DataGeneratorType>
  void TimeSeekAndReadFileWithNulls(DataGeneratorType* generator,
                                    const BlockId& block_id, size_t num_entries) {
    gscoped_ptr<ReadableBlock> block;
    ASSERT_OK(fs_manager_->OpenBlock(block_id, &block));
    gscoped_ptr<CFileReader> reader;
    ASSERT_OK(CFileReader::Open(block.Pass(), ReaderOptions(), &reader));
    ASSERT_EQ(DataGeneratorType::kDataType, reader->type_info()->type());

    gscoped_ptr<CFileIterator> iter;
    ASSERT_OK(reader->NewIterator(&iter, CFileReader::CACHE_BLOCK));

    Arena arena(8192, 8*1024*1024);
    ScopedColumnBlock<DataGeneratorType::kDataType> cb(10);

    const int kNumLoops = AllowSlowTests() ? num_entries : 10;
    for (int loop = 0; loop < kNumLoops; loop++) {
      // Seek to a random point in the file,
      // or just try each entry as starting point if you're running SlowTests
      int target = AllowSlowTests() ? loop : (random() % (num_entries - 1));
      SCOPED_TRACE(target);
      ASSERT_OK(iter->SeekToOrdinal(target));
      ASSERT_TRUE(iter->HasNext());

      // Read and verify several ColumnBlocks from this point in the file.
      int read_offset = target;
      for (int block = 0; block < 3 && iter->HasNext(); block++) {
        SCOPED_TRACE(block);
        size_t n = cb.nrows();
        ASSERT_OK_FAST(iter->CopyNextValues(&n, &cb));
        ASSERT_EQ(n, std::min(num_entries - read_offset, cb.nrows()));

        // Verify that the block data is correct.
        generator->Build(read_offset, n);
        for (size_t j = 0; j < n; ++j) {
          SCOPED_TRACE(j);
          bool expected_null = generator->TestValueShouldBeNull(read_offset + j);
          ASSERT_EQ(expected_null, cb.is_null(j));
          if (!expected_null) {
            ASSERT_EQ((*generator)[j], cb[j]);
          }
        }
        cb.arena()->Reset();
        read_offset += n;
      }
    }
  }

  template <class DataGeneratorType>
  void TestNullTypes(DataGeneratorType* generator, EncodingType encoding,
                     CompressionType compression) {
    BlockId block_id;
    WriteTestFile(generator, encoding, compression, 10000, SMALL_BLOCKSIZE, &block_id);

    size_t n;
    TimeReadFile(fs_manager_.get(), block_id, &n);
    ASSERT_EQ(n, 10000);

    generator->Reset();
    TimeSeekAndReadFileWithNulls(generator, block_id, n);
  }


  void TestReadWriteRawBlocks(CompressionType compression, int num_entries) {
    // Test Write
    gscoped_ptr<WritableBlock> sink;
    ASSERT_OK(fs_manager_->CreateNewBlock(&sink));
    BlockId id = sink->id();
    WriterOptions opts;
    opts.write_posidx = true;
    opts.write_validx = false;
    opts.storage_attributes.cfile_block_size = FLAGS_cfile_test_block_size;
    opts.storage_attributes.encoding = PLAIN_ENCODING;
    CFileWriter w(opts, GetTypeInfo(STRING), false, sink.Pass());
    ASSERT_OK(w.Start());
    for (uint32_t i = 0; i < num_entries; i++) {
      vector<Slice> slices;
      slices.push_back(Slice("Head"));
      slices.push_back(Slice("Body"));
      slices.push_back(Slice("Tail"));
      slices.push_back(Slice(reinterpret_cast<uint8_t *>(&i), 4));
      ASSERT_OK(w.AppendRawBlock(slices, i, nullptr, "raw-data"));
    }
    ASSERT_OK(w.Finish());

    // Test Read
    gscoped_ptr<ReadableBlock> source;
    ASSERT_OK(fs_manager_->OpenBlock(id, &source));
    gscoped_ptr<CFileReader> reader;
    ASSERT_OK(CFileReader::Open(source.Pass(), ReaderOptions(), &reader));

    gscoped_ptr<IndexTreeIterator> iter;
    iter.reset(IndexTreeIterator::Create(reader.get(), reader->posidx_root()));
    ASSERT_OK(iter->SeekToFirst());

    uint8_t data[16];
    Slice expected_data(data, 16);
    memcpy(data, "HeadBodyTail", 12);

    uint32_t count = 0;
    do {
      BlockHandle dblk_data;
      BlockPointer blk_ptr = iter->GetCurrentBlockPointer();
      ASSERT_OK(reader->ReadBlock(blk_ptr, CFileReader::CACHE_BLOCK, &dblk_data));

      memcpy(data + 12, &count, 4);
      ASSERT_EQ(expected_data, dblk_data.data());

      count++;
    } while (iter->Next().ok());
    ASSERT_EQ(num_entries, count);
  }

  void TestReadWriteStrings(EncodingType encoding);

#ifdef NDEBUG
  void TestWrite100MFileStrings(EncodingType encoding) {
    BlockId block_id;
    LOG_TIMING(INFO, "writing 100M strings") {
      LOG(INFO) << "Starting writefile";
      StringDataGenerator<false> generator("hello %zu");
      WriteTestFile(&generator, encoding, NO_COMPRESSION, 100000000, NO_FLAGS, &block_id);
      LOG(INFO) << "Done writing";
    }

    LOG_TIMING(INFO, "reading 100M strings") {
      LOG(INFO) << "Starting readfile";
      size_t n;
      TimeReadFile(fs_manager_.get(), block_id, &n);
      ASSERT_EQ(100000000, n);
      LOG(INFO) << "End readfile";
    }
  }
#endif

};

// Subclass of TestCFile which is parameterized on the block cache type.
// Tests that use TEST_P(TestCFileBothCacheTypes, ...) will run twice --
// once for each cache type (DRAM, NVM).
class TestCFileBothCacheTypes : public TestCFile,
                                public ::testing::WithParamInterface<CacheType> {
 public:
  void SetUp() OVERRIDE {
#if defined(__linux__)
    // The NVM cache can run using any directory as its path -- it doesn't have
    // a lot of practical use outside of an actual NVM device, but for testing
    // purposes, we'll point it at our test dir, unless otherwise specified.
    if (google::GetCommandLineFlagInfoOrDie("nvm_cache_path").is_default) {
      FLAGS_nvm_cache_path = GetTestPath("nvm-cache");
      ASSERT_OK(Env::Default()->CreateDir(FLAGS_nvm_cache_path));
    }
#endif
    switch (GetParam()) {
      case DRAM_CACHE:
        FLAGS_block_cache_type = "DRAM";
        break;
#if defined(__linux__)
      case NVM_CACHE:
        FLAGS_block_cache_type = "NVM";
        break;
#endif
      default:
        LOG(FATAL) << "Unknown block cache type: '" << GetParam();
    }
    CFileTestBase::SetUp();
  }

  void TearDown() OVERRIDE {
    Singleton<BlockCache>::UnsafeReset();
  }
};

#if defined(__linux__)
INSTANTIATE_TEST_CASE_P(CacheTypes, TestCFileBothCacheTypes,
                        ::testing::Values(DRAM_CACHE, NVM_CACHE));
#else
INSTANTIATE_TEST_CASE_P(CacheTypes, TestCFileBothCacheTypes, ::testing::Values(DRAM_CACHE));
#endif

template<DataType type>
void CopyOne(CFileIterator *it,
             typename TypeTraits<type>::cpp_type *ret,
             Arena *arena) {
  ColumnBlock cb(GetTypeInfo(type), nullptr, ret, 1, arena);
  size_t n = 1;
  ASSERT_OK(it->CopyNextValues(&n, &cb));
  ASSERT_EQ(1, n);
}

#ifdef NDEBUG
// Only run the 100M entry tests in non-debug mode.
// They take way too long with debugging enabled.

TEST_P(TestCFileBothCacheTypes, TestWrite100MFileInts) {
  BlockId block_id;
  LOG_TIMING(INFO, "writing 100m ints") {
    LOG(INFO) << "Starting writefile";
    UInt32DataGenerator<false> generator;
    WriteTestFile(&generator, GROUP_VARINT, NO_COMPRESSION, 100000000, NO_FLAGS, &block_id);
    LOG(INFO) << "Done writing";
  }

  LOG_TIMING(INFO, "reading 100M ints") {
    LOG(INFO) << "Starting readfile";
    size_t n;
    TimeReadFile(fs_manager_.get(), block_id, &n);
    ASSERT_EQ(100000000, n);
    LOG(INFO) << "End readfile";
  }
}

TEST_P(TestCFileBothCacheTypes, TestWrite100MFileNullableInts) {
  BlockId block_id;
  LOG_TIMING(INFO, "writing 100m nullable ints") {
    LOG(INFO) << "Starting writefile";
    UInt32DataGenerator<true> generator;
    WriteTestFile(&generator, PLAIN_ENCODING, NO_COMPRESSION, 100000000, NO_FLAGS, &block_id);
    LOG(INFO) << "Done writing";
  }

  LOG_TIMING(INFO, "reading 100M nullable ints") {
    LOG(INFO) << "Starting readfile";
    size_t n;
    TimeReadFile(fs_manager_.get(), block_id, &n);
    ASSERT_EQ(100000000, n);
    LOG(INFO) << "End readfile";
  }
}

TEST_P(TestCFileBothCacheTypes, TestWrite100MFileStringsPrefixEncoding) {
  TestWrite100MFileStrings(PREFIX_ENCODING);
}

TEST_P(TestCFileBothCacheTypes, TestWrite100MFileStringsDictEncoding) {
  TestWrite100MFileStrings(DICT_ENCODING);
}

TEST_P(TestCFileBothCacheTypes, TestWrite100MFileStringsPlainEncoding) {
  TestWrite100MFileStrings(PLAIN_ENCODING);
}

#endif

// Write and Read 1 million unique strings with dictionary encoding
TEST_P(TestCFileBothCacheTypes, TestWrite1MUniqueFileStringsDictEncoding) {
  BlockId block_id;
  LOG_TIMING(INFO, "writing 1M unique strings") {
    LOG(INFO) << "Starting writefile";
    StringDataGenerator<false> generator("hello %zu");
    WriteTestFile(&generator, DICT_ENCODING, NO_COMPRESSION, 1000000, NO_FLAGS, &block_id);
    LOG(INFO) << "Done writing";
  }

  LOG_TIMING(INFO, "reading 1M strings") {
    LOG(INFO) << "Starting readfile";
    size_t n;
    TimeReadFile(fs_manager_.get(), block_id, &n);
    ASSERT_EQ(1000000, n);
    LOG(INFO) << "End readfile";
  }
}

// Write and Read 1 million strings, which contains duplicates with dictionary encoding
TEST_P(TestCFileBothCacheTypes, TestWrite1MDuplicateFileStringsDictEncoding) {
  BlockId block_id;
  LOG_TIMING(INFO, "writing 1M duplicate strings") {
    LOG(INFO) << "Starting writefile";

    // The second parameter specify how many distinct strings are there
    DuplicateStringDataGenerator<false> generator("hello %zu", 256);
    WriteTestFile(&generator, DICT_ENCODING, NO_COMPRESSION, 1000000, NO_FLAGS, &block_id);
    LOG(INFO) << "Done writing";
  }

  LOG_TIMING(INFO, "reading 1M strings") {
    LOG(INFO) << "Starting readfile";
    size_t n;
    TimeReadFile(fs_manager_.get(), block_id, &n);
    ASSERT_EQ(1000000, n);
    LOG(INFO) << "End readfile";
  }
}

TEST_P(TestCFileBothCacheTypes, TestFixedSizeReadWritePlainEncodingUInt32) {
  TestReadWriteFixedSizeTypes<UInt32DataGenerator<false> >(GROUP_VARINT);
  TestReadWriteFixedSizeTypes<UInt32DataGenerator<false> >(PLAIN_ENCODING);
}

TEST_P(TestCFileBothCacheTypes, TestFixedSizeReadWritePlainEncodingInt32) {
  TestReadWriteFixedSizeTypes<Int32DataGenerator<false> >(PLAIN_ENCODING);
}

TEST_P(TestCFileBothCacheTypes, TestFixedSizeReadWritePlainEncodingFloat) {
  TestReadWriteFixedSizeTypes<FPDataGenerator<FLOAT, false> >(PLAIN_ENCODING);
}
TEST_P(TestCFileBothCacheTypes, TestFixedSizeReadWritePlainEncodingDouble) {
  TestReadWriteFixedSizeTypes<FPDataGenerator<DOUBLE, false> >(PLAIN_ENCODING);
}

// Test for BitShuffle builder for UINT8, INT8, UINT16, INT16, UINT32, INT32, FLOAT, DOUBLE
template <typename T>
class BitShuffleTest : public TestCFile {
  public:
    void TestBitShuffle() {
      TestReadWriteFixedSizeTypes<T>(BIT_SHUFFLE);
    }
};
typedef ::testing::Types<UInt8DataGenerator<false>,
                         Int8DataGenerator<false>,
                         UInt16DataGenerator<false>,
                         Int16DataGenerator<false>,
                         UInt32DataGenerator<false>,
                         Int32DataGenerator<false>,
                         FPDataGenerator<FLOAT, false>,
                         FPDataGenerator<DOUBLE, false> > MyTypes;
TYPED_TEST_CASE(BitShuffleTest, MyTypes);
TYPED_TEST(BitShuffleTest, TestFixedSizeReadWriteBitShuffle) {
  this->TestBitShuffle();
}

void EncodeStringKey(const Schema &schema, const Slice& key,
                     gscoped_ptr<EncodedKey> *encoded_key) {
  EncodedKeyBuilder kb(&schema);
  kb.AddColumnKey(&key);
  encoded_key->reset(kb.BuildEncodedKey());
}

void TestCFile::TestReadWriteStrings(EncodingType encoding) {
  Schema schema({ ColumnSchema("key", STRING) }, 1);

  const int nrows = 10000;
  BlockId block_id;
  StringDataGenerator<false> generator("hello %04d");
  WriteTestFile(&generator, encoding, NO_COMPRESSION, nrows,
                SMALL_BLOCKSIZE | WRITE_VALIDX, &block_id);

  gscoped_ptr<ReadableBlock> block;
  ASSERT_OK(fs_manager_->OpenBlock(block_id, &block));
  gscoped_ptr<CFileReader> reader;
  ASSERT_OK(CFileReader::Open(block.Pass(), ReaderOptions(), &reader));

  rowid_t reader_nrows;
  ASSERT_OK(reader->CountRows(&reader_nrows));
  ASSERT_EQ(nrows, reader_nrows);

  BlockPointer ptr;

  gscoped_ptr<CFileIterator> iter;
  ASSERT_OK(reader->NewIterator(&iter, CFileReader::CACHE_BLOCK));

  Arena arena(1024, 1024*1024);

  ASSERT_OK(iter->SeekToOrdinal(5000));
  ASSERT_EQ(5000u, iter->GetCurrentOrdinal());
  Slice s;

  CopyOne<STRING>(iter.get(), &s, &arena);
  ASSERT_EQ(string("hello 5000"), s.ToString());

  // Seek to last key exactly, should succeed
  ASSERT_OK(iter->SeekToOrdinal(9999));
  ASSERT_EQ(9999u, iter->GetCurrentOrdinal());

  // Seek to after last key. Should result in not found.
  ASSERT_TRUE(iter->SeekToOrdinal(10000).IsNotFound());


  ////////
  // Now try some seeks by the value instead of position
  /////////

  gscoped_ptr<EncodedKey> encoded_key;
  bool exact;

  // Seek in between each key
  for (int i = 1; i < 10000; i++) {
    SCOPED_TRACE(i);
    char buf[100];
    snprintf(buf, sizeof(buf), "hello %04d.5", i - 1);
    s = Slice(buf);
    EncodeStringKey(schema, s, &encoded_key);
    ASSERT_OK(iter->SeekAtOrAfter(*encoded_key, &exact));
    ASSERT_FALSE(exact);
    ASSERT_EQ(i, iter->GetCurrentOrdinal());
    CopyOne<STRING>(iter.get(), &s, &arena);
    ASSERT_EQ(StringPrintf("hello %04d", i), s.ToString());
  }

  // Seek exactly to each key
  for (int i = 0; i < 9999; i++) {
    SCOPED_TRACE(i);
    char buf[100];
    snprintf(buf, sizeof(buf), "hello %04d", i);
    s = Slice(buf);
    EncodeStringKey(schema, s, &encoded_key);
    ASSERT_OK(iter->SeekAtOrAfter(*encoded_key, &exact));
    ASSERT_TRUE(exact);
    ASSERT_EQ(i, iter->GetCurrentOrdinal());
    Slice read_back;
    CopyOne<STRING>(iter.get(), &read_back, &arena);
    ASSERT_EQ(read_back.ToString(), s.ToString());
  }

  // after last entry
  s = "hello 9999x";
  EncodeStringKey(schema, s, &encoded_key);
  EXPECT_TRUE(iter->SeekAtOrAfter(*encoded_key, &exact).IsNotFound());

  // before first entry
  s = "hello";
  EncodeStringKey(schema, s, &encoded_key);
  ASSERT_OK(iter->SeekAtOrAfter(*encoded_key, &exact));
  ASSERT_FALSE(exact);
  ASSERT_EQ(0u, iter->GetCurrentOrdinal());
  CopyOne<STRING>(iter.get(), &s, &arena);
  ASSERT_EQ(string("hello 0000"), s.ToString());

  // Seek to start of file by ordinal
  ASSERT_OK(iter->SeekToFirst());
  ASSERT_EQ(0u, iter->GetCurrentOrdinal());
  CopyOne<STRING>(iter.get(), &s, &arena);
  ASSERT_EQ(string("hello 0000"), s.ToString());

  // Reseek to start and fetch all data.
  ASSERT_OK(iter->SeekToFirst());

  ScopedColumnBlock<STRING> cb(10000);
  size_t n = 10000;
  ASSERT_OK(iter->CopyNextValues(&n, &cb));
  ASSERT_EQ(10000, n);
}


TEST_P(TestCFileBothCacheTypes, TestReadWriteStringsPrefixEncoding) {
  TestReadWriteStrings(PREFIX_ENCODING);
}

// Read/Write test for dictionary encoded blocks
TEST_P(TestCFileBothCacheTypes, TestReadWriteStringsDictEncoding) {
  TestReadWriteStrings(DICT_ENCODING);
}

// Test that metadata entries stored in the cfile are persisted.
TEST_P(TestCFileBothCacheTypes, TestMetadata) {
  BlockId block_id;

  // Write the file.
  {
    gscoped_ptr<WritableBlock> sink;
    ASSERT_OK(fs_manager_->CreateNewBlock(&sink));
    block_id = sink->id();
    WriterOptions opts;
    CFileWriter w(opts, GetTypeInfo(INT32), false, sink.Pass());

    w.AddMetadataPair("key_in_header", "header value");
    ASSERT_OK(w.Start());

    uint32_t val = 1;
    ASSERT_OK(w.AppendEntries(&val, 1));

    w.AddMetadataPair("key_in_footer", "footer value");
    ASSERT_OK(w.Finish());
  }

  // Read the file and ensure metadata is present.
  {
    gscoped_ptr<ReadableBlock> source;
    ASSERT_OK(fs_manager_->OpenBlock(block_id, &source));
    gscoped_ptr<CFileReader> reader;
    ASSERT_OK(CFileReader::Open(source.Pass(), ReaderOptions(), &reader));
    string val;
    ASSERT_TRUE(reader->GetMetadataEntry("key_in_header", &val));
    ASSERT_EQ(val, "header value");
    ASSERT_TRUE(reader->GetMetadataEntry("key_in_footer", &val));
    ASSERT_EQ(val, "footer value");
    ASSERT_FALSE(reader->GetMetadataEntry("not a key", &val));

    // Test that, even though we didn't specify an encoding or compression, the
    // resulting file has them explicitly set.
    ASSERT_EQ(PLAIN_ENCODING, reader->type_encoding_info()->encoding_type());
    ASSERT_EQ(NO_COMPRESSION, reader->footer().compression());
  }
}

TEST_P(TestCFileBothCacheTypes, TestDefaultColumnIter) {
  const int kNumItems = 64;
  uint8_t null_bitmap[BitmapSize(kNumItems)];
  uint32_t data[kNumItems];

  // Test Int Default Value
  uint32_t int_value = 15;
  DefaultColumnValueIterator iter(GetTypeInfo(UINT32), &int_value);
  ColumnBlock int_col(GetTypeInfo(UINT32), nullptr, data, kNumItems, nullptr);
  ASSERT_OK(iter.Scan(&int_col));
  for (size_t i = 0; i < int_col.nrows(); ++i) {
    ASSERT_EQ(int_value, *reinterpret_cast<const uint32_t *>(int_col.cell_ptr(i)));
  }

  // Test Int Nullable Default Value
  int_value = 321;
  DefaultColumnValueIterator nullable_iter(GetTypeInfo(UINT32), &int_value);
  ColumnBlock nullable_col(GetTypeInfo(UINT32), null_bitmap, data, kNumItems, nullptr);
  ASSERT_OK(nullable_iter.Scan(&nullable_col));
  for (size_t i = 0; i < nullable_col.nrows(); ++i) {
    ASSERT_FALSE(nullable_col.is_null(i));
    ASSERT_EQ(int_value, *reinterpret_cast<const uint32_t *>(nullable_col.cell_ptr(i)));
  }

  // Test NULL Default Value
  DefaultColumnValueIterator null_iter(GetTypeInfo(UINT32),  nullptr);
  ColumnBlock null_col(GetTypeInfo(UINT32), null_bitmap, data, kNumItems, nullptr);
  ASSERT_OK(null_iter.Scan(&null_col));
  for (size_t i = 0; i < null_col.nrows(); ++i) {
    ASSERT_TRUE(null_col.is_null(i));
  }

  // Test String Default Value
  Slice str_data[kNumItems];
  Slice str_value("Hello");
  Arena arena(32*1024, 256*1024);
  DefaultColumnValueIterator str_iter(GetTypeInfo(STRING), &str_value);
  ColumnBlock str_col(GetTypeInfo(STRING), nullptr, str_data, kNumItems, &arena);
  ASSERT_OK(str_iter.Scan(&str_col));
  for (size_t i = 0; i < str_col.nrows(); ++i) {
    ASSERT_EQ(str_value, *reinterpret_cast<const Slice *>(str_col.cell_ptr(i)));
  }
}

TEST_P(TestCFileBothCacheTypes, TestAppendRaw) {
  TestReadWriteRawBlocks(NO_COMPRESSION, 1000);
  TestReadWriteRawBlocks(SNAPPY, 1000);
  TestReadWriteRawBlocks(LZ4, 1000);
  TestReadWriteRawBlocks(ZLIB, 1000);
}

TEST_P(TestCFileBothCacheTypes, TestNullInts) {
  UInt32DataGenerator<true> generator;
  TestNullTypes(&generator, GROUP_VARINT, NO_COMPRESSION);
  TestNullTypes(&generator, GROUP_VARINT, LZ4);
}

TEST_P(TestCFileBothCacheTypes, TestNullFloats) {
  FPDataGenerator<FLOAT, true> generator;
  TestNullTypes(&generator, PLAIN_ENCODING, NO_COMPRESSION);
}

TEST_P(TestCFileBothCacheTypes, TestNullPrefixStrings) {
  StringDataGenerator<true> generator("hello %zu");
  TestNullTypes(&generator, PLAIN_ENCODING, NO_COMPRESSION);
  TestNullTypes(&generator, PLAIN_ENCODING, LZ4);
}

TEST_P(TestCFileBothCacheTypes, TestNullPlainStrings) {
  StringDataGenerator<true> generator("hello %zu");
  TestNullTypes(&generator, PREFIX_ENCODING, NO_COMPRESSION);
  TestNullTypes(&generator, PREFIX_ENCODING, LZ4);
}

// Test for dictionary encoding
TEST_P(TestCFileBothCacheTypes, TestNullDictStrings) {
  StringDataGenerator<true> generator("hello %zu");
  TestNullTypes(&generator, DICT_ENCODING, NO_COMPRESSION);
  TestNullTypes(&generator, DICT_ENCODING, LZ4);
}

TEST_P(TestCFileBothCacheTypes, TestReleaseBlock) {
  gscoped_ptr<WritableBlock> sink;
  ASSERT_OK(fs_manager_->CreateNewBlock(&sink));
  ASSERT_EQ(WritableBlock::CLEAN, sink->state());
  WriterOptions opts;
  CFileWriter w(opts, GetTypeInfo(STRING), false, sink.Pass());
  ASSERT_OK(w.Start());
  fs::ScopedWritableBlockCloser closer;
  ASSERT_OK(w.FinishAndReleaseBlock(&closer));
  if (FLAGS_cfile_do_on_finish == "flush") {
    ASSERT_EQ(1, closer.blocks().size());
    ASSERT_EQ(WritableBlock::FLUSHING, closer.blocks()[0]->state());
  } else if (FLAGS_cfile_do_on_finish == "close") {
    ASSERT_EQ(0, closer.blocks().size());
  } else if (FLAGS_cfile_do_on_finish == "nothing") {
    ASSERT_EQ(1, closer.blocks().size());
    ASSERT_EQ(WritableBlock::DIRTY, closer.blocks()[0]->state());
  } else {
    LOG(FATAL) << "Unknown value for cfile_do_on_finish: "
               << FLAGS_cfile_do_on_finish;
  }
  ASSERT_OK(closer.CloseBlocks());
  ASSERT_EQ(0, closer.blocks().size());
}

TEST_P(TestCFileBothCacheTypes, TestLazyInit) {
  // Create a small test file.
  BlockId block_id;
  {
    const int nrows = 1000;
    StringDataGenerator<false> generator("hello %04d");
    WriteTestFile(&generator, PREFIX_ENCODING, NO_COMPRESSION, nrows,
                  SMALL_BLOCKSIZE | WRITE_VALIDX, &block_id);
  }

  shared_ptr<MemTracker> tracker = MemTracker::CreateTracker(-1, "test");
  int64_t initial_mem_usage = tracker->consumption();

  // Open it using a "counting" readable block.
  gscoped_ptr<ReadableBlock> block;
  ASSERT_OK(fs_manager_->OpenBlock(block_id, &block));
  size_t bytes_read = 0;
  gscoped_ptr<ReadableBlock> count_block(
      new CountingReadableBlock(block.Pass(), &bytes_read));
  ASSERT_EQ(initial_mem_usage, tracker->consumption());

  // Lazily opening the cfile should not trigger any reads.
  ReaderOptions opts;
  opts.parent_mem_tracker = tracker;
  gscoped_ptr<CFileReader> reader;
  ASSERT_OK(CFileReader::OpenNoInit(count_block.Pass(), opts, &reader));
  ASSERT_EQ(0, bytes_read);
  int64_t lazy_mem_usage = tracker->consumption();
  ASSERT_GT(lazy_mem_usage, initial_mem_usage);

  // But initializing it should (only the first time), and the reader's
  // memory usage should increase.
  ASSERT_OK(reader->Init());
  ASSERT_GT(bytes_read, 0);
  size_t bytes_read_after_init = bytes_read;
  ASSERT_OK(reader->Init());
  ASSERT_EQ(bytes_read_after_init, bytes_read);
  ASSERT_GT(tracker->consumption(), lazy_mem_usage);

  // And let's test non-lazy open for good measure; it should yield the
  // same number of bytes read.
  ASSERT_OK(fs_manager_->OpenBlock(block_id, &block));
  bytes_read = 0;
  count_block.reset(new CountingReadableBlock(block.Pass(), &bytes_read));
  ASSERT_OK(CFileReader::Open(count_block.Pass(), ReaderOptions(), &reader));
  ASSERT_EQ(bytes_read_after_init, bytes_read);
}

// Tests that the block cache keys used by CFileReaders are stable. That is,
// different reader instances operating on the same block should use the same
// block cache keys.
TEST_P(TestCFileBothCacheTypes, TestCacheKeysAreStable) {
  // Set up block cache instrumentation.
  MetricRegistry registry;
  scoped_refptr<MetricEntity> entity(METRIC_ENTITY_server.Instantiate(&registry, "test_entity"));
  BlockCache* cache = BlockCache::GetSingleton();
  cache->StartInstrumentation(entity);

  // Create a small test file.
  BlockId block_id;
  {
    const int nrows = 1000;
    StringDataGenerator<false> generator("hello %04d");
    WriteTestFile(&generator, PREFIX_ENCODING, NO_COMPRESSION, nrows,
                  SMALL_BLOCKSIZE | WRITE_VALIDX, &block_id);
  }

  // Open and read from it twice, checking the block cache statistics.
  for (int i = 0; i < 2; i++) {
    gscoped_ptr<ReadableBlock> source;
    ASSERT_OK(fs_manager_->OpenBlock(block_id, &source));
    gscoped_ptr<CFileReader> reader;
    ASSERT_OK(CFileReader::Open(source.Pass(), ReaderOptions(), &reader));

    gscoped_ptr<IndexTreeIterator> iter;
    iter.reset(IndexTreeIterator::Create(reader.get(), reader->posidx_root()));
    ASSERT_OK(iter->SeekToFirst());

    BlockHandle bh;
    ASSERT_OK(reader->ReadBlock(iter->GetCurrentBlockPointer(),
                                CFileReader::CACHE_BLOCK,
                                &bh));

    // The first time through, we miss in the seek and in the ReadBlock().
    // But the second time through, both are hits, because we've got the same
    // cache keys as before.
    ASSERT_EQ(i * 2, down_cast<Counter*>(
        entity->FindOrNull(METRIC_block_cache_hits_caching).get())->value());
  }
}

#if defined(__linux__)
// Inject failures in nvm allocation and ensure that we can still read a file.
TEST_P(TestCFileBothCacheTypes, TestNvmAllocationFailure) {
  if (GetParam() != NVM_CACHE) return;
  FLAGS_nvm_cache_simulate_allocation_failure = true;
  TestReadWriteFixedSizeTypes<UInt32DataGenerator<false> >(PLAIN_ENCODING);
}
#endif

} // namespace cfile
} // namespace kudu
