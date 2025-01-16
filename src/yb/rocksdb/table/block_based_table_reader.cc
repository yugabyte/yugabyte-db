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

#include "yb/rocksdb/table/block_based_table_reader.h"

#include <string>
#include <utility>

#include "yb/gutil/casts.h"
#include "yb/gutil/macros.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/block.h"
#include "yb/rocksdb/table/block_based_filter_block.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/block_based_table_internal.h"
#include "yb/rocksdb/table/block_hash_index.h"
#include "yb/rocksdb/table/block_prefix_index.h"
#include "yb/rocksdb/table/filter_block.h"
#include "yb/rocksdb/table/fixed_size_filter_block.h"
#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/table/full_filter_block.h"
#include "yb/rocksdb/table/get_context.h"
#include "yb/rocksdb/table/index_reader.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/meta_blocks.h"
#include "yb/rocksdb/table/table_properties_internal.h"
#include "yb/rocksdb/table/two_level_iterator.h"
#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/perf_context_imp.h"
#include "yb/rocksdb/util/statistics.h"
#include "yb/rocksdb/util/stop_watch.h"

#include "yb/util/atomic.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/stats/perf_step_timer.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/tostring.h"

using yb::operator"" _KB;

DEFINE_RUNTIME_uint64(rocksdb_iterator_sequential_disk_reads_for_auto_readahead, 4,
    "Enable readahead when RocksDB iterator attempts to perform a configured number of sequential "
    "disk reads. If set to 0, the iterator readahead is disabled. If set to 1, iterator readahead "
    "will be enabled with the first disk read. If set to N > 1, iterator readahead will be used "
    "with the Nth sequential disk read.");

DEFINE_RUNTIME_uint64(
    rocksdb_iterator_sequential_disk_reads_factor, 1,
    "Treat read as sequential for readahead purposes if next read operation is skipping up to "
    "readahead window size multiplied by rocksdb_iterator_sequential_disk_reads_factor bytes.");
TAG_FLAG(rocksdb_iterator_sequential_disk_reads_for_auto_readahead, advanced);

DEFINE_RUNTIME_uint64(rocksdb_iterator_init_readahead_size, 32_KB,
    "Initial RocksDB iterator readahead size.");
TAG_FLAG(rocksdb_iterator_init_readahead_size, advanced);

DEFINE_RUNTIME_uint64(rocksdb_iterator_max_readahead_size, 2_MB,
    "Maximum RocksDB iterator readahead size.");
TAG_FLAG(rocksdb_iterator_max_readahead_size, advanced);

DEFINE_RUNTIME_bool(rocksdb_iterator_disable_restart_block_keys_caching, false,
    "Disables restart block keys caching. Having restart block keys caching turned on may give "
    "a better performance for backward iteration over a block with restart interval greater "
    "than 0 (for example for data block), refer to block_restart_interval gflag.");
TAG_FLAG(rocksdb_iterator_disable_restart_block_keys_caching, advanced);

DEFINE_test_flag(bool, rocksdb_record_readahead_stats_only_for_data_blocks, false,
    "For testing only. Record readahead statistics only for data blocks.");

namespace rocksdb {

extern const uint64_t kBlockBasedTableMagicNumber;
extern const char kHashIndexPrefixesBlock[];
extern const char kHashIndexPrefixesMetadataBlock[];
using std::unique_ptr;

typedef FilterPolicy::FilterType FilterType;

namespace {

// Delete the resource that is held by the iterator.
template <class ResourceType>
void DeleteHeldResource(void* arg, void* ignored) {
  delete reinterpret_cast<ResourceType*>(arg);
}

// Delete the entry resided in the cache.
template <class Entry>
void DeleteCachedEntry(const Slice& key, void* value) {
  auto entry = reinterpret_cast<Entry*>(value);
  delete entry;
}

// Release the cached entry and decrement its ref count.
void ReleaseCachedEntry(void* arg, void* h) {
  Cache* cache = reinterpret_cast<Cache*>(arg);
  Cache::Handle* handle = reinterpret_cast<Cache::Handle*>(h);
  cache->Release(handle);
}

template <class TValue>
void DestroyCachedEntry(void* arg, void* h) {
  delete reinterpret_cast<TValue*>(arg);
}

Cache::Handle* GetEntryFromCache(Cache* block_cache, const Slice& key,
                                 Tickers block_cache_miss_ticker,
                                 Tickers block_cache_hit_ticker,
                                 Statistics* statistics,
                                 const QueryId query_id) {
  auto cache_handle = block_cache->Lookup(key, query_id, statistics);
  if (cache_handle != nullptr) {
    PERF_COUNTER_ADD(block_cache_hit_count, 1);
    // block-type specific cache hit
    RecordTick(statistics, block_cache_hit_ticker);
  } else {
    // block-type specific cache miss
    RecordTick(statistics, block_cache_miss_ticker);
  }

  return cache_handle;
}

class NotMatchingFilterBlockReader : public FilterBlockReader {
 public:
  NotMatchingFilterBlockReader() {}
  NotMatchingFilterBlockReader(const NotMatchingFilterBlockReader&) = delete;
  void operator=(const NotMatchingFilterBlockReader&) = delete;

  bool KeyMayMatch(Slice key, uint64_t block_offset = 0) override { return false; }
  bool PrefixMayMatch(const Slice& prefix, uint64_t block_offset = 0) override { return false; }
  size_t ApproximateMemoryUsage() const override { return 0; }
};

InternalIterator* ReturnErrorIterator(const Status& status, BlockIter* input_iter) {
  if (input_iter != nullptr) {
    input_iter->SetStatus(status);
    return input_iter;
  } else {
    return NewErrorInternalIterator(status);
  }
}

Status ReturnNoIOError() {
  return STATUS(Incomplete, "no blocking io");
}

}  // namespace

// Originally following data was stored in BlockBasedTable::Rep and related to a single SST file.
// Since SST file is now split into two files - data file and metadata file, all file-related data
// was moved into dedicated structure for each file.
struct BlockBasedTable::FileReaderWithCachePrefix {
  // Pointer to file reader.
  unique_ptr<RandomAccessFileReader> reader;

  // BlockBasedTableReader uses the block cache passed to BlockBasedTableReader::Open inside
  // a BlockBasedTableOptions instance to reduce the number of file read requests. If block cache
  // pointer in options is nullptr, cache is not used. File blocks are referred in cache by keys,
  // which are composed from the following data (see GetCacheKey helper function):
  // - cache key prefix (unique for each file), generated by BlockBasedTable::GenerateCachePrefix
  // - block offset within a file.
  block_based_table::CacheKeyPrefixBuffer cache_key_prefix;

  // Similar prefix, but for compressed blocks cache:
  block_based_table::CacheKeyPrefixBuffer compressed_cache_key_prefix;

  explicit FileReaderWithCachePrefix(unique_ptr<RandomAccessFileReader>&& _reader) :
      reader(std::move(_reader)) {}
};

// CachableEntry represents the entries that *may* be fetched from block cache.
//  field `value` is the item we want to get.
//  field `cache_handle` is the cache handle to the block cache. If the value
//    was not read from cache, `cache_handle` will be nullptr.
template <class TValue>
struct BlockBasedTable::CachableEntry {
  CachableEntry(TValue* _value, Cache::Handle& _cache_handle)
      : value(_value), cache_handle(&_cache_handle) {}

  CachableEntry(TValue* _value, bool _owns)
      : value(_value), owns(_owns) {}

  CachableEntry() = default;

  void Release(Cache* cache) {
    if (cache_handle) {
      cache->Release(cache_handle);
      value = nullptr;
      cache_handle = nullptr;
    } else if (owns) {
      delete value;
    }
  }

  TValue* value = nullptr;
  // if the entry is from the cache, cache_handle will be populated.
  Cache::Handle* cache_handle = nullptr;
  bool owns = false;
};

struct BlockBasedTable::Rep {
  struct NotMatchingFilterEntry : public CachableEntry<FilterBlockReader> {
    NotMatchingFilterEntry() : CachableEntry(&filter, false) {}
    NotMatchingFilterBlockReader filter;
  };

  Rep(const ImmutableCFOptions& _ioptions, const EnvOptions& _env_options,
      const BlockBasedTableOptions& _table_opt,
      const InternalKeyComparatorPtr& _internal_comparator, bool skip_filters,
      const DataIndexLoadMode data_index_load_mode_)
      : ioptions(_ioptions),
        env_options(_env_options),
        table_options(_table_opt),
        filter_policy(skip_filters ? nullptr : _table_opt.filter_policy.get()),
        filter_key_transformer(filter_policy ? filter_policy->GetKeyTransformer() : nullptr),
        comparator(_internal_comparator),
        filter_type(FilterType::kNoFilter),
        whole_key_filtering(_table_opt.whole_key_filtering),
        prefix_filtering(true),
        data_index_load_mode(data_index_load_mode_) {
    if (ioptions.block_based_table_mem_tracker) {
      mem_tracker = ioptions.block_based_table_mem_tracker;
    } else if (ioptions.mem_tracker) {
      mem_tracker = yb::MemTracker::FindOrCreateTracker("BlockBasedTable", ioptions.mem_tracker);
    }
  }

  const ImmutableCFOptions& ioptions;
  const EnvOptions& env_options;
  const BlockBasedTableOptions& table_options;
  const FilterPolicy* filter_policy;
  const FilterPolicy::KeyTransformer* filter_key_transformer;
  InternalKeyComparatorPtr comparator;
  const NotMatchingFilterEntry not_matching_filter_entry;
  Status status;
  std::shared_ptr<FileReaderWithCachePrefix> base_reader_with_cache_prefix;
  std::shared_ptr<FileReaderWithCachePrefix> data_reader_with_cache_prefix;

  // Footer contains the fixed table information
  Footer footer;
  std::mutex data_index_reader_mutex;
  yb::AtomicUniquePtr<IndexReader> data_index_reader;
  std::unique_ptr<BinarySearchIndexReader> filter_index_reader;
  std::unique_ptr<FilterBlockReader> filter;

  FilterType filter_type;

  // Handle of fixed-size bloom filter index block or simply filter block for filters of other
  // types.
  BlockHandle filter_handle;

  std::shared_ptr<const TableProperties> table_properties;
  IndexType index_type = IndexType::kBinarySearch;
  bool hash_index_allow_collision = false;
  bool whole_key_filtering = false;
  bool prefix_filtering = false;
  KeyValueEncodingFormat data_block_key_value_encoding_format =
      KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix;
  // TODO(kailiu) It is very ugly to use internal key in table, since table
  // module should not be relying on db module. However to make things easier
  // and compatible with existing code, we introduce a wrapper that allows
  // block to extract prefix without knowing if a key is internal or not.
  unique_ptr<SliceTransform> internal_prefix_transform;

  DataIndexLoadMode data_index_load_mode = static_cast<DataIndexLoadMode>(0);
  yb::MemTrackerPtr mem_tracker;
  // The checksum of the meta block used to construct block cache key prefix.
  uint32_t meta_block_checksum;
};

struct BlockBasedTable::BlockRetrievalInfo {
  BlockType type;
  BlockHandle handle;
  RandomAccessFileReader* file_reader;
  Slice cache_key;
  Slice compressed_cache_key;

