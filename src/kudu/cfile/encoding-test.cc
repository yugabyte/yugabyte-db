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

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/utility/binary.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <limits>

#include "kudu/cfile/block_encodings.h"
#include "kudu/cfile/bshuf_block.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/cfile/gvint_block.h"
#include "kudu/cfile/plain_bitmap_block.h"
#include "kudu/cfile/plain_block.h"
#include "kudu/cfile/rle_block.h"
#include "kudu/cfile/binary_plain_block.h"
#include "kudu/cfile/binary_prefix_block.h"
#include "kudu/common/columnblock.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/util/group_varint-inl.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/stopwatch.h"

namespace kudu { namespace cfile {

extern void DumpSSETable();

class TestEncoding : public ::testing::Test {
 public:
  TestEncoding()
    : ::testing::Test(),
      arena_(1024, 1024*1024) {
  }

 protected:
  virtual void SetUp() OVERRIDE {
    arena_.Reset();
  }

  template<DataType type>
  void CopyOne(BlockDecoder *decoder,
               typename TypeTraits<type>::cpp_type *ret) {
    ColumnBlock cb(GetTypeInfo(type), nullptr, ret, 1, &arena_);
    ColumnDataView cdv(&cb);
    size_t n = 1;
    ASSERT_OK(decoder->CopyNextValues(&n, &cdv));
    ASSERT_EQ(1, n);
  }

  // Insert a given number of strings into the provided
  // BinaryPrefixBlockBuilder.
  template<class BuilderType>
  static Slice CreateBinaryBlock(BuilderType *sbb,
                                 int num_items,
                                 const char *fmt_str) {
    boost::ptr_vector<string> to_insert;
    std::vector<Slice> slices;

    for (uint i = 0; i < num_items; i++) {
      string *val = new string(StringPrintf(fmt_str, i));
      to_insert.push_back(val);
      slices.push_back(Slice(*val));
    }


    int rem = slices.size();
    Slice *ptr = &slices[0];
    while (rem > 0) {
      int added = sbb->Add(reinterpret_cast<const uint8_t *>(ptr),
                           rem);
      CHECK(added > 0);
      rem -= added;
      ptr += added;
    }

    CHECK_EQ(slices.size(), sbb->Count());
    return sbb->Finish(12345L);
  }

  WriterOptions* NewWriterOptions() {
    auto ret = new WriterOptions();
    ret->storage_attributes.cfile_block_size = 256 * 1024;
    return ret;
  }

  template<class BuilderType, class DecoderType>
  void TestBinarySeekByValueSmallBlock() {
    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    BuilderType sbb(opts.get());
    // Insert "hello 0" through "hello 9"
    const uint kCount = 10;
    Slice s = CreateBinaryBlock(&sbb, kCount, "hello %d");
    DecoderType sbd(s);
    ASSERT_OK(sbd.ParseHeader());

    // Seeking to just after a key should return the
    // next key ('hello 4x' falls between 'hello 4' and 'hello 5')
    Slice q = "hello 4x";
    bool exact;
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    ASSERT_FALSE(exact);

    Slice ret;
    ASSERT_EQ(5u, sbd.GetCurrentIndex());
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 5"), ret.ToString());

    sbd.SeekToPositionInBlock(0);

    // Seeking to an exact key should return that key
    q = "hello 4";
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    ASSERT_EQ(4u, sbd.GetCurrentIndex());
    ASSERT_TRUE(exact);
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 4"), ret.ToString());

    // Seeking to before the first key should return first key
    q = "hello";
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    ASSERT_EQ(0, sbd.GetCurrentIndex());
    ASSERT_FALSE(exact);
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 0"), ret.ToString());

    // Seeking after the last key should return not found
    q = "zzzz";
    ASSERT_TRUE(sbd.SeekAtOrAfterValue(&q, &exact).IsNotFound());

    // Seeking to the last key should succeed
    q = "hello 9";
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    ASSERT_EQ(9u, sbd.GetCurrentIndex());
    ASSERT_TRUE(exact);
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 9"), ret.ToString());
  }

