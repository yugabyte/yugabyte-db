// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/docdb_fwd.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/rate_limiter.h"
#include "yb/rocksdb/table.h"

#include "yb/tablet/tablet_options.h"

#include "yb/util/slice.h"

namespace yb::docdb {

// Map from old cotable id to new cotable id.
// Used to restore snapshot to a new database/tablegroup and update cotable ids in the frontiers.
using CotableIdsMap = std::unordered_map<Uuid, Uuid>;

const int kDefaultGroupNo = 0;

dockv::KeyBytes AppendDocHt(Slice key, const DocHybridTime& doc_ht);

YB_DEFINE_ENUM(BloomFilterMode, (kInactive)(kFixed)(kVariable));

class BloomFilterOptions {
 public:
  static BloomFilterOptions Inactive() {
    return BloomFilterOptions(BloomFilterMode::kInactive);
  }

  static BloomFilterOptions Variable() {
    return BloomFilterOptions(BloomFilterMode::kVariable);
  }

  static BloomFilterOptions Fixed(Slice key) {
    return BloomFilterOptions(BloomFilterMode::kFixed, key);
  }

  static Result<BloomFilterOptions> Make(
      const DocReadContext& doc_read_context, Slice lower, Slice upper, bool allow_variable);

  BloomFilterMode mode() const {
    return mode_;
  }

  Slice key() const {
    return key_;
  }

 private:
  explicit BloomFilterOptions(BloomFilterMode mode, Slice key = Slice()) : mode_(mode), key_(key) {
  }