  char cache_key_buf[block_based_table::kCacheKeyBufferSize];
  char compressed_cache_key_buf[block_based_table::kCacheKeyBufferSize];
};

// BlockEntryIteratorState is used by TwoLevelIterator and MultiLevelIterator in order to check if
// key prefix may match the filter of the SST file or to create a secondary iterator.
class BlockBasedTable::BlockEntryIteratorState : public TwoLevelIteratorState {
 public:
  BlockEntryIteratorState(
      BlockBasedTable* table, const ReadOptions& read_options, bool skip_filters,
      BlockType block_type)
      : TwoLevelIteratorState(table->rep_->ioptions.prefix_extractor != nullptr),
        table_(table),
        read_options_(read_options),
        skip_filters_(skip_filters),
        block_type_(block_type),
        statistics_(
            (PREDICT_FALSE(FLAGS_TEST_rocksdb_record_readahead_stats_only_for_data_blocks) &&
             block_type_ != BlockType::kData)
            ? nullptr
            : read_options.statistics ? read_options.statistics
                                      : table_->rep_->ioptions.statistics) {}

  InternalIterator* NewSecondaryIterator(const Slice& index_value) override {
    PERF_TIMER_GUARD(new_table_block_iter_nanos);

    BlockRetrievalInfo block_info;
    {
      auto status = table_->GetBlockRetrievalInfo(index_value, block_type_, &block_info);
      if (!status.ok()) {
        return NewErrorInternalIterator(status);
      }
    }
    auto block_res = table_->GetBlockFromCache(read_options_, block_info);
    const auto& handle = block_info.handle;
    const auto is_sequential_read = IsSequentialRead(handle);
    VLOG_WITH_FUNC(5) << "handle: " << handle.ToDebugString()
                      << " num_sequential_disk_reads_: " << num_sequential_disk_reads_
                      << " prev_offset_: " << prev_offset_ << " prev_length_: " << prev_length_
                      << " skip_size: "
                      << static_cast<int64_t>(handle.offset() - prev_offset_ - prev_length_)
                      << " is_sequential_read: " << is_sequential_read
                      << " block from cache: "
                      << (block_res.ok() ? yb::AsString(static_cast<void*>(block_res->value))
                                         : AsString(block_res.status()))
                      << " for file: " << block_info.file_reader->file()->filename();

    if (!is_sequential_read &&
        (num_sequential_disk_reads_ > 0 || readahead_limit_ > 0)) {
      VLOG_WITH_FUNC(4) << "handle: " << handle.ToDebugString() << " prev_offset_: " << prev_offset_
                        << " prev_length_: " << prev_length_ << " skip_size: "
                        << static_cast<int64_t>(handle.offset() - prev_offset_ - prev_length_)
                        << " readahead_size_: " << readahead_size_
                        << ". Resetting readahead for iterator for file: "
                        << block_info.file_reader->file()->filename();
      if (VLOG_IS_ON(6)) {
        YB_LOG_EVERY_N_SECS(INFO, 1) << "Resetting readahead at: " << yb::GetStackTrace();
      }
      ResetReadahead();
    }

    if (block_res.ok() && !block_res->value) {
      // Automatically prefetch additional data when a range scan (iterator) does
      // sequential_disk_reads_for_auto_readahead_ sequential IOs.
      // TODO(readahead): look into implementing readahead for backward scan.
      if (sequential_disk_reads_for_auto_readahead_ > 0) {
        const auto is_block_after_readahead_limit =
            handle.offset() + handle.size() + kBlockTrailerSize > readahead_limit_;

        // 1) If we are reading within readahead window it doesn't cause disk read, so no need to
        // count it.
        // 2) Count first disk read in a row.
        if (is_block_after_readahead_limit &&
            (is_sequential_read || num_sequential_disk_reads_ == 0)) {
          ++num_sequential_disk_reads_;
        }

        // If sequential_disk_reads_for_auto_readahead_ = N, we want to start readahead with the Nth
        // sequential disk read in a row.
        // For example, for N = 2, initial readahead size = 4096, max readahead size = 8192:
        // 1st disk read: offset = 0, length = 1024, num_sequential_disk_reads_ = 1 - just read 1024
        //   bytes.
        // 2nd disk read: offset = 1024, length = 1024, num_sequential_disk_reads_ = 2 - readahead
        //   4096 bytes until offset 1024 + 4096 = 5120.
        // ... reads from readahead window ...
        // 3rd disk read: offset = 5120, length = 1024, num_sequential_disk_reads_ = 3 - readahead
        //   4096*2 = 8192 bytes until offset 5120 + 8192 = 13312.
        if ((num_sequential_disk_reads_ >= sequential_disk_reads_for_auto_readahead_) &&
            is_block_after_readahead_limit) {
          VLOG_WITH_FUNC(4) << "handle: " << handle.ToDebugString()
                            << " calling readahead with size: " << readahead_size_
                            << " for file: " << block_info.file_reader->file()->filename();
          block_info.file_reader->Readahead(handle.offset(), readahead_size_);
          RecordTick(statistics_, READAHEAD_CALLS);
          RecordTick(statistics_, READAHEAD_BYTES_READ, readahead_size_);
          readahead_limit_ = handle.offset() + readahead_size_;
          // Keep exponentially increasing readahead size up to max_readahead_size_.
          readahead_size_ = std::min(readahead_size_ * 2, max_readahead_size_);
        }
      }

      block_res = table_->ReadBlockFromFileAndMaybePutToCache(read_options_, block_info);
    }
    UpdateReadPattern(handle);

    if (!block_res.ok()) {
      return NewErrorInternalIterator(block_res.status());
    }

    return table_->NewBlockIterator(
        read_options_, block_res.get_ptr(), block_type_, /* input_iter = */ nullptr);
  }

  bool PrefixMayMatch(const Slice& internal_key) override {
    if (read_options_.total_order_seek || skip_filters_) {
      return true;
    }
    return table_->PrefixMayMatch(read_options_, internal_key);
  }

 private:
  void ResetReadahead() {
    num_sequential_disk_reads_ = 0;
    readahead_size_ = initial_readahead_size_;
    readahead_limit_ = 0;
    RecordTick(statistics_, READAHEAD_RESET);
  }

  void UpdateReadPattern(BlockHandle handle) {
    prev_offset_ = handle.offset();
    prev_length_ = handle.size() + kBlockTrailerSize;
  }

  bool IsSequentialRead(BlockHandle handle) {
    const auto prev_read_end = prev_offset_ + prev_length_;
    return (handle.offset() >= prev_read_end) &&
           (handle.offset() <=
            prev_read_end + readahead_size_ * FLAGS_rocksdb_iterator_sequential_disk_reads_factor);
  }

  // Don't own table_. BlockEntryIteratorState should only be stored in iterators or in
  // corresponding BlockBasedTable. TableReader (superclass of BlockBasedTable) is only destroyed
  // after iterator is deleted.
  BlockBasedTable* const table_;
  const ReadOptions read_options_;
  const bool skip_filters_;
  const BlockType block_type_;
  Statistics* const statistics_;

  const uint64_t sequential_disk_reads_for_auto_readahead_ =
      yb::GetAtomicFlag(&FLAGS_rocksdb_iterator_sequential_disk_reads_for_auto_readahead);
  const size_t max_readahead_size_ = yb::GetAtomicFlag(&FLAGS_rocksdb_iterator_max_readahead_size);
  const size_t initial_readahead_size_ = std::min<size_t>(
      yb::GetAtomicFlag(&FLAGS_rocksdb_iterator_init_readahead_size), max_readahead_size_);

  size_t readahead_size_ = initial_readahead_size_;
  size_t prev_offset_ = std::numeric_limits<size_t>::max();
  size_t prev_length_ = 0;
  uint64_t num_sequential_disk_reads_ = 0;
  size_t readahead_limit_ = 0;
};


class BlockBasedTable::IndexIteratorHolder {
 public:
  IndexIteratorHolder(BlockBasedTable* table_reader, ReadOptions read_options)
      : iter_holder_(table_reader->NewIndexIterator(read_options, &iter_)),
        iter_ptr_(iter_holder_ ? iter_holder_.get() : implicit_cast<InternalIterator*>(&iter_)) {}

  InternalIterator* iter() const { return iter_ptr_; }

