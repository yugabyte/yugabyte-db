// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
#include <unordered_map>
#include <memory>
#include <vector>
#include <string>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table_properties.h"

#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/table/table_reader.h"
#include "yb/rocksdb/table/plain_table_factory.h"
#include "yb/rocksdb/table/plain_table_index.h"

#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/dynamic_bloom.h"
#include "yb/rocksdb/util/file_reader_writer.h"

namespace rocksdb {

class Block;
struct BlockContents;
class BlockHandle;
class Footer;
struct Options;
struct ReadOptions;
class TableCache;
class TableReader;
class InternalKeyComparator;
class PlainTableKeyDecoder;
class GetContext;
class InternalIterator;

extern const uint32_t kPlainTableVariableLength;

struct PlainTableReaderFileInfo {
  bool is_mmap_mode;
  Slice file_data;
  uint32_t data_end_offset;
  std::unique_ptr<RandomAccessFileReader> file;

  PlainTableReaderFileInfo(std::unique_ptr<RandomAccessFileReader>&& _file,
                           const EnvOptions& storage_options,
                           uint32_t _data_size_offset)
      : is_mmap_mode(storage_options.use_mmap_reads),
        data_end_offset(_data_size_offset),
        file(std::move(_file)) {}
};

// Based on following output file format shown in plain_table_factory.h
// When opening the output file, IndexedTableReader creates a hash table
// from key prefixes to offset of the output file. IndexedTable will decide
// whether it points to the data offset of the first key with the key prefix
// or the offset of it. If there are too many keys share this prefix, it will
// create a binary search-able index from the suffix to offset on disk.
//
// The implementation of IndexedTableReader requires output file is mmaped
class PlainTableReader: public TableReader {
 public:
  static Status Open(const ImmutableCFOptions& ioptions,
                     const EnvOptions& env_options,
                     const InternalKeyComparatorPtr& internal_comparator,
                     std::unique_ptr<RandomAccessFileReader>&& file,
                     uint64_t file_size, std::unique_ptr<TableReader>* table,
                     const int bloom_bits_per_key, double hash_table_ratio,
                     size_t index_sparseness, size_t huge_page_tlb_size,
                     bool full_scan_mode);

  bool IsSplitSst() const override { return false; }

  void SetDataFileReader(std::unique_ptr<RandomAccessFileReader>&& data_file) override {
    LOG(FATAL) << "PlainTableReader::SetDataFileReader is not supported";
  }

  InternalIterator* NewIterator(const ReadOptions&, Arena* arena = nullptr,
                                bool skip_filters = false) override;

  void Prepare(const Slice& target) override;

  Status Get(const ReadOptions&, const Slice& key, GetContext* get_context,
             bool skip_filters = false) override;

  uint64_t ApproximateOffsetOf(const Slice& key) override;

  uint32_t GetIndexSize() const { return index_.GetIndexSize(); }
  void SetupForCompaction() override;

  std::shared_ptr<const TableProperties> GetTableProperties() const override {
    return table_properties_;
  }

  virtual size_t ApproximateMemoryUsage() const override {
    return arena_.MemoryAllocatedBytes();
  }

  InternalIterator* NewIndexIterator(const ReadOptions& read_options) override;

  PlainTableReader(const ImmutableCFOptions& ioptions,
                   std::unique_ptr<RandomAccessFileReader>&& file,
                   const EnvOptions& env_options,
                   const InternalKeyComparatorPtr& internal_comparator,
                   EncodingType encoding_type, uint64_t file_size,
                   const TableProperties* table_properties);
  virtual ~PlainTableReader();

 protected:
  // Check bloom filter to see whether it might contain this prefix.
  // The hash of the prefix is given, since it can be reused for index lookup
  // too.
  virtual bool MatchBloom(uint32_t hash) const;

  // PopulateIndex() builds index of keys. It must be called before any query
  // to the table.
  //
  // props: the table properties object that need to be stored. Ownership of
  //        the object will be passed.
  //

  Status PopulateIndex(TableProperties* props, int bloom_bits_per_key,
                       double hash_table_ratio, size_t index_sparseness,
                       size_t huge_page_tlb_size);

  Status MmapDataIfNeeded();

 private:
  InternalKeyComparatorPtr internal_comparator_;
  EncodingType encoding_type_;
  // represents plain table's current status.
  Status status_;

  PlainTableIndex index_;
  bool full_scan_mode_;

  // data_start_offset_ and data_end_offset_ defines the range of the
  // sst file that stores data.
  const uint32_t data_start_offset_ = 0;
  const uint32_t user_key_len_;
  const SliceTransform* prefix_extractor_;

  static const size_t kNumInternalBytes = 8;

  // Bloom filter is used to rule out non-existent key
  bool enable_bloom_;
  DynamicBloom bloom_;
  PlainTableReaderFileInfo file_info_;
  Arena arena_;
  TrackedAllocation index_block_alloc_;
  TrackedAllocation bloom_block_alloc_;

  const ImmutableCFOptions& ioptions_;
  uint64_t file_size_;
  std::shared_ptr<const TableProperties> table_properties_;
  std::shared_ptr<yb::MemTracker> mem_tracker_;
  size_t tracked_consumption_ = 0;

  bool IsFixedLength() const {
    return user_key_len_ != kPlainTableVariableLength;
  }

  size_t GetFixedInternalKeyLength() const {
    return user_key_len_ + kNumInternalBytes;
  }

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

  friend class TableCache;
  friend class PlainTableIterator;

  // Internal helper function to generate an IndexRecordList object from all
  // the rows, which contains index records as a list.
  // If bloom_ is not null, all the keys' full-key hash will be added to the
  // bloom filter.
  Status PopulateIndexRecordList(PlainTableIndexBuilder* index_builder,
                                 std::vector<uint32_t>* prefix_hashes);

  // Internal helper function to allocate memory for bloom filter and fill it
  void AllocateAndFillBloom(int bloom_bits_per_key, int num_prefixes,
                            size_t huge_page_tlb_size,
                            std::vector<uint32_t>* prefix_hashes);

  void FillBloom(std::vector<uint32_t>* prefix_hashes);

  // Read the key and value at `offset` to parameters for keys, the and
  // `seekable`.
  // On success, `offset` will be updated as the offset for the next key.
  // `parsed_key` will be key in parsed format.
  // if `internal_key` is not empty, it will be filled with key with slice
  // format.
  // if `seekable` is not null, it will return whether we can directly read
  // data using this offset.
  Status Next(PlainTableKeyDecoder* decoder, uint32_t* offset,
              ParsedInternalKey* parsed_key, Slice* internal_key, Slice* value,
              bool* seekable = nullptr) const;
  // Get file offset for key target.
  // return value prefix_matched is set to true if the offset is confirmed
  // for a key with the same prefix as target.
  Status GetOffset(PlainTableKeyDecoder* decoder, const Slice& target,
                   const Slice& prefix, uint32_t prefix_hash,
                   bool* prefix_matched, uint32_t* offset) const;

  bool IsTotalOrderMode() const { return (prefix_extractor_ == nullptr); }

  // No copying allowed
  explicit PlainTableReader(const TableReader&) = delete;
  void operator=(const TableReader&) = delete;
};
}  // namespace rocksdb
