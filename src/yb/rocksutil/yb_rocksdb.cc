// Copyright (c) YugaByte, Inc.

#include <string>

#include "rocksdb/include/rocksdb/rate_limiter.h"

#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/rocksutil/yb_rocksdb_logger.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/bytes_formatter.h"

using std::shared_ptr;
using std::string;
using strings::Substitute;

using yb::util::FormatBytesAsStr;

using namespace rocksdb;

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

namespace yb {

void InitRocksDBOptions(rocksdb::Options* options,
                        const string& tablet_id,
                        const shared_ptr<rocksdb::Statistics>& statistics) {
  options->create_if_missing = true;
  options->disableDataSync = true;
  options->statistics = statistics;
  options->info_log = std::make_shared<YBRocksDBLogger>(Substitute("T $0: ", tablet_id));
  options->info_log_level = YBRocksDBLogger::ConvertToRocksDBLogLevel(FLAGS_minloglevel);
  options->set_last_seq_based_on_sstable_metadata = true;

  // Compaction related options.

  // Enable universal style compactions.
  options->compaction_style = CompactionStyle::kCompactionStyleUniversal;
  // Set the number of levels to 1.
  options->num_levels = 1;

  options->level0_file_num_compaction_trigger = FLAGS_rocksdb_level0_file_num_compaction_trigger;
  options->level0_slowdown_writes_trigger = FLAGS_rocksdb_level0_slowdown_writes_trigger;
  options->level0_stop_writes_trigger = FLAGS_rocksdb_level0_stop_writes_trigger;
  // This determines the algo used to compute which files will be included. The "total size" based
  // computation compares the size of every new file with the sum of all files included so far.
  options->compaction_options_universal.stop_style =
    CompactionStopStyle::kCompactionStopStyleTotalSize;
  options->compaction_options_universal.size_ratio = FLAGS_rocksdb_universal_compaction_size_ratio;
  options->compaction_options_universal.min_merge_width =
    FLAGS_rocksdb_universal_compaction_min_merge_width;
  if (FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec > 0) {
    options->rate_limiter.reset(
      rocksdb::NewGenericRateLimiter(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec));
  }
}

void InitRocksDBWriteOptions(rocksdb::WriteOptions* write_options) {
  // We disable the WAL in RocksDB because we already have the Raft log and we should
  // replay it during recovery.
  write_options->disableWAL = true;
  write_options->sync = false;
}

std::string FormatRocksDBSliceAsStr(const rocksdb::Slice& rocksdb_slice) {
  return FormatBytesAsStr(rocksdb_slice.data(), rocksdb_slice.size());
}

}
