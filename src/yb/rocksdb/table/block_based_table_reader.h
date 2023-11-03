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

#pragma once

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/table/table_reader.h"

#include "yb/util/strongly_typed_bool.h"

namespace rocksdb {

class Block;
class BlockIter;
class BlockHandle;
class Cache;
class FilterBlockReader;
class BlockBasedFilterBlockReader;
class FullFilterBlockReader;
class Footer;
class InternalKeyComparator;
class Iterator;
class TableCache;
class TableReader;
class WritableFile;
struct BlockBasedTableOptions;
struct EnvOptions;
struct ReadOptions;
class GetContext;
class InternalIterator;
class IndexReader;

// Index reader special unique pointer to control the instance's way of deletion. Can be removed
// when https://github.com/yugabyte/yugabyte-db/issues/4720 is resolved.
using IndexReaderDeleter = std::function<void(IndexReader*)>;
using IndexReaderCleanablePtr = std::unique_ptr<IndexReader, IndexReaderDeleter>;

enum class DataIndexLoadMode {
  // Preload on Open, store in block cache or in table reader depending on
  // BlockBasedTableOptions::cache_index_and_filter_blocks.
  PRELOAD_ON_OPEN,
  // Load on first data index access, store in block cache or in table reader depending on
  // BlockBasedTableOptions::cache_index_and_filter_blocks.
  LAZY,
  // Don't preload data index, access as needed, use block cache if available.
  USE_CACHE
};

enum class PrefetchFilter {
  YES,
  NO
};

YB_DEFINE_ENUM(BlockType, (kData)(kIndex));

// BloomFilterAwareFileFilter should only be used when scanning within the same hashed components of
// the key and it should be used together with DocDbAwareFilterPolicy which only takes into account
// hashed components of key for filtering.
// BloomFilterAwareFileFilter ignores an SST file completely if there are no keys with the same
// hashed components as the key specified in constructor.
class BloomFilterAwareFileFilter : public TableAwareReadFileFilter {
 public:
  BloomFilterAwareFileFilter(const ReadOptions& read_options, const Slice& user_key);

  bool Filter(TableReader* reader) const override;

 private:
  const ReadOptions read_options_;
  const std::string user_key_;
};

// A Table is a sorted map from strings to strings.  Tables are
// immutable and persistent.  A Table may be safely accessed from
// multiple threads without external synchronization.
class BlockBasedTable : public TableReader {
 public:
  // No copying allowed
  explicit BlockBasedTable(const TableReader&) = delete;
  void operator=(const TableReader&) = delete;

  // Attempt to open the table that is stored in bytes [0..base_file_size) of "base_file" (may be
  // only metadata and data will be read from separate file passed via SetDataFileReader), and read
  // the metadata entries necessary to allow retrieving data from the table.
  //
  // If successful, returns ok and sets "*table_reader" to the newly opened table. The client
  // should delete "*table_reader" when no longer needed. If there was an error while initializing
  // the table, sets "*table_reader" to nullptr and returns a non-ok status.
  //
  // base_file must remain live while this Table is in use.
  // data_index_load_mode can be used to control loading of data index (see DataIndexLoadMode
  // description).
  // prefetch_filter can be used to disable prefetching of filter blocks at startup. For fixed-size
  // bloom filter only filter index could be prefetched.
  // skip_filters Disables loading/accessing the filter block. Overrides prefetch_filter, so filter
  //   will be skipped if both are set.
  static Status Open(
      const ImmutableCFOptions& ioptions,
      const EnvOptions& env_options,
      const BlockBasedTableOptions& table_options,
      const InternalKeyComparatorPtr& internal_key_comparator,
      std::unique_ptr<RandomAccessFileReader>&& base_file,
      uint64_t base_file_size,
      std::unique_ptr<TableReader>* table_reader,
      DataIndexLoadMode data_index_load_mode = DataIndexLoadMode::LAZY,
      PrefetchFilter prefetch_filter = PrefetchFilter::YES,
      bool skip_filters = false);

  bool IsSplitSst() const override { return true; }

  void SetDataFileReader(std::unique_ptr<RandomAccessFileReader>&& data_file) override;

  bool PrefixMayMatch(const ReadOptions& read_options, const Slice& internal_key);

