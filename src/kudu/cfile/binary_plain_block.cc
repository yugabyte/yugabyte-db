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

#include "kudu/cfile/binary_plain_block.h"

#include <glog/logging.h>
#include <algorithm>

#include "kudu/cfile/cfile_util.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/common/columnblock.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/util/coding.h"
#include "kudu/util/coding-inl.h"
#include "kudu/util/group_varint-inl.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/memory/arena.h"

namespace kudu {
namespace cfile {

BinaryPlainBlockBuilder::BinaryPlainBlockBuilder(const WriterOptions *options)
  : end_of_data_offset_(0),
    size_estimate_(0),
    options_(options) {
  Reset();
}

void BinaryPlainBlockBuilder::Reset() {
  offsets_.clear();
  buffer_.clear();
  buffer_.resize(kMaxHeaderSize);
  buffer_.reserve(options_->storage_attributes.cfile_block_size);

  size_estimate_ = kMaxHeaderSize;
  end_of_data_offset_ = kMaxHeaderSize;
  finished_ = false;
}

bool BinaryPlainBlockBuilder::IsBlockFull(size_t limit) const {
  return size_estimate_ > limit;
}

Slice BinaryPlainBlockBuilder::Finish(rowid_t ordinal_pos) {
  finished_ = true;

  size_t offsets_pos = buffer_.size();

  // Set up the header
  InlineEncodeFixed32(&buffer_[0], ordinal_pos);
  InlineEncodeFixed32(&buffer_[4], offsets_.size());
  InlineEncodeFixed32(&buffer_[8], offsets_pos);

  // append the offsets, if non-empty
  if (!offsets_.empty()) {
    coding::AppendGroupVarInt32Sequence(&buffer_, 0, &offsets_[0], offsets_.size());
  }

  return Slice(buffer_);
}

int BinaryPlainBlockBuilder::Add(const uint8_t *vals, size_t count) {
  DCHECK(!finished_);
  DCHECK_GT(count, 0);
  size_t i = 0;

  // If the block is full, should stop adding more items.
  while (!IsBlockFull(options_->storage_attributes.cfile_block_size) && i < count) {

    // Every fourth entry needs a gvint selector byte
    // TODO: does it cost a lot to account these things specifically?
    // maybe cheaper to just over-estimate - allocation is cheaper than math?
    if (offsets_.size() % 4 == 0) {
      size_estimate_++;
    }

    const Slice *src = reinterpret_cast<const Slice *>(vals);
    size_t offset = buffer_.size();
    offsets_.push_back(offset);
    size_estimate_ += coding::CalcRequiredBytes32(offset);

    buffer_.append(src->data(), src->size());
    size_estimate_ += src->size();

    i++;
    vals += sizeof(Slice);
  }

  end_of_data_offset_ = buffer_.size();

  return i;
}


size_t BinaryPlainBlockBuilder::Count() const {
  return offsets_.size();
}

Status BinaryPlainBlockBuilder::GetFirstKey(void *key_void) const {
  CHECK(finished_);

  Slice *slice = reinterpret_cast<Slice *>(key_void);

  if (offsets_.empty()) {
    return Status::NotFound("no keys in data block");
  }

  if (PREDICT_FALSE(offsets_.size() == 1)) {
    *slice = Slice(&buffer_[kMaxHeaderSize],
                   end_of_data_offset_ - kMaxHeaderSize);
  } else {
    *slice = Slice(&buffer_[kMaxHeaderSize],
                   offsets_[1] - offsets_[0]);
  }
  return Status::OK();
}

////////////////////////////////////////////////////////////
// Decoding
////////////////////////////////////////////////////////////

BinaryPlainBlockDecoder::BinaryPlainBlockDecoder(Slice slice)
    : data_(std::move(slice)),
      parsed_(false),
      num_elems_(0),
      ordinal_pos_base_(0),
      cur_idx_(0) {
}

Status BinaryPlainBlockDecoder::ParseHeader() {
  CHECK(!parsed_);

  if (data_.size() < kMinHeaderSize) {
    return Status::Corruption(
      strings::Substitute("not enough bytes for header: string block header "
        "size ($0) less than minimum possible header length ($1)",
        data_.size(), kMinHeaderSize));
  }

  // Decode header.
  ordinal_pos_base_  = DecodeFixed32(&data_[0]);
  num_elems_         = DecodeFixed32(&data_[4]);
  size_t offsets_pos = DecodeFixed32(&data_[8]);

  // Sanity check.
  if (offsets_pos > data_.size()) {
    return Status::Corruption(
      StringPrintf("offsets_pos %ld > block size %ld in plain string block",
                   offsets_pos, data_.size()));
  }

  // Decode the string offsets themselves
  const uint8_t *p = data_.data() + offsets_pos;
  const uint8_t *limit = data_.data() + data_.size();

  offsets_.clear();
  offsets_.reserve(num_elems_);

  size_t rem = num_elems_;
  while (rem >= 4) {
    uint32_t ints[4];
    if (p + 16 < limit) {
      p = coding::DecodeGroupVarInt32_SSE(p, &ints[0], &ints[1], &ints[2], &ints[3]);
    } else {
      p = coding::DecodeGroupVarInt32_SlowButSafe(p, &ints[0], &ints[1], &ints[2], &ints[3]);
    }
    if (p > limit) {
      LOG(WARNING) << "bad block: " << HexDump(data_);
      return Status::Corruption(
        StringPrintf("unable to decode offsets in block"));
    }

    offsets_.push_back(ints[0]);
    offsets_.push_back(ints[1]);
    offsets_.push_back(ints[2]);
    offsets_.push_back(ints[3]);
    rem -= 4;
  }

  if (rem > 0) {
    uint32_t ints[4];
    p = coding::DecodeGroupVarInt32_SlowButSafe(p, &ints[0], &ints[1], &ints[2], &ints[3]);
    if (p > limit) {
      LOG(WARNING) << "bad block: " << HexDump(data_);
      return Status::Corruption(
        StringPrintf("unable to decode offsets in block"));
    }

    for (int i = 0; i < rem; i++) {
      offsets_.push_back(ints[i]);
    }
  }

  // Add one extra entry pointing after the last item to make the indexing easier.
  offsets_.push_back(offsets_pos);

  parsed_ = true;

  return Status::OK();
}

void BinaryPlainBlockDecoder::SeekToPositionInBlock(uint pos) {
  if (PREDICT_FALSE(num_elems_ == 0)) {
    DCHECK_EQ(0, pos);
    return;
  }

  DCHECK_LE(pos, num_elems_);
  cur_idx_ = pos;
}

Status BinaryPlainBlockDecoder::SeekAtOrAfterValue(const void *value_void, bool *exact) {
  DCHECK(value_void != nullptr);

  const Slice &target = *reinterpret_cast<const Slice *>(value_void);

  // Binary search in restart array to find the first restart point
  // with a key >= target
  int32_t left = 0;
  int32_t right = num_elems_;
  while (left != right) {
    uint32_t mid = (left + right) / 2;
    Slice mid_key(string_at_index(mid));
    int c = mid_key.compare(target);
    if (c < 0) {
      left = mid + 1;
    } else if (c > 0) {
      right = mid;
    } else {
      cur_idx_ = mid;
      *exact = true;
      return Status::OK();
    }
  }
  *exact = false;
  cur_idx_ = left;
  if (cur_idx_ == num_elems_) {
    return Status::NotFound("after last key in block");
  }

  return Status::OK();
}

Status BinaryPlainBlockDecoder::CopyNextValues(size_t *n, ColumnDataView *dst) {
  DCHECK(parsed_);
  CHECK_EQ(dst->type_info()->physical_type(), BINARY);
  DCHECK_LE(*n, dst->nrows());
  DCHECK_EQ(dst->stride(), sizeof(Slice));

  Arena *out_arena = dst->arena();
  if (PREDICT_FALSE(*n == 0 || cur_idx_ >= num_elems_)) {
    *n = 0;
    return Status::OK();
  }

  size_t max_fetch = std::min(*n, static_cast<size_t>(num_elems_ - cur_idx_));

  Slice *out = reinterpret_cast<Slice *>(dst->data());
  size_t i;
  for (i = 0; i < max_fetch; i++) {
    Slice elem(string_at_index(cur_idx_));

    // TODO: in a lot of cases, we might be able to get away with the decoder
    // owning it and not truly copying. But, we should extend the CopyNextValues
    // API so that the caller can specify if they truly _need_ copies or not.
    CHECK(out_arena->RelocateSlice(elem, out));
    out++;
    cur_idx_++;
  }

  *n = i;
  return Status::OK();
}

} // namespace cfile
} // namespace kudu
