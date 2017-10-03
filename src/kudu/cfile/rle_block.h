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

#ifndef KUDU_CFILE_RLE_BLOCK_H
#define KUDU_CFILE_RLE_BLOCK_H

#include <algorithm>
#include <string>

#include "kudu/gutil/port.h"
#include "kudu/cfile/block_encodings.h"
#include "kudu/common/columnblock.h"
#include "kudu/util/coding.h"
#include "kudu/util/coding-inl.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/bit-stream-utils.inline.h"
#include "kudu/util/bitmap.h"
#include "kudu/util/rle-encoding.h"


namespace kudu {
namespace cfile {

struct WriterOptions;

enum {
  kRleBitmapBlockHeaderSize = 8
};

//
// RLE encoder for the BOOL datatype: uses an RLE-encoded bitmap to
// represent a bool column.
//
class RleBitMapBlockBuilder : public BlockBuilder {
 public:
  RleBitMapBlockBuilder()
      : encoder_(&buf_, 1) {
    Reset();
  }

  virtual int Add(const uint8_t* vals, size_t count) OVERRIDE {
     for (const uint8_t* val = vals;
          val < vals + count;
          ++val) {
       // TODO (perf) : doing this one bit a time is probably
       //               inefficient.
       encoder_.Put(*val, 1);
    }
    count_ += count;
    return count;
  }

  virtual bool IsBlockFull(size_t limit) const OVERRIDE {
    return encoder_.len() > limit;
  }

  virtual Slice Finish(rowid_t ordinal_pos) OVERRIDE {
    InlineEncodeFixed32(&buf_[0], count_);
    InlineEncodeFixed32(&buf_[4], ordinal_pos);
    encoder_.Flush();
    return Slice(buf_);
  }

  virtual void Reset() OVERRIDE {
    count_ = 0;
    encoder_.Clear();
    encoder_.Reserve(kRleBitmapBlockHeaderSize, 0);
  }

  virtual size_t Count() const OVERRIDE {
    return count_;
  }

  // TODO Implement this method
  virtual Status GetFirstKey(void* key) const OVERRIDE {
    return Status::NotSupported("BOOL keys not supported");
  }

 private:
  faststring buf_;
  RleEncoder<bool> encoder_;
  size_t count_;
};

//
// RLE decoder for bool datatype
//
class RleBitMapBlockDecoder : public BlockDecoder {
 public:
  explicit RleBitMapBlockDecoder(Slice slice)
      : data_(std::move(slice)),
        parsed_(false),
        num_elems_(0),
        ordinal_pos_base_(0),
        cur_idx_(0) {
  }

  virtual Status ParseHeader() OVERRIDE {
    CHECK(!parsed_);

    if (data_.size() < kRleBitmapBlockHeaderSize) {
      return Status::Corruption(
          "not enough bytes for header in RleBitMapBlockDecoder");
    }

    num_elems_ = DecodeFixed32(&data_[0]);
    ordinal_pos_base_ = DecodeFixed32(&data_[4]);

    parsed_ = true;

    rle_decoder_ = RleDecoder<bool>(data_.data() + kRleBitmapBlockHeaderSize,
                                    data_.size() - kRleBitmapBlockHeaderSize, 1);

    SeekToPositionInBlock(0);

    return Status::OK();
  }

  virtual void SeekToPositionInBlock(uint pos) OVERRIDE {
    CHECK(parsed_) << "Must call ParseHeader()";

    if (cur_idx_ == pos) {
      // No need to seek.
      return;
    } else if (cur_idx_ < pos) {
      uint nskip = pos - cur_idx_;
      rle_decoder_.Skip(nskip);
    } else {
      // This approach is also used by CFileReader to
      // seek backwards in an RLE encoded block
      rle_decoder_ = RleDecoder<bool>(data_.data() + kRleBitmapBlockHeaderSize,
                                      data_.size() - kRleBitmapBlockHeaderSize, 1);
      rle_decoder_.Skip(pos);
    }
    cur_idx_ = pos;
  }

