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
#ifndef KUDU_CFILE_PLAIN_BLOCK_H
#define KUDU_CFILE_PLAIN_BLOCK_H

#include <algorithm>
#include <string>

#include "kudu/cfile/block_encodings.h"
#include "kudu/cfile/cfile_util.h"
#include "kudu/common/columnblock.h"
#include "kudu/util/coding.h"
#include "kudu/util/coding-inl.h"
#include "kudu/util/hexdump.h"

namespace kudu {
namespace cfile {

template<typename Type>
inline Type Decode(const uint8_t *ptr) {
  Type result;
  memcpy(&result, ptr, sizeof(result));
  return result;
}

static const size_t kPlainBlockHeaderSize = sizeof(uint32_t) * 2;

//
// A plain encoder for generic fixed size data types.
//
template<DataType Type>
class PlainBlockBuilder : public BlockBuilder {
 public:
  explicit PlainBlockBuilder(const WriterOptions *options)
      : options_(options) {
    // Reserve enough space for the block, plus a bit of slop since
    // we often overrun the block by a few values.
    buffer_.reserve(kPlainBlockHeaderSize + options_->storage_attributes.cfile_block_size + 1024);
    Reset();
  }

  virtual int Add(const uint8_t *vals_void, size_t count) OVERRIDE {
    int old_size = buffer_.size();
    buffer_.resize(old_size + count * kCppTypeSize);
    memcpy(&buffer_[old_size], vals_void, count * kCppTypeSize);
    count_ += count;
    return count;
  }

  virtual bool IsBlockFull(size_t limit) const OVERRIDE {
    return buffer_.size() > limit;
  }

  virtual Slice Finish(rowid_t ordinal_pos) OVERRIDE {
    InlineEncodeFixed32(&buffer_[0], count_);
    InlineEncodeFixed32(&buffer_[4], ordinal_pos);
    return Slice(buffer_);
  }

  virtual void Reset() OVERRIDE {
    count_ = 0;
    buffer_.clear();
    buffer_.resize(kPlainBlockHeaderSize);
  }

  virtual size_t Count() const OVERRIDE {
    return count_;
  }

  virtual Status GetFirstKey(void *key) const OVERRIDE {
    DCHECK_GT(count_, 0);
    *reinterpret_cast<CppType *>(key) = Decode<CppType>(&buffer_[kPlainBlockHeaderSize]);
    return Status::OK();
  }

 private:
  faststring buffer_;
  const WriterOptions *options_;
  size_t count_;
  typedef typename TypeTraits<Type>::cpp_type CppType;
  enum {
    kCppTypeSize = TypeTraits<Type>::size
  };

};

//
// A plain decoder for generic fixed size data types.
//
template<DataType Type>
class PlainBlockDecoder : public BlockDecoder {
 public:
  explicit PlainBlockDecoder(Slice slice)
      : data_(std::move(slice)),
        parsed_(false),
        num_elems_(0),
        ordinal_pos_base_(0),
        cur_idx_(0) {
  }

  virtual Status ParseHeader() OVERRIDE {
    CHECK(!parsed_);

    if (data_.size() < kPlainBlockHeaderSize) {
      return Status::Corruption(
          "not enough bytes for header in PlainBlockDecoder");
    }

    num_elems_ = DecodeFixed32(&data_[0]);
    ordinal_pos_base_ = DecodeFixed32(&data_[4]);

    if (data_.size() != kPlainBlockHeaderSize + num_elems_ * size_of_type) {
      return Status::Corruption(
          string("unexpected data size. ") + "\nFirst 100 bytes: "
              + HexDump(
                  Slice(data_.data(),
                        (data_.size() < 100 ? data_.size() : 100))));
    }

    parsed_ = true;

    SeekToPositionInBlock(0);

    return Status::OK();
  }

  virtual void SeekToPositionInBlock(uint pos) OVERRIDE {
    CHECK(parsed_) << "Must call ParseHeader()";

    if (PREDICT_FALSE(num_elems_ == 0)) {
      DCHECK_EQ(0, pos);
      return;
    }

    DCHECK_LE(pos, num_elems_);
    cur_idx_ = pos;
  }

  virtual Status SeekAtOrAfterValue(const void *value, bool *exact_match) OVERRIDE {
    DCHECK(value != NULL);

    const CppType &target = *reinterpret_cast<const CppType *>(value);

    uint32_t left = 0;
    uint32_t right = num_elems_;
    while (left != right) {
      uint32_t mid = (left + right) / 2;
      CppType mid_key = Decode<CppType>(
          &data_[kPlainBlockHeaderSize + mid * size_of_type]);
      // assumes CppType has an implementation of operator<()
      if (mid_key < target) {
        left = mid + 1;
      } else if (mid_key > target) {
        right = mid;
      } else {
        cur_idx_ = mid;
        *exact_match = true;
        return Status::OK();
      }
    }

    *exact_match = false;
    cur_idx_ = left;
    if (cur_idx_ == num_elems_) {
      return Status::NotFound("after last key in block");
    }

    return Status::OK();
  }

  virtual Status CopyNextValues(size_t *n, ColumnDataView *dst) OVERRIDE {
    DCHECK(parsed_);
    DCHECK_LE(*n, dst->nrows());
    DCHECK_EQ(dst->stride(), sizeof(CppType));

    if (PREDICT_FALSE(*n == 0 || cur_idx_ >= num_elems_)) {
      *n = 0;
      return Status::OK();
    }

    size_t max_fetch = std::min(*n, static_cast<size_t>(num_elems_ - cur_idx_));
    memcpy(dst->data(),
           &data_[kPlainBlockHeaderSize + cur_idx_ * size_of_type],
           max_fetch * size_of_type);
    cur_idx_ += max_fetch;
    *n = max_fetch;
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
  }

 private:

  Slice data_;
  bool parsed_;
  uint32_t num_elems_;
  rowid_t ordinal_pos_base_;
  uint32_t cur_idx_;
  typedef typename TypeTraits<Type>::cpp_type CppType;
  enum {
    size_of_type = TypeTraits<Type>::size
  };

};

} // namespace cfile
} // namespace kudu

#endif