  template<class BuilderType, class DecoderType>
  void TestStringSeekByValueLargeBlock() {
    Arena arena(1024, 1024*1024); // TODO: move to fixture?
    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    BinaryPrefixBlockBuilder sbb(opts.get());
    const uint kCount = 1000;
    // Insert 'hello 000' through 'hello 999'
    Slice s = CreateBinaryBlock(&sbb, kCount, "hello %03d");
    BinaryPrefixBlockDecoder sbd(s);
    ASSERT_OK(sbd.ParseHeader());

    // Seeking to just after a key should return the
    // next key ('hello 444x' falls between 'hello 444' and 'hello 445')
    Slice q = "hello 444x";
    bool exact;
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    ASSERT_FALSE(exact);

    Slice ret;
    ASSERT_EQ(445u, sbd.GetCurrentIndex());
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 445"), ret.ToString());

    sbd.SeekToPositionInBlock(0);

    // Seeking to an exact key should return that key
    q = "hello 004";
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    EXPECT_TRUE(exact);
    EXPECT_EQ(4u, sbd.GetCurrentIndex());
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 004"), ret.ToString());

    // Seeking to before the first key should return first key
    q = "hello";
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    EXPECT_FALSE(exact);
    EXPECT_EQ(0, sbd.GetCurrentIndex());
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 000"), ret.ToString());

    // Seeking after the last key should return not found
    q = "zzzz";
    ASSERT_TRUE(sbd.SeekAtOrAfterValue(&q, &exact).IsNotFound());

    // Seeking to the last key should succeed
    q = "hello 999";
    ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
    EXPECT_TRUE(exact);
    EXPECT_EQ(999u, sbd.GetCurrentIndex());
    CopyOne<STRING>(&sbd, &ret);
    ASSERT_EQ(string("hello 999"), ret.ToString());

    // Randomized seek
    char target[20];
    char before_target[20];
    for (int i = 0; i < 1000; i++) {
      int ord = random() % kCount;
      int len = snprintf(target, sizeof(target), "hello %03d", ord);
      q = Slice(target, len);

      ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
      EXPECT_TRUE(exact);
      EXPECT_EQ(ord, sbd.GetCurrentIndex());
      CopyOne<STRING>(&sbd, &ret);
      ASSERT_EQ(string(target), ret.ToString());

      // Seek before this key
      len = snprintf(before_target, sizeof(target), "hello %03d.before", ord-1);
      q = Slice(before_target, len);
      ASSERT_OK(sbd.SeekAtOrAfterValue(&q, &exact));
      EXPECT_FALSE(exact);
      EXPECT_EQ(ord, sbd.GetCurrentIndex());
      CopyOne<STRING>(&sbd, &ret);
      ASSERT_EQ(string(target), ret.ToString());
    }
  }

  template<class BuilderType, class DecoderType>
  void TestBinaryBlockRoundTrip() {
    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    BuilderType sbb(opts.get());
    const uint kCount = 10;
    Slice s = CreateBinaryBlock(&sbb, kCount, "hello %d");

    LOG(INFO) << "Block: " << HexDump(s);

    // the slice should take at least a few bytes per entry
    ASSERT_GT(s.size(), kCount * 2u);

    DecoderType sbd(s);
    ASSERT_OK(sbd.ParseHeader());
    ASSERT_EQ(kCount, sbd.Count());
    ASSERT_EQ(12345u, sbd.GetFirstRowId());
    ASSERT_TRUE(sbd.HasNext());

    // Iterate one by one through data, verifying that it matches
    // what we put in.
    for (uint i = 0; i < kCount; i++) {
      ASSERT_EQ(i, sbd.GetCurrentIndex());
      ASSERT_TRUE(sbd.HasNext()) << "Failed on iter " << i;
      Slice s;
      CopyOne<STRING>(&sbd, &s);
      string expected = StringPrintf("hello %d", i);
      ASSERT_EQ(expected, s.ToString()) << "failed at iter " << i;
    }
    ASSERT_FALSE(sbd.HasNext());

    // Now iterate backwards using positional seeking
    for (int i = kCount - 1; i >= 0; i--) {
      sbd.SeekToPositionInBlock(i);
      ASSERT_EQ(i, sbd.GetCurrentIndex());
    }

    // Try to request a bunch of data in one go
    ScopedColumnBlock<STRING> cb(kCount + 10);
    ColumnDataView cdv(&cb);
    sbd.SeekToPositionInBlock(0);
    size_t n = kCount + 10;
    ASSERT_OK(sbd.CopyNextValues(&n, &cdv));
    ASSERT_EQ(kCount, n);
    ASSERT_FALSE(sbd.HasNext());

    for (uint i = 0; i < kCount; i++) {
      string expected = StringPrintf("hello %d", i);
      ASSERT_EQ(expected, cb[i].ToString());
    }
  }