  virtual Status CopyNextValues(size_t *n, ColumnDataView* dst) OVERRIDE {
    DCHECK(parsed_);

    DCHECK_LE(*n, dst->nrows());
    DCHECK_EQ(dst->stride(), sizeof(bool));

    if (PREDICT_FALSE(*n == 0 || cur_idx_ >= num_elems_)) {
      *n = 0;
      return Status::OK();
    }

    size_t bits_to_fetch = std::min(*n, static_cast<size_t>(num_elems_ - cur_idx_));
    size_t remaining = bits_to_fetch;
    uint8_t* data_ptr = dst->data();
    // TODO : do this a word/byte at a time as opposed bit at a time
    while (remaining > 0) {
      bool result = rle_decoder_.Get(reinterpret_cast<bool*>(data_ptr));
      DCHECK(result);
      remaining--;
      data_ptr++;
    }

    cur_idx_ += bits_to_fetch;
    *n = bits_to_fetch;

    return Status::OK();
  }

  virtual Status SeekAtOrAfterValue(const void *value,
                                    bool *exact_match) OVERRIDE {
    return Status::NotSupported("BOOL keys are not supported!");
  }

  virtual bool HasNext() const OVERRIDE { return cur_idx_ < num_elems_; }

  virtual size_t Count() const OVERRIDE { return num_elems_; }

  virtual size_t GetCurrentIndex() const OVERRIDE { return cur_idx_; }

  virtual rowid_t GetFirstRowId() const OVERRIDE { return ordinal_pos_base_; }

 private:
  Slice data_;
  bool parsed_;
  uint32_t num_elems_;
  rowid_t ordinal_pos_base_;
  uint32_t cur_idx_;
  RleDecoder<bool> rle_decoder_;
};

//
// RLE builder for generic integer types. What is missing is some way
// to enforce that this can only be instantiated for INT types.
// TODO : consider if this can also be used for BOOL with only minor
//        alterations
template <DataType IntType>
class RleIntBlockBuilder : public BlockBuilder {
 public:
  explicit RleIntBlockBuilder(const WriterOptions* opts = NULL)
      : rle_encoder_(&buf_, kCppTypeSize * 8) {
    Reset();
  }

  virtual bool IsBlockFull(size_t limit) const OVERRIDE {
    return rle_encoder_.len() > limit;
  }

  virtual int Add(const uint8_t* vals_void, size_t count) OVERRIDE {
    if (PREDICT_FALSE(count_ == 0)) {
      first_key_ = *reinterpret_cast<const CppType*>(vals_void);
    }
    const CppType* vals = reinterpret_cast<const CppType*>(vals_void);
    for (size_t i = 0; i < count; ++i) {
      rle_encoder_.Put(vals[i], 1);
    }
    count_ += count;
    return count;
  }

  virtual Slice Finish(rowid_t ordinal_pos) OVERRIDE {
    InlineEncodeFixed32(&buf_[0], count_);
    InlineEncodeFixed32(&buf_[4], ordinal_pos);
    rle_encoder_.Flush();
    return Slice(buf_);
  }

  virtual void Reset() OVERRIDE {
    count_ = 0;
    rle_encoder_.Clear();
    rle_encoder_.Reserve(kRleBitmapBlockHeaderSize, 0);
  }

  virtual size_t Count() const OVERRIDE {
    return count_;
  }

  virtual Status GetFirstKey(void* key) const OVERRIDE {
    if (count_ > 0) {
      *reinterpret_cast<CppType*>(key) = first_key_;
      return Status::OK();
    }
    return Status::NotFound("No keys in the block");
  }

 private:
  typedef typename TypeTraits<IntType>::cpp_type CppType;

  enum {
    kCppTypeSize = TypeTraits<IntType>::size
  };

  CppType first_key_;
  faststring buf_;
  size_t count_;
  RleEncoder<CppType> rle_encoder_;
};

//
// RLE decoder for generic integer types.
//
// TODO : as with the matching BlockBuilder above (see comments for
//        that class), it may be be possible to re-use most of the
//        code here for the BOOL type.
//
template <DataType IntType>
class RleIntBlockDecoder : public BlockDecoder {
 public:
  explicit RleIntBlockDecoder(Slice slice)
      : data_(std::move(slice)),
        parsed_(false),
        num_elems_(0),
        ordinal_pos_base_(0),
        cur_idx_(0) {
  }

