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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#ifndef YB_CFILE_PLAIN_BITMAP_BLOCK_H_
#define YB_CFILE_PLAIN_BITMAP_BLOCK_H_

#include <algorithm>
#include <string>

#include "yb/cfile/block_encodings.h"
#include "yb/common/columnblock.h"
#include "yb/gutil/port.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/bit-stream-utils.inline.h"
#include "yb/util/bitmap.h"
#include "yb/util/coding.h"
#include "yb/util/coding-inl.h"
#include "yb/util/hexdump.h"

namespace yb {
namespace cfile {

struct WriterOptions;

//
// A plain encoder for the BOOL datatype: stores a column of BOOL values
// as a packed bitmap.
//
class PlainBitMapBlockBuilder : public BlockBuilder {
 public:
  PlainBitMapBlockBuilder()
      : writer_(&buf_) {
    Reset();
  }

  virtual bool IsBlockFull(size_t limit) const override {
    return writer_.bytes_written() > limit;
  }

  virtual int Add(const uint8_t* vals, size_t count) override  {
    for (const uint8_t* val = vals;
         val < vals + count;
         ++val) {
      // TODO (perf) : doing this one bit a time is probably
      //               inefficient.
      writer_.PutValue(*val, 1);
    }
    count_ += count;
    return count;
  }

  virtual Slice Finish(rowid_t ordinal_pos) override {
    InlineEncodeFixed32(&buf_[0], count_);
    InlineEncodeFixed32(&buf_[4], ordinal_pos);
    writer_.Flush(false);
    return Slice(buf_);
  }

  virtual void Reset() override {
    count_ = 0;
    writer_.Clear();
    // Reserve space for a header
    writer_.PutValue(0xdeadbeef, 32);
    writer_.PutValue(0xdeadbeef, 32);
  }

  virtual size_t Count() const override {
    return count_;
  }

  // TODO Implement this method
  virtual CHECKED_STATUS GetFirstKey(void* key) const override {
    return STATUS(NotSupported, "BOOL keys not supported");
  }

 private:
  faststring buf_;
  BitWriter writer_;
  size_t count_;
};


//
// Plain decoder for the BOOL datatype
//
class PlainBitMapBlockDecoder : public BlockDecoder {
 public:
  explicit PlainBitMapBlockDecoder(Slice slice)
      : data_(std::move(slice)),
        parsed_(false),
        num_elems_(0),
        ordinal_pos_base_(0),
        cur_idx_(0) {
  }

  virtual CHECKED_STATUS ParseHeader() override {
    CHECK(!parsed_);

    if (data_.size() < kHeaderSize) {
      return STATUS(Corruption,
          "not enough bytes for header in PlainBitMapBlockDecoder");
    }

    num_elems_ = DecodeFixed32(&data_[0]);
    ordinal_pos_base_ = DecodeFixed32(&data_[4]);

    if (data_.size() != kHeaderSize + BitmapSize(num_elems_))  {
      return STATUS(Corruption,
          strings::Substitute(
              "unexpected data size (expected $0 bytes got $1 bytes).\100 bytes: $2",
              data_.size(), kHeaderSize + BitmapSize(num_elems_),
              HexDump(Slice(data_.data(),
                            data_.size() < 100 ? data_.size() : 100))));
    }

    parsed_ = true;

    reader_ = BitReader(data_.data() + kHeaderSize, data_.size() - kHeaderSize);

    SeekToPositionInBlock(0);

    return Status::OK();
  }

  virtual void SeekToPositionInBlock(uint pos) override {
    CHECK(parsed_) << "Must call ParseHeader()";

    if (PREDICT_FALSE(num_elems_ == 0)) {
      DCHECK_EQ(0, pos);
      return;
    }

    DCHECK_LT(pos, num_elems_);

    reader_.SeekToBit(pos);

    cur_idx_ = pos;
  }

  // TODO : Support BOOL keys
  virtual CHECKED_STATUS SeekAtOrAfterValue(const void *value,
                                    bool *exact_match) override {
    return STATUS(NotSupported, "BOOL keys are not supported!");
  }

  virtual CHECKED_STATUS CopyNextValues(size_t *n, ColumnDataView *dst) override {
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
      bool result = reader_.GetValue(1, data_ptr);
      DCHECK(result);
      remaining--;
      data_ptr++;
    }

    cur_idx_ += bits_to_fetch;
    *n = bits_to_fetch;

    return Status::OK();
  }

  virtual bool HasNext() const override { return cur_idx_ < num_elems_; }

  virtual size_t Count() const override { return num_elems_; }

  virtual size_t GetCurrentIndex() const override { return cur_idx_; }

  virtual rowid_t GetFirstRowId() const override { return ordinal_pos_base_; }

 private:
  enum {
    kHeaderSize = 8
  };

  Slice data_;
  bool parsed_;
  uint32_t num_elems_;
  rowid_t ordinal_pos_base_;
  uint32_t cur_idx_;
  BitReader reader_;
};

} // namespace cfile
} // namespace yb

#endif