  template<class BlockBuilderType, class BlockDecoderType, DataType IntType>
  void DoSeekTest(BlockBuilderType* ibb, int num_ints, int num_queries, bool verify) {
    // TODO : handle and verify seeking inside a run for testing RLE
    typedef typename TypeTraits<IntType>::cpp_type CppType;

    const CppType kBase = 6;

    CppType data[num_ints];
    for (CppType i = 0; i < num_ints; i++) {
      data[i] = kBase + i * 2;
    }

    CHECK_EQ(num_ints, ibb->Add(reinterpret_cast<uint8_t *>(&data[0]),
                               num_ints));

    Slice s = ibb->Finish(0);
    BlockDecoderType ibd(s);
    ASSERT_OK(ibd.ParseHeader());

    // Benchmark seeking
    LOG_TIMING(INFO, strings::Substitute("Seeking in $0 block", TypeTraits<IntType>::name())) {
      for (int i = 0; i < num_queries; i++) {
        bool exact = false;
        CppType target = random() % (num_ints * 2 + kBase);
        Status s = ibd.SeekAtOrAfterValue(&target, &exact);
        if (verify) {
          SCOPED_TRACE(target);
          if (s.IsNotFound()) {
            ASSERT_EQ(kBase + num_ints * 2 - 1, target);
            continue;
          }
          ASSERT_OK_FAST(s);

          CppType got;
          CopyOne<IntType>(&ibd, &got);

          if (target < kBase) {
            ASSERT_EQ(kBase, got);
            ASSERT_FALSE(exact);
          } else if (target % 2 == 0) {
            // Was inserted
            ASSERT_EQ(target, got);
            ASSERT_TRUE(exact);
          } else {
            ASSERT_EQ(target + 1, got);
            ASSERT_FALSE(exact);
          }
        }
      }
    }
  }


  template <class BlockBuilderType, class BlockDecoderType>
  void TestEmptyBlockEncodeDecode() {
    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    BlockBuilderType bb(opts.get());
    Slice s = bb.Finish(0);
    ASSERT_GT(s.size(), 0);
    LOG(INFO) << "Encoded size for 0 items: " << s.size();

    BlockDecoderType bd(s);
    ASSERT_OK(bd.ParseHeader());
    ASSERT_EQ(0, bd.Count());
    ASSERT_FALSE(bd.HasNext());
  }

  template <DataType Type, class BlockBuilder, class BlockDecoder>
  void TestEncodeDecodeTemplateBlockEncoder(typename TypeTraits<Type>::cpp_type* src,
                                            uint32_t size) {
    typedef typename TypeTraits<Type>::cpp_type CppType;
    const uint32_t kOrdinalPosBase = 12345;
    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    BlockBuilder pbb(opts.get());

    pbb.Add(reinterpret_cast<const uint8_t *>(src), size);
    Slice s = pbb.Finish(kOrdinalPosBase);

    LOG(INFO)<< "Encoded size for 10k elems: " << s.size();

    BlockDecoder pbd(s);
    ASSERT_OK(pbd.ParseHeader());
    ASSERT_EQ(kOrdinalPosBase, pbd.GetFirstRowId());
    ASSERT_EQ(0, pbd.GetCurrentIndex());

    std::vector<CppType> decoded;
    decoded.resize(size);

    ColumnBlock dst_block(GetTypeInfo(Type), nullptr, &decoded[0], size, &arena_);
    ColumnDataView view(&dst_block);
    int dec_count = 0;
    while (pbd.HasNext()) {
      ASSERT_EQ((int32_t )(dec_count), pbd.GetCurrentIndex());

      size_t to_decode = (random() % 30) + 1;
      size_t n = to_decode > view.nrows() ? view.nrows() : to_decode;
      ASSERT_OK_FAST(pbd.CopyNextValues(&n, &view));
      ASSERT_GE(to_decode, n);
      view.Advance(n);
      dec_count += n;
    }

    ASSERT_EQ(0, view.nrows())<< "Should have no space left in the buffer after "
        << "decoding all rows";

    for (uint i = 0; i < size; i++) {
      if (src[i] != decoded[i]) {
        FAIL()<< "Fail at index " << i <<
            " inserted=" << src[i] << " got=" << decoded[i];
      }
    }

    // Test Seek within block by ordinal
    for (int i = 0; i < 100; i++) {
      int seek_off = random() % decoded.size();
      pbd.SeekToPositionInBlock(seek_off);

      EXPECT_EQ((int32_t )(seek_off), pbd.GetCurrentIndex());
      CppType ret;
      CopyOne<Type>(&pbd, &ret);
      EXPECT_EQ(decoded[seek_off], ret);
    }
  }

