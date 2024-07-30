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

#include "yb/docdb/docdb_rocksdb_util.h"

#include <memory>
#include <thread>

#include <boost/algorithm/string/predicate.hpp>

#include "yb/common/transaction.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/docdb_filter_policy.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/key_bounds.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/sysinfo.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/version_edit.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/db/writebuffer.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/metadata.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/rate_limiter.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/block_based_table_reader.h"
#include "yb/rocksdb/table/filtering_iterator.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/util/compression.h"

#include "yb/rocksutil/yb_rocksdb_logger.h"

#include "yb/util/flags.h"
#include "yb/util/priority_thread_pool.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/trace.h"
#include "yb/util/logging.h"

using namespace yb::size_literals;  // NOLINT.
using namespace std::literals;

namespace {

constexpr int32_t kMinBlockRestartInterval = 1;
constexpr int32_t kDefaultDataBlockRestartInterval = 16;
constexpr int32_t kDefaultIndexBlockRestartInterval = kMinBlockRestartInterval;
constexpr int32_t kMaxBlockRestartInterval = 256;

} // namespace

DEFINE_UNKNOWN_int32(rocksdb_max_background_flushes, -1,
    "Number threads to do background flushes.");
DEFINE_UNKNOWN_bool(rocksdb_disable_compactions, false, "Disable rocksdb compactions.");
DEFINE_UNKNOWN_bool(rocksdb_compaction_measure_io_stats, false,
    "Measure stats for rocksdb compactions.");
DEFINE_UNKNOWN_int32(rocksdb_base_background_compactions, -1,
             "Number threads to do background compactions.");
DEFINE_UNKNOWN_int32(rocksdb_max_background_compactions, -1,
             "Increased number of threads to do background compactions (used when compactions need "
             "to catch up.) Unless rocksdb_disable_compactions=true, this cannot be set to zero.");
DEFINE_UNKNOWN_int32(rocksdb_level0_file_num_compaction_trigger, 5,
             "Number of files to trigger level-0 compaction. -1 if compaction should not be "
             "triggered by number of files at all.");

DEFINE_UNKNOWN_int32(rocksdb_level0_slowdown_writes_trigger, -1,
             "The number of files above which writes are slowed down.");
DEFINE_UNKNOWN_int32(rocksdb_level0_stop_writes_trigger, -1,
             "The number of files above which compactions are stopped.");
DEFINE_UNKNOWN_int32(rocksdb_universal_compaction_size_ratio, 20,
             "The percentage upto which files that are larger are include in a compaction.");
DEFINE_UNKNOWN_uint64(rocksdb_universal_compaction_always_include_size_threshold, 64_MB,
             "Always include files of smaller or equal size in a compaction.");
DEFINE_UNKNOWN_int32(rocksdb_universal_compaction_min_merge_width, 4,
             "The minimum number of files in a single compaction run.");
DEFINE_UNKNOWN_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec, 1_GB,
             "Use to control write rate of flush and compaction.");
DEFINE_UNKNOWN_string(rocksdb_compact_flush_rate_limit_sharing_mode, "tserver",
              "Allows to control rate limit sharing/calculation across RocksDB instances\n"
              "  tserver - rate limit is shared across all RocksDB instances"
              " at tabset server level\n"
              "  none - rate limit is calculated independently for every RocksDB instance");
DEFINE_UNKNOWN_uint64(rocksdb_compaction_size_threshold_bytes, 2ULL * 1024 * 1024 * 1024,
             "Threshold beyond which compaction is considered large.");
DEFINE_UNKNOWN_uint64(rocksdb_max_file_size_for_compaction, 0,
             "Maximal allowed file size to participate in RocksDB compaction. 0 - unlimited.");

// Use big enough default value for rocksdb_max_write_buffer_number, so behavior defined by
// db_max_flushing_bytes will be actual default.
DEFINE_NON_RUNTIME_int32(rocksdb_max_write_buffer_number, 100500,
             "Maximum number of write buffers that are built up in memory.");
DECLARE_int64(db_block_size_bytes);

DEFINE_UNKNOWN_int64(db_filter_block_size_bytes, 64_KB,
             "Size of RocksDB filter block (in bytes).");

DEFINE_UNKNOWN_int64(db_index_block_size_bytes, 32_KB,
             "Size of RocksDB index block (in bytes).");

DEFINE_UNKNOWN_int64(db_min_keys_per_index_block, 100,
             "Minimum number of keys per index block.");

DEFINE_UNKNOWN_int64(db_write_buffer_size, -1,
             "Size of RocksDB write buffer (in bytes). -1 to use default.");

DEFINE_UNKNOWN_int32(memstore_size_mb, 128,
             "Max size (in mb) of the memstore, before needing to flush.");

// Use a value slightly less than 2 default mem store sizes.
DEFINE_NON_RUNTIME_uint64(db_max_flushing_bytes, 250_MB,
    "The limit for the number of bytes in immutable mem tables. "
    "After reaching this limit new writes are blocked. 0 - unlimited.");

DEFINE_UNKNOWN_bool(use_docdb_aware_bloom_filter, true,
            "Whether to use the DocDbAwareFilterPolicy for both bloom storage and seeks.");

DEFINE_UNKNOWN_bool(use_multi_level_index, true, "Whether to use multi-level data index.");

// Using class kExternal as this change affects the format of data in the SST files which are sent
// to xClusters during bootstrap.
DEFINE_RUNTIME_AUTO_string(regular_tablets_data_block_key_value_encoding, kExternal,
    "shared_prefix", "three_shared_parts",
    "Key-value encoding to use for regular data blocks in RocksDB. Possible options: "
    "shared_prefix, three_shared_parts");

