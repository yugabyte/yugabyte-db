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

#include "yb/rocksdb/table/full_filter_block.h"

#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/util/perf_context_imp.h"

namespace rocksdb {

FullFilterBlockBuilder::FullFilterBlockBuilder(
    const SliceTransform* prefix_extractor, bool whole_key_filtering,
    FilterBitsBuilder* filter_bits_builder)
    : prefix_extractor_(prefix_extractor),
      whole_key_filtering_(whole_key_filtering),
      num_added_(0) {
  assert(filter_bits_builder != nullptr);
  filter_bits_builder_.reset(filter_bits_builder);
}

void FullFilterBlockBuilder::Add(const Slice& key) {
  if (whole_key_filtering_) {
    AddKey(key);
  }
  if (prefix_extractor_ && prefix_extractor_->InDomain(key)) {
    AddPrefix(key);
  }
}

// Add key to filter if needed
inline void FullFilterBlockBuilder::AddKey(const Slice& key) {
  filter_bits_builder_->AddKey(key);
  num_added_++;
}

// Add prefix to filter if needed
inline void FullFilterBlockBuilder::AddPrefix(const Slice& key) {
  Slice prefix = prefix_extractor_->Transform(key);
  filter_bits_builder_->AddKey(prefix);
  num_added_++;
}

Slice FullFilterBlockBuilder::Finish() {
  if (num_added_ != 0) {
    num_added_ = 0;
    return filter_bits_builder_->Finish(&filter_data_);
  }
  return Slice();
}

FullFilterBlockReader::FullFilterBlockReader(
    const SliceTransform* prefix_extractor, bool whole_key_filtering,
    const Slice& contents, FilterBitsReader* filter_bits_reader)
    : prefix_extractor_(prefix_extractor),
      whole_key_filtering_(whole_key_filtering),
      contents_(contents) {
  assert(filter_bits_reader != nullptr);
  filter_bits_reader_.reset(filter_bits_reader);
}

FullFilterBlockReader::FullFilterBlockReader(
    const SliceTransform* prefix_extractor, bool whole_key_filtering,
    BlockContents&& contents, FilterBitsReader* filter_bits_reader)
    : FullFilterBlockReader(prefix_extractor, whole_key_filtering,
                            contents.data, filter_bits_reader) {
  block_contents_ = std::move(contents);
}

bool FullFilterBlockReader::KeyMayMatch(Slice key, uint64_t block_offset) {
  assert(block_offset == kNotValid);
  if (!whole_key_filtering_) {
    return true;
  }
  return MayMatch(key);
}

bool FullFilterBlockReader::PrefixMayMatch(const Slice& prefix,
                                           uint64_t block_offset) {
  assert(block_offset == kNotValid);
  if (!prefix_extractor_) {
    return true;
  }
  return MayMatch(prefix);
}

bool FullFilterBlockReader::MayMatch(const Slice& entry) {
  if (contents_.size() != 0)  {
    if (filter_bits_reader_->MayMatch(entry)) {
      PERF_COUNTER_ADD(bloom_sst_hit_count, 1);
      return true;
    } else {
      PERF_COUNTER_ADD(bloom_sst_miss_count, 1);
      return false;
    }
  }
  return true;  // remain the same with block_based filter
}

size_t FullFilterBlockReader::ApproximateMemoryUsage() const {
  return contents_.size();
}
}  // namespace rocksdb