  // Test truncation of blocks
  template<class BuilderType, class DecoderType>
  void TestBinaryBlockTruncation() {
    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    BuilderType sbb(opts.get());
    const uint kCount = 10;
    size_t sbsize;

    Slice s = CreateBinaryBlock(&sbb, kCount, "hello %d");
    do {
      sbsize = s.size();

      LOG(INFO) << "Block: " << HexDump(s);

      DecoderType sbd(s);
      Status st = sbd.ParseHeader();

      if (sbsize < DecoderType::kMinHeaderSize) {
        ASSERT_TRUE(st.IsCorruption());
        ASSERT_STR_CONTAINS(st.ToString(), "not enough bytes for header");
      } else if (sbsize < coding::DecodeGroupVarInt32_GetGroupSize(s.data())) {
        ASSERT_TRUE(st.IsCorruption());
        ASSERT_STR_CONTAINS(st.ToString(), "less than length");
      }
      if (sbsize > 0) {
        s.truncate(sbsize - 1);
      }
    } while (sbsize > 0);
  }

  // Test encoding and decoding of integer datatypes
  template <class BuilderType, class DecoderType, DataType IntType>
  void TestIntBlockRoundTrip(BuilderType* ibb) {
    typedef typename DataTypeTraits<IntType>::cpp_type CppType;

    LOG(INFO) << "Testing with IntType = " << DataTypeTraits<IntType>::name();

    const uint32_t kOrdinalPosBase = 12345;

    srand(123);

    std::vector<CppType> to_insert;
    for (int i = 0; i < 10003; i++) {
      to_insert.push_back(random() % std::numeric_limits<CppType>::max());
    }

    ibb->Add(reinterpret_cast<const uint8_t *>(&to_insert[0]),
             to_insert.size());
    Slice s = ibb->Finish(kOrdinalPosBase);

    DecoderType ibd(s);
    ASSERT_OK(ibd.ParseHeader());

    ASSERT_EQ(kOrdinalPosBase, ibd.GetFirstRowId());

    std::vector<CppType> decoded;
    decoded.resize(to_insert.size());

    ColumnBlock dst_block(GetTypeInfo(IntType), nullptr,
                          &decoded[0],
                          to_insert.size(),
                          &arena_);
    int dec_count = 0;
    while (ibd.HasNext()) {
      ASSERT_EQ((uint32_t)(dec_count), ibd.GetCurrentIndex());

      size_t to_decode = std::min(to_insert.size() - dec_count,
                                  static_cast<size_t>((random() % 30) + 1));
      size_t n = to_decode;
      ColumnDataView dst_data(&dst_block, dec_count);
      DCHECK_EQ((unsigned char *)(&decoded[dec_count]), dst_data.data());
      ASSERT_OK_FAST(ibd.CopyNextValues(&n, &dst_data));
      ASSERT_GE(to_decode, n);
      dec_count += n;
    }

    ASSERT_EQ(dec_count, dst_block.nrows())
        << "Should have decoded all rows to fill the buffer";

    for (uint i = 0; i < to_insert.size(); i++) {
      if (to_insert[i] != decoded[i]) {
        FAIL() << "Fail at index " << i <<
            " inserted=" << to_insert[i] << " got=" << decoded[i];
      }
    }

    // Test Seek within block by ordinal
    for (int i = 0; i < 100; i++) {
      int seek_off = random() % decoded.size();
      ibd.SeekToPositionInBlock(seek_off);

      EXPECT_EQ((uint32_t)(seek_off), ibd.GetCurrentIndex());
      CppType ret;
      CopyOne<IntType>(&ibd, &ret);
      EXPECT_EQ(decoded[seek_off], ret);
    }
  }