  BloomFilterMode mode_;
  Slice key_;
};

// It is only allowed to use bloom filters on scans within the same hashed components of the key,
// because BloomFilterAwareIterator relies on it and ignores SST file completely if there are no
// keys with the same hashed components as key specified for seek operation.
// Note: bloom_filter should be specified explicitly to avoid using it incorrectly by default.
// Set `cache_restart_block_keys` to kTrue to allow underlying block iterator to cache block
// entries per restart block. This could be useful for a backward scan, but should not be used for
// a forward scan.
BoundedRocksDbIterator CreateRocksDBIterator(
    rocksdb::DB* rocksdb,
    const KeyBounds* docdb_key_bounds,
    const BloomFilterOptions& bloom_filter,
    const rocksdb::QueryId query_id,
    std::shared_ptr<rocksdb::ReadFileFilter> file_filter,
    const Slice* iterate_upper_bound,
    rocksdb::CacheRestartBlockKeys cache_restart_block_keys,
    rocksdb::Statistics* statistics = nullptr);

// Values and transactions committed later than high_ht can be skipped, so we won't spend time
// for re-requesting pending transaction status if we already know it wasn't committed at high_ht.
// Set `use_fast_backward_scan` to true only if an iterator will be used for a backward scan. In
// this case the iterator would use an optimized version of backward scan. Tested for YSQL only.
IntentAwareIteratorPtr CreateIntentAwareIterator(
    const DocDB& doc_db,
    const BloomFilterOptions& bloom_filter,
    const rocksdb::QueryId query_id,
    const TransactionOperationContext& transaction_context,
    const ReadOperationData& read_operation_data,
    std::shared_ptr<rocksdb::ReadFileFilter> file_filter = nullptr,
    const Slice* iterate_upper_bound = nullptr,
    FastBackwardScan use_fast_backward_scan = FastBackwardScan::kFalse,
    AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek =
        AvoidUselessNextInsteadOfSeek::kFalse);

// Set `cache_restart_block_keys` to kTrue to allow underlying block iterator to cache block
// entries per restart block. This could be useful for a backward scan, but should not be used for
// a forward scan.
BoundedRocksDbIterator CreateIntentsIteratorWithHybridTimeFilter(
    rocksdb::DB* intentsdb,
    const TransactionStatusManager* status_manager,
    const KeyBounds* docdb_key_bounds,
    const Slice* iterate_upper_bound,
    rocksdb::CacheRestartBlockKeys cache_restart_block_keys,
    rocksdb::Statistics* statistics = nullptr);

std::shared_ptr<rocksdb::RocksDBPriorityThreadPoolMetrics> CreateRocksDBPriorityThreadPoolMetrics(
    scoped_refptr<yb::MetricEntity> entity);

// Request RocksDB compaction and wait until it completes.
Status ForceRocksDBCompact(rocksdb::DB* db, const rocksdb::CompactRangeOptions& options);

rocksdb::Options TEST_AutoInitFromRocksDBFlags();

rocksdb::BlockBasedTableOptions TEST_AutoInitFromRocksDbTableFlags();

Result<rocksdb::CompressionType> TEST_GetConfiguredCompressionType(const std::string& flag_value);

Result<rocksdb::KeyValueEncodingFormat> GetConfiguredKeyValueEncodingFormat(
    const std::string& flag_value);

// Defines how rate limiter is shared across a node
YB_DEFINE_ENUM(RateLimiterSharingMode, (NONE)(TSERVER));

// Extracts rate limiter's sharing mode depending on the value of
// flag `FLAGS_rocksdb_compact_flush_rate_limit_sharing_mode`;
// `RateLimiterSharingMode::NONE` is returned if extraction failed
RateLimiterSharingMode GetRocksDBRateLimiterSharingMode();

// Creates `rocksdb::RateLimiter` taking into account related GFlags,
// calls `rocksdb::NewGenericRateLimiter` internally
std::shared_ptr<rocksdb::RateLimiter> CreateRocksDBRateLimiter();

// Initialize the RocksDB base 'options' that are common for regular DB and intents DB.
// The 'statistics' object provided by the caller will be used by RocksDB to maintain the stats for
// the tablet.
void InitRocksDBBaseOptions(
    rocksdb::Options* options, const std::string& log_prefix, const TabletId& tablet_id,
    const tablet::TabletOptions& tablet_options,
    const uint64_t group_no = kDefaultGroupNo);

void InitRocksDBOptionsTableFactory(
    rocksdb::Options* options, const tablet::TabletOptions& tablet_options,
    rocksdb::BlockBasedTableOptions table_options = rocksdb::BlockBasedTableOptions());

void InitRocksDBOptions(
    rocksdb::Options* options, const std::string& log_prefix,
    const TabletId& tablet_id,
    const std::shared_ptr<rocksdb::Statistics>& statistics,
    const tablet::TabletOptions& tablet_options,
    rocksdb::BlockBasedTableOptions table_options = rocksdb::BlockBasedTableOptions(),
    const uint64_t group_no = kDefaultGroupNo);

// Sets logs prefix for RocksDB options. This will also reinitialize options->info_log.
void SetLogPrefix(rocksdb::Options* options, const std::string& log_prefix);

// Gets the configured size of the node-global RocksDB priority thread pool.
int32_t GetGlobalRocksDBPriorityThreadPoolSize();

// Gets the configured priority thread pool instance.
PriorityThreadPool* GetGlobalPriorityThreadPool();

// Class to edit RocksDB manifest w/o fully loading DB into memory.
class RocksDBPatcher {
 public:
  explicit RocksDBPatcher(const std::string& dbpath, const rocksdb::Options& options);
  ~RocksDBPatcher();

  // Loads DB into patcher.
  Status Load();

  // Set hybrid time filter for DB.
  Status SetHybridTimeFilter(std::optional<uint32_t> db_oid, HybridTime value);

  // Modify flushed frontier and clean up smallest/largest op id in per-SST file metadata.
  Status ModifyFlushedFrontier(
      const ConsensusFrontier& frontier, const CotableIdsMap& cotable_ids_map);

  // Update file sizes in manifest if actual file size was changed because of direct manipulation
  // with .sst files.
  // Like all other methods in this class it updates manifest file.
  Status UpdateFileSizes();

  // Returns true if at least one sst file contains a valid hybrid time filter
  // in its largest frontier.
  bool TEST_ContainsHybridTimeFilter();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::docdb