DEFINE_UNKNOWN_uint64(initial_seqno, 1ULL << 50, "Initial seqno for new RocksDB instances.");

DEFINE_UNKNOWN_int32(num_reserved_small_compaction_threads, -1,
    "Number of reserved small compaction "
    "threads. It allows splitting small vs. large compactions.");

DEFINE_UNKNOWN_bool(enable_ondisk_compression, true,
            "Determines whether SSTable compression is enabled or not.");

DEFINE_UNKNOWN_int32(priority_thread_pool_size, -1,
             "Max running workers in compaction thread pool. "
             "If -1 and max_background_compactions is specified - use max_background_compactions. "
             "If -1 and max_background_compactions is not specified - use sqrt(num_cpus).");

DEFINE_UNKNOWN_string(compression_type, "Snappy",
              "On-disk compression type to use in RocksDB."
              "By default, Snappy is used if supported.");

DEFINE_UNKNOWN_int32(block_restart_interval, kDefaultDataBlockRestartInterval,
             "Controls the number of keys to look at for computing the diff encoding.");

DEFINE_UNKNOWN_int32(index_block_restart_interval, kDefaultIndexBlockRestartInterval,
             "Controls the number of data blocks to be indexed inside an index block.");

DEFINE_UNKNOWN_bool(prioritize_tasks_by_disk, false,
            "Consider disk load when considering compaction and flush priorities.");

namespace yb {

namespace {

Result<rocksdb::CompressionType> GetConfiguredCompressionType(const std::string& flag_value) {
  if (!FLAGS_enable_ondisk_compression) {
    return rocksdb::kNoCompression;
  }
  const std::vector<rocksdb::CompressionType> kValidRocksDBCompressionTypes = {
    rocksdb::kNoCompression,
    rocksdb::kSnappyCompression,
    rocksdb::kZlibCompression,
    rocksdb::kLZ4Compression
  };
  for (const auto& compression_type : kValidRocksDBCompressionTypes) {
    if (boost::iequals(flag_value, rocksdb::CompressionTypeToString(compression_type))) {
      if (rocksdb::CompressionTypeSupported(compression_type)) {
        return compression_type;
      }
      return STATUS_FORMAT(
          InvalidArgument, "Configured compression type $0 is not supported.", flag_value);
    }
  }
  return STATUS_FORMAT(
      InvalidArgument, "Configured compression type $0 is not valid.", flag_value);
}

} // namespace

namespace docdb {

  Result<rocksdb::KeyValueEncodingFormat> GetConfiguredKeyValueEncodingFormat(
    const std::string& flag_value) {
    for (const auto& encoding_format : rocksdb::KeyValueEncodingFormatList()) {
      if (flag_value == KeyValueEncodingFormatToString(encoding_format)) {
        return encoding_format;
      }
    }
    return STATUS_FORMAT(InvalidArgument, "Key-value encoding format $0 is not valid.", flag_value);
  }

} // namespace docdb

} // namespace yb

namespace {

bool CompressionTypeValidator(const char* flag_name, const std::string& flag_compression_type) {
  auto res = yb::GetConfiguredCompressionType(flag_compression_type);
  if (!res.ok()) {
    // Below we CHECK_RESULT on the same value returned here, and validating the result here ensures
    // that CHECK_RESULT will never fail once the process is running.
    LOG_FLAG_VALIDATION_ERROR(flag_name, flag_compression_type) << res.status().ToString();
    return false;
  }
  return true;
}

bool KeyValueEncodingFormatValidator(const char* flag_name, const std::string& flag_value) {
  auto res = yb::docdb::GetConfiguredKeyValueEncodingFormat(flag_value);
  bool ok = res.ok();
  if (!ok) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, flag_value) << res.status();
  }
  return ok;
}

} // namespace

DEFINE_validator(compression_type, &CompressionTypeValidator);
DEFINE_validator(regular_tablets_data_block_key_value_encoding, &KeyValueEncodingFormatValidator);

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using strings::Substitute;

