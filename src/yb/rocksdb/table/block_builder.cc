//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// BlockBuilder generates blocks where keys are prefix-compressed:
//
// When we store a key, we drop the prefix shared with the previous
// string.  This helps reduce the space requirement significantly.
// Furthermore, once every K keys, we do not apply the prefix
// compression and store the entire key.  We call this a "restart
// point".  The tail end of the block stores the offsets of all of the
// restart points, and can be used to do a binary search when looking
// for a particular key.  Values are stored as-is (without compression)
// immediately following the corresponding key.
//
// An entry for a particular key-value pair has the form:
//     shared_bytes: varint32
//     unshared_bytes: varint32
//     value_length: varint32
//     key_delta: char[unshared_bytes]
//     value: char[value_length]
// shared_bytes == 0 for restart points.
//
// The trailer of the block has the form:
//     restarts: uint32[num_restarts]
//     num_restarts: uint32
// restarts[i] contains the offset within the block of the ith restart point.

#include "yb/rocksdb/table/block_builder.h"

#include <assert.h>

#include <algorithm>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/table/block_builder_internal.h"
#include "yb/rocksdb/util/coding.h"

#include "yb/util/string_util.h"

namespace rocksdb {

BlockBuilder::BlockBuilder(
    int block_restart_interval, const KeyValueEncodingFormat key_value_encoding_format,
    const bool use_delta_encoding)
    : block_restart_interval_(block_restart_interval),
      use_delta_encoding_(use_delta_encoding),
      key_value_encoding_format_(key_value_encoding_format),
      restarts_(),
      counter_(0),
      finished_(false) {
  assert(block_restart_interval_ >= 1);
  restarts_.push_back(0);       // First restart point is at offset 0
}

void BlockBuilder::Reset() {
  buffer_.clear();
  restarts_.clear();
  restarts_.push_back(0);       // First restart point is at offset 0
  counter_ = 0;
  finished_ = false;
  last_key_.clear();
}

size_t BlockBuilder::CurrentSizeEstimate() const {
  size_t size = buffer_.size(); // Raw data buffer.
  if (!finished_) {
    // Restarts haven't been flushed to buffer yet.
    size += restarts_.size() * sizeof(uint32_t) +    // Restart array.
            sizeof(uint32_t);                        // Restart array length.
  }
  return size;
}

size_t BlockBuilder::EstimateSizeAfterKV(const Slice& key, const Slice& value)
  const {
  size_t estimate = CurrentSizeEstimate();
  estimate += key.size() + value.size();
  if (counter_ >= block_restart_interval_) {
    estimate += sizeof(uint32_t); // a new restart entry.
  }

  estimate += sizeof(int32_t); // varint for shared prefix length.
  estimate += VarintLength(key.size()); // varint for key length.
  estimate += VarintLength(value.size()); // varint for value length.

  return estimate;
}

size_t BlockBuilder::NumKeys() const {
  return restarts_.size() * block_restart_interval_ + counter_;
}

namespace {

// Find maximum common substring starting at the same position in both input strings.
// Returns pair of substring index and size.
inline std::pair<size_t, size_t> FindMaxSharedSubstringAtTheSamePos(
    const uint8_t* lhs, const uint8_t* rhs, const size_t min_size) {
  size_t max_shared_size = 0;
  const auto* offset_of_max_shared = lhs;
  {
    size_t shared_size = 0;
    const auto lhs_limit = lhs + min_size;
    for (auto *p1 = lhs, *p2 = rhs; p1 < lhs_limit; ++p1, ++p2) {
      if (*p1 == *p2) {
        shared_size++;
      } else {
        if (shared_size > max_shared_size) {
          max_shared_size = shared_size;
          offset_of_max_shared = p1 - shared_size;
        }
        shared_size = 0;
      }
    }
  }
  DCHECK(strings::memeq(offset_of_max_shared, offset_of_max_shared - lhs + rhs,
      max_shared_size));
  return std::make_pair(offset_of_max_shared - lhs, max_shared_size);
}

inline ComponentSizes NothingToShare(
    const size_t prev_key_non_shared_1_size, const size_t non_shared_1_size) {
  return ComponentSizes {
    .prev_key_non_shared_1_size = prev_key_non_shared_1_size,
    .non_shared_1_size = non_shared_1_size,
    .shared_middle_size = 0,
    .prev_key_non_shared_2_size = 0,
    .non_shared_2_size = 0,
  };
}

// Trying to find max shared_middle matching the following pattern:
//   lhs = <lhs_non_shared_1><shared_middle><lhs_non_shared_2>
//   rhs = <rhs_non_shared_1><shared_middle><rhs_non_shared_2>
// Returns sizes of the above components.
//
// For runtime efficiency we assume that either lhs_key_non_shared_1 and rhs_non_shared_1 or
// lhs_non_shared_2 and rhs_non_shared_2 are of the same size (and this happens in most cases for
// docdb encoded keys), so we can find shared_middle in linear time by trying to search at the same
// positions in maximum prefixes and maximum suffixes of lhs and rhs.
inline ComponentSizes FindMaxSharedMiddle(const Slice& lhs, const Slice& rhs) {
  const auto lhs_size = lhs.size();
  const auto rhs_size = rhs.size();

  size_t min_length;
  std::pair<size_t, size_t> max_shared;
  bool from_left = true;
  if (lhs_size == rhs_size) {
    min_length = rhs_size;
    max_shared = FindMaxSharedSubstringAtTheSamePos(lhs.data(), rhs.data(), min_length);
  } else {
    const uint8_t* lhs_suffix_start;
    const uint8_t* rhs_suffix_start;
    if (lhs_size > rhs_size) {
      min_length = rhs_size;
      lhs_suffix_start = lhs.end() - min_length;
      rhs_suffix_start = rhs.data();
    } else {
      min_length = lhs_size;
      lhs_suffix_start = lhs.data();
      rhs_suffix_start = rhs.end() - min_length;
    }
    max_shared = FindMaxSharedSubstringAtTheSamePos(lhs.data(), rhs.data(), min_length);

    // Search max shared substring in the max suffixes of the same size.
    const auto max_shared_from_right = FindMaxSharedSubstringAtTheSamePos(
        lhs_suffix_start, rhs_suffix_start, min_length);
    if (max_shared_from_right.second > max_shared.second) {
      from_left = false;
      max_shared = max_shared_from_right;
    }
  }

  if (max_shared.second == 0) {
    return NothingToShare(lhs.size(), rhs.size());
  }

  if (from_left) {
    const auto non_shared_1_plus_shared_middle_size = max_shared.first + max_shared.second;
    return ComponentSizes {
      .prev_key_non_shared_1_size = max_shared.first,
      .non_shared_1_size = max_shared.first,
      .shared_middle_size = max_shared.second,
      .prev_key_non_shared_2_size = lhs_size - non_shared_1_plus_shared_middle_size,
      .non_shared_2_size = rhs_size - non_shared_1_plus_shared_middle_size,
    };
  } else {
    const auto shared_middle_plus_non_shared_2_size = min_length - max_shared.first;
    const auto non_shared_2_size = shared_middle_plus_non_shared_2_size - max_shared.second;
    return ComponentSizes {
      .prev_key_non_shared_1_size = lhs_size - shared_middle_plus_non_shared_2_size,
      .non_shared_1_size = rhs_size - shared_middle_plus_non_shared_2_size,
      .shared_middle_size = max_shared.second,
      .prev_key_non_shared_2_size = non_shared_2_size,
      .non_shared_2_size = non_shared_2_size,
    };
  }
}

inline void CalculateLastInternalComponentReuse(
    const Slice& prev_key, const Slice& key, const size_t min_length,
    const size_t shared_prefix_size, size_t* last_internal_component_reuse_size,
    bool* is_last_internal_component_inc) {
  DCHECK_EQ(min_length, std::min(prev_key.size(), key.size()));
  if (min_length >= shared_prefix_size + kLastInternalComponentSize) {
    const auto prev_last_internal_comp =
        DecodeFixed64(prev_key.end() - kLastInternalComponentSize);
    const auto cur_last_internal_comp = DecodeFixed64(key.end() - kLastInternalComponentSize);
    if (cur_last_internal_comp == prev_last_internal_comp + 0x100) {
      *is_last_internal_component_inc = true;
      *last_internal_component_reuse_size = kLastInternalComponentSize;
    } else {
      *is_last_internal_component_inc = false;
      if (cur_last_internal_comp == prev_last_internal_comp) {
        *last_internal_component_reuse_size = kLastInternalComponentSize;
      } else {
        *last_internal_component_reuse_size = 0;
      }
    }
  } else {
    *is_last_internal_component_inc = false;
    *last_internal_component_reuse_size = 0;
  }
}

// Specific delta encoding optimized for the following keys pattern:
//
// Prev key:
// <shared_prefix>[<prev_key_non_shared_1>[<shared_middle>[<prev_key_non_shared_2>]]]
//   [<last_internal_component_to_reuse>]
//
// Current key:
// <shared_prefix>[<non_shared_1>[<shared_middle>[<non_shared_2>]]]
//   [<last_internal_component_to_reuse> (optionally incremented)]
//
// <last_internal_component_to_reuse> is either empty (that means not reused) or contains rocksdb
// internal key suffix (see rocksdb/db/dbformat.cc - PackSequenceAndType) of size
// kLastInternalComponentSize in case it is reused as is or sequence number is incremented from
// prev key.
//
// Based on prev_key and key calculates sizes of shared/non-shared key components mentioned above.
// Tries to maximize shared_prefix and shared_middle sizes.
class ThreeSharedPartsEncoder {
 public:
  ThreeSharedPartsEncoder(const Slice& prev_key, const Slice& key)
      : prev_key_(prev_key),
        prev_key_size_(prev_key.size()),
        key_(key),
        key_size_(key.size()),
        rest_components_sizes_{
            .prev_key_non_shared_1_size = prev_key_size_,
            .non_shared_1_size = key_size_,
        } {}