  template <DataType IntType>
  void TestRleIntBlockRoundTrip() {
    gscoped_ptr<RleIntBlockBuilder<IntType> > ibb(new RleIntBlockBuilder<IntType>());
    TestIntBlockRoundTrip<RleIntBlockBuilder<IntType>, RleIntBlockDecoder<IntType>, IntType>(
        ibb.get());
  }

  // Test encoding and decoding BOOL datatypes
  template <class BuilderType, class DecoderType>
  void TestBoolBlockRoundTrip() {
    const uint32_t kOrdinalPosBase = 12345;

    srand(123);

    std::vector<uint8_t> to_insert;
    for (int i = 0; i < 10003; ) {
      int run_size = random() % 100;
      bool val = random() % 2;
      for (int j = 0; j < run_size; j++) {
        to_insert.push_back(val);
      }
      i += run_size;
    }

    BuilderType bb;
    bb.Add(reinterpret_cast<const uint8_t *>(&to_insert[0]),
           to_insert.size());
    Slice s = bb.Finish(kOrdinalPosBase);

    DecoderType bd(s);
    ASSERT_OK(bd.ParseHeader());

    ASSERT_EQ(kOrdinalPosBase, bd.GetFirstRowId());

    std::vector<uint8_t> decoded;
    decoded.resize(to_insert.size());

    ColumnBlock dst_block(GetTypeInfo(BOOL), nullptr,
                          &decoded[0],
                          to_insert.size(),
                          &arena_);

    int dec_count = 0;
    while (bd.HasNext()) {
      ASSERT_EQ((uint32_t)(dec_count), bd.GetCurrentIndex());

      size_t to_decode = std::min(to_insert.size() - dec_count,
                                  static_cast<size_t>((random() % 30) + 1));
      size_t n = to_decode;
      ColumnDataView dst_data(&dst_block, dec_count);
      DCHECK_EQ((unsigned char *)(&decoded[dec_count]), dst_data.data());
      ASSERT_OK_FAST(bd.CopyNextValues(&n, &dst_data));
      ASSERT_GE(to_decode, n);
      dec_count += n;
    }

    ASSERT_EQ(dec_count, dst_block.nrows())
        << "Should have decoded all rows to fill the buffer";

    for (uint i = 0; i < to_insert.size(); i++) {
      if (to_insert[i] != decoded[i]) {
        FAIL() << "Fail at index " << i <<
            " inserted=" << to_insert[i] << " got=" << decoded[i];
      }
    }

    // Test Seek within block by ordinal
    for (int i = 0; i < 100; i++) {
      int seek_off = random() % decoded.size();
      bd.SeekToPositionInBlock(seek_off);

      EXPECT_EQ((uint32_t)(seek_off), bd.GetCurrentIndex());
      bool ret;
      CopyOne<BOOL>(&bd, &ret);
      EXPECT_EQ(static_cast<bool>(decoded[seek_off]), ret);
    }
  }