namespace yb {
namespace docdb {

using dockv::KeyBytes;

std::shared_ptr<rocksdb::BoundaryValuesExtractor> DocBoundaryValuesExtractorInstance();

KeyBytes AppendDocHt(Slice key, const DocHybridTime& doc_ht) {
  char buf[kMaxBytesPerEncodedHybridTime + 1];
  buf[0] = dockv::KeyEntryTypeAsChar::kHybridTime;
  auto end = doc_ht.EncodedInDocDbFormat(buf + 1);
  return KeyBytes(key, Slice(buf, end));
}

namespace {

rocksdb::ReadOptions PrepareReadOptions(
    rocksdb::DB* rocksdb,
    BloomFilterMode bloom_filter_mode,
    const boost::optional<const Slice>& user_key_for_filter,
    const rocksdb::QueryId query_id,
    std::shared_ptr<rocksdb::ReadFileFilter> file_filter,
    const Slice* iterate_upper_bound,
    rocksdb::Statistics* statistics) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = query_id;
  read_opts.statistics = statistics;
  if (FLAGS_use_docdb_aware_bloom_filter &&
    bloom_filter_mode == BloomFilterMode::USE_BLOOM_FILTER) {
    DCHECK(user_key_for_filter);
    static const rocksdb::BloomFilterAwareFileFilter bloom_filter_aware_file_filter;
    read_opts.table_aware_file_filter = &bloom_filter_aware_file_filter;
    read_opts.user_key_for_filter = *user_key_for_filter;
  }
  read_opts.file_filter = std::move(file_filter);
  read_opts.iterate_upper_bound = iterate_upper_bound;
  return read_opts;
}

} // namespace

BoundedRocksDbIterator CreateRocksDBIterator(
    rocksdb::DB* rocksdb,
    const KeyBounds* docdb_key_bounds,
    BloomFilterMode bloom_filter_mode,
    const boost::optional<const Slice>& user_key_for_filter,
    const rocksdb::QueryId query_id,
    std::shared_ptr<rocksdb::ReadFileFilter> file_filter,
    const Slice* iterate_upper_bound,
    rocksdb::Statistics* statistics) {
  rocksdb::ReadOptions read_opts = PrepareReadOptions(rocksdb, bloom_filter_mode,
      user_key_for_filter, query_id, std::move(file_filter), iterate_upper_bound, statistics);
  return BoundedRocksDbIterator(rocksdb, read_opts, docdb_key_bounds);
}

unique_ptr<IntentAwareIterator> CreateIntentAwareIterator(
    const DocDB& doc_db,
    BloomFilterMode bloom_filter_mode,
    const boost::optional<const Slice>& user_key_for_filter,
    const rocksdb::QueryId query_id,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data,
    std::shared_ptr<rocksdb::ReadFileFilter> file_filter,
    const Slice* iterate_upper_bound,
    const FastBackwardScan use_fast_backward_scan,
    const DocDBStatistics* statistics) {
  // TODO(dtxn) do we need separate options for intents db?
  rocksdb::ReadOptions read_opts = PrepareReadOptions(doc_db.regular, bloom_filter_mode,
      user_key_for_filter, query_id, std::move(file_filter), iterate_upper_bound,
      statistics ? statistics->RegularDBStatistics() : nullptr);
  return std::make_unique<IntentAwareIterator>(
      doc_db, read_opts, read_operation_data, txn_op_context, use_fast_backward_scan,
      statistics ? statistics->IntentsDBStatistics() : nullptr);
}

BoundedRocksDbIterator CreateIntentsIteratorWithHybridTimeFilter(
    rocksdb::DB* intentsdb,
    const TransactionStatusManager* status_manager,
    const KeyBounds* docdb_key_bounds,
    const Slice* iterate_upper_bound,
    rocksdb::Statistics* statistics) {
  auto min_running_ht = status_manager->MinRunningHybridTime();
  if (min_running_ht == HybridTime::kMax) {
    VLOG(4) << "No transactions running";
    return {};
  }
  return CreateRocksDBIterator(
      intentsdb,
      docdb_key_bounds,
      docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
      boost::none /* user_key_for_filter */,
      rocksdb::kDefaultQueryId,
      CreateIntentHybridTimeFileFilter(min_running_ht),
      iterate_upper_bound,
      statistics);
}

namespace {

std::mutex rocksdb_flags_mutex;

int32_t GetMaxBackgroundFlushes() {
  const auto kNumCpus = base::NumCPUs();
  if (FLAGS_rocksdb_max_background_flushes == -1) {
    constexpr auto kCpusPerFlushThread = 8;
    constexpr auto kAutoMaxBackgroundFlushesHighLimit = 4;
    auto flushes = 1 + kNumCpus / kCpusPerFlushThread;
    auto max_flushes = std::min(flushes, kAutoMaxBackgroundFlushesHighLimit);
    YB_LOG_EVERY_N_SECS(INFO, 100)
        << "FLAGS_rocksdb_max_background_flushes was not set, automatically configuring "
        << max_flushes << " max background flushes";
    return max_flushes;
  } else {
    return FLAGS_rocksdb_max_background_flushes;
  }
}

// This controls the maximum number of schedulable compactions, per each instance of rocksdb, of
// which we will have many. We also do not want to waste resources by having too many queued
// compactions.
int32_t GetMaxBackgroundCompactions() {
  if (FLAGS_rocksdb_disable_compactions) {
    return 0;
  }
  int rocksdb_max_background_compactions = FLAGS_rocksdb_max_background_compactions;

  if (rocksdb_max_background_compactions == 0) {
    LOG(FATAL) << "--rocksdb_max_background_compactions may not be set to zero with compactions "
        << "enabled. Either change this flag or set --rocksdb_disable_compactions=true.";
  } else if (rocksdb_max_background_compactions > 0) {
    return rocksdb_max_background_compactions;
  }

  const auto kNumCpus = base::NumCPUs();
  if (kNumCpus <= 4) {
    rocksdb_max_background_compactions = 1;
  } else if (kNumCpus <= 8) {
    rocksdb_max_background_compactions = 2;
  } else if (kNumCpus <= 32) {
    rocksdb_max_background_compactions = 3;
  } else {
    rocksdb_max_background_compactions = 4;
  }
  YB_LOG_EVERY_N_SECS(INFO, 100)
      << "FLAGS_rocksdb_max_background_compactions was not set, automatically configuring "
      << rocksdb_max_background_compactions << " background compactions.";
  return rocksdb_max_background_compactions;
}

int32_t GetBaseBackgroundCompactions() {
  if (FLAGS_rocksdb_disable_compactions) {
    return 0;
  }

  if (FLAGS_rocksdb_base_background_compactions == -1) {
    const auto base_background_compactions = GetMaxBackgroundCompactions();
    YB_LOG_EVERY_N_SECS(INFO, 100)
        << "FLAGS_rocksdb_base_background_compactions was not set, automatically configuring "
        << base_background_compactions << " base background compactions.";
    return base_background_compactions;
  }

  return FLAGS_rocksdb_base_background_compactions;
}

// Auto initialize some of the RocksDB flags.
void AutoInitFromRocksDBFlags(rocksdb::Options* options) {
  std::unique_lock<std::mutex> lock(rocksdb_flags_mutex);

  options->max_background_flushes = GetMaxBackgroundFlushes();

  if (FLAGS_rocksdb_disable_compactions) {
    return;
  }

  options->max_background_compactions = GetMaxBackgroundCompactions();
  options->base_background_compactions = GetBaseBackgroundCompactions();
}

void AutoInitFromBlockBasedTableOptions(rocksdb::BlockBasedTableOptions* table_options) {
  std::unique_lock<std::mutex> lock(rocksdb_flags_mutex);

  table_options->block_size = FLAGS_db_block_size_bytes;
  table_options->filter_block_size = FLAGS_db_filter_block_size_bytes;
  table_options->index_block_size = FLAGS_db_index_block_size_bytes;
  table_options->min_keys_per_index_block = FLAGS_db_min_keys_per_index_block;

  if (FLAGS_block_restart_interval < kMinBlockRestartInterval) {
    LOG(INFO) << "FLAGS_block_restart_interval was set to a very low value, overriding "
              << "block_restart_interval to " << kDefaultDataBlockRestartInterval << ".";
    table_options->block_restart_interval = kDefaultDataBlockRestartInterval;
  } else if (FLAGS_block_restart_interval > kMaxBlockRestartInterval) {
    LOG(INFO) << "FLAGS_block_restart_interval was set to a very high value, overriding "
              << "block_restart_interval to " << kMaxBlockRestartInterval << ".";
    table_options->block_restart_interval = kMaxBlockRestartInterval;
  } else {
    table_options->block_restart_interval = FLAGS_block_restart_interval;
  }

  if (FLAGS_index_block_restart_interval < kMinBlockRestartInterval) {
    LOG(INFO) << "FLAGS_index_block_restart_interval was set to a very low value, overriding "
              << "index_block_restart_interval to " << kMinBlockRestartInterval << ".";
    table_options->index_block_restart_interval = kMinBlockRestartInterval;
  } else if (FLAGS_index_block_restart_interval > kMaxBlockRestartInterval) {
    LOG(INFO) << "FLAGS_index_block_restart_interval was set to a very high value, overriding "
              << "index_block_restart_interval to " << kMaxBlockRestartInterval << ".";
    table_options->index_block_restart_interval = kMaxBlockRestartInterval;
  } else {
    table_options->index_block_restart_interval = FLAGS_index_block_restart_interval;
  }
}

class HybridTimeFilteringIterator : public rocksdb::FilteringIterator {
 public:
  HybridTimeFilteringIterator(
      rocksdb::InternalIterator* iterator, bool arena_mode, Slice hybrid_time_filter)
      : rocksdb::FilteringIterator(iterator, arena_mode) {
    uint64_t ht = ExtractGlobalFilter(hybrid_time_filter);
    if (HybridTime::FromPB(ht).is_valid()) {
      global_ht_filter_ = HybridTime::FromPB(ht);
    }
    Slice cotables_filter = hybrid_time_filter.WithoutPrefix(sizeof(ht));
    if (!cotables_filter.empty()) {
      num_filters_ = cotables_filter.size() / kSizePerDbFilter;
      db_oid_ptr_ = pointer_cast<const uint32_t*>(cotables_filter.data());
      cotables_ht_ptr_ = pointer_cast<const uint64_t*>(
          cotables_filter.data() + (num_filters_ * kSizeDbOid));
    }
  }