 private:
  BlockIter iter_;
  std::unique_ptr<InternalIterator> iter_holder_;
  InternalIterator* iter_ptr_;
};

BlockBasedTable::~BlockBasedTable() {
  delete rep_;
}

void BlockBasedTable::SetupCacheKeyPrefix(Rep* rep,
    FileReaderWithCachePrefix* reader_with_cache_prefix) {
  reader_with_cache_prefix->cache_key_prefix.size = 0;
  reader_with_cache_prefix->compressed_cache_key_prefix.size = 0;
  if (rep->table_options.block_cache != nullptr) {
    GenerateCachePrefix(rep->table_options.block_cache.get(),
        reader_with_cache_prefix->reader->file(), rep->meta_block_checksum,
        &reader_with_cache_prefix->cache_key_prefix);
  }
  if (rep->table_options.block_cache_compressed != nullptr) {
    GenerateCachePrefix(rep->table_options.block_cache_compressed.get(),
        reader_with_cache_prefix->reader->file(), rep->meta_block_checksum,
        &reader_with_cache_prefix->compressed_cache_key_prefix);
  }
}

KeyValueEncodingFormat BlockBasedTable::GetKeyValueEncodingFormat(
    const BlockType block_type) const {
  switch (block_type) {
    case BlockType::kData:
      return rep_->data_block_key_value_encoding_format;
    case BlockType::kIndex:
      return kIndexBlockKeyValueEncodingFormat;
  }
  FATAL_INVALID_ENUM_VALUE(BlockType, block_type);
}

BlockBasedTable::FileReaderWithCachePrefix* BlockBasedTable::GetBlockReader(
    BlockType block_type) const {
  switch (block_type) {
    case BlockType::kData:
      return rep_->data_reader_with_cache_prefix.get();
    case BlockType::kIndex:
      return rep_->base_reader_with_cache_prefix.get();
  }
  FATAL_INVALID_ENUM_VALUE(BlockType, block_type);
}

BloomFilterAwareFileFilter::BloomFilterAwareFileFilter() {}

bool BloomFilterAwareFileFilter::Filter(
    const ReadOptions& read_options, Slice user_key, FilterKeyCache* filter_key_cache,
    TableReader* reader) const {
  auto table = down_cast<BlockBasedTable*>(reader);
  if (PREDICT_FALSE(table->rep_->filter_type != FilterType::kFixedSizeFilter)) {
    // For non fixed-size filters - take file into account. We are only using fixed-size bloom
    // filters for DocDB, so not need to support others.
    return true;
  }
  const auto filter_key = table->GetFilterKeyFromUserKey(user_key, filter_key_cache);
  if (filter_key.empty()) {
    return true;
  }
  auto filter_entry = table->GetFilter(
      read_options.query_id, read_options.read_tier == kBlockCacheTier /* no_io */, &filter_key,
      read_options.statistics);
  FilterBlockReader* filter = filter_entry.value;
  // If bloom filter was not useful, then take this file into account.
  const bool use_file = table->NonBlockBasedFilterKeyMayMatch(
      filter, filter_key, read_options.statistics);
  filter_entry.Release(table->rep_->table_options.block_cache.get());
  return use_file;
}

namespace {
// Return True if table_properties has `user_prop_name` has a `true` value
// or it doesn't contain this property (for backward compatible).
bool IsFeatureSupported(const TableProperties& table_properties,
                        const std::string& user_prop_name, Logger* info_log) {
  auto& props = table_properties.user_collected_properties;
  auto pos = props.find(user_prop_name);
  // Older version doesn't have this value set. Skip this check.
  if (pos != props.end()) {
    if (pos->second == kPropFalse) {
      return false;
    } else if (pos->second != kPropTrue) {
      RLOG(InfoLogLevel::WARN_LEVEL, info_log,
          "Property %s has invalidate value %s", user_prop_name.c_str(),
          pos->second.c_str());
    }
  }
  return true;
}
}  // namespace

Status BlockBasedTable::Open(const ImmutableCFOptions& ioptions,
                             const EnvOptions& env_options,
                             const BlockBasedTableOptions& table_options,
                             const InternalKeyComparatorPtr& internal_comparator,
                             unique_ptr<RandomAccessFileReader>&& base_file,
                             uint64_t base_file_size,
                             unique_ptr<TableReader>* table_reader,
                             DataIndexLoadMode data_index_load_mode,
                             PrefetchFilter prefetch_filter,
                             const bool skip_filters) {
  table_reader->reset();

  Footer footer;
  RETURN_NOT_OK(ReadFooterFromFile(
      base_file.get(), base_file_size, &footer, kBlockBasedTableMagicNumber));
  if (!BlockBasedTableSupportedVersion(footer.version())) {
    return STATUS(Corruption,
        "Unknown Footer version. Maybe this file was created with newer "
        "version of RocksDB?");
  }

  // We've successfully read the footer and the index block: we're
  // ready to serve requests.
  Rep* rep = new BlockBasedTable::Rep(ioptions, env_options, table_options,
                                      internal_comparator, skip_filters, data_index_load_mode);
  rep->base_reader_with_cache_prefix =
      std::make_shared<FileReaderWithCachePrefix>(std::move(base_file));
  rep->data_reader_with_cache_prefix = rep->base_reader_with_cache_prefix;
  rep->footer = footer;
  rep->index_type = table_options.index_type;
  rep->hash_index_allow_collision = table_options.hash_index_allow_collision;
  unique_ptr<BlockBasedTable> new_table(new BlockBasedTable(rep));

  // Read meta index
  std::unique_ptr<Block> meta;
  std::unique_ptr<InternalIterator> meta_iter;
  RETURN_NOT_OK(ReadMetaBlock(rep, &meta, &meta_iter));

  // cache key prefix needs the checksum of meta-block populated by ReadMetaBlock
  SetupCacheKeyPrefix(rep, rep->base_reader_with_cache_prefix.get());

  RETURN_NOT_OK(new_table->ReadPropertiesBlock(meta_iter.get()));

  RETURN_NOT_OK(new_table->SetupFilter(meta_iter.get()));

  if (data_index_load_mode == DataIndexLoadMode::PRELOAD_ON_OPEN) {
    // Will use block cache for data index access?
    if (table_options.cache_index_and_filter_blocks) {
      DCHECK_ONLY_NOTNULL(table_options.block_cache.get());
      // Hack: Call NewIndexIterator() to implicitly add index to the
      // block_cache
      unique_ptr<InternalIterator> iter(new_table->NewIndexIterator(ReadOptions::kDefault));
      RETURN_NOT_OK(iter->status());
    } else {
      // If we don't use block cache for data index access, we'll pre-load it, which will kept in
      // member variables in Rep and with a same life-time as this table object.
      // NOTE: Table reader objects are cached in table cache (table_cache.cc).
      auto index_reader = VERIFY_RESULT(new_table->CreateDataBlockIndexReader(meta_iter.get()));
      rep->data_index_reader.reset(index_reader.release());
    }
  }

  if (prefetch_filter == PrefetchFilter::YES) {
    // pre-fetching of blocks is turned on
    // NOTE: Table reader objects are cached in table cache (table_cache.cc).
    if (rep->filter_policy && rep->filter_type == FilterType::kFixedSizeFilter) {
      // TODO: may be put it in block cache instead of table reader in case
      // table_options.cache_index_and_filter_blocks is set?
      rep->filter_index_reader = VERIFY_RESULT(new_table->CreateFilterIndexReader());
    }

    // Will use block cache for filter blocks access?
    if (table_options.cache_index_and_filter_blocks) {
      assert(table_options.block_cache != nullptr);
      bool corrupted_filter_type = true;
      switch (rep->filter_type) {
        case FilterType::kFullFilter:
          FALLTHROUGH_INTENDED;
        case FilterType::kBlockBasedFilter: {
          // Hack: Call GetFilter() to implicitly add filter to the block_cache
          auto filter_entry = new_table->GetFilter(kDefaultQueryId);
          filter_entry.Release(table_options.block_cache.get());
          corrupted_filter_type = false;
          break;
        }
        case FilterType::kFixedSizeFilter:
          // We never pre-cache fixed-size bloom filters.
          FALLTHROUGH_INTENDED;
        case FilterType::kNoFilter:
          corrupted_filter_type = false;
          break;
      }
      if (corrupted_filter_type) {
        RLOG(InfoLogLevel::FATAL_LEVEL, rep->ioptions.info_log, "Corrupted bloom filter type: %d",
            rep->filter_type);
        assert(false);
        return STATUS_SUBSTITUTE(Corruption, "Corrupted bloom filter type: $0", rep->filter_type);
      }
    } else {
      // If we don't use block cache for filter access, we'll pre-load these blocks, which will
      // kept in member variables in Rep and with a same life-time as this table object.
      bool corrupted_filter_type = true;
      switch (rep->filter_type) {
        case FilterType::kFullFilter:
          FALLTHROUGH_INTENDED;
        case FilterType::kBlockBasedFilter:
          rep->filter.reset(ReadFilterBlock(rep->filter_handle, rep, nullptr));
          corrupted_filter_type = false;
          break;
        case FilterType::kFixedSizeFilter:
          // We never pre-load fixed-size bloom filters.
          FALLTHROUGH_INTENDED;
        case FilterType::kNoFilter:
          corrupted_filter_type = false;
          break;
      }
      if (corrupted_filter_type) {
        RLOG(InfoLogLevel::FATAL_LEVEL, rep->ioptions.info_log, "Corrupted bloom filter type: %d",
            rep->filter_type);
        assert(false);
        return STATUS_SUBSTITUTE(Corruption, "Corrupted bloom filter type: $0", rep->filter_type);
      }
    }
  }

  *table_reader = std::move(new_table);

  return Status::OK();
}

Status BlockBasedTable::ReadPropertiesBlock(InternalIterator* meta_iter) {
  // Read the properties
  bool found_properties_block = true;
  auto s = SeekToPropertiesBlock(meta_iter, &found_properties_block);

  if (!s.ok()) {
    RLOG(InfoLogLevel::WARN_LEVEL, rep_->ioptions.info_log,
        "Cannot seek to properties block from file: %s",
        s.ToString().c_str());
    return s;
  }

  if (found_properties_block) {
    s = meta_iter->status();
    TableProperties* table_properties = nullptr;
    if (s.ok()) {
      s = ReadProperties(
            meta_iter->value(), rep_->base_reader_with_cache_prefix->reader.get(),
            rep_->footer, rep_->ioptions.env, rep_->ioptions.info_log, &table_properties);
    }

    if (!s.ok()) {
      RLOG(InfoLogLevel::WARN_LEVEL, rep_->ioptions.info_log,
        "Encountered error while reading data from properties "
        "block %s", s.ToString().c_str());
      return s;
    }
    rep_->table_properties.reset(table_properties);
  } else {
    RLOG(InfoLogLevel::ERROR_LEVEL, rep_->ioptions.info_log,
        "Cannot find Properties block from file.");
  }

  // Determine whether whole key filtering is supported.
  if (rep_->table_properties) {
    rep_->whole_key_filtering &=
        IsFeatureSupported(*(rep_->table_properties),
                           BlockBasedTablePropertyNames::kWholeKeyFiltering,
                           rep_->ioptions.info_log);
    rep_->prefix_filtering &= IsFeatureSupported(
        *(rep_->table_properties),
        BlockBasedTablePropertyNames::kPrefixFiltering, rep_->ioptions.info_log);

    auto& props = rep_->table_properties->user_collected_properties;
    auto it = props.find(BlockBasedTablePropertyNames::kDataBlockKeyValueEncodingFormat);
    if (it != props.end()) {
      rep_->data_block_key_value_encoding_format =
          static_cast<KeyValueEncodingFormat>(DecodeFixed8(it->second.c_str()));
    }
  }

  return Status::OK();
}

Status BlockBasedTable::SetupFilter(InternalIterator* meta_iter) {
  // Find filter handle and filter type.
  if (!rep_->filter_policy) {
    return Status::OK();
  }
  const auto& table_filter_policy_name = rep_->table_properties->filter_policy_name;
  if (rep_->filter_policy->Name() != table_filter_policy_name &&
      !table_filter_policy_name.empty()) {
    // SST file has been written using another filter policy - use it for reading if it is still
    // supported.
    const FilterPolicy* table_filter_policy = nullptr;
    const auto& policies = rep_->table_options.supported_filter_policies;
    if (policies) {
      const auto it = policies->find(table_filter_policy_name);
      if (it != policies->end()) {
        table_filter_policy = it->second.get();
      }
    }
    if (!table_filter_policy) {
      rep_->filter_policy = nullptr;
      rep_->filter_key_transformer = nullptr;
      const auto error_message = yb::Format(
        "Filter policy '$0' is not supported, not using use bloom filters for reading '$1'",
          table_filter_policy_name,
          rep_->base_reader_with_cache_prefix->reader->file()->filename());
      RLOG(InfoLogLevel::ERROR_LEVEL, rep_->ioptions.info_log, error_message.c_str());
      // For testing in debug build we want to fail in case some filter policy is not supported, but
      // for production we prefer to continue operation with lower performance due to lack of
      // supported bloom filters for this file. And eventually during compaction this file will
      // be replaced and latest version of filter policy will be used.
#ifndef NDEBUG
      return STATUS(IllegalState, error_message);
#else
      return Status::OK();
#endif
    }
    rep_->filter_policy = table_filter_policy;
    rep_->filter_key_transformer = table_filter_policy->GetKeyTransformer();
  }

  for (const auto& prefix : {block_based_table::kFullFilterBlockPrefix,
                             block_based_table::kFilterBlockPrefix,
                             block_based_table::kFixedSizeFilterBlockPrefix}) {
    // Unsuccessful read implies we should not use filter.
    std::string filter_block_key = prefix;
    filter_block_key.append(rep_->filter_policy->Name());
    if (FindMetaBlock(meta_iter, filter_block_key, &rep_->filter_handle).ok()) {
      if (prefix == block_based_table::kFullFilterBlockPrefix) {
        rep_->filter_type = FilterType::kFullFilter;
      } else if (prefix == block_based_table::kFilterBlockPrefix) {
        rep_->filter_type = FilterType::kBlockBasedFilter;
      } else if (prefix == block_based_table::kFixedSizeFilterBlockPrefix) {
        rep_->filter_type = FilterType::kFixedSizeFilter;
      } else {
        // That means we have memory corruption, so we should fail.
        RLOG(
            InfoLogLevel::FATAL_LEVEL, rep_->ioptions.info_log, "Invalid filter block prefix: %s",
            prefix);
        assert(false);
        return STATUS(Corruption, "Invalid filter block prefix", prefix);
      }
      break;
    }
  }

  return Status::OK();
}

void BlockBasedTable::SetDataFileReader(unique_ptr<RandomAccessFileReader> &&data_file) {
  rep_->data_reader_with_cache_prefix =
      std::make_shared<FileReaderWithCachePrefix>(std::move(data_file));
  SetupCacheKeyPrefix(rep_, rep_->data_reader_with_cache_prefix.get());
}

namespace {
void SetupFileReaderForCompaction(const Options::AccessHint &access_hint,
    RandomAccessFileReader *reader) {
  if (reader != nullptr) {
    switch (access_hint) {
      case Options::NONE:
        break;
      case Options::NORMAL:
        reader->file()->Hint(RandomAccessFile::NORMAL);
        break;
      case Options::SEQUENTIAL:
        reader->file()->Hint(RandomAccessFile::SEQUENTIAL);
        break;
      case Options::WILLNEED:
        reader->file()->Hint(RandomAccessFile::WILLNEED);
        break;
      default:
        assert(false);
    }
  }
}
} // anonymous namespace

void BlockBasedTable::SetupForCompaction() {
  auto access_hint = rep_->ioptions.access_hint_on_compaction_start;
  ::rocksdb::SetupFileReaderForCompaction(access_hint,
      rep_->base_reader_with_cache_prefix->reader.get());
  ::rocksdb::SetupFileReaderForCompaction(access_hint,
      rep_->data_reader_with_cache_prefix->reader.get());
}

std::shared_ptr<const TableProperties> BlockBasedTable::GetTableProperties()
    const {
  return rep_->table_properties;
}

size_t BlockBasedTable::ApproximateMemoryUsage() const {
  size_t usage = 0;
  if (rep_->filter) {
    usage += rep_->filter->ApproximateMemoryUsage();
  }
  if (rep_->filter_index_reader) {
    usage += rep_->filter_index_reader->ApproximateMemoryUsage();
  }
  IndexReader* data_index_reader = rep_->data_index_reader.get(std::memory_order_relaxed);
  if (data_index_reader) {
    usage += data_index_reader->ApproximateMemoryUsage();
  }
  return usage;
}

// Load the meta-block from the file. On success, return the loaded meta block
// and its iterator.
Status BlockBasedTable::ReadMetaBlock(Rep* rep,
                                      std::unique_ptr<Block>* meta_block,
                                      std::unique_ptr<InternalIterator>* iter) {
  // TODO(sanjay): Skip this if footer.metaindex_handle() size indicates
  // it is an empty block.
  // TODO: we never really verify check sum for meta index block
  std::unique_ptr<Block> meta;
  Status s = block_based_table::ReadBlockFromFile(
      rep->base_reader_with_cache_prefix->reader.get(),
      rep->footer,
      ReadOptions::kDefault,
      rep->footer.metaindex_handle(),
      &meta,
      rep->ioptions.env,
      rep->mem_tracker);

  if (!s.ok()) {
    RLOG(InfoLogLevel::ERROR_LEVEL, rep->ioptions.info_log,
        "Encountered error while reading data from properties"
        " block %s", s.ToString().c_str());
    return s;
  }

  // Instead of reading the checksum of the meta-block from disk, it is simpler and more
  // efficient to compute it based on the block data. Some benefits include:
  // (1) Minimizing changes: Reading the checksum from disk would require modifications in many
  //     functions.
  // (2) The function is executed only once when a file is opened, so the overhead is minimal.
  // (3) Checksum calculation is limited to the meta-block, ensuring that reading other blocks
  //     remains unaffected.
  rep->meta_block_checksum = block_based_table::ComputeChecksumForBlockCacheKey(
      rep->table_options.checksum, Slice(meta->data(), meta->size()));

  *meta_block = std::move(meta);
  // meta block uses bytewise comparator.
  iter->reset(
      meta_block->get()->NewIterator(BytewiseComparator(), kMetaIndexBlockKeyValueEncodingFormat));
  return Status::OK();
}

namespace {

Tickers GetBlockCacheMissTicker(BlockType block_type) {
  switch (block_type) {
    case BlockType::kData:
      return BLOCK_CACHE_DATA_MISS;
    case BlockType::kIndex:
      return BLOCK_CACHE_INDEX_MISS;
  }
  FATAL_INVALID_ENUM_VALUE(BlockType, block_type);
}

Tickers GetBlockCacheHitTicker(BlockType block_type) {
  switch (block_type) {
    case BlockType::kData:
      return BLOCK_CACHE_DATA_HIT;
    case BlockType::kIndex:
      return BLOCK_CACHE_INDEX_HIT;
  }
  FATAL_INVALID_ENUM_VALUE(BlockType, block_type);
}

} // namespace

Status BlockBasedTable::GetDataBlockFromCache(
    const Slice& block_cache_key, const Slice& compressed_block_cache_key,
    Cache* block_cache, Cache* block_cache_compressed, Statistics* statistics,
    const ReadOptions& read_options, BlockBasedTable::CachableEntry<Block>* block,
    uint32_t format_version, BlockType block_type,
    const std::shared_ptr<yb::MemTracker>& mem_tracker) {
  Block* compressed_block = nullptr;
  Cache::Handle* block_cache_compressed_handle = nullptr;

  // Lookup uncompressed cache first
  if (block_cache != nullptr) {
    block->cache_handle =
        GetEntryFromCache(
            block_cache, block_cache_key, GetBlockCacheMissTicker(block_type),
            GetBlockCacheHitTicker(block_type), statistics, read_options.query_id);
    if (block->cache_handle != nullptr) {
      block->value =
          static_cast<Block*>(block_cache->Value(block->cache_handle));
      return Status::OK();
    }
  }

  // If not found, search from the compressed block cache.
  assert(block->cache_handle == nullptr && block->value == nullptr);

  if (block_cache_compressed == nullptr) {
    return Status::OK();
  }

  assert(!compressed_block_cache_key.empty());
  block_cache_compressed_handle =
      block_cache_compressed->Lookup(compressed_block_cache_key, read_options.query_id);
  // if we found in the compressed cache, then uncompress and insert into
  // uncompressed cache
  if (block_cache_compressed_handle == nullptr) {
    RecordTick(statistics, BLOCK_CACHE_COMPRESSED_MISS);
    return Status::OK();
  }

  // found compressed block
  RecordTick(statistics, BLOCK_CACHE_COMPRESSED_HIT);
  compressed_block = static_cast<Block*>(
      block_cache_compressed->Value(block_cache_compressed_handle));
  assert(compressed_block->compression_type() != kNoCompression);

  // Retrieve the uncompressed contents into a new buffer
  BlockContents contents;
  auto s = UncompressBlockContents(compressed_block->data(), compressed_block->size(), &contents,
                              format_version, mem_tracker);

  // Insert uncompressed block into block cache
  if (s.ok()) {
    block->value = new Block(std::move(contents));  // uncompressed block
    assert(block->value->compression_type() == kNoCompression);
    if (block_cache != nullptr && block->value->cachable() &&
        read_options.fill_cache) {
      s = block_cache->Insert(block_cache_key, read_options.query_id, block->value,
                              block->value->usable_size(), &DeleteCachedEntry<Block>,
                              &block->cache_handle, statistics);
      if (!s.ok()) {
        delete block->value;
        block->value = nullptr;
      }
    }
  }

  // Release hold on compressed cache entry
  block_cache_compressed->Release(block_cache_compressed_handle);
  return s;
}

Status BlockBasedTable::PutDataBlockToCache(
    const Slice& block_cache_key, const Slice& compressed_block_cache_key,
    Cache* block_cache, Cache* block_cache_compressed,
    const ReadOptions& read_options, Statistics* statistics,
    CachableEntry<Block>* block, Block* raw_block, uint32_t format_version,
    const std::shared_ptr<yb::MemTracker>& mem_tracker) {
  assert(raw_block->compression_type() == kNoCompression ||
         block_cache_compressed != nullptr);

  Status s;
  // Retrieve the uncompressed contents into a new buffer
  BlockContents contents;
  if (raw_block->compression_type() != kNoCompression) {
    s = UncompressBlockContents(raw_block->data(), raw_block->size(), &contents,
                                format_version, mem_tracker);
  }
  if (!s.ok()) {
    delete raw_block;
    return s;
  }

  if (raw_block->compression_type() != kNoCompression) {
    block->value = new Block(std::move(contents));  // uncompressed block
  } else {
    block->value = raw_block;
    raw_block = nullptr;
  }

  // Insert compressed block into compressed block cache.
  // Release the hold on the compressed cache entry immediately.
  if (block_cache_compressed != nullptr && raw_block != nullptr &&
      raw_block->cachable()) {
    s = block_cache_compressed->Insert(compressed_block_cache_key, read_options.query_id, raw_block,
                                       raw_block->usable_size(), &DeleteCachedEntry<Block>);
    if (s.ok()) {
      // Avoid the following code to delete this cached block.
      raw_block = nullptr;
      RecordTick(statistics, BLOCK_CACHE_COMPRESSED_ADD);
    } else {
      RecordTick(statistics, BLOCK_CACHE_COMPRESSED_ADD_FAILURES);
    }
  }
  delete raw_block;

  // insert into uncompressed block cache
  assert((block->value->compression_type() == kNoCompression));
  if (block_cache != nullptr && block->value->cachable()) {
    s = block_cache->Insert(block_cache_key, read_options.query_id, block->value,
                            block->value->usable_size(),
                            &DeleteCachedEntry<Block>, &block->cache_handle, statistics);
    if (!s.ok()) {
      delete block->value;
      block->value = nullptr;
    }
  }

  return s;
}

Result<std::unique_ptr<BinarySearchIndexReader>> BlockBasedTable::CreateFilterIndexReader() {
  auto base_file_reader = rep_->base_reader_with_cache_prefix->reader.get();
  auto env = rep_->ioptions.env;
  auto footer = rep_->footer;
  return BinarySearchIndexReader::Create(base_file_reader, footer, rep_->filter_handle, env,
      SharedBytewiseComparator(), rep_->mem_tracker);
}

FilterBlockReader* BlockBasedTable::ReadFilterBlock(const BlockHandle& filter_handle, Rep* rep,
    size_t* filter_size, Statistics* statistics) {
  // TODO: We might want to unify with ReadBlockFromFile() if we start
  // requiring checksum verification in Table::Open.
  if (rep->filter_type == FilterType::kNoFilter) {
    return nullptr;
  }
  BlockContents block;
  ReadOptions options = ReadOptions::kDefault;
  options.statistics = statistics;
  if (!ReadBlockContents(
           rep->base_reader_with_cache_prefix->reader.get(), rep->footer, options,
           filter_handle, &block, rep->ioptions.env, rep->mem_tracker, false).ok()) {
    // Error reading the block
    return nullptr;
  }

  if (filter_size) {
    *filter_size = block.data.size();
  }

  assert(rep->filter_policy);

  switch (rep->filter_type) {
    case FilterType::kNoFilter:
      // Shouldn't happen, since we already checked for that above. In case of memory corruption
      // will be caught after switch statement.
      break;
    case FilterType::kBlockBasedFilter:
      return new BlockBasedFilterBlockReader(
          rep->prefix_filtering ? rep->ioptions.prefix_extractor : nullptr,
          rep->table_options, rep->whole_key_filtering, std::move(block));
    case FilterType::kFullFilter: {
      auto filter_bits_reader = rep->filter_policy->GetFilterBitsReader(block.data);
      assert(filter_bits_reader);
      return new FullFilterBlockReader(
          rep->prefix_filtering ? rep->ioptions.prefix_extractor : nullptr,
          rep->whole_key_filtering, std::move(block), filter_bits_reader);
    }
    case FilterType::kFixedSizeFilter:
      return new FixedSizeFilterBlockReader(
          rep->prefix_filtering ? rep->ioptions.prefix_extractor : nullptr,
          rep->table_options, rep->whole_key_filtering, std::move(block));
      break;
  }
  RLOG(InfoLogLevel::FATAL_LEVEL, rep->ioptions.info_log, "Corrupted filter_type: %d",
      rep->filter_type);
  return nullptr;
}

Status BlockBasedTable::GetFixedSizeFilterBlockHandle(const Slice& filter_key,
    BlockHandle* filter_block_handle) const {
  // Determine block of fixed-size bloom filter using filter index. It is expected `NewIterator()`
  // is reusing `fiter` and not creating a new iterator (multi-level index case).
  BlockIter fiter;
  rep_->filter_index_reader->NewIterator(&fiter);
  const auto& entry = fiter.Seek(filter_key);
  if (entry.Valid()) {
    Slice filter_block_handle_encoded = entry.value;
    return filter_block_handle->DecodeFrom(&filter_block_handle_encoded);
  } else {
    // We are beyond the index, that means key is absent in filter, we use null block handle
    // stub to indicate that.
    filter_block_handle->set_offset(0);
    filter_block_handle->set_size(0);
    return Status::OK();
  }
}

Slice BlockBasedTable::GetFilterKeyFromInternalKey(Slice internal_key) const {
  return GetFilterKeyFromUserKey(ExtractUserKey(internal_key));
}

Slice BlockBasedTable::GetFilterKeyFromUserKey(Slice user_key) const {
  FilterKeyCache filter_key_cache(user_key);
  return GetFilterKeyFromUserKey(user_key, &filter_key_cache);
}

Slice BlockBasedTable::GetFilterKeyFromUserKey(
    Slice user_key, FilterKeyCache* filter_key_cache) const {
  auto transformer = rep_->filter_key_transformer;
  if (transformer != filter_key_cache->transformer) {
    filter_key_cache->transformer = transformer;
    filter_key_cache->filter_key = transformer ? transformer->Transform(user_key) : user_key;
  }
  return filter_key_cache->filter_key;
}

BlockBasedTable::CachableEntry<FilterBlockReader> BlockBasedTable::GetFilter(
    const QueryId query_id,
    bool no_io,
    const Slice* filter_key,
    Statistics* statistics) const {
  const bool is_fixed_size_filter = rep_->filter_type == FilterType::kFixedSizeFilter;

  // Key is required for fixed size filter.
  assert(!is_fixed_size_filter || filter_key != nullptr);

  // If cache_index_and_filter_blocks is false, filter (except fixed-size filter) should be
  // pre-populated.
  // We will return rep_->filter anyway. rep_->filter can be nullptr if filter
  // read fails at Open() time. We don't want to reload again since it will
  // most probably fail again.
  // Note: rep_->filter can be nullptr also if Open was called with
  // prefetch_index_and_filter == false. That means bloom filters are not be used if
  // both prefetch_index_and_filter and table_options.cache_index_and_filter_blocks are false.
  if (!rep_->table_options.cache_index_and_filter_blocks && !is_fixed_size_filter) {
    return {rep_->filter.get(), false};
  }

  PERF_TIMER_GUARD(read_filter_block_nanos);

  Cache* block_cache = rep_->table_options.block_cache.get();
  if (rep_->filter_policy == nullptr /* do not use filter */ ||
      block_cache == nullptr /* no block cache at all */) {
    // If we get here, we have:
    // table_options.cache_index_and_filter_blocks || is_fixed_size_filter
    // table_options.block_cache == nullptr
    return {};
  }

  const BlockHandle* filter_block_handle;
  // Determine filter block handle
  BlockHandle fixed_size_filter_block_handle;
  if (is_fixed_size_filter) {
    Status s = GetFixedSizeFilterBlockHandle(*filter_key, &fixed_size_filter_block_handle);
    if (s.ok()) {
      if (fixed_size_filter_block_handle.IsNull()) {
        // Key is beyond filter index - return stub filter.
        return rep_->not_matching_filter_entry;
      }
      filter_block_handle = &fixed_size_filter_block_handle;
    } else {
      // If we failed to decode filter block handle from filter index we will just log error in
      // production to continue operation in case of just filter corruption,
      // but we should fail in debug and under tests to be able to catch possible bugs.
      RLOG(InfoLogLevel::ERROR_LEVEL, rep_->ioptions.info_log,
          "Failed to decode fixed-size filter block handle from filter index.");
      FAIL_IF_NOT_PRODUCTION();
      return {};
    }
  } else {
    filter_block_handle = &rep_->filter_handle;
  }

  // Fetching from the cache
  char cache_key_buffer[block_based_table::kCacheKeyBufferSize];
  auto filter_block_cache_key = GetCacheKey(rep_->base_reader_with_cache_prefix->cache_key_prefix,
      *filter_block_handle, cache_key_buffer);

  Statistics* effective_statistics = statistics ? statistics : rep_->ioptions.statistics;
  auto cache_handle = GetEntryFromCache(block_cache, filter_block_cache_key,
      BLOCK_CACHE_FILTER_MISS, BLOCK_CACHE_FILTER_HIT, effective_statistics, query_id);

  FilterBlockReader* filter = nullptr;
  if (cache_handle != nullptr) {
    filter = static_cast<FilterBlockReader*>(block_cache->Value(cache_handle));
  } else if (no_io && rep_->filter_type != FilterType::kFixedSizeFilter) {
    // Do not invoke any io.
    return CachableEntry<FilterBlockReader>();
  } else {
    // For fixed-size filter we don't prefetch all filter blocks and ignore no_io parameter always
    // loading necessary filter block through block cache.
    size_t filter_size = 0;
    filter = ReadFilterBlock(*filter_block_handle, rep_, &filter_size, effective_statistics);
    if (filter != nullptr) {
      assert(filter_size > 0);
      Status s = block_cache->Insert(filter_block_cache_key, query_id,
                                     filter, filter_size,
                                     &DeleteCachedEntry<FilterBlockReader>, &cache_handle,
                                     effective_statistics);
      if (!s.ok()) {
        delete filter;
        return CachableEntry<FilterBlockReader>();
      }
    }
  }

  return { filter, *cache_handle };
}

yb::Result<BlockBasedTable::CachableEntry<IndexReader>> BlockBasedTable::GetIndexReader(
    const ReadOptions& read_options) {
  auto* index_reader = rep_->data_index_reader.get(std::memory_order_acquire);
  if (index_reader) {
    // Index reader has already been pre-populated.
    return BlockBasedTable::CachableEntry<IndexReader>{index_reader, false};
  }
  PERF_TIMER_GUARD(read_index_block_nanos);

  const bool no_io = read_options.read_tier == kBlockCacheTier;
  Cache* const block_cache = rep_->table_options.block_cache.get();

  if (block_cache && (rep_->data_index_load_mode == DataIndexLoadMode::USE_CACHE ||
      rep_->table_options.cache_index_and_filter_blocks)) {
    char cache_key[block_based_table::kCacheKeyBufferSize];
    auto key = GetCacheKey(rep_->base_reader_with_cache_prefix->cache_key_prefix,
        rep_->footer.index_handle(), cache_key);
    Statistics* statistics =
        read_options.statistics ? read_options.statistics : rep_->ioptions.statistics;
    auto cache_handle =
        GetEntryFromCache(block_cache, key, BLOCK_CACHE_INDEX_MISS,
            BLOCK_CACHE_INDEX_HIT, statistics, read_options.query_id);

    if (cache_handle == nullptr && no_io) {
      return ReturnNoIOError();
    }

    if (cache_handle != nullptr) {
      index_reader = static_cast<IndexReader*>(block_cache->Value(cache_handle));
    } else {
      // Create index reader and put it in the cache.
      auto index_reader_unique = VERIFY_RESULT(CreateDataBlockIndexReader());
      RETURN_NOT_OK(block_cache->Insert(
          key, read_options.query_id, index_reader_unique.get(), index_reader_unique->usable_size(),
          &DeleteCachedEntry<IndexReader>, &cache_handle, statistics));
      DCHECK(cache_handle || read_options.query_id == kNoCacheQueryId);
      index_reader = index_reader_unique.release();
      if (!cache_handle) {
        return BlockBasedTable::CachableEntry<IndexReader>{index_reader, true};
      }
    }

    return BlockBasedTable::CachableEntry<IndexReader>{index_reader, *cache_handle};
  } else {
    if (no_io) {
      return ReturnNoIOError();
    }
    // Note that we've already performed first check at the beginning of method.
    std::lock_guard lock(rep_->data_index_reader_mutex);
    index_reader = rep_->data_index_reader.get(std::memory_order_relaxed);
    if (!index_reader) {
      // preloaded_meta_index_iter is not needed for kBinarySearch data index which DocDB uses,
      // for kHashSearch data index it will do one more access to file to load it.
      // TODO: if we need to optimize kHashSearch data index load, we can preload and store in
      // rep_ meta index with iterator during Open.
      index_reader = VERIFY_RESULT(CreateDataBlockIndexReader(
          /* preloaded_meta_index_iter =*/ nullptr)).release();
      rep_->data_index_reader.reset(index_reader, std::memory_order_acq_rel);
    }
    return BlockBasedTable::CachableEntry<IndexReader>{index_reader, false};
  }
}

template <typename IndexIteratorType>
void BlockBasedTable::RegisterCleanupForIndexIterator(
    const CachableEntry<IndexReader>& index_reader_entry, IndexIteratorType* iter) {
  if (index_reader_entry.cache_handle) {
    iter->RegisterCleanup(
        &ReleaseCachedEntry, rep_->table_options.block_cache.get(),
        index_reader_entry.cache_handle);
  } else if (index_reader_entry.owns) {
    iter->RegisterCleanup(
        &DestroyCachedEntry<IndexReader>, index_reader_entry.value, nullptr);
  }
}

namespace {

constexpr bool kSkipFiltersForIndex = true;

} // namespace

// TODO: consider allocating index iterator on arena, try and measure potential performance
// improvements.
InternalIterator* BlockBasedTable::NewIndexIterator(
    const ReadOptions& read_options, BlockIter* input_iter) {
  const auto index_reader_result = GetIndexReader(read_options);
  if (!index_reader_result.ok()) {
    return ReturnErrorIterator(index_reader_result.status(), input_iter);
  }

  auto* new_iter = index_reader_result->value->NewIterator(
      input_iter,
      std::make_unique<BlockEntryIteratorState>(
          this, ReadOptions::kDefault, kSkipFiltersForIndex, BlockType::kIndex),
      read_options.total_order_seek);

  auto* iter = new_iter ? new_iter : input_iter;
  RegisterCleanupForIndexIterator(index_reader_result.get(), iter);

  return new_iter;
}

InternalIterator* BlockBasedTable::NewIndexIterator(const ReadOptions& read_options) {
  return NewIndexIterator(read_options, /* input_iter = */ nullptr);
}

DataBlockAwareIndexInternalIterator* BlockBasedTable::NewDataBlockAwareIndexIterator(
    const ReadOptions& read_options) {
  const auto index_reader_result = GetIndexReader(read_options);
  if (!index_reader_result.ok()) {
    return NewErrorIterator<DataBlockAwareIndexInternalIterator>(index_reader_result.status());
  }

  auto* iter = index_reader_result->value->NewDataBlockAwareIterator(
      /* iter = */ nullptr,
      std::make_unique<BlockEntryIteratorState>(
          this, ReadOptions::kDefault, kSkipFiltersForIndex, BlockType::kIndex),
      std::make_unique<BlockEntryIteratorState>(
          this, ReadOptions::kDefault, /* skip_filters = */ false, BlockType::kData),
      read_options.total_order_seek);

  RegisterCleanupForIndexIterator(index_reader_result.get(), iter);
  return iter;
}

// Using status return value because returning Result<BlockBasedTable::BlockRetrievalInfo> leads to
// 1.26x higher latency for scanG4_pkey_INclause_rangescan_increasing_rows_1000row workload on RF=1
// local cluster with release build.
// The size of the Result's underlying type matters and may provide a noticeable degradation in
// hot paths due to a compiler is not always able to use RVO/elide constructors and the Result
// with the corresponding type could be copied.
Status BlockBasedTable::GetBlockRetrievalInfo(
    Slice index_value, const BlockType block_type, BlockBasedTable::BlockRetrievalInfo* info) {
  info->type = block_type;

  // We intentionally allow extra stuff in index_value so that we can add more features in the
  // future.
  RETURN_NOT_OK(info->handle.DecodeFrom(&index_value));

  auto* reader = GetBlockReader(block_type);
  info->file_reader = reader->reader.get();

  // Create key for block cache.
  if (rep_->table_options.block_cache) {
    info->cache_key = GetCacheKey(reader->cache_key_prefix, info->handle, info->cache_key_buf);
  }

  if (rep_->table_options.block_cache_compressed) {
    info->compressed_cache_key = GetCacheKey(
        reader->compressed_cache_key_prefix, info->handle, info->compressed_cache_key_buf);
  }

  return Status::OK();
}

yb::Result<BlockBasedTable::CachableEntry<Block>> BlockBasedTable::GetBlockFromCache(
    const ReadOptions& ro, const BlockRetrievalInfo& block_info) {
  Cache* block_cache = rep_->table_options.block_cache.get();
  Cache* block_cache_compressed = rep_->table_options.block_cache_compressed.get();
  CachableEntry<Block> block;

  // If either block cache is enabled, we'll try to read from it.
  if ((block_cache != nullptr || block_cache_compressed != nullptr)) {
    Statistics* statistics = ro.statistics ? ro.statistics : rep_->ioptions.statistics;
    RETURN_NOT_OK(GetDataBlockFromCache(
        block_info.cache_key, block_info.compressed_cache_key, block_cache, block_cache_compressed,
        statistics, ro, &block, rep_->table_options.format_version, block_info.type,
        rep_->mem_tracker));
  }

  return block;
}

yb::Result<BlockBasedTable::CachableEntry<Block>>
BlockBasedTable::ReadBlockFromFileAndMaybePutToCache(
    const ReadOptions& ro, const BlockBasedTable::BlockRetrievalInfo& block_info) {
  if (ro.read_tier == kBlockCacheTier) {
    return ReturnNoIOError();
  }

  auto* block_cache = rep_->table_options.block_cache.get();
  auto* block_cache_compressed = rep_->table_options.block_cache_compressed.get();

  Statistics* statistics = ro.statistics ? ro.statistics : rep_->ioptions.statistics;

  std::unique_ptr<Block> raw_block;
  {
    // Don't uncompress for now if we need to fill compressed block cache.
    // It will be uncompressed by PutDataBlockToCache.
    const auto skip_uncompress = ro.fill_cache && block_cache_compressed != nullptr;
    StopWatch sw(rep_->ioptions.env, statistics, READ_BLOCK_GET_MICROS);
    RETURN_NOT_OK(block_based_table::ReadBlockFromFile(
        block_info.file_reader, rep_->footer, ro, block_info.handle, &raw_block, rep_->ioptions.env,
        rep_->mem_tracker, /* do_uncompress = */ !skip_uncompress));
  }

  CachableEntry<Block> block;
  if (ro.fill_cache && (block_cache != nullptr || block_cache_compressed != nullptr)) {
    RETURN_NOT_OK(PutDataBlockToCache(
        block_info.cache_key, block_info.compressed_cache_key, block_cache, block_cache_compressed,
        ro, statistics, &block, raw_block.release(), rep_->table_options.format_version,
        rep_->mem_tracker));
  } else {
    block.value = raw_block.release();
  }

  RSTATUS_DCHECK(block.value, Incomplete, "No block"); // Not expected to happen.
  return block;
}

yb::Result<BlockBasedTable::CachableEntry<Block>> BlockBasedTable::RetrieveBlock(
    const ReadOptions& ro, const Slice index_value, const BlockType block_type) {
  BlockRetrievalInfo block_info;
  RETURN_NOT_OK(GetBlockRetrievalInfo(index_value, block_type, &block_info));
  auto block_res = GetBlockFromCache(ro, block_info);
  if (block_res.ok() && !block_res->value) {
    return ReadBlockFromFileAndMaybePutToCache(ro, block_info);
  }
  return block_res;
}

namespace {

int GetBlockRestartInterval(const BlockBasedTableOptions& table_options, BlockType block_type) {
  return block_type == BlockType::kData ? table_options.block_restart_interval
                                        : table_options.index_block_restart_interval;
}

size_t GetRestartBlockCacheCapacity(
    const ReadOptions& read_options,
    const BlockBasedTableOptions& table_options,
    BlockType block_type) {
  if (FLAGS_rocksdb_iterator_disable_restart_block_keys_caching ||
      !read_options.cache_restart_block_keys) {
    return 0;
  }

  return yb::make_unsigned(GetBlockRestartInterval(table_options, block_type));
}

} // namespace

InternalIterator* BlockBasedTable::NewBlockIterator(
    const ReadOptions& read_options, BlockBasedTable::CachableEntry<Block>* block,
    BlockType block_type, BlockIter* input_iter) {
  const auto restart_block_cache_capacity =
      GetRestartBlockCacheCapacity(read_options, rep_->table_options, block_type);
  InternalIterator* iter = block->value->NewIterator(
      rep_->comparator.get(), GetKeyValueEncodingFormat(block_type), input_iter,
      /* total_order_seek = */ true, restart_block_cache_capacity);
  if (block->cache_handle) {
    Cache* block_cache = rep_->table_options.block_cache.get();
    iter->RegisterCleanup(&ReleaseCachedEntry, block_cache, block->cache_handle);
  } else {
    iter->RegisterCleanup(&DeleteHeldResource<Block>, block->value, nullptr);
  }
  return iter;
}

InternalIterator* BlockBasedTable::NewBlockIterator(
    const ReadOptions& ro, const Slice index_value, BlockType block_type, BlockIter* input_iter) {
  PERF_TIMER_GUARD(new_table_block_iter_nanos);

  auto block = RetrieveBlock(ro, index_value, block_type);
  if (!block.ok()) {
    return ReturnErrorIterator(block.status(), input_iter);
  }

  return NewBlockIterator(ro, block.get_ptr(), block_type, input_iter);
}

// This will be broken if the user specifies an unusual implementation
// of Options.comparator, or if the user specifies an unusual
// definition of prefixes in BlockBasedTableOptions.filter_policy.
// In particular, we require the following three properties:
//
// 1) key.starts_with(prefix(key))
// 2) Compare(prefix(key), key) <= 0.
// 3) If Compare(key1, key2) <= 0, then Compare(prefix(key1), prefix(key2)) <= 0
//
// Otherwise, this method guarantees no I/O will be incurred.
//
// REQUIRES: this method shouldn't be called while the DB lock is held.
bool BlockBasedTable::PrefixMayMatch(const ReadOptions& read_options, const Slice& internal_key) {
  if (!rep_->filter_policy) {
    return true;
  }

  assert(rep_->ioptions.prefix_extractor != nullptr);
  auto user_key = ExtractUserKey(internal_key);
  auto filter_key = GetFilterKeyFromUserKey(user_key);
  if (filter_key.empty() ||
      !rep_->ioptions.prefix_extractor->InDomain(filter_key) ||
      !rep_->ioptions.prefix_extractor->InDomain(user_key)) {
    return true;
  }
  auto user_key_prefix = rep_->ioptions.prefix_extractor->Transform(user_key);
  auto filter_key_prefix = rep_->ioptions.prefix_extractor->Transform(filter_key);
  InternalKey internal_key_prefix(user_key_prefix, kMaxSequenceNumber, kTypeValue);
  auto internal_prefix = internal_key_prefix.Encode();

  bool may_match = true;
  Status s;

  // To prevent any io operation in this method, we set `read_tier` to make
  // sure we always read index or filter only when they have already been
  // loaded to memory.
  ReadOptions no_io_read_options;
  no_io_read_options.read_tier = kBlockCacheTier;
  no_io_read_options.statistics = read_options.statistics;

  // First check non block-based filter.
  auto filter_entry = GetFilter(no_io_read_options.query_id, true /* no io */, &filter_key,
                                read_options.statistics);
  FilterBlockReader* filter = filter_entry.value;
  const bool is_block_based_filter = rep_->filter_type == FilterType::kBlockBasedFilter;
  if (filter != nullptr && !is_block_based_filter) {
    may_match = filter->PrefixMayMatch(filter_key_prefix);
  }

  // If filter is block-based or checking filter was not successful we need to get data block
  // offset. For block-based filter we need to know offset of data block to get and check
  // corresponding filter block. For non block-based filter we just need offset to try to get data
  // for the key.
  if (may_match) {
    unique_ptr<InternalIterator> iiter(NewIndexIterator(no_io_read_options));
    iiter->Seek(internal_prefix);

    if (!iiter->Valid()) {
      // we're past end of file
      // if it's incomplete, it means that we avoided I/O
      // and we're not really sure that we're past the end
      // of the file
      may_match = iiter->status().IsIncomplete();
    } else if (ExtractUserKey(iiter->key()).starts_with(
                ExtractUserKey(internal_prefix))) {
      // we need to check for this subtle case because our only
      // guarantee is that "the key is a string >= last key in that data
      // block" according to the doc/table_format.txt spec.
      //
      // Suppose iiter.key() starts with the desired prefix; it is not
      // necessarily the case that the corresponding data block will
      // contain the prefix, since iiter.key() need not be in the
      // block.  However, the next data block may contain the prefix, so
      // we return true to play it safe.
      may_match = true;
    } else if (filter != nullptr && is_block_based_filter) {
      // iiter.key() does NOT start with the desired prefix.  Because
      // Seek() finds the first key that is >= the seek target, this
      // means that iiter.key() > prefix.  Thus, any data blocks coming
      // after the data block corresponding to iiter.key() cannot
      // possibly contain the key.  Thus, the corresponding data block
      // is the only on could potentially contain the prefix.
      Slice handle_value = iiter->value();
      BlockHandle handle;
      s = handle.DecodeFrom(&handle_value);
      assert(s.ok());
      may_match = filter->PrefixMayMatch(filter_key_prefix, handle.offset());
    }
  }

  Statistics* statistics =
      read_options.statistics ? read_options.statistics : rep_->ioptions.statistics;
  RecordTick(statistics, BLOOM_FILTER_PREFIX_CHECKED);
  if (!may_match) {
    RecordTick(statistics, BLOOM_FILTER_PREFIX_USEFUL);
  }

  filter_entry.Release(rep_->table_options.block_cache.get());
  return may_match;
}

InternalIterator* BlockBasedTable::NewIterator(const ReadOptions& read_options,
                                               Arena* arena,
                                               bool skip_filters) {
  auto state = std::make_unique<BlockEntryIteratorState>(
      this, read_options, skip_filters, BlockType::kData);
  // TODO: unify the semantics across NewIterator callsites, so that we can pass an arena across
  // them, and decide the free / no free based on that. This callsite, for example, allows us to
  // put the top level iterator on the arena and potentially even the State object, however, not
  // the IndexIterator, as that does not expose arena allocation semantics...
  return NewTwoLevelIterator(
      state.release(), NewIndexIterator(read_options), arena, true /* need_free_iter_and_state */
  );
}

bool BlockBasedTable::NonBlockBasedFilterKeyMayMatch(
    FilterBlockReader* filter, Slice filter_key, Statistics* statistics) const {
  assert(rep_->filter_type != FilterType::kBlockBasedFilter);
  if (filter == nullptr) {
    return true;
  }
  RecordTick(statistics, rep_->ioptions.statistics, BLOOM_FILTER_CHECKED);
  if (!filter->KeyMayMatch(filter_key)) {
    // Record that the bloom filter was useful.
    RecordTick(statistics, rep_->ioptions.statistics, BLOOM_FILTER_USEFUL);
    return false;
  }
  if (rep_->ioptions.prefix_extractor &&
      rep_->ioptions.prefix_extractor->InDomain(filter_key) &&
      !filter->PrefixMayMatch(
          rep_->ioptions.prefix_extractor->Transform(filter_key))) {
    RecordTick(statistics, rep_->ioptions.statistics, BLOOM_FILTER_USEFUL);
    return false;
  }
  return true;
}

Status BlockBasedTable::Get(const ReadOptions& read_options, const Slice& internal_key,
                            GetContext* get_context, bool skip_filters) {
  Status s;
  CachableEntry<FilterBlockReader> filter_entry;
  Slice filter_key;
  if (!skip_filters) {
    filter_key = GetFilterKeyFromInternalKey(internal_key);
    if (!filter_key.empty()) {
      filter_entry =
          GetFilter(read_options.query_id, read_options.read_tier == kBlockCacheTier, &filter_key,
                    read_options.statistics);
    } else {
      skip_filters = true;
    }
  }
  FilterBlockReader* filter = filter_entry.value;

  const bool is_block_based_filter = rep_->filter_type == FilterType::kBlockBasedFilter;

  // First check non block-based filter.
  if (is_block_based_filter ||
      NonBlockBasedFilterKeyMayMatch(filter, filter_key, read_options.statistics)) {
    // Either filter is block-based or key may match.
    IndexIteratorHolder iiter_holder(this, read_options);
    InternalIterator& iiter = *iiter_holder.iter();

    RETURN_NOT_OK(iiter.status());

    bool done = false;
    for (iiter.Seek(internal_key); iiter.Valid() && !done; iiter.Next()) {
      {
        Slice data_block_handle_encoded = iiter.value();

        if (!skip_filters && is_block_based_filter) {
          RecordTick(rep_->ioptions.statistics, BLOOM_FILTER_CHECKED);
          BlockHandle data_block_handle;
          const bool absent_from_filter =
              data_block_handle.DecodeFrom(&data_block_handle_encoded).ok()
              && !filter->KeyMayMatch(filter_key, data_block_handle.offset());

          if (absent_from_filter) {
            // Not found
            // TODO: think about interaction with Merge. If a user key cannot
            // cross one data block, we should be fine.
            RecordTick(rep_->ioptions.statistics, BLOOM_FILTER_USEFUL);
            break;
          }
        }
      }

      BlockIter biter;
      NewBlockIterator(read_options, iiter.value(), BlockType::kData, &biter);

      if (read_options.read_tier == kBlockCacheTier &&
          biter.status().IsIncomplete()) {
        // couldn't get block from block_cache
        // Update Saver.state to Found because we are only looking for whether
        // we can guarantee the key is not there when "no_io" is set
        get_context->MarkKeyMayExist();
        break;
      }
      if (!biter.status().ok()) {
        s = biter.status();
        break;
      }

      // Call the *saver function on each entry/block until it returns false
      for (biter.Seek(internal_key); biter.Valid(); biter.Next()) {
        ParsedInternalKey parsed_key;
        if (!ParseInternalKey(biter.key(), &parsed_key)) {
          s = STATUS(Corruption, Slice());
        }

        if (!get_context->SaveValue(parsed_key, biter.value())) {
          done = true;
          break;
        }
      }
      s = biter.status();
    }
    if (s.ok()) {
      s = iiter.status();
    }
  }

  filter_entry.Release(rep_->table_options.block_cache.get());
  return s;
}

Status BlockBasedTable::Prefetch(const Slice* const begin,
                                 const Slice* const end) {
  auto& comparator = *rep_->comparator;
  // pre-condition
  if (begin && end && comparator.Compare(*begin, *end) > 0) {
    return STATUS(InvalidArgument, *begin, *end);
  }

  IndexIteratorHolder iiter_holder(this, ReadOptions::kDefault);
  InternalIterator& iiter = *iiter_holder.iter();

  RETURN_NOT_OK(iiter.status());

  // indicates if we are on the last page that need to be pre-fetched
  bool prefetching_boundary_page = false;

  for (begin ? iiter.Seek(*begin) : iiter.SeekToFirst(); iiter.Valid();
       iiter.Next()) {
    Slice block_handle = iiter.value();

    if (end && comparator.Compare(iiter.key(), *end) >= 0) {
      if (prefetching_boundary_page) {
        break;
      }

      // The index entry represents the last key in the data block.
      // We should load this page into memory as well, but no more
      prefetching_boundary_page = true;
    }

    // Load the block specified by the block_handle into the block cache
    BlockIter biter;
    NewBlockIterator(ReadOptions::kDefault, block_handle, BlockType::kData, &biter);

    if (!biter.status().ok()) {
      // there was an unexpected error while pre-fetching
      return biter.status();
    }
  }

  return Status::OK();
}

bool BlockBasedTable::TEST_KeyInCache(const ReadOptions& options,
                                      const Slice& key) {
  std::unique_ptr<InternalIterator> iiter(NewIndexIterator(options));
  iiter->Seek(key);
  assert(iiter->Valid());
  CachableEntry<Block> block;

  BlockHandle handle;
  Slice input = iiter->value();
  Status s = handle.DecodeFrom(&input);
  assert(s.ok());
  Cache* block_cache = rep_->table_options.block_cache.get();
  assert(block_cache != nullptr);

  char cache_key_storage[block_based_table::kCacheKeyBufferSize];
  Slice cache_key =
      GetCacheKey(rep_->data_reader_with_cache_prefix->cache_key_prefix, handle, cache_key_storage);
  Slice ckey;

  s = GetDataBlockFromCache(cache_key, ckey, block_cache, nullptr, nullptr, options, &block,
      rep_->table_options.format_version, BlockType::kData, rep_->mem_tracker);
  assert(s.ok());
  bool in_cache = block.value != nullptr;
  if (in_cache) {
    ReleaseCachedEntry(block_cache, block.cache_handle);
  }
  return in_cache;
}

// REQUIRES: The following fields of rep_ should have already been populated:
//  1. file
//  2. index_handle,
//  3. options
//  4. internal_comparator
//  5. index_type
Result<std::unique_ptr<IndexReader>> BlockBasedTable::CreateDataBlockIndexReader(
    InternalIterator* preloaded_meta_index_iter) {
  // Some old version of block-based tables don't have index type present in
  // table properties. If that's the case we can safely use the kBinarySearch.
  auto index_type_on_file = IndexType::kBinarySearch;
  if (rep_->table_properties) {
    auto& props = rep_->table_properties->user_collected_properties;
    auto pos = props.find(BlockBasedTablePropertyNames::kIndexType);
    if (pos != props.end()) {
      index_type_on_file = static_cast<IndexType>(
          DecodeFixed32(pos->second.c_str()));
    }
  }

  auto file = rep_->base_reader_with_cache_prefix->reader.get();
  auto env = rep_->ioptions.env;
  const auto& comparator = rep_->comparator;
  const Footer& footer = rep_->footer;

  if (index_type_on_file == IndexType::kHashSearch &&
      rep_->ioptions.prefix_extractor == nullptr) {
    RLOG(InfoLogLevel::WARN_LEVEL, rep_->ioptions.info_log,
        "IndexType::kHashSearch requires "
        "options.prefix_extractor to be set."
        " Fall back to binary search index.");
    index_type_on_file = IndexType::kBinarySearch;
  }

  switch (index_type_on_file) {
    case IndexType::kBinarySearch: {
      return BinarySearchIndexReader::Create(
          file, footer, footer.index_handle(), env, comparator, rep_->mem_tracker);
    }
    case IndexType::kHashSearch: {
      std::unique_ptr<Block> meta_guard;
      std::unique_ptr<InternalIterator> meta_iter_guard;
      auto meta_index_iter = preloaded_meta_index_iter;
      if (meta_index_iter == nullptr) {
        auto s = ReadMetaBlock(rep_, &meta_guard, &meta_iter_guard);
        if (!s.ok()) {
          // we simply fall back to binary search in case there is any
          // problem with prefix hash index loading.
          RLOG(InfoLogLevel::WARN_LEVEL, rep_->ioptions.info_log,
              "Unable to read the metaindex block."
              " Fall back to binary search index.");
          return BinarySearchIndexReader::Create(
              file, footer, footer.index_handle(), env, comparator, rep_->mem_tracker);
        }
        meta_index_iter = meta_iter_guard.get();
      }

      // We need to wrap data with internal_prefix_transform to make sure it can
      // handle prefix correctly.
      rep_->internal_prefix_transform.reset(
          new InternalKeySliceTransform(rep_->ioptions.prefix_extractor));
      return HashIndexReader::Create(
          rep_->internal_prefix_transform.get(), footer, file, env, comparator,
          footer.index_handle(), meta_index_iter, rep_->hash_index_allow_collision,
          rep_->mem_tracker);
    }
    case IndexType::kMultiLevelBinarySearch: {
      auto& props = DCHECK_NOTNULL(rep_->table_properties.get())->user_collected_properties;
      auto pos = props.find(BlockBasedTablePropertyNames::kNumIndexLevels);
      if (pos == props.end()) {
        return STATUS_FORMAT(
            NotFound, "Missed table property $0 for multi-level binary-search index",
            BlockBasedTablePropertyNames::kNumIndexLevels);
      }
      const auto num_levels = DecodeFixed32(pos->second.c_str());
      return MultiLevelIndexReader::Create(
          file, footer, num_levels, footer.index_handle(), env, comparator, rep_->mem_tracker);
    }
    default: {
      std::string error_message =
          "Unrecognized index type: " + ToString(rep_->index_type);
      return STATUS(InvalidArgument, error_message.c_str());
    }
  }
}

uint64_t BlockBasedTable::ApproximateOffsetOf(const Slice& key) {
  unique_ptr<InternalIterator> index_iter(NewIndexIterator(ReadOptions::kDefault));

  index_iter->Seek(key);
  uint64_t result;
  if (index_iter->Valid()) {
    BlockHandle handle;
    Slice input = index_iter->value();
    Status s = handle.DecodeFrom(&input);
    if (s.ok()) {
      result = handle.offset();
    } else {
      // Strange: we can't decode the block handle in the index block.
      // We'll just return the offset of the metaindex block, which is
      // close to the whole file size for this case.
      result = rep_->footer.metaindex_handle().offset();
    }
  } else {
    // key is past the last key in the file. If table_properties is not
    // available, approximate the offset by returning the offset of the
    // metaindex block (which is right near the end of the file).
    result = 0;
    if (rep_->table_properties) {
      result = rep_->table_properties->data_size;
    }
    // table_properties is not present in the table.
    if (result == 0) {
      result = rep_->footer.metaindex_handle().offset();
    }
  }
  return result;
}

bool BlockBasedTable::TEST_filter_block_preloaded() const {
  return rep_->filter != nullptr;
}

bool BlockBasedTable::TEST_index_reader_loaded() const {
  return rep_->data_index_reader.get() != nullptr;
}

Status BlockBasedTable::DumpTable(WritableFile* out_file) {
  // Output Footer
  RETURN_NOT_OK(out_file->Append(
      "Footer Details:\n"
      "--------------------------------------\n"
      "  "));
  RETURN_NOT_OK(out_file->Append(rep_->footer.ToString().c_str()));
  RETURN_NOT_OK(out_file->Append("\n"));

  // Output MetaIndex
  RETURN_NOT_OK(out_file->Append(
      "Metaindex Details:\n"
      "--------------------------------------\n"));
  std::unique_ptr<Block> meta;
  std::unique_ptr<InternalIterator> meta_iter;
  Status s = ReadMetaBlock(rep_, &meta, &meta_iter);
  if (s.ok()) {
    for (meta_iter->SeekToFirst(); meta_iter->Valid(); meta_iter->Next()) {
      s = meta_iter->status();
      if (!s.ok()) {
        return s;
      }
      if (meta_iter->key() == rocksdb::kPropertiesBlock) {
        RETURN_NOT_OK(out_file->Append("  Properties block handle: "));
        RETURN_NOT_OK(out_file->Append(meta_iter->value().ToString(true).c_str()));
        RETURN_NOT_OK(out_file->Append("\n"));
      } else if (strstr(meta_iter->key().ToString().c_str(),
                        "filter.rocksdb.") != nullptr) {
        RETURN_NOT_OK(out_file->Append("  Filter block handle: "));
        RETURN_NOT_OK(out_file->Append(meta_iter->value().ToString(true).c_str()));
        RETURN_NOT_OK(out_file->Append("\n"));
      }
    }
    RETURN_NOT_OK(out_file->Append("\n"));
  } else {
    return s;
  }

  // Output TableProperties
  const rocksdb::TableProperties* table_properties;
  table_properties = rep_->table_properties.get();

  if (table_properties != nullptr) {
    RETURN_NOT_OK(out_file->Append(
        "Table Properties:\n"
        "--------------------------------------\n"
        "  "));
    RETURN_NOT_OK(out_file->Append(table_properties->ToString("\n  ", ": ").c_str()));
    RETURN_NOT_OK(out_file->Append("\n"));
  }

  // Output Filter blocks
  if (!rep_->filter && !table_properties->filter_policy_name.empty()) {
    // Support only BloomFilter as off now
    rocksdb::BlockBasedTableOptions table_options;
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(1));
    if (table_properties->filter_policy_name.compare(
            table_options.filter_policy->Name()) == 0) {
      std::string filter_block_key = block_based_table::kFilterBlockPrefix;
      filter_block_key.append(table_properties->filter_policy_name);
      BlockHandle handle;
      if (FindMetaBlock(meta_iter.get(), filter_block_key, &handle).ok()) {
        BlockContents block;
        if (ReadBlockContents(
                rep_->base_reader_with_cache_prefix->reader.get(), rep_->footer,
                ReadOptions::kDefault, handle, &block, rep_->ioptions.env, rep_->mem_tracker,
                false).ok()) {
          rep_->filter.reset(new BlockBasedFilterBlockReader(
              rep_->ioptions.prefix_extractor, table_options,
              table_options.whole_key_filtering, std::move(block)));
        }
      }
    }
  }
  if (rep_->filter) {
    RETURN_NOT_OK(out_file->Append(
        "Filter Details:\n"
        "--------------------------------------\n"
        "  "));
    RETURN_NOT_OK(out_file->Append(rep_->filter->ToString().c_str()));
    RETURN_NOT_OK(out_file->Append("\n"));
  }