  virtual Status ParseHeader() OVERRIDE {
    CHECK(!parsed_);

    if (data_.size() < kRleBitmapBlockHeaderSize) {
      return Status::Corruption(
          "not enough bytes for header in RleIntBlockDecoder");
    }

    num_elems_ = DecodeFixed32(&data_[0]);
    ordinal_pos_base_ = DecodeFixed32(&data_[4]);

    parsed_ = true;

    rle_decoder_ = RleDecoder<CppType>(data_.data() + kRleBitmapBlockHeaderSize,
                                       data_.size() - kRleBitmapBlockHeaderSize,
                                       kCppTypeSize * 8);

    SeekToPositionInBlock(0);

    return Status::OK();
  }

  virtual void SeekToPositionInBlock(uint pos) OVERRIDE {
    CHECK(parsed_) << "Must call ParseHeader()";
    CHECK_LT(pos, num_elems_)
        << "Tried to seek to " << pos << " which is >= number of elements ("
        << num_elems_ << ") in the block!.";

    if (cur_idx_ == pos) {
      // No need to seek.
      return;
    } else if (cur_idx_ < pos) {
      uint nskip = pos - cur_idx_;
      rle_decoder_.Skip(nskip);
    } else {
      rle_decoder_ = RleDecoder<CppType>(data_.data() + kRleBitmapBlockHeaderSize,
                                         data_.size() - kRleBitmapBlockHeaderSize,
                                         kCppTypeSize * 8);
      rle_decoder_.Skip(pos);
    }
    cur_idx_ = pos;
  }

  virtual Status SeekAtOrAfterValue(const void *value_void, bool *exact_match) OVERRIDE {
    // Currently using linear search as we do not check whether a
    // mid-point of a buffer will fall on a literal or not.
    //
    // TODO (perf): make this faster by moving forward a 'run at a time'
    //              by perhaps pushing this loop down into RleDecoder itself
    // TODO (perf): investigate placing pointers somewhere in either the
    // header or the tail to speed up search.

    SeekToPositionInBlock(0);

    CppType target = *reinterpret_cast<const CppType *>(value_void);

    while (cur_idx_ < num_elems_) {
      CppType cur_elem;
      if (!rle_decoder_.Get(&cur_elem)) {
        break;
      }
      if (cur_elem == target) {
        rle_decoder_.RewindOne();
        *exact_match = true;
        return Status::OK();
      }
      if (cur_elem > target) {
        rle_decoder_.RewindOne();
        *exact_match = false;
        return Status::OK();
      }
      cur_idx_++;
    }

    return Status::NotFound("not in block");
  }

  virtual Status CopyNextValues(size_t *n, ColumnDataView *dst) OVERRIDE {
    DCHECK(parsed_);

    DCHECK_LE(*n, dst->nrows());
    DCHECK_EQ(dst->stride(), sizeof(CppType));

    if (PREDICT_FALSE(*n == 0 || cur_idx_ >= num_elems_)) {
      *n = 0;
      return Status::OK();
    }

    size_t to_fetch = std::min(*n, static_cast<size_t>(num_elems_ - cur_idx_));
    size_t remaining = to_fetch;
    uint8_t* data_ptr = dst->data();
    while (remaining > 0) {
      bool result = rle_decoder_.Get(reinterpret_cast<CppType*>(data_ptr));
      DCHECK(result);
      remaining--;
      data_ptr += kCppTypeSize;
    }

    cur_idx_ += to_fetch;
    *n = to_fetch;
    return Status::OK();
  }

  virtual bool HasNext() const OVERRIDE {
    return cur_idx_ < num_elems_;
  }

  virtual size_t Count() const OVERRIDE {
    return num_elems_;
  }

  virtual size_t GetCurrentIndex() const OVERRIDE {
    return cur_idx_;
  }

  virtual rowid_t GetFirstRowId() const OVERRIDE {
    return ordinal_pos_base_;
  };
 private:
  typedef typename TypeTraits<IntType>::cpp_type CppType;

  enum {
    kCppTypeSize = TypeTraits<IntType>::size
  };

  Slice data_;
  bool parsed_;
  uint32_t num_elems_;
  rowid_t ordinal_pos_base_;
  size_t cur_idx_;
  RleDecoder<CppType> rle_decoder_;
};

} // namespace cfile
} // namespace kudu

#endif