  // Returns a new iterator over the table contents.
  // The result of NewIterator() is initially invalid (caller must
  // call one of the Seek methods on the iterator before using it).
  // @param skip_filters Disables loading/accessing the filter block
  InternalIterator* NewIterator(const ReadOptions&, Arena* arena = nullptr,
                                bool skip_filters = false) override;

  // @param skip_filters Disables loading/accessing the filter block.
  // key should be internal key in case bloom filters are used.
  Status Get(
      const ReadOptions& readOptions, const Slice& key, GetContext* get_context,
      bool skip_filters = false) override;

  // Pre-fetch the disk blocks that correspond to the key range specified by
  // (kbegin, kend). The call will return return error status in the event of
  // IO or iteration error.
  Status Prefetch(const Slice* begin, const Slice* end) override;

  // Given a key, return an approximate byte offset in the file where
  // the data for that key begins (or would begin if the key were
  // present in the file).  The returned value is in terms of file
  // bytes, and so includes effects like compression of the underlying data.
  // E.g., the approximate offset of the last key in the table will
  // be close to the file length.
  uint64_t ApproximateOffsetOf(const Slice& key) override;

  // Returns true if the block for the specified key is in cache.
  // REQUIRES: key is in this table && block cache enabled
  bool TEST_KeyInCache(const ReadOptions& options, const Slice& key);

  // Set up the table for Compaction. Might change some parameters with
  // posix_fadvise
  void SetupForCompaction() override;

  std::shared_ptr<const TableProperties> GetTableProperties() const override;

  size_t ApproximateMemoryUsage() const override;

  // convert SST file to a human readable form
  Status DumpTable(WritableFile* out_file) override;

  // Get the iterator from the index reader.
  // If input_iter is not set, return new Iterator
  // If input_iter is set, update it and return:
  //  - newly created data index iterator in case it was created (if we use multi-level data index,
  //    input_iter is an iterator of the top level index, but not the whole index iterator).
  //  - nullptr if input_iter is a data index iterator and no new iterators were created.
  //
  // Note: ErrorIterator with error will be returned if GetIndexReader returned an error.
  InternalIterator* NewIndexIterator(
      const ReadOptions& read_options, BlockIter* input_iter);

  InternalIterator* NewIndexIterator(const ReadOptions& read_options) override;

  // Converts an index entry (i.e. an encoded BlockHandle) into an iterator over the contents of
  // a correspoding block. Updates and returns input_iter if the one is specified, or returns
  // a new iterator.
  InternalIterator* NewDataBlockIterator(
      const ReadOptions& ro, const Slice& index_value, BlockType block_type,
      BlockIter* input_iter = nullptr);

  const ImmutableCFOptions& ioptions();

  yb::Result<std::string> GetMiddleKey() override;

  // Helper function that force reading block from a file and takes care about block cleanup.
  yb::Result<std::unique_ptr<Block>> RetrieveBlockFromFile(const ReadOptions& ro,
      const Slice& index_value, BlockType block_type);

  ~BlockBasedTable();

  bool TEST_filter_block_preloaded() const;
  bool TEST_index_reader_loaded() const;

  // Helper function to correctly release index reader. Can be replaced with direct call to
  // GetIndexReader() when https://github.com/yugabyte/yugabyte-db/issues/4720 is resolved.
  yb::Result<IndexReaderCleanablePtr> TEST_GetIndexReader();

 private:
  template <class TValue>
  struct CachableEntry;

  struct FileReaderWithCachePrefix;

  struct Rep;
  Rep* rep_;

  class BlockEntryIteratorState;
  class IndexIteratorHolder;

  // Returns filter block handle for fixed-size bloom filter using filter index and filter key.
  Status GetFixedSizeFilterBlockHandle(const Slice& filter_key,
      BlockHandle* filter_block_handle) const;

  // Returns key to be added to filter or verified against filter based on internal_key.
  Slice GetFilterKeyFromInternalKey(const Slice &internal_key) const;

  // Returns key to be added to filter or verified against filter based on user_key.
  Slice GetFilterKeyFromUserKey(const Slice& user_key) const;

  // If `no_io == true`, we will not try to read filter/index from sst file (except fixed-size
  // filter blocks) were they not present in cache yet.
  // filter_key is only required when using fixed-size bloom filter in order to use the filter index
  // to get the correct filter block.
  // Note: even if we check prefix match we still need to get filter based on filter_key, not its
  // prefix, because prefix for the key goes to the same filter block as key itself.
  CachableEntry<FilterBlockReader> GetFilter(const QueryId query_id,
                                             bool no_io = false,
                                             const Slice* filter_key = nullptr,
                                             Statistics* statistics = nullptr) const;