  // Output Index block
  s = DumpIndexBlock(out_file);
  if (!s.ok()) {
    return s;
  }
  // Output Data blocks
  s = DumpDataBlocks(out_file);

  return s;
}

Status BlockBasedTable::DumpIndexBlock(WritableFile* out_file) {
  RETURN_NOT_OK(out_file->Append(
      "Index Details:\n"
      "--------------------------------------\n"));

  std::unique_ptr<InternalIterator> blockhandles_iter(
      NewIndexIterator(ReadOptions::kDefault));
  Status s = blockhandles_iter->status();
  if (!s.ok()) {
    RETURN_NOT_OK(out_file->Append("Can not read Index Block \n\n"));
    return s;
  }

  RETURN_NOT_OK(out_file->Append("  Block key hex dump: Data block handle\n"));
  RETURN_NOT_OK(out_file->Append("  Block key ascii\n\n"));
  for (blockhandles_iter->SeekToFirst(); blockhandles_iter->Valid();
       blockhandles_iter->Next()) {
    s = blockhandles_iter->status();
    if (!s.ok()) {
      break;
    }
    Slice key = blockhandles_iter->key();
    InternalKey ikey = InternalKey::DecodeFrom(key);

    RETURN_NOT_OK(out_file->Append("  HEX    "));
    RETURN_NOT_OK(out_file->Append(ikey.user_key().ToString(true).c_str()));
    RETURN_NOT_OK(out_file->Append(": "));
    RETURN_NOT_OK(out_file->Append(blockhandles_iter->value().ToString(true).c_str()));
    RETURN_NOT_OK(out_file->Append("\n"));

    std::string str_key = ikey.user_key().ToString();
    std::string res_key("");
    char cspace = ' ';
    for (size_t i = 0; i < str_key.size(); i++) {
      res_key.append(&str_key[i], 1);
      res_key.append(1, cspace);
    }
    RETURN_NOT_OK(out_file->Append("  ASCII  "));
    RETURN_NOT_OK(out_file->Append(res_key.c_str()));
    RETURN_NOT_OK(out_file->Append("\n  ------\n"));
  }
  RETURN_NOT_OK(out_file->Append("\n"));
  return Status::OK();
}