  inline void FindComponents(const size_t min_length, const size_t shared_prefix_size) {
    DCHECK_EQ(min_length, std::min(prev_key_.size(), key_.size()));
    CalculateLastInternalComponentReuse(
        prev_key_, key_, min_length, shared_prefix_size, &last_internal_component_reuse_size_,
        &is_last_internal_component_inc_);

    const auto prev_key_between_shared = Slice(
        prev_key_.data() + shared_prefix_size,
        prev_key_.end() - last_internal_component_reuse_size_);
    const auto cur_key_between_shared =
        Slice(key_.data() + shared_prefix_size, key_.end() - last_internal_component_reuse_size_);

    rest_components_sizes_ = FindMaxSharedMiddle(prev_key_between_shared, cur_key_between_shared);
#ifndef DEBUG
    auto check_result =
        rest_components_sizes_.DebugVerify(prev_key_between_shared, cur_key_between_shared);
    if (!check_result.ok()) {
      LOG(FATAL) << check_result << ": " << rest_components_sizes_.ToString();
    }
#endif
  }

  // Encodes components sizes + non-shared key parts and value into buffer.
  // See EncodeThreeSharedPartsSizes for description of how we encode component sizes.
  inline void Encode(size_t shared_prefix_size, const Slice& value, std::string* buffer) {
    const auto value_size = value.size();

    EncodeThreeSharedPartsSizes(
        shared_prefix_size, last_internal_component_reuse_size_, is_last_internal_component_inc_,
        rest_components_sizes_, prev_key_size_, key_size_, value_size, buffer);

    yb::EnlargeBufferIfNeeded(
        buffer, buffer->size() + rest_components_sizes_.non_shared_1_size +
                    rest_components_sizes_.non_shared_2_size + value_size);

    // Add key deltas to buffer followed by value.
    buffer->append(key_.cdata() + shared_prefix_size, rest_components_sizes_.non_shared_1_size);
    buffer->append(
        key_.cend() - last_internal_component_reuse_size_ -
            rest_components_sizes_.non_shared_2_size,
        rest_components_sizes_.non_shared_2_size);
    buffer->append(value.cdata(), value_size);
  }

