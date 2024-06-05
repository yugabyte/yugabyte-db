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

class FilterPolicy;
class FilterBitsBuilder;
class FilterBitsReader;

// A FullFilterBlockBuilder is used to construct a full filter for a
// particular Table.  It generates a single string which is stored as
// a special block in the Table.
// The format of full filter block is:
// +----------------------------------------------------------------+
// |              full filter for all keys in sst file              |
// +----------------------------------------------------------------+
// The full filter can be very large. At the end of it, we put
// num_probes: how many hash functions are used in bloom filter
//
class FullFilterBlockBuilder : public FilterBlockBuilder {
 public:
  explicit FullFilterBlockBuilder(const SliceTransform* prefix_extractor,
                                  bool whole_key_filtering,
                                  FilterBitsBuilder* filter_bits_builder);
  // bits_builder is created in filter_policy, it should be passed in here
  // directly. and be deleted here
  ~FullFilterBlockBuilder() {}

  virtual void StartBlock(uint64_t block_offset) override {}
  virtual void Add(const Slice& key) override;
  virtual Slice Finish() override;
  virtual bool ShouldFlush() const override { return false; }

 private:
  // important: all of these might point to invalid addresses
  // at the time of destruction of this filter block. destructor
  // should NOT dereference them.
  const SliceTransform* prefix_extractor_;
  bool whole_key_filtering_;

  uint32_t num_added_;
  std::unique_ptr<FilterBitsBuilder> filter_bits_builder_;
  std::unique_ptr<const char[]> filter_data_;

  void AddKey(const Slice& key);
  void AddPrefix(const Slice& key);

  // No copying allowed
  FullFilterBlockBuilder(const FullFilterBlockBuilder&);
  void operator=(const FullFilterBlockBuilder&);
};

// A FilterBlockReader is used to parse filter from SST table.
// KeyMayMatch and PrefixMayMatch would trigger filter checking
class FullFilterBlockReader : public FilterBlockReader {
 public:
  // REQUIRES: "contents" and filter_bits_reader must stay live
  // while *this is live.
  explicit FullFilterBlockReader(const SliceTransform* prefix_extractor,
                                 bool whole_key_filtering,
                                 const Slice& contents,
                                 FilterBitsReader* filter_bits_reader);
  explicit FullFilterBlockReader(const SliceTransform* prefix_extractor,
                                 bool whole_key_filtering,
                                 BlockContents&& contents,
                                 FilterBitsReader* filter_bits_reader);

  // bits_reader is created in filter_policy, it should be passed in here
  // directly. and be deleted here
  ~FullFilterBlockReader() {}

  virtual bool KeyMayMatch(const Slice& key,
                           uint64_t block_offset = kNotValid) override;
  virtual bool PrefixMayMatch(const Slice& prefix,
                              uint64_t block_offset = kNotValid) override;
  virtual size_t ApproximateMemoryUsage() const override;

 private:
  const SliceTransform* prefix_extractor_;
  bool whole_key_filtering_;

  std::unique_ptr<FilterBitsReader> filter_bits_reader_;
  Slice contents_;
  BlockContents block_contents_;
  std::unique_ptr<const char[]> filter_data_;

  bool MayMatch(const Slice& entry);

  // No copying allowed
  FullFilterBlockReader(const FullFilterBlockReader&);
  void operator=(const FullFilterBlockReader&);
};

}  // namespace rocksdb