Status BlockBasedTable::DumpDataBlocks(WritableFile* out_file) {
  std::unique_ptr<InternalIterator> blockhandles_iter(
      NewIndexIterator(ReadOptions::kDefault));
  Status s = blockhandles_iter->status();
  if (!s.ok()) {
    RETURN_NOT_OK(out_file->Append("Can not read Index Block \n\n"));
    return s;
  }

  size_t block_id = 1;
  for (blockhandles_iter->SeekToFirst(); blockhandles_iter->Valid();
       block_id++, blockhandles_iter->Next()) {
    s = blockhandles_iter->status();
    if (!s.ok()) {
      break;
    }

    RETURN_NOT_OK(out_file->Append("Data Block # "));
    RETURN_NOT_OK(out_file->Append(rocksdb::ToString(block_id)));
    RETURN_NOT_OK(out_file->Append(" @ "));
    RETURN_NOT_OK(out_file->Append(blockhandles_iter->value().ToString(true).c_str()));
    RETURN_NOT_OK(out_file->Append("\n"));
    RETURN_NOT_OK(out_file->Append("--------------------------------------\n"));

    std::unique_ptr<InternalIterator> datablock_iter;
    datablock_iter.reset(
        NewBlockIterator(ReadOptions::kDefault, blockhandles_iter->value(), BlockType::kData));
    s = datablock_iter->status();

    if (!s.ok()) {
      RETURN_NOT_OK(out_file->Append("Error reading the block - Skipped \n\n"));
      continue;
    }

    for (datablock_iter->SeekToFirst(); datablock_iter->Valid();
         datablock_iter->Next()) {
      s = datablock_iter->status();
      if (!s.ok()) {
        RETURN_NOT_OK(out_file->Append("Error reading the block - Skipped \n"));
        break;
      }
      Slice key = datablock_iter->key();
      Slice value = datablock_iter->value();
      InternalKey ikey = InternalKey::DecodeFrom(key);

      RETURN_NOT_OK(out_file->Append("  HEX    "));
      RETURN_NOT_OK(out_file->Append(ikey.user_key().ToString(true).c_str()));
      RETURN_NOT_OK(out_file->Append(": "));
      RETURN_NOT_OK(out_file->Append(value.ToString(true).c_str()));
      RETURN_NOT_OK(out_file->Append("\n"));

      std::string str_key = ikey.user_key().ToString();
      std::string str_value = value.ToString();
      std::string res_key(""), res_value("");
      char cspace = ' ';
      for (size_t i = 0; i < str_key.size(); i++) {
        res_key.append(&str_key[i], 1);
        res_key.append(1, cspace);
      }
      for (size_t i = 0; i < str_value.size(); i++) {
        res_value.append(&str_value[i], 1);
        res_value.append(1, cspace);
      }

      RETURN_NOT_OK(out_file->Append("  ASCII  "));
      RETURN_NOT_OK(out_file->Append(res_key.c_str()));
      RETURN_NOT_OK(out_file->Append(": "));
      RETURN_NOT_OK(out_file->Append(res_value.c_str()));
      RETURN_NOT_OK(out_file->Append("\n  ------\n"));
    }
    RETURN_NOT_OK(out_file->Append("\n"));
  }
  return Status::OK();
}