 private:
  std::string CoTablesFilterToString() {
    std::string res = "[ ";
    for (size_t i = 0; i < num_filters_; i++) {
      res += Format("$0:$1, ", db_oid_ptr_[i], HybridTime(cotables_ht_ptr_[i]));
    }
    res += " ]";
    return res;
  }

  bool Satisfied(Slice user_key) override {
    auto doc_ht = DocHybridTime::DecodeFromEnd(&user_key);
    if (!doc_ht.ok()) {
      LOG(DFATAL) << "Unable to decode doc ht " << user_key << ": "
                  << doc_ht.status();
      return true;
    }
    VLOG(5) << "Key: " << user_key.ToDebugHexString() << ", filter details: "
            << "{ Cotables filter: " << CoTablesFilterToString() << " }"
            << "{ All keys filter: " << global_ht_filter_.ToDebugString() << " }";
    // Logical AND of both the filters.
    if (global_ht_filter_.is_valid() && doc_ht->hybrid_time() > global_ht_filter_) {
      return false;
    }
    if (num_filters_ == 0) {
      return true;
    }
    // Should only reach here in case of ysql catalog tables on the master.
    bool cotable_uuid_present = user_key.TryConsumeByte(dockv::KeyEntryTypeAsChar::kTableId);
    if (!cotable_uuid_present) {
      return true;
    }
    Slice cotable_slice(user_key.cdata(), kUuidSize);
    auto decoded_cotable_uuid_res = Uuid::FromComparable(cotable_slice);
    if (!decoded_cotable_uuid_res.ok()) {
      return true;
    }
    uint32_t key_db_oid = LittleEndian::Load32(decoded_cotable_uuid_res->data() + 12);

    VLOG(5) << "DB Oid of key " << key_db_oid;
    auto it = std::lower_bound(db_oid_ptr_, db_oid_ptr_ + num_filters_, key_db_oid);
    if (it != db_oid_ptr_ + num_filters_ && *it == key_db_oid) {
      auto idx = it - db_oid_ptr_;
      HybridTime ht(cotables_ht_ptr_[idx]);
      VLOG(5) << "Found db oid " << key_db_oid << " at index " << idx
              << " with hybrid time " << ht;
      return doc_ht->hybrid_time() <= ht;
    }
    return true;
  }

