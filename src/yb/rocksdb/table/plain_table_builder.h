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

#include <stdint.h>
#include <vector>
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/table/table_builder.h"
#include "yb/rocksdb/table/plain_table_key_coding.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/table/bloom_block.h"
#include "yb/rocksdb/table/plain_table_index.h"

namespace rocksdb {

class BlockBuilder;
class BlockHandle;
class WritableFile;
class TableBuilder;

class PlainTableBuilder: public TableBuilder {
 public:
  // Create a builder that will store the contents of the table it is
  // building in *file.  Does not close the file.  It is up to the
  // caller to close the file after calling Finish(). The output file
  // will be part of level specified by 'level'.  A value of -1 means
  // that the caller does not know which level the output file will reside.
  PlainTableBuilder(
      const ImmutableCFOptions& ioptions,
      const IntTblPropCollectorFactories& int_tbl_prop_collector_factories,
      uint32_t column_family_id, WritableFileWriter* file,
      uint32_t user_key_size, EncodingType encoding_type,
      size_t index_sparseness, uint32_t bloom_bits_per_key,
      uint32_t num_probes = 6, size_t huge_page_tlb_size = 0,
      double hash_table_ratio = 0, bool store_index_in_file = false);

  // REQUIRES: Either Finish() or Abandon() has been called.
  ~PlainTableBuilder();

  // Add key,value to the table being constructed.
  // REQUIRES: key is after any previously added key according to comparator.
  // REQUIRES: Finish(), Abandon() have not been called
  void Add(const Slice& key, const Slice& value) override;

  // Return non-ok iff some error has been detected.
  Status status() const override;

  // Finish building the table.  Stops using the file passed to the
  // constructor after this function returns.
  // REQUIRES: Finish(), Abandon() have not been called
  Status Finish() override;

  // Indicate that the contents of this builder should be abandoned.  Stops
  // using the file passed to the constructor after this function returns.
  // If the caller is not going to call Finish(), it must call Abandon()
  // before destroying this builder.
  // REQUIRES: Finish(), Abandon() have not been called
  void Abandon() override;

  // Number of calls to Add() so far.
  uint64_t NumEntries() const override;

  // Size of the file generated so far.  If invoked after a successful
  // Finish() call, returns the size of the final generated file.
  uint64_t TotalFileSize() const override;

  // For plain table SST file format - the same as TotalFileSize, since it uses the only base file
  // for both data and metadata.
  uint64_t BaseFileSize() const override;

  TableProperties GetTableProperties() const override { return properties_; }

  bool SaveIndexInFile() const { return store_index_in_file_; }

  const std::string& LastKey() const override;

 private:
  Arena arena_;
  const ImmutableCFOptions& ioptions_;
  std::vector<std::unique_ptr<IntTblPropCollector>>
      table_properties_collectors_;

  BloomBlockBuilder bloom_block_;
  std::unique_ptr<PlainTableIndexBuilder> index_builder_;

  WritableFileWriter* file_;
  uint64_t offset_ = 0;
  uint32_t bloom_bits_per_key_;
  size_t huge_page_tlb_size_;
  Status status_;
  TableProperties properties_;
  PlainTableKeyEncoder encoder_;

  bool store_index_in_file_;

  std::vector<uint32_t> keys_or_prefixes_hashes_;
  bool closed_ = false;  // Either Finish() or Abandon() has been called.

  const SliceTransform* prefix_extractor_;
  std::string last_key_;

  Slice GetPrefix(const Slice& target) const {
    assert(target.size() >= 8);  // target is internal key
    return GetPrefixFromUserKey(GetUserKey(target));
  }

  Slice GetPrefix(const ParsedInternalKey& target) const {
    return GetPrefixFromUserKey(target.user_key);
  }

  Slice GetUserKey(const Slice& key) const {
    return Slice(key.data(), key.size() - 8);
  }

  Slice GetPrefixFromUserKey(const Slice& user_key) const {
    if (!IsTotalOrderMode()) {
      return prefix_extractor_->Transform(user_key);
    } else {
      // Use empty slice as prefix if prefix_extractor is not set.
      // In that case,
      // it falls back to pure binary search and
      // total iterator seek is supported.
      return Slice();
    }
  }

  bool IsTotalOrderMode() const { return (prefix_extractor_ == nullptr); }

  // No copying allowed
  PlainTableBuilder(const PlainTableBuilder&) = delete;
  void operator=(const PlainTableBuilder&) = delete;
};

}  // namespace rocksdb