  Arena arena_;
};

TEST_F(TestEncoding, TestPlainBlockEncoder) {
  const uint32_t kSize = 10000;

  gscoped_ptr<int32_t[]> ints(new int32_t[kSize]);
  for (int i = 0; i < kSize; i++) {
    ints.get()[i] = random();
  }

  TestEncodeDecodeTemplateBlockEncoder<INT32, PlainBlockBuilder<INT32>,
                                    PlainBlockDecoder<INT32> >(ints.get(), kSize);
}

// Test for bitshuffle block, for INT32, FLOAT, DOUBLE
TEST_F(TestEncoding, TestBShufIntBlockEncoder) {
  const uint32_t kSize = 10000;

  gscoped_ptr<int32_t[]> ints(new int32_t[kSize]);
  for (int i = 0; i < kSize; i++) {
    ints.get()[i] = random();
  }

  TestEncodeDecodeTemplateBlockEncoder<INT32, BShufBlockBuilder<INT32>,
                                    BShufBlockDecoder<INT32> >(ints.get(), kSize);
}

TEST_F(TestEncoding, TestBShufFloatBlockEncoder) {
  const uint32_t kSize = 10000;

  gscoped_ptr<float[]> floats(new float[kSize]);
  for (int i = 0; i < kSize; i++) {
    floats.get()[i] = random() + static_cast<float>(random())/INT_MAX;
  }

  TestEncodeDecodeTemplateBlockEncoder<FLOAT, BShufBlockBuilder<FLOAT>,
                                    BShufBlockDecoder<FLOAT> >(floats.get(), kSize);
}

TEST_F(TestEncoding, TestBShufDoubleBlockEncoder) {
  const uint32_t kSize = 10000;

  gscoped_ptr<double[]> doubles(new double[kSize]);
  for (int i = 0; i < kSize; i++) {
    doubles.get()[i] = random() + + static_cast<double>(random())/INT_MAX;
  }

  TestEncodeDecodeTemplateBlockEncoder<DOUBLE, BShufBlockBuilder<DOUBLE>,
                                    BShufBlockDecoder<DOUBLE> >(doubles.get(), kSize);
}

TEST_F(TestEncoding, TestIntBlockEncoder) {
  gscoped_ptr<WriterOptions> opts(NewWriterOptions());
  GVIntBlockBuilder ibb(opts.get());

  auto ints = new int[10000];
  for (int i = 0; i < 10000; i++) {
    ints[i] = random();
  }
  ibb.Add(reinterpret_cast<const uint8_t *>(ints), 10000);
  delete[] ints;

  Slice s = ibb.Finish(12345);
  LOG(INFO) << "Encoded size for 10k ints: " << s.size();

  // Test empty case -- should be 5 bytes for just the
  // header word (all zeros)
  ibb.Reset();
  s = ibb.Finish(0);
  ASSERT_EQ(5UL, s.size());
}

TEST_F(TestEncoding, TestRleIntBlockEncoder) {
  RleIntBlockBuilder<UINT32> ibb;
  gscoped_ptr<int[]> ints(new int[10000]);
  for (int i = 0; i < 10000; i++) {
    ints[i] = random();
  }
  ibb.Add(reinterpret_cast<const uint8_t *>(ints.get()), 10000);

  Slice s = ibb.Finish(12345);
  LOG(INFO) << "RLE Encoded size for 10k ints: " << s.size();

  ibb.Reset();
  ints.reset(new int[100]);
  for (int i = 0; i < 100; i++) {
    ints[i] = 0;
  }
  ibb.Add(reinterpret_cast<const uint8_t *>(ints.get()), 100);
  s = ibb.Finish(12345);
  ASSERT_EQ(14UL, s.size());
}

TEST_F(TestEncoding, TestPlainBitMapRoundTrip) {
  TestBoolBlockRoundTrip<PlainBitMapBlockBuilder, PlainBitMapBlockDecoder>();
}

TEST_F(TestEncoding, TestRleBitMapRoundTrip) {
  TestBoolBlockRoundTrip<RleBitMapBlockBuilder, RleBitMapBlockDecoder>();
}

TEST_F(TestEncoding, TestGVIntBlockRoundTrip) {
  gscoped_ptr<WriterOptions> opts(NewWriterOptions());
  gscoped_ptr<GVIntBlockBuilder> ibb(new GVIntBlockBuilder(opts.get()));
  TestIntBlockRoundTrip<GVIntBlockBuilder, GVIntBlockDecoder, UINT32>(ibb.get());
}

TEST_F(TestEncoding, TestRleIntBlockRoundTripAllTypes) {
  LOG(INFO) << "Testing all integer types with RLE block encoding";

  TestRleIntBlockRoundTrip<UINT8>();
  TestRleIntBlockRoundTrip<INT8>();
  TestRleIntBlockRoundTrip<UINT16>();
  TestRleIntBlockRoundTrip<INT16>();
  TestRleIntBlockRoundTrip<UINT32>();
  TestRleIntBlockRoundTrip<INT32>();
}


TEST_F(TestEncoding, TestGVIntEmptyBlockEncodeDecode) {
  TestEmptyBlockEncodeDecode<GVIntBlockBuilder, GVIntBlockDecoder>();
}

// Test seeking to a value in a small block.
// Regression test for a bug seen in development where this would
// infinite loop when there are no 'restarts' in a given block.
TEST_F(TestEncoding, TestBinaryPrefixBlockBuilderSeekByValueSmallBlock) {
  TestBinarySeekByValueSmallBlock<BinaryPrefixBlockBuilder, BinaryPrefixBlockDecoder>();
}

TEST_F(TestEncoding, TestBinaryPlainBlockBuilderSeekByValueSmallBlock) {
  TestBinarySeekByValueSmallBlock<BinaryPlainBlockBuilder, BinaryPlainBlockDecoder>();
}

// Test seeking to a value in a large block which contains
// many 'restarts'
TEST_F(TestEncoding, TestBinaryPrefixBlockBuilderSeekByValueLargeBlock) {
  TestStringSeekByValueLargeBlock<BinaryPrefixBlockBuilder, BinaryPrefixBlockDecoder>();
}

TEST_F(TestEncoding, TestBinaryPlainBlockBuilderSeekByValueLargeBlock) {
  TestStringSeekByValueLargeBlock<BinaryPlainBlockBuilder, BinaryPlainBlockDecoder>();
}

// Test round-trip encode/decode of a binary block.
TEST_F(TestEncoding, TestBinaryPrefixBlockBuilderRoundTrip) {
  TestBinaryBlockRoundTrip<BinaryPrefixBlockBuilder, BinaryPrefixBlockDecoder>();
}

TEST_F(TestEncoding, TestBinaryPlainBlockBuilderRoundTrip) {
  TestBinaryBlockRoundTrip<BinaryPlainBlockBuilder, BinaryPlainBlockDecoder>();
}

// Test empty block encode/decode
TEST_F(TestEncoding, TestBinaryPlainEmptyBlockEncodeDecode) {
  TestEmptyBlockEncodeDecode<BinaryPlainBlockBuilder, BinaryPlainBlockDecoder>();
}

TEST_F(TestEncoding, TestBinaryPrefixEmptyBlockEncodeDecode) {
  TestEmptyBlockEncodeDecode<BinaryPrefixBlockBuilder, BinaryPrefixBlockDecoder>();
}

// Test encode/decode of a binary block with various-sized truncations.
TEST_F(TestEncoding, TestBinaryPlainBlockBuilderTruncation) {
  TestBinaryBlockTruncation<BinaryPlainBlockBuilder, BinaryPlainBlockDecoder>();
}

TEST_F(TestEncoding, TestBinaryPrefixBlockBuilderTruncation) {
  TestBinaryBlockTruncation<BinaryPrefixBlockBuilder, BinaryPrefixBlockDecoder>();
}

#ifdef NDEBUG
TEST_F(TestEncoding, GVIntSeekBenchmark) {
  gscoped_ptr<WriterOptions> opts(NewWriterOptions());
  gscoped_ptr<GVIntBlockBuilder> ibb(new GVIntBlockBuilder(opts.get()));
  DoSeekTest<GVIntBlockBuilder, GVIntBlockDecoder, UINT32>(ibb.get(), 32768, 100000, false);
}
#endif

TEST_F(TestEncoding, GVIntSeekTest) {
  gscoped_ptr<WriterOptions> opts(NewWriterOptions());
  gscoped_ptr<GVIntBlockBuilder> ibb(new GVIntBlockBuilder(opts.get()));
  DoSeekTest<GVIntBlockBuilder, GVIntBlockDecoder, UINT32>(ibb.get(), 64, 1000, true);
}

TEST_F(TestEncoding, GVIntSeekTestTinyBlock) {
  gscoped_ptr<WriterOptions> opts(NewWriterOptions());
  for (int block_size = 1; block_size < 16; block_size++) {
    gscoped_ptr<GVIntBlockBuilder> ibb(new GVIntBlockBuilder(opts.get()));
    DoSeekTest<GVIntBlockBuilder, GVIntBlockDecoder, UINT32>(ibb.get(), block_size, 1000, true);
  }
}


// We have several different encodings for INT blocks.
// The following tests use GTest's TypedTest functionality to run the tests
// for each of the encodings.
//
// Beware ugly template magic below.
struct PlainTestTraits {
  template<DataType type>
  struct Classes {
    typedef PlainBlockBuilder<type> encoder_type;
    typedef PlainBlockDecoder<type> decoder_type;
  };
};

struct RleTestTraits {
  template<DataType type>
  struct Classes {
    typedef RleIntBlockBuilder<type> encoder_type;
    typedef RleIntBlockDecoder<type> decoder_type;
  };
};

struct BitshuffleTestTraits {
  template<DataType type>
  struct Classes {
    typedef BShufBlockBuilder<type> encoder_type;
    typedef BShufBlockDecoder<type> decoder_type;
  };
};
typedef testing::Types<RleTestTraits, BitshuffleTestTraits, PlainTestTraits> MyTestFixtures;
TYPED_TEST_CASE(IntEncodingTest, MyTestFixtures);

template<class TestTraits>
class IntEncodingTest : public TestEncoding {
 public:
  template <DataType IntType>
  void DoIntSeekTest(int num_ints, int num_queries, bool verify) {
    typedef typename TestTraits::template Classes<IntType>::encoder_type encoder_type;
    typedef typename TestTraits::template Classes<IntType>::decoder_type decoder_type;

    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    gscoped_ptr<encoder_type> ibb(new encoder_type(opts.get()));
    DoSeekTest<encoder_type, decoder_type, IntType>(ibb.get(), num_ints, num_queries, verify);
  }