  HybridTime global_ht_filter_;
  const uint32_t* db_oid_ptr_ = nullptr; // owned externally.
  const uint64_t* cotables_ht_ptr_ = nullptr; // owned externally.
  size_t num_filters_ = 0;
};

template <class T, class... Args>
T* CreateOnArena(rocksdb::Arena* arena, Args&&... args) {
  if (!arena) {
    return new T(std::forward<Args>(args)...);
  }
  auto mem = arena->AllocateAligned(sizeof(T));
  return new (mem) T(std::forward<Args>(args)...);
}

rocksdb::InternalIterator* WrapIterator(
    rocksdb::InternalIterator* iterator, rocksdb::Arena* arena, Slice filter) {
  if (filter.empty()) {
    return iterator;
  }
  return CreateOnArena<HybridTimeFilteringIterator>(
      arena, iterator, arena != nullptr, filter);
}

void AddSupportedFilterPolicy(
    const rocksdb::BlockBasedTableOptions::FilterPolicyPtr& filter_policy,
    rocksdb::BlockBasedTableOptions* table_options) {
  table_options->supported_filter_policies->emplace(filter_policy->Name(), filter_policy);
}

PriorityThreadPool* GetGlobalPriorityThreadPool() {
  static PriorityThreadPool priority_thread_pool_for_compactions_and_flushes(
      GetGlobalRocksDBPriorityThreadPoolSize(), FLAGS_prioritize_tasks_by_disk);
  return &priority_thread_pool_for_compactions_and_flushes;
}

} // namespace

rocksdb::Options TEST_AutoInitFromRocksDBFlags() {
  rocksdb::Options options;
  AutoInitFromRocksDBFlags(&options);
  return options;
}

rocksdb::BlockBasedTableOptions TEST_AutoInitFromRocksDbTableFlags() {
  rocksdb::BlockBasedTableOptions blockBasedTableOptions;
  AutoInitFromBlockBasedTableOptions(&blockBasedTableOptions);
  return blockBasedTableOptions;
}

Result<rocksdb::CompressionType> TEST_GetConfiguredCompressionType(const std::string& flag_value) {
  return yb::GetConfiguredCompressionType(flag_value);
}

int32_t GetGlobalRocksDBPriorityThreadPoolSize() {
  if (FLAGS_rocksdb_disable_compactions) {
    return 1;
  }

  auto priority_thread_pool_size = FLAGS_priority_thread_pool_size;

  if (priority_thread_pool_size == 0) {
    LOG(FATAL) << "--priority_thread_pool_size may not be set to zero with compactions "
        << "enabled. Either change this flag or set --rocksdb_disable_compactions=true.";
  } else if (priority_thread_pool_size > 0) {
    return priority_thread_pool_size;
  }

  if (FLAGS_rocksdb_max_background_compactions != -1) {
    // If we did set the per-rocksdb flag, but not FLAGS_priority_thread_pool_size, just port
    // over that value.
    priority_thread_pool_size = GetMaxBackgroundCompactions();
  } else {
    const int kNumCpus = base::NumCPUs();
    // If we did not override the per-rocksdb queue size, then just use a production friendly
    // formula.
    //
    // For less then 8cpus, just manually tune to 1-2 threads. Above that, we can use 3.5/8.
    if (kNumCpus < 4) {
      priority_thread_pool_size = 1;
    } else if (kNumCpus < 8) {
      priority_thread_pool_size = 2;
    } else {
      priority_thread_pool_size = (int32_t) std::floor(kNumCpus * 3.5 / 8.0);
    }
  }

  YB_LOG_EVERY_N_SECS(INFO, 100)
      << "FLAGS_priority_thread_pool_size was not set, automatically configuring to "
      << priority_thread_pool_size << ".";

  return priority_thread_pool_size;
}

void InitRocksDBOptions(
    rocksdb::Options* options, const string& log_prefix,
    const TabletId& tablet_id,
    const shared_ptr<rocksdb::Statistics>& statistics,
    const tablet::TabletOptions& tablet_options,
    rocksdb::BlockBasedTableOptions table_options,
    const uint64_t group_no) {
  AutoInitFromRocksDBFlags(options);
  SetLogPrefix(options, log_prefix);
  options->tablet_id = tablet_id;
  options->create_if_missing = true;
  // We should always sync data to ensure we can recover rocksdb from crash.
  options->disableDataSync = false;
  options->statistics = statistics;
  options->info_log_level = YBRocksDBLogger::ConvertToRocksDBLogLevel(FLAGS_minloglevel);
  options->initial_seqno = FLAGS_initial_seqno;
  options->boundary_extractor = DocBoundaryValuesExtractorInstance();
  options->compaction_measure_io_stats = FLAGS_rocksdb_compaction_measure_io_stats;
  options->memory_monitor = tablet_options.memory_monitor;
  options->disk_group_no = group_no;
  if (FLAGS_db_write_buffer_size != -1) {
    options->write_buffer_size = FLAGS_db_write_buffer_size;
  } else {
    options->write_buffer_size = FLAGS_memstore_size_mb * 1_MB;
  }
  LOG(INFO) << log_prefix << "Write buffer size: " << options->write_buffer_size;
  options->env = tablet_options.rocksdb_env;
  options->checkpoint_env = rocksdb::Env::Default();
  options->priority_thread_pool_for_compactions_and_flushes = GetGlobalPriorityThreadPool();

  if (FLAGS_num_reserved_small_compaction_threads != -1) {
    options->num_reserved_small_compaction_threads = FLAGS_num_reserved_small_compaction_threads;
  }

  // Since the flag validator for FLAGS_compression_type will fail if the result of this call is not
  // OK, this CHECK_RESULT should never fail and is safe.
  options->compression = CHECK_RESULT(GetConfiguredCompressionType(FLAGS_compression_type));

  options->listeners.insert(
      options->listeners.end(), tablet_options.listeners.begin(),
      tablet_options.listeners.end()); // Append listeners

  // Set block cache options.
  if (tablet_options.block_cache) {
    table_options.block_cache = tablet_options.block_cache;
    // Cache the bloom filters in the block cache.
    table_options.cache_index_and_filter_blocks = true;
  } else {
    table_options.no_block_cache = true;
    table_options.cache_index_and_filter_blocks = false;
  }

  AutoInitFromBlockBasedTableOptions(&table_options);

  // Set our custom bloom filter that is docdb aware.
  if (FLAGS_use_docdb_aware_bloom_filter) {
    const auto filter_block_size_bits = table_options.filter_block_size * 8;
    table_options.filter_policy = std::make_shared<const DocDbAwareV3FilterPolicy>(
        filter_block_size_bits, options->info_log.get());
    table_options.supported_filter_policies =
        std::make_shared<rocksdb::BlockBasedTableOptions::FilterPoliciesMap>();
    AddSupportedFilterPolicy(std::make_shared<const DocDbAwareHashedComponentsFilterPolicy>(
            filter_block_size_bits, options->info_log.get()), &table_options);
    AddSupportedFilterPolicy(std::make_shared<const DocDbAwareV2FilterPolicy>(
            filter_block_size_bits, options->info_log.get()), &table_options);
  }

  if (FLAGS_use_multi_level_index) {
    table_options.index_type = rocksdb::IndexType::kMultiLevelBinarySearch;
  } else {
    table_options.index_type = rocksdb::IndexType::kBinarySearch;
  }

  options->table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

  // Compaction related options.

  // Enable universal style compactions.
  bool compactions_enabled = !FLAGS_rocksdb_disable_compactions;
  options->compaction_style = compactions_enabled
    ? rocksdb::CompactionStyle::kCompactionStyleUniversal
    : rocksdb::CompactionStyle::kCompactionStyleNone;
  // Set the number of levels to 1.
  options->num_levels = 1;

  AutoInitFromRocksDBFlags(options);
  if (compactions_enabled) {
    options->level0_file_num_compaction_trigger = FLAGS_rocksdb_level0_file_num_compaction_trigger;
    options->level0_slowdown_writes_trigger = max_if_negative(
        FLAGS_rocksdb_level0_slowdown_writes_trigger);
    options->level0_stop_writes_trigger = max_if_negative(FLAGS_rocksdb_level0_stop_writes_trigger);
    // This determines the algo used to compute which files will be included. The "total size" based
    // computation compares the size of every new file with the sum of all files included so far.
    options->compaction_options_universal.stop_style =
        rocksdb::CompactionStopStyle::kCompactionStopStyleTotalSize;
    options->compaction_options_universal.size_ratio =
        FLAGS_rocksdb_universal_compaction_size_ratio;
    options->compaction_options_universal.always_include_size_threshold =
        FLAGS_rocksdb_universal_compaction_always_include_size_threshold;
    options->compaction_options_universal.min_merge_width =
        FLAGS_rocksdb_universal_compaction_min_merge_width;
    options->compaction_size_threshold_bytes = FLAGS_rocksdb_compaction_size_threshold_bytes;
    options->rate_limiter = tablet_options.rate_limiter ? tablet_options.rate_limiter
                                                        : CreateRocksDBRateLimiter();
  } else {
    options->level0_slowdown_writes_trigger = std::numeric_limits<int>::max();
    options->level0_stop_writes_trigger = std::numeric_limits<int>::max();
  }

  options->max_write_buffer_number = FLAGS_rocksdb_max_write_buffer_number;
  if (FLAGS_db_max_flushing_bytes != 0) {
    options->max_flushing_bytes = FLAGS_db_max_flushing_bytes;
  }

  options->memtable_factory = std::make_shared<rocksdb::SkipListFactory>(
      0 /* lookahead */, rocksdb::ConcurrentWrites::kFalse);

  options->iterator_replacer = std::make_shared<rocksdb::IteratorReplacer>(&WrapIterator);

  options->priority_thread_pool_metrics = tablet_options.priority_thread_pool_metrics;
}

void SetLogPrefix(rocksdb::Options* options, const std::string& log_prefix) {
  options->log_prefix = log_prefix;
  options->info_log = std::make_shared<YBRocksDBLogger>(options->log_prefix);
}

namespace {

// Helper class for RocksDBPatcher.
class RocksDBPatcherHelper {
 public:
  explicit RocksDBPatcherHelper(rocksdb::VersionSet* version_set)
      : version_set_(version_set), cfd_(version_set->GetColumnFamilySet()->GetDefault()),
        delete_edit_(cfd_), add_edit_(cfd_) {
  }

