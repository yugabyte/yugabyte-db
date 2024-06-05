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

#include "yb/rocksdb/options.h"
#include "yb/rocksdb/flush_block_policy.h"
#include "yb/rocksdb/table/block_builder.h"
#include "yb/util/slice.h"

namespace rocksdb {

// Flush block by size
class FlushBlockBySizePolicy : public FlushBlockPolicy {
 public:
  // @params block_size:           Approximate size of user data packed per
  //                               block.
  // @params block_size_deviation: This is used to close a block before it
  //                               reaches the configured
  FlushBlockBySizePolicy(const uint64_t block_size,
                         const uint64_t block_size_deviation,
                         const size_t min_keys_per_block,
                         const BlockBuilder& data_block_builder) :
      block_size_(block_size),
      block_size_deviation_(block_size_deviation),
      min_keys_per_block_(min_keys_per_block),
      data_block_builder_(data_block_builder) {
  }

  virtual bool Update(const Slice& key,
                      const Slice& value) override {
    // it makes no sense to flush when the data block is empty
    if (data_block_builder_.empty()) {
      return false;
    }

    auto curr_size = data_block_builder_.CurrentSizeEstimate();

    // Do flush if one of the below two conditions is true and we already have the required minimum
    // number of keys in block:
    // 1) if the current estimated size already exceeds the block size,
    // 2) block_size_deviation is set and the estimated size after appending
    // the kv will exceed the block size and the current size is under the
    // the deviation.
    return (curr_size >= block_size_ || BlockAlmostFull(key, value))
        && data_block_builder_.NumKeys() >= min_keys_per_block_;
  }

 private:
  bool BlockAlmostFull(const Slice& key, const Slice& value) const {
    const auto curr_size = data_block_builder_.CurrentSizeEstimate();
    const auto estimated_size_after =
      data_block_builder_.EstimateSizeAfterKV(key, value);

    return
      estimated_size_after > block_size_ &&
      block_size_deviation_ > 0 &&
      curr_size * 100 > block_size_ * (100 - block_size_deviation_);
  }

  const uint64_t block_size_;
  const uint64_t block_size_deviation_;
  const size_t min_keys_per_block_;
  const BlockBuilder& data_block_builder_;
};

FlushBlockPolicy* FlushBlockBySizePolicyFactory::NewFlushBlockPolicy(
    const BlockBasedTableOptions& table_options,
    const BlockBuilder& data_block_builder) const {
  return new FlushBlockBySizePolicy(
      table_options.block_size, table_options.block_size_deviation, 1 /* min_keys_per_block */,
      data_block_builder);
}

std::unique_ptr<FlushBlockPolicy> FlushBlockBySizePolicyFactory::NewFlushBlockPolicy(
    const uint64_t size, const int deviation, const size_t min_keys_per_block,
    const BlockBuilder& data_block_builder) {
  return std::make_unique<FlushBlockBySizePolicy>(
      size, deviation, min_keys_per_block, data_block_builder);
}

}  // namespace rocksdb