  template <DataType IntType>
  void DoIntSeekTestTinyBlock() {
    for (int block_size = 1; block_size < 16; block_size++) {
      DoIntSeekTest<IntType>(block_size, 1000, true);
    }
  }

  template <DataType IntType>
  void DoIntRoundTripTest() {
    typedef typename TestTraits::template Classes<IntType>::encoder_type encoder_type;
    typedef typename TestTraits::template Classes<IntType>::decoder_type decoder_type;

    gscoped_ptr<WriterOptions> opts(NewWriterOptions());
    gscoped_ptr<encoder_type> ibb(new encoder_type(opts.get()));
    TestIntBlockRoundTrip<encoder_type, decoder_type, IntType>(ibb.get());
  }
};


TYPED_TEST(IntEncodingTest, TestSeekAllTypes) {
  this->template DoIntSeekTest<UINT8>(32, 1000, true);
  this->template DoIntSeekTest<INT8>(32, 1000, true);
  this->template DoIntSeekTest<UINT16>(64, 1000, true);
  this->template DoIntSeekTest<INT16>(64, 1000, true);
  this->template DoIntSeekTest<UINT32>(64, 1000, true);
  this->template DoIntSeekTest<INT32>(64, 1000, true);
}

TYPED_TEST(IntEncodingTest, IntSeekTestTinyBlockAllTypes) {
  this->template DoIntSeekTestTinyBlock<UINT8>();
  this->template DoIntSeekTestTinyBlock<INT8>();
  this->template DoIntSeekTestTinyBlock<UINT16>();
  this->template DoIntSeekTestTinyBlock<INT16>();
  this->template DoIntSeekTestTinyBlock<UINT32>();
  this->template DoIntSeekTestTinyBlock<INT32>();
}

TYPED_TEST(IntEncodingTest, TestRoundTrip) {
  this->template DoIntRoundTripTest<UINT8>();
  this->template DoIntRoundTripTest<INT8>();
  this->template DoIntRoundTripTest<UINT16>();
  this->template DoIntRoundTripTest<INT16>();
  this->template DoIntRoundTripTest<UINT32>();
  this->template DoIntRoundTripTest<INT32>();
}

#ifdef NDEBUG
TYPED_TEST(IntEncodingTest, IntSeekBenchmark) {
  this->template DoIntSeekTest<INT32>(32768, 10000, false);
}
#endif

} // namespace cfile
} // namespace kudu