 private:
  const Slice prev_key_;
  const size_t prev_key_size_ = 0;
  const Slice key_;
  const size_t key_size_ = 0;

  size_t last_internal_component_reuse_size_ = 0;
  bool is_last_internal_component_inc_ = false;
  ComponentSizes rest_components_sizes_;
};

} // namespace

Slice BlockBuilder::Finish() {
  // Append restart array
  for (size_t i = 0; i < restarts_.size(); i++) {
    PutFixed32(&buffer_, restarts_[i]);
  }
  PutFixed32(&buffer_, static_cast<uint32_t>(restarts_.size()));
  finished_ = true;
  return Slice(buffer_);
}

void BlockBuilder::Add(const Slice& key, const Slice& value) {
  const Slice prev_key_piece(last_key_);
  assert(!finished_);
  assert(counter_ <= block_restart_interval_);

  size_t shared_prefix_size = 0; // number of prefix bytes shared with prev key

  // Initial values to be used on restarts or when delta encoding is off.
  ThreeSharedPartsEncoder three_shared_parts_encoder(prev_key_piece, key);

  if (counter_ >= block_restart_interval_) {
    // Restart compression
    restarts_.push_back(static_cast<uint32_t>(buffer_.size()));
    counter_ = 0;
  } else if (use_delta_encoding_) {
    // See how much sharing to do with previous string
    const size_t min_length = std::min(prev_key_piece.size(), key.size());
    shared_prefix_size =
        strings::MemoryDifferencePos(prev_key_piece.data(), key.data(), min_length);
    bool valid_encoding_format = false;
    switch (key_value_encoding_format_) {
      case KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix:
        valid_encoding_format = true;
        break;
      case KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts:
        valid_encoding_format = true;
        three_shared_parts_encoder.FindComponents(min_length, shared_prefix_size);
        break;
    }
    if (!valid_encoding_format) {
      FATAL_INVALID_ENUM_VALUE(KeyValueEncodingFormat, key_value_encoding_format_);
    }
  }

  DVLOG_WITH_FUNC(4) << "key: " << Slice(key).ToDebugHexString() << " size: " << key.size()
                    << " offset: " << buffer_.size() << " counter: " << counter_;

  const size_t after_shared_prefix_size = key.size() - shared_prefix_size;

  switch (key_value_encoding_format_) {
    // If we don't use key delta encoding, we still use the same encoding format, but shared
    // components sizes are 0.
    case KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix: {
      // Add "<shared_prefix_size><after_shared_prefix_size><value_size>" to buffer_
      PutVarint32(&buffer_, static_cast<uint32_t>(shared_prefix_size));
      PutVarint32(&buffer_, static_cast<uint32_t>(after_shared_prefix_size));
      PutVarint32(&buffer_, static_cast<uint32_t>(value.size()));

      // Add string delta to buffer_ followed by value
      buffer_.append(key.cdata() + shared_prefix_size, after_shared_prefix_size);
      buffer_.append(value.cdata(), value.size());
      break;
    }
    case KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts: {
      three_shared_parts_encoder.Encode(shared_prefix_size, value, &buffer_);
      break;
    }
  }

  // Update state
  last_key_.resize(shared_prefix_size);
  last_key_.append(key.cdata() + shared_prefix_size, after_shared_prefix_size);

  assert(Slice(last_key_) == key);
  counter_++;
}

}  // namespace rocksdb
