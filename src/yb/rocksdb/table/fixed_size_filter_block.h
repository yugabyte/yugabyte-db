// Copyright (c) YugaByte, Inc.
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

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table/filter_block.h"
#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/util/hash.h"

#include "yb/util/slice.h"

namespace rocksdb {

// A fixed size Bloom filter divides the Bloom Filter into blocks of fixed size
// in which each block holds the information of a range of keys.
// Each Bloom filter block is built as keys are inserted into the block.
// As long as the terminating condition is reached, the Bloom filter block is flushed into disk
// and its offset being written to the index block.

// Since FixedSizeFilterBlockBuilder does not buffer any key,
// FixedSizeFilterBlockBuilder treats each insertion independently
// and won't check for duplicate keys.

// The sequence of calls to FixedSizeFilterBlockBuilder must match the regexp:
//      (StartBlock Add* Finish)*
class FixedSizeFilterBlockBuilder : public FilterBlockBuilder {
 public:
  FixedSizeFilterBlockBuilder(const SliceTransform* prefix_extractor,
      const BlockBasedTableOptions& table_opt);

  ~FixedSizeFilterBlockBuilder() {
    results_.clear();
  }

  // No copying allowed
  FixedSizeFilterBlockBuilder(const FixedSizeFilterBlockBuilder&) = delete;
  void operator=(const FixedSizeFilterBlockBuilder&) = delete;

  virtual void StartBlock(uint64_t block_offset) override;
  virtual void Add(const Slice& key) override;
  virtual bool ShouldFlush() const override;
  virtual Slice Finish() override;

 private:
  void AddKey(const Slice& key);
  void AddPrefix(const Slice& key);

  // Important: all of these might point to invalid addresses at the time of destruction of this
  // filter block. Destructor should NOT dereference them.
  const FilterPolicy* policy_;
  const SliceTransform* prefix_extractor_;
  bool whole_key_filtering_;

  std::unique_ptr<FilterBitsBuilder> bits_builder_; // writer to the results
  std::unique_ptr<const char[]> active_block_data_;
  std::vector<std::unique_ptr<const char[]>> results_;
};

// A FilterBlockReader will attempt to read the block associated with the keys of interest.
// KeyMayMatch and PrefixMayMatch would trigger filter checking.
class FixedSizeFilterBlockReader : public FilterBlockReader {
 public:
  // REQUIRES: "contents" and *policy must stay live while *this is live.
  FixedSizeFilterBlockReader(const SliceTransform* prefix_extractor,
                             const BlockBasedTableOptions& table_opt,
                             bool whole_key_filtering,
                             BlockContents&& contents);
  FixedSizeFilterBlockReader(const FixedSizeFilterBlockReader&) = delete;
  void operator=(const FixedSizeFilterBlockReader&) = delete;

  virtual bool KeyMayMatch(const Slice& key,
                           uint64_t block_offset = 0) override;
  virtual bool PrefixMayMatch(const Slice& prefix,
                              uint64_t block_offset = 0) override;
  virtual size_t ApproximateMemoryUsage() const override;

  // convert this object to a human readable form
  std::string ToString() const override;

 private:
  const FilterPolicy* policy_;
  const SliceTransform* prefix_extractor_;
  bool whole_key_filtering_;
  std::unique_ptr<FilterBitsReader> reader_;
  BlockContents contents_;

  // Checks whether entry is present in filter. Entry could be any binary data, but used here for
  // either key or key prefix when calling from corresponding public methods.
  bool MayMatch(const Slice& entry);

};
}  // namespace rocksdb