  int Levels() const {
    return cfd_->NumberLevels();
  }

  const std::vector<rocksdb::FileMetaData*>& LevelFiles(int level) {
    return cfd_->current()->storage_info()->LevelFiles(level);
  }

  template <class F>
  auto IterateFiles(const F& f) {
    // Auto routing based on f return type.
    return IterateFilesHelper(f, static_cast<decltype(f(0, *LevelFiles(0).front()))*>(nullptr));
  }

  void ModifyFile(int level, const rocksdb::FileMetaData& fmd) {
    delete_edit_->DeleteFile(level, fmd.fd.GetNumber());
    add_edit_->AddCleanedFile(level, fmd);
  }

  rocksdb::VersionEdit& Edit() {
    return *add_edit_;
  }

  Status Apply(
      const rocksdb::Options& options, const rocksdb::ImmutableCFOptions& imm_cf_options) {
    if (!delete_edit_.modified() && !add_edit_.modified()) {
      return Status::OK();
    }

    rocksdb::MutableCFOptions mutable_cf_options(options, imm_cf_options);
    {
      rocksdb::InstrumentedMutex mutex;
      rocksdb::InstrumentedMutexLock lock(&mutex);
      for (auto* edit : {&delete_edit_, &add_edit_}) {
        if (edit->modified()) {
          RETURN_NOT_OK(version_set_->LogAndApply(cfd_, mutable_cf_options, edit->get(), &mutex));
        }
      }
    }

    return Status::OK();
  }

