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
#include "yb/rocksdb/util/coding.h"

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
  Slice last_key_piece(last_key_);
  assert(!finished_);
  assert(counter_ <= block_restart_interval_);
  size_t shared = 0;  // number of bytes shared with prev key
  if (counter_ >= block_restart_interval_) {
    // Restart compression
    restarts_.push_back(static_cast<uint32_t>(buffer_.size()));
    counter_ = 0;
  } else if (use_delta_encoding_) {
    bool valid_encoding_format = false;
    switch (key_value_encoding_format_) {
      case KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix:
        valid_encoding_format = true;
        break;
    }
    if (!valid_encoding_format) {
      FATAL_INVALID_ENUM_VALUE(KeyValueEncodingFormat, key_value_encoding_format_);
    }
    // See how much sharing to do with previous string
    const size_t min_length = std::min(last_key_piece.size(), key.size());
    while ((shared < min_length) && (last_key_piece[shared] == key[shared])) {
      shared++;
    }
  }
  const size_t non_shared = key.size() - shared;

  // Add "<shared><non_shared><value_size>" to buffer_
  PutVarint32(&buffer_, static_cast<uint32_t>(shared));
  PutVarint32(&buffer_, static_cast<uint32_t>(non_shared));
  PutVarint32(&buffer_, static_cast<uint32_t>(value.size()));

  // Add string delta to buffer_ followed by value
  buffer_.append(key.cdata() + shared, non_shared);
  buffer_.append(value.cdata(), value.size());

  // Update state
  last_key_.resize(shared);
  last_key_.append(key.cdata() + shared, non_shared);
  assert(Slice(last_key_) == key);
  counter_++;
}

}  // namespace rocksdb