  // Returns index reader.
  // If index reader is not stored in either block or internal cache:
  // - If read_options.read_tier == kBlockCacheTier: Status::Incomplete error will be returned.
  // - If read_options.read_tier != kBlockCacheTier: new index reader will be created and cached.
  yb::Result<CachableEntry<IndexReader>> GetIndexReader(const ReadOptions& read_options);

  // Read block cache from block caches (if set): block_cache and
  // block_cache_compressed.
  // On success, Status::OK with be returned and @block will be populated with
  // pointer to the block as well as its block handle.
  static Status GetDataBlockFromCache(
      const Slice& block_cache_key, const Slice& compressed_block_cache_key,
      Cache* block_cache, Cache* block_cache_compressed, Statistics* statistics,
      const ReadOptions& read_options, BlockBasedTable::CachableEntry<Block>* block,
      uint32_t format_version, BlockType block_type,
      const std::shared_ptr<yb::MemTracker>& mem_tracker);

  // Put a raw block (maybe compressed) to the corresponding block caches.
  // This method will perform decompression against raw_block if needed and then
  // populate the block caches.
  // On success, Status::OK will be returned; also @block will be populated with
  // uncompressed block and its cache handle.
  //
  // REQUIRES: raw_block is heap-allocated. PutDataBlockToCache() will be
  // responsible for releasing its memory if error occurs.
  static Status PutDataBlockToCache(
      const Slice& block_cache_key, const Slice& compressed_block_cache_key,
      Cache* block_cache, Cache* block_cache_compressed,
      const ReadOptions& read_options, Statistics* statistics,
      CachableEntry<Block>* block, Block* raw_block, uint32_t format_version,
      const std::shared_ptr<yb::MemTracker>& mem_tracker);

  // Calls (*handle_result)(arg, ...) repeatedly, starting with the entry found
  // after a call to Seek(key), until handle_result returns false.
  // May not make such a call if filter policy says that key is not present.
  friend class TableCache;
  friend class BlockBasedTableBuilder;
  friend class BloomFilterAwareFileFilter;

  void ReadMeta(const Footer& footer);

  // Create a index reader based on the index type stored in the table.
  // Optionally, user can pass a preloaded meta_index_iter for the index that
  // need to access extra meta blocks for index construction. This parameter
  // helps avoid re-reading meta index block if caller already created one.
  Status CreateDataBlockIndexReader(
      std::unique_ptr<IndexReader>* index_reader,
      InternalIterator* preloaded_meta_index_iter = nullptr);

  bool NonBlockBasedFilterKeyMayMatch(FilterBlockReader* filter, const Slice& filter_key) const;

  Status ReadPropertiesBlock(InternalIterator* meta_iter);

  Status SetupFilter(InternalIterator* meta_iter);

  // Read the meta block from sst.
  static Status ReadMetaBlock(
      Rep* rep, std::unique_ptr<Block>* meta_block, std::unique_ptr<InternalIterator>* iter);

  // Create the filter from the filter block.
  static FilterBlockReader* ReadFilterBlock(const BlockHandle& filter_block, Rep* rep,
      size_t* filter_size = nullptr, Statistics* statistics = nullptr);

  // CreateFilterIndexReader from sst
  Status CreateFilterIndexReader(std::unique_ptr<IndexReader>* filter_index_reader);

  // Helper function to setup the cache key's prefix for block of file passed within a reader
  // instance. Used for both data and metadata files.
  static void SetupCacheKeyPrefix(Rep* rep, FileReaderWithCachePrefix* reader_with_cache_prefix);

  FileReaderWithCachePrefix* GetBlockReader(BlockType block_type) const;
  KeyValueEncodingFormat GetKeyValueEncodingFormat(BlockType block_type) const;

  // Retrieves block from file system or cache.
  // NOTE! A caller is responsible for a block cleanup.
  yb::Result<CachableEntry<Block>> RetrieveBlock(const ReadOptions& ro, const Slice& index_value,
      BlockType block_type, bool use_cache = true);

  explicit BlockBasedTable(Rep* rep) : rep_(rep) {}

  // Helper functions for DumpTable()
  Status DumpIndexBlock(WritableFile* out_file);
  Status DumpDataBlocks(WritableFile* out_file);
};

}  // namespace rocksdb