 private:
  template <class F>
  void IterateFilesHelper(const F& f, void*) {
    for (int level = 0; level < Levels(); ++level) {
      for (const auto* file : LevelFiles(level)) {
        f(level, *file);
      }
    }
  }

  template <class F>
  Status IterateFilesHelper(const F& f, Status*) {
    for (int level = 0; level < Levels(); ++level) {
      for (const auto* file : LevelFiles(level)) {
        RETURN_NOT_OK(f(level, *file));
      }
    }
    return Status::OK();
  }

  class TrackedEdit {
   public:
    explicit TrackedEdit(rocksdb::ColumnFamilyData* cfd) {
      edit_.SetColumnFamily(cfd->GetID());
    }

    rocksdb::VersionEdit* get() {
      modified_ = true;
      return &edit_;
    }

    rocksdb::VersionEdit* operator->() {
      return get();
    }

    rocksdb::VersionEdit& operator*() {
      return *get();
    }

    bool modified() const {
      return modified_;
    }

   private:
    rocksdb::VersionEdit edit_;
    bool modified_ = false;
  };

  rocksdb::VersionSet* version_set_;
  rocksdb::ColumnFamilyData* cfd_;
  TrackedEdit delete_edit_;
  TrackedEdit add_edit_;
};

} // namespace

class RocksDBPatcher::Impl {
 public:
  Impl(const std::string& dbpath, const rocksdb::Options& options)
      : options_(SanitizeOptions(dbpath, &comparator_, options)),
        imm_cf_options_(options_),
        env_options_(options_),
        cf_options_(options_),
        version_set_(dbpath, &options_, env_options_, block_cache_.get(), &write_buffer_, nullptr) {
    cf_options_.comparator = comparator_.user_comparator();
  }

  Status Load() {
    std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
    column_families.emplace_back("default", cf_options_);
    return version_set_.Recover(column_families);
  }

  Status SetHybridTimeFilter(std::optional<uint32_t> db_oid, HybridTime value) {
    RocksDBPatcherHelper helper(&version_set_);

    helper.IterateFiles([&helper, db_oid, value](
        int level, const rocksdb::FileMetaData& file) {
      if (!file.largest.user_frontier) {
        return;
      }
      auto& consensus_frontier = down_cast<ConsensusFrontier&>(*file.largest.user_frontier);
      // If all the data in the file is already as of old time, no need to set any filter.
      if (consensus_frontier.hybrid_time() <= value) {
        LOG(INFO) << "No need to set hybrid time filter since the largest frontier is already"
                  << " older. Largest frontier HT " << consensus_frontier.hybrid_time()
                  << ", filter HT " << value;
        return;
      }
      if (db_oid) {
        std::vector<std::pair<uint32_t, HybridTime>> new_filter;
        consensus_frontier.CotablesFilter(&new_filter);
        // Since existing filter is sorted, we can perform binary search to find the db oid.
        auto it = std::lower_bound(new_filter.begin(), new_filter.end(), *db_oid,
            [](const std::pair<uint32_t, HybridTime>& a, const uint32_t& b) {
              return a.first < b;
            });
        // Simply append if not found.
        if (it == new_filter.end() || it->first != *db_oid) {
          new_filter.insert(it, { *db_oid, value });
        } else {
          // Update if found.
          it->second = std::min(it->second, value);
        }
        rocksdb::FileMetaData fmd = file;
        down_cast<ConsensusFrontier&>(*fmd.largest.user_frontier).SetCoTablesFilter(
            std::move(new_filter));
        LOG(INFO) << "Largest frontier post restore " << fmd.FrontiersToString();
        helper.ModifyFile(level, fmd);
        return;
      } /* if (db_oid) */
      if (HybridTime(consensus_frontier.GlobalFilter()) <= value) {
        LOG(INFO) << "No need to set hybrid time filter since the largest frontier already"
                  << " has an older filter. Largest frontier HT "
                  << consensus_frontier.GlobalFilter()
                  << ", filter " << value;
        return;
      }
      rocksdb::FileMetaData fmd = file;
      down_cast<ConsensusFrontier&>(*fmd.largest.user_frontier).SetGlobalFilter(value);
      LOG(INFO) << "Largest frontier post restore " << fmd.FrontiersToString();
      helper.ModifyFile(level, fmd);
    });

    return helper.Apply(options_, imm_cf_options_);
  }

