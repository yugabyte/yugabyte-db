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

#include "kudu/cfile/binary_prefix_block.h"

#include <algorithm>
#include <string>

#include "kudu/cfile/cfile_writer.h"
#include "kudu/common/columnblock.h"
#include "kudu/gutil/port.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/coding.h"
#include "kudu/util/coding-inl.h"
#include "kudu/util/group_varint-inl.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/slice.h"

namespace kudu {
namespace cfile {

using kudu::coding::AppendGroupVarInt32;
using strings::Substitute;

////////////////////////////////////////////////////////////
// Utility code used by both encoding and decoding
////////////////////////////////////////////////////////////

static const uint8_t *DecodeEntryLengths(
  const uint8_t *ptr, const uint8_t *limit,
  uint32_t *shared, uint32_t *non_shared) {

  if ((ptr = GetVarint32Ptr(ptr, limit, shared)) == nullptr) return nullptr;
  if ((ptr = GetVarint32Ptr(ptr, limit, non_shared)) == nullptr) return nullptr;
  if (limit - ptr < *non_shared) {
    return nullptr;
  }

  return ptr;
}

////////////////////////////////////////////////////////////
// StringPrefixBlockBuilder encoding
////////////////////////////////////////////////////////////

BinaryPrefixBlockBuilder::BinaryPrefixBlockBuilder(const WriterOptions *options)
  : val_count_(0),
    vals_since_restart_(0),
    finished_(false),
    options_(options) {
  Reset();
}

void BinaryPrefixBlockBuilder::Reset() {
  finished_ = false;
  val_count_ = 0;
  vals_since_restart_ = 0;

  buffer_.clear();
  buffer_.resize(kHeaderReservedLength);
  buffer_.reserve(options_->storage_attributes.cfile_block_size);

  restarts_.clear();
  last_val_.clear();
}

bool BinaryPrefixBlockBuilder::IsBlockFull(size_t limit) const {
  // TODO: take restarts size into account
  return buffer_.size() > limit;
}

Slice BinaryPrefixBlockBuilder::Finish(rowid_t ordinal_pos) {
  CHECK(!finished_) << "already finished";
  DCHECK_GE(buffer_.size(), kHeaderReservedLength);

  faststring header(kHeaderReservedLength);

  AppendGroupVarInt32(&header, val_count_, ordinal_pos,
                      options_->block_restart_interval, 0);

  int header_encoded_len = header.size();

  // Copy the header into the buffer at the right spot.
  // Since the header is likely shorter than the amount of space
  // reserved for it, need to find where it fits:
  int header_offset = kHeaderReservedLength - header_encoded_len;
  DCHECK_GE(header_offset, 0);
  uint8_t *header_dst = buffer_.data() + header_offset;
  strings::memcpy_inlined(header_dst, header.data(), header_encoded_len);

  // Serialize the restart points.
  // Note that the values stored in restarts_ are relative to the
  // start of the *buffer*, which is not the same as the start of
  // the block. So, we must subtract the header offset from each.
  buffer_.reserve(buffer_.size()
                  + restarts_.size() * sizeof(uint32_t) // the data
                  + sizeof(uint32_t)); // the restart count);
  for (uint32_t restart : restarts_) {
    DCHECK_GE(static_cast<int>(restart), header_offset);
    uint32_t relative_to_block = restart - header_offset;
    VLOG(2) << "appending restart " << relative_to_block;
    InlinePutFixed32(&buffer_, relative_to_block);
  }
  InlinePutFixed32(&buffer_, restarts_.size());

  finished_ = true;
  return Slice(&buffer_[header_offset], buffer_.size() - header_offset);
}

int BinaryPrefixBlockBuilder::Add(const uint8_t *vals, size_t count) {
  DCHECK_GT(count, 0);
  DCHECK(!finished_);
  DCHECK_LE(vals_since_restart_, options_->block_restart_interval);

  int added = 0;
  const Slice* slices = reinterpret_cast<const Slice*>(vals);
  Slice prev_val(last_val_);
  // We generate a static call to IsBlockFull() to avoid the vtable lookup
  // in this hot path.
  while (!BinaryPrefixBlockBuilder::IsBlockFull(options_->storage_attributes.cfile_block_size) &&
         added < count) {
    const Slice val = slices[added];

    int old_size = buffer_.size();
    buffer_.resize(old_size + 5 * 2 + val.size());
    uint8_t* dst_p = &buffer_[old_size];

    size_t shared = 0;
    if (vals_since_restart_ < options_->block_restart_interval) {
      // See how much sharing to do with previous string
      shared = CommonPrefixLength(prev_val, val);
    } else {
      // Restart compression
      restarts_.push_back(old_size);
      vals_since_restart_ = 0;
    }
    const size_t non_shared = val.size() - shared;

    // Add "<shared><non_shared>" to buffer_
    dst_p = InlineEncodeVarint32(dst_p, shared);
    dst_p = InlineEncodeVarint32(dst_p, non_shared);

    // Add string delta to buffer_
    memcpy(dst_p, val.data() + shared, non_shared);
    dst_p += non_shared;

    // Chop off the extra size on the end of the buffer.
    buffer_.resize(dst_p - &buffer_[0]);

    // Update state
    prev_val = val;
    added++;
    vals_since_restart_++;
  }

  last_val_.assign_copy(prev_val.data(), prev_val.size());
  val_count_ += added;

  return added;
}

size_t BinaryPrefixBlockBuilder::Count() const {
  return val_count_;
}


Status BinaryPrefixBlockBuilder::GetFirstKey(void *key) const {
  if (val_count_ == 0) {
    return Status::NotFound("no keys in data block");
  }

  const uint8_t *p = &buffer_[kHeaderReservedLength];
  uint32_t shared, non_shared;
  p = DecodeEntryLengths(p, &buffer_[buffer_.size()], &shared, &non_shared);
  if (p == nullptr) {
    return Status::Corruption("Could not decode first entry in string block");
  }

  CHECK(shared == 0) << "first entry in string block had a non-zero 'shared': "
                     << shared;

  *reinterpret_cast<Slice *>(key) = Slice(p, non_shared);
  return Status::OK();
}

size_t BinaryPrefixBlockBuilder::CommonPrefixLength(const Slice& slice_a,
                                                    const Slice& slice_b) {
  // This implementation is modeled after strings::fastmemcmp_inlined().
  int len = std::min(slice_a.size(), slice_b.size());
  const uint8_t* a = slice_a.data();
  const uint8_t* b = slice_b.data();
  const uint8_t* a_limit = a + len;

  const size_t sizeof_uint64 = sizeof(uint64_t);
  // Move forward 8 bytes at a time until finding an unequal portion.
  while (a + sizeof_uint64 <= a_limit &&
         UNALIGNED_LOAD64(a) == UNALIGNED_LOAD64(b)) {
    a += sizeof_uint64;
    b += sizeof_uint64;
  }

  // Same, 4 bytes at a time.
  const size_t sizeof_uint32 = sizeof(uint32_t);
  while (a + sizeof_uint32 <= a_limit &&
         UNALIGNED_LOAD32(a) == UNALIGNED_LOAD32(b)) {
    a += sizeof_uint32;
    b += sizeof_uint32;
  }

  // Now one byte at a time. We could do a 2-bytes-at-a-time loop,
  // but we're following the example of fastmemcmp_inlined(). The benefit of
  // 2-at-a-time likely doesn't outweigh the cost of added code size.
  while (a < a_limit &&
         *a == *b) {
    a++;
    b++;
  }

  return a - slice_a.data();
}

////////////////////////////////////////////////////////////
// StringPrefixBlockDecoder
////////////////////////////////////////////////////////////

BinaryPrefixBlockDecoder::BinaryPrefixBlockDecoder(Slice slice)
    : data_(std::move(slice)),
      parsed_(false),
      num_elems_(0),
      ordinal_pos_base_(0),
      num_restarts_(0),
      restarts_(nullptr),
      data_start_(nullptr),
      cur_idx_(0),
      next_ptr_(nullptr) {
}

Status BinaryPrefixBlockDecoder::ParseHeader() {
  // First parse the actual header.
  uint32_t unused;

  // Make sure the Slice we are referring to is at least the size of the
  // minimum possible header
  if (PREDICT_FALSE(data_.size() < kMinHeaderSize)) {
    return Status::Corruption(
      strings::Substitute("not enough bytes for header: string block header "
        "size ($0) less than minimum possible header length ($1)",
        data_.size(), kMinHeaderSize));
    // TODO include hexdump
  }

  // Make sure the actual size of the group varints in the Slice we are
  // referring to is as big as it claims to be
  size_t header_size = coding::DecodeGroupVarInt32_GetGroupSize(data_.data());
  if (PREDICT_FALSE(data_.size() < header_size)) {
    return Status::Corruption(
      strings::Substitute("string block header size ($0) less than length "
        "from in header ($1)", data_.size(), header_size));
    // TODO include hexdump
  }

  // We should have enough space in the Slice to decode the group varints
  // safely now
  data_start_ =
    coding::DecodeGroupVarInt32_SlowButSafe(
      data_.data(),
      &num_elems_, &ordinal_pos_base_,
      &restart_interval_, &unused);

  // Then the footer, which points us to the restarts array
  num_restarts_ = DecodeFixed32(
    data_.data() + data_.size() - sizeof(uint32_t));

  // sanity check the restarts size
  uint32_t restarts_size = num_restarts_ * sizeof(uint32_t);
  if (restarts_size > data_.size()) {
    return Status::Corruption(
      StringPrintf("restart count %d too big to fit in block size %d",
                   num_restarts_, static_cast<int>(data_.size())));
  }

  // TODO: check relationship between num_elems, num_restarts_,
  // and restart_interval_

  restarts_ = reinterpret_cast<const uint32_t *>(
    data_.data() + data_.size()
    - sizeof(uint32_t) // rewind before the restart length
    - restarts_size);

  SeekToStart();
  parsed_ = true;
  return Status::OK();
}

void BinaryPrefixBlockDecoder::SeekToStart() {
  SeekToRestartPoint(0);
}

void BinaryPrefixBlockDecoder::SeekToPositionInBlock(uint pos) {
  if (PREDICT_FALSE(num_elems_ == 0)) {
    DCHECK_EQ(0, pos);
    return;
  }

  DCHECK_LE(pos, num_elems_);

  int target_restart = pos/restart_interval_;
  SeekToRestartPoint(target_restart);

  // Seek forward to the right index

  // TODO: Seek calls should return a Status
  CHECK_OK(SkipForward(pos - cur_idx_));
  DCHECK_EQ(cur_idx_, pos);
}

// Get the pointer to the entry corresponding to the given restart
// point. Note that the restart points in the file do not include
// the '0' restart point, since that is simply the beginning of
// the data and hence a waste of space. So, 'idx' may range from
// 0 (first record) through num_restarts_ (last recorded restart point)
const uint8_t * BinaryPrefixBlockDecoder::GetRestartPoint(uint32_t idx) const {
  DCHECK_LE(idx, num_restarts_);

  if (PREDICT_TRUE(idx > 0)) {
    return data_.data() + restarts_[idx - 1];
  } else {
    return data_start_;
  }
}

// Note: see GetRestartPoint() for 'idx' semantics
void BinaryPrefixBlockDecoder::SeekToRestartPoint(uint32_t idx) {
  if (PREDICT_FALSE(num_elems_ == 0)) {
    DCHECK_EQ(0, idx);
    return;
  }

  next_ptr_ = GetRestartPoint(idx);
  cur_idx_ = idx * restart_interval_;
  CHECK_OK(ParseNextValue()); // TODO: handle corrupted blocks
}

Status BinaryPrefixBlockDecoder::SeekAtOrAfterValue(const void *value_void,
                                              bool *exact_match) {
  DCHECK(value_void != nullptr);

  const Slice &target = *reinterpret_cast<const Slice *>(value_void);

  // Binary search in restart array to find the first restart point
  // with a key >= target
  int32_t left = 0;
  int32_t right = num_restarts_;
  while (left < right) {
    uint32_t mid = (left + right + 1) / 2;
    const uint8_t *entry = GetRestartPoint(mid);
    uint32_t shared, non_shared;
    const uint8_t *key_ptr = DecodeEntryLengths(entry, &shared, &non_shared);
    if (key_ptr == nullptr || (shared != 0)) {
      string err =
        StringPrintf("bad entry restart=%d shared=%d\n", mid, shared) +
        HexDump(Slice(entry, 16));
      return Status::Corruption(err);
    }
    Slice mid_key(key_ptr, non_shared);
    if (mid_key.compare(target) < 0) {
      // Key at "mid" is smaller than "target".  Therefore all
      // blocks before "mid" are uninteresting.
      left = mid;
    } else {
      // Key at "mid" is >= "target".  Therefore all blocks at or
      // after "mid" are uninteresting.
      right = mid - 1;
    }
  }

  // Linear search (within restart block) for first key >= target
  SeekToRestartPoint(left);

  while (true) {
#ifndef NDEBUG
    VLOG(3) << "loop iter:\n"
            << "cur_idx = " << cur_idx_ << "\n"
            << "target  =" << target.ToString() << "\n"
            << "cur_val_=" << Slice(cur_val_).ToString();
#endif
    int cmp = Slice(cur_val_).compare(target);
    if (cmp >= 0) {
      *exact_match = (cmp == 0);
      return Status::OK();
    }
    RETURN_NOT_OK(ParseNextValue());
    cur_idx_++;
  }
}

Status BinaryPrefixBlockDecoder::CopyNextValues(size_t *n, ColumnDataView *dst) {
  DCHECK(parsed_);
  CHECK_EQ(dst->type_info()->physical_type(), BINARY);

  DCHECK_EQ(dst->stride(), sizeof(Slice));
  DCHECK_LE(*n, dst->nrows());

  Arena *out_arena = dst->arena();
  Slice *out = reinterpret_cast<Slice *>(dst->data());

  if (PREDICT_FALSE(*n == 0 || cur_idx_ >= num_elems_)) {
    *n = 0;
    return Status::OK();
  }

  size_t i = 0;
  size_t max_fetch = std::min(*n, static_cast<size_t>(num_elems_ - cur_idx_));

  // Grab the first row, which we've cached from the last call or seek.
  const uint8_t *out_data = out_arena->AddSlice(cur_val_);
  if (PREDICT_FALSE(out_data == nullptr)) {
    return Status::IOError(
      "Out of memory",
      StringPrintf("Failed to allocate %d bytes in output arena",
                   static_cast<int>(cur_val_.size())));
  }

  // Put a slice to it in the output array
  Slice prev_val(out_data, cur_val_.size());
  *out++ = prev_val;
  i++;
  cur_idx_++;

  #ifndef NDEBUG
  cur_val_.assign_copy("INVALID");
  #endif

  // Now iterate pulling more rows from the block, decoding relative
  // to the previous value.

  for (; i < max_fetch; i++) {
    Slice copied;
    RETURN_NOT_OK(ParseNextIntoArena(prev_val, dst->arena(), &copied));
    *out++  = copied;
    prev_val = copied;
    cur_idx_++;
  }

  // Fetch the next value to be returned, using the last value we fetched
  // for the delta.
  cur_val_.assign_copy(prev_val.data(), prev_val.size());
  if (cur_idx_ < num_elems_) {
    RETURN_NOT_OK(ParseNextValue());
  } else {
    next_ptr_ = nullptr;
  }

  *n = i;
  return Status::OK();
}

// Decode the lengths pointed to by 'ptr', doing bounds checking.
//
// Returns a pointer to where the value itself starts.
// Returns NULL if the varints themselves, or the value that
// they prefix extend past the end of the block data.
const uint8_t *BinaryPrefixBlockDecoder::DecodeEntryLengths(
  const uint8_t *ptr, uint32_t *shared, uint32_t *non_shared) const {

  // data ends where the restart info begins
  const uint8_t *limit = reinterpret_cast<const uint8_t *>(restarts_);
  return kudu::cfile::DecodeEntryLengths(ptr, limit, shared, non_shared);
}

Status BinaryPrefixBlockDecoder::SkipForward(int n) {
  DCHECK_LE(cur_idx_ + n, num_elems_) <<
    "skip(" << n << ") curidx=" << cur_idx_
            << " num_elems=" << num_elems_;

  // If we're seeking exactly to the end of the data, we don't
  // need to actually prepare the next value (in fact, it would
  // crash). So, short-circuit here.
  if (PREDICT_FALSE(cur_idx_ + n == num_elems_)) {
    cur_idx_ += n;
    return Status::OK();
  }

  // Probably a faster way to implement this using restarts,
  for (int i = 0; i < n; i++) {
    RETURN_NOT_OK(ParseNextValue());
    cur_idx_++;
  }
  return Status::OK();
}

Status BinaryPrefixBlockDecoder::CheckNextPtr() {
  DCHECK(next_ptr_ != nullptr);

  if (PREDICT_FALSE(next_ptr_ == reinterpret_cast<const uint8_t *>(restarts_))) {
    DCHECK_EQ(cur_idx_, num_elems_ - 1);
    return Status::NotFound("Trying to parse past end of array");
  }
  return Status::OK();
}

inline Status BinaryPrefixBlockDecoder::ParseNextIntoArena(Slice prev_val,
                                                           Arena *dst,
                                                           Slice *copied) {
  RETURN_NOT_OK(CheckNextPtr());
  uint32_t shared, non_shared;
  const uint8_t *val_delta = DecodeEntryLengths(next_ptr_, &shared, &non_shared);
  if (val_delta == nullptr) {
    return Status::Corruption(
      StringPrintf("Could not decode value length data at idx %d",
                   cur_idx_));
  }

  DCHECK_LE(shared, prev_val.size())
    << "Spcified longer shared amount than previous key length";

  uint8_t *buf = reinterpret_cast<uint8_t *>(dst->AllocateBytes(non_shared + shared));
  strings::memcpy_inlined(buf, prev_val.data(), shared);
  strings::memcpy_inlined(buf + shared, val_delta, non_shared);

  *copied = Slice(buf, non_shared + shared);
  next_ptr_ = val_delta + non_shared;
  return Status::OK();
}

// Parses the data pointed to by next_ptr_ and stores it in cur_val_
// Advances next_ptr_ to point to the following values.
// Does not modify cur_idx_
inline Status BinaryPrefixBlockDecoder::ParseNextValue() {
  RETURN_NOT_OK(CheckNextPtr());

  uint32_t shared, non_shared;
  const uint8_t *val_delta = DecodeEntryLengths(next_ptr_, &shared, &non_shared);
  if (val_delta == nullptr) {
    return Status::Corruption(
      StringPrintf("Could not decode value length data at idx %d",
                   cur_idx_));
  }

  // Chop the current key to the length that is shared with the next
  // key, then append the delta portion.
  DCHECK_LE(shared, cur_val_.size())
    << "Specified longer shared amount than previous key length";

  cur_val_.resize(shared);
  cur_val_.append(val_delta, non_shared);

  DCHECK_EQ(cur_val_.size(), shared + non_shared);

  next_ptr_ = val_delta + non_shared;
  return Status::OK();
}

} // namespace cfile
} // namespace kudu