const ImmutableCFOptions& BlockBasedTable::ioptions() {
  return rep_->ioptions;
}

yb::Result<std::string> BlockBasedTable::GetMiddleKey() {
  auto index_reader = VERIFY_RESULT(GetIndexReader(ReadOptions::kDefault));

  // TODO: remove this trick after https://github.com/yugabyte/yugabyte-db/issues/4720 is resolved.
  auto se = yb::ScopeExit([this, &index_reader] {
    index_reader.Release(rep_->table_options.block_cache.get());
  });

  auto index_middle_key = index_reader.value->GetMiddleKey();
  if (PREDICT_TRUE(index_middle_key.ok())) {
    // Seek to a nearest data block key is required as index may contain non-existent key
    std::unique_ptr<InternalIterator> iter(
        NewIterator(ReadOptions::kDefault, nullptr, /* skip_filters = */ true));
    iter->Seek(*index_middle_key);
    if (!iter->Valid()) {
      return STATUS(Incomplete, "Failed to locate a middle key in data SST file.");
    }
    return iter->key().ToBuffer();
  }

  // Incomplete status might mean number of block entries is 0 or 1. Let's check the last case and
  // try to manually locate a data block and retrieve it's middle key.
  if (!index_middle_key.status().IsIncomplete()) {
    return index_middle_key.status().CloneAndPrepend(
        "Failed to locate a middle key in index SST file");
  }

  std::unique_ptr<InternalIterator> index_iter(NewIndexIterator(ReadOptions::kDefault));
  RETURN_NOT_OK_PREPEND(index_iter->status(), "Index iterator creation failed");
  index_iter->SeekToFirst();
  RETURN_NOT_OK_PREPEND(index_iter->status(), "Failed to seek to first index entry");
  if (!index_iter->Valid()) {
    return STATUS(Incomplete, "Index iterator is not valid after SeekToFirst");
  }

  auto data_block = VERIFY_RESULT(
      RetrieveBlock(ReadOptions::kDefault, index_iter->value(), BlockType::kData));
  auto middle_key_res = data_block.value->GetMiddleKey(
      GetKeyValueEncodingFormat(BlockType::kData), rep_->comparator.get(),
      MiddlePointPolicy::kMiddleHigh);
  // TODO: consider porting https://github.com/facebook/rocksdb/pull/5252 and then
  // https://github.com/facebook/rocksdb/pull/12694
  if (data_block.cache_handle) {
    data_block.Release(rep_->table_options.block_cache.get());
  } else {
    delete data_block.value;
  }
  return middle_key_res;
}

yb::Result<IndexReaderCleanablePtr> BlockBasedTable::TEST_GetIndexReader() {
  auto index_reader = VERIFY_RESULT(GetIndexReader(ReadOptions::kDefault));
  auto cache = rep_->table_options.block_cache;
  IndexReaderCleanablePtr index_reader_ptr(index_reader.value, [=](IndexReader* value) mutable {
    CHECK_EQ(index_reader.value, value);
    if (index_reader.cache_handle) {
      index_reader.Release(cache.get());
    } else {
      delete index_reader.value;
      index_reader.value = nullptr;
    }
  });
  return index_reader_ptr;
}

yb::Result<uint32_t> BlockBasedTable::TEST_GetBlockNumRestarts(
    const ReadOptions& ro, const Slice index_value, BlockType block_type) {
  auto data_block = VERIFY_RESULT(RetrieveBlock(ro, index_value, block_type));
  auto num_restarts = data_block.value->NumRestarts();
  if (data_block.cache_handle) {
    data_block.Release(rep_->table_options.block_cache.get());
  } else {
    delete data_block.value;
  }
  return num_restarts;
}

}  // namespace rocksdb