  Status ModifyFlushedFrontier(
      const ConsensusFrontier& frontier, const CotableIdsMap& cotable_ids_map) {
    RocksDBPatcherHelper helper(&version_set_);

    docdb::ConsensusFrontier final_frontier = frontier;

    auto* existing_frontier = down_cast<docdb::ConsensusFrontier*>(version_set_.FlushedFrontier());
    if (existing_frontier) {
      if (!frontier.history_cutoff_valid()) {
        final_frontier.set_history_cutoff_information(
            existing_frontier->history_cutoff());
      }
      if (!frontier.op_id()) {
        // Update op id only if it was specified in frontier.
        final_frontier.set_op_id(existing_frontier->op_id());
      }
    }

    helper.Edit().ModifyFlushedFrontier(
        final_frontier.Clone(), rocksdb::FrontierModificationMode::kForce);

    helper.IterateFiles([&helper, &frontier, &cotable_ids_map](
        int level, rocksdb::FileMetaData fmd) {
      bool modified = false;
      for (auto* user_frontier : {&fmd.smallest.user_frontier, &fmd.largest.user_frontier}) {
        if (!*user_frontier) {
          continue;
        }
        auto& consensus_frontier = down_cast<ConsensusFrontier&>(**user_frontier);
        if (!consensus_frontier.op_id().empty()) {
          consensus_frontier.set_op_id(OpId());
          modified = true;
        }
        if (frontier.history_cutoff_valid()) {
          consensus_frontier.set_history_cutoff_information(frontier.history_cutoff());
          modified = true;
        }
        for (const auto& [table_id, new_table_id] : cotable_ids_map) {
          if (consensus_frontier.UpdateCoTableId(table_id, new_table_id)) {
            modified = true;
          }
        }
      }
      if (modified) {
        helper.ModifyFile(level, fmd);
      }
    });

    return helper.Apply(options_, imm_cf_options_);
  }

  Status UpdateFileSizes() {
    RocksDBPatcherHelper helper(&version_set_);

    RETURN_NOT_OK(helper.IterateFiles(
        [&helper, this](int level, const rocksdb::FileMetaData& file) -> Status {
      auto base_path = rocksdb::MakeTableFileName(
          options_.db_paths[file.fd.GetPathId()].path, file.fd.GetNumber());
      auto data_path = rocksdb::TableBaseToDataFileName(base_path);
      auto base_size = VERIFY_RESULT(options_.env->GetFileSize(base_path));
      auto data_size = VERIFY_RESULT(options_.env->GetFileSize(data_path));
      auto total_size = base_size + data_size;
      if (file.fd.base_file_size == base_size && file.fd.total_file_size == total_size) {
        return Status::OK();
      }
      rocksdb::FileMetaData fmd = file;
      fmd.fd.base_file_size = base_size;
      fmd.fd.total_file_size = total_size;
      helper.ModifyFile(level, fmd);
      return Status::OK();
    }));

    return helper.Apply(options_, imm_cf_options_);
  }

  bool TEST_ContainsHybridTimeFilter() {
    RocksDBPatcherHelper helper(&version_set_);
    bool contains_filter = false;
    helper.IterateFiles([&contains_filter](
        int level, const rocksdb::FileMetaData& file) {
      if (!file.largest.user_frontier) {
        return;
      }
      auto& consensus_frontier = down_cast<ConsensusFrontier&>(*file.largest.user_frontier);
      if (consensus_frontier.HasFilter()) {
        contains_filter = true;
      }
    });
    return contains_filter;
  }

 private:
  const rocksdb::InternalKeyComparator comparator_{rocksdb::BytewiseComparator()};
  rocksdb::WriteBuffer write_buffer_{1_KB};
  std::shared_ptr<rocksdb::Cache> block_cache_{rocksdb::NewLRUCache(1_MB)};

  rocksdb::Options options_;
  rocksdb::ImmutableCFOptions imm_cf_options_;
  rocksdb::EnvOptions env_options_;
  rocksdb::ColumnFamilyOptions cf_options_;
  rocksdb::VersionSet version_set_;
};

RocksDBPatcher::RocksDBPatcher(const std::string& dbpath, const rocksdb::Options& options)
    : impl_(new Impl(dbpath, options)) {
}

RocksDBPatcher::~RocksDBPatcher() {
}

Status RocksDBPatcher::Load() {
  return impl_->Load();
}

Status RocksDBPatcher::SetHybridTimeFilter(std::optional<uint32_t> db_oid, HybridTime value) {
  return impl_->SetHybridTimeFilter(db_oid, value);
}

Status RocksDBPatcher::ModifyFlushedFrontier(
    const ConsensusFrontier& frontier, const CotableIdsMap& cotable_ids_map) {
  return impl_->ModifyFlushedFrontier(frontier, cotable_ids_map);
}

Status RocksDBPatcher::UpdateFileSizes() {
  return impl_->UpdateFileSizes();
}

bool RocksDBPatcher::TEST_ContainsHybridTimeFilter() {
  return impl_->TEST_ContainsHybridTimeFilter();
}

Status ForceRocksDBCompact(rocksdb::DB* db,
    const rocksdb::CompactRangeOptions& options) {
  RETURN_NOT_OK_PREPEND(
      db->CompactRange(options, /* begin = */ nullptr, /* end = */ nullptr),
      "Compact range failed");
  return Status::OK();
}

RateLimiterSharingMode GetRocksDBRateLimiterSharingMode() {
  auto result = ParseEnumInsensitive<RateLimiterSharingMode>(
      FLAGS_rocksdb_compact_flush_rate_limit_sharing_mode);
  if (PREDICT_TRUE(result.ok())) {
    return *result;
  }
  LOG(DFATAL) << result.status();
  return RateLimiterSharingMode::NONE;
}

std::shared_ptr<rocksdb::RateLimiter> CreateRocksDBRateLimiter() {
  if (PREDICT_TRUE((FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec > 0))) {
    return std::shared_ptr<rocksdb::RateLimiter>(
      rocksdb::NewGenericRateLimiter(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec));
  }
  return nullptr;
}

} // namespace docdb
} // namespace yb
