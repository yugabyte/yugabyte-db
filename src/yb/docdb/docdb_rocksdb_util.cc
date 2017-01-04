// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb_rocksdb_util.h"

#include <memory>

#include "rocksdb/include/rocksdb/rate_limiter.h"
#include "rocksdb/table.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/key_bytes.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/rocksutil/yb_rocksdb_logger.h"

DEFINE_int32(rocksdb_level0_file_num_compaction_trigger, 5,
             "Number of files to trigger level-0 compaction. -1 if compaction should not be "
             "triggered by number of files at all.");

DEFINE_int32(rocksdb_level0_slowdown_writes_trigger, 24,
             "The number of files above which writes are slowed down.");
DEFINE_int32(rocksdb_level0_stop_writes_trigger, 48,
             "The number of files above which compactions are stopped.");
DEFINE_int32(rocksdb_universal_compaction_size_ratio, 20,
             "The percentage upto which files that are larger are include in a compaction.");
DEFINE_int32(rocksdb_universal_compaction_min_merge_width, 3,
             "The minimum number of files in a single compaction run.");
DEFINE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec, 100 * 1024 * 1024,
             "Use to control write rate of flush and compaction.");

DEFINE_int64(db_block_size_bytes, 64 * 1024,
             "Size of RocksDB block (in bytes).");

DEFINE_bool(use_docdb_aware_bloom_filter, true,
            "Whether to use the DocDbAwareFilterPolicy for both bloom storage and seeks.");

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using strings::Substitute;

namespace yb {
namespace docdb {

void PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &key,
    const char* file_name,
    int line) {
  if (key.size() == 0) {
    iter->SeekToFirst();
  } else {
    iter->Seek(key);
  }
  VLOG(4) << Substitute(
      "RocksDB seek:\n"
      "RocksDB seek at $0:$1:\n"
      "    Seek key:       $2\n"
      "    Seek key (raw): $3\n"
      "    Actual key:     $4\n"
      "    Actual value:   $5",
      file_name, line,
      BestEffortDocDBKeyToStr(KeyBytes(key)), FormatRocksDBSliceAsStr(key),
      iter->Valid() ? BestEffortDocDBKeyToStr(KeyBytes(iter->key())) : "N/A",
      iter->Valid() ? FormatRocksDBSliceAsStr(iter->value()) : "N/A");
}

unique_ptr<rocksdb::Iterator> CreateRocksDBIterator(rocksdb::DB* rocksdb, bool use_bloom_on_scan) {
  // TODO: avoid instantiating ReadOptions every time. Pre-create it once and use for all iterators.
  //       We'll need some sort of a stateful wrapper class around RocksDB for that.
  rocksdb::ReadOptions read_opts;
  read_opts.use_bloom_on_scan = FLAGS_use_docdb_aware_bloom_filter && use_bloom_on_scan;
  return unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
}

void InitRocksDBOptions(
    rocksdb::Options* options, const string& tablet_id,
    const shared_ptr<rocksdb::Statistics>& statistics,
    const shared_ptr<rocksdb::Cache>& block_cache) {
  options->create_if_missing = true;
  options->disableDataSync = true;
  options->statistics = statistics;
  options->info_log = std::make_shared<YBRocksDBLogger>(Substitute("T $0: ", tablet_id));
  options->info_log_level = YBRocksDBLogger::ConvertToRocksDBLogLevel(FLAGS_minloglevel);
  options->set_last_seq_based_on_sstable_metadata = true;

  // Set block cache options.
  rocksdb::BlockBasedTableOptions table_options;
  table_options.block_cache = block_cache;
  table_options.block_size = FLAGS_db_block_size_bytes;
  // Cache the bloom filters in the block cache.
  table_options.cache_index_and_filter_blocks = true;

  // Set our custom bloom filter that is docdb aware.
  if (FLAGS_use_docdb_aware_bloom_filter) {
    table_options.filter_policy.reset(new DocDbAwareFilterPolicy());
    options->table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));
  }

  // Compaction related options.

  // Enable universal style compactions.
  options->compaction_style = rocksdb::CompactionStyle::kCompactionStyleUniversal;
  // Set the number of levels to 1.
  options->num_levels = 1;

  options->level0_file_num_compaction_trigger = FLAGS_rocksdb_level0_file_num_compaction_trigger;
  options->level0_slowdown_writes_trigger = FLAGS_rocksdb_level0_slowdown_writes_trigger;
  options->level0_stop_writes_trigger = FLAGS_rocksdb_level0_stop_writes_trigger;
  // This determines the algo used to compute which files will be included. The "total size" based
  // computation compares the size of every new file with the sum of all files included so far.
  options->compaction_options_universal.stop_style =
      rocksdb::CompactionStopStyle::kCompactionStopStyleTotalSize;
  options->compaction_options_universal.size_ratio = FLAGS_rocksdb_universal_compaction_size_ratio;
  options->compaction_options_universal.min_merge_width =
      FLAGS_rocksdb_universal_compaction_min_merge_width;
  if (FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec > 0) {
    options->rate_limiter.reset(
        rocksdb::NewGenericRateLimiter(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec));
  }
}

}  // namespace docdb
}  // namespace yb
