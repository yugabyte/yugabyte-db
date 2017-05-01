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
#include "yb/server/hybrid_clock.h"
#include "yb/util/trace.h"

DEFINE_int32(rocksdb_max_background_flushes, 1, "Number threads to do background flushes.");
DEFINE_int32(rocksdb_base_background_compactions, 1,
             "Number threads to do background compactions.");
DEFINE_int32(rocksdb_max_background_compactions, 2,
             "Increased number of threads to do background compactions (used when compactions need "
             "to catch up.)");
DEFINE_int32(rocksdb_level0_file_num_compaction_trigger, 5,
             "Number of files to trigger level-0 compaction. -1 if compaction should not be "
             "triggered by number of files at all.");

DEFINE_int32(rocksdb_level0_slowdown_writes_trigger, 24,
             "The number of files above which writes are slowed down.");
DEFINE_int32(rocksdb_level0_stop_writes_trigger, 48,
             "The number of files above which compactions are stopped.");
DEFINE_int32(rocksdb_universal_compaction_size_ratio, 20,
             "The percentage upto which files that are larger are include in a compaction.");
DEFINE_int32(rocksdb_universal_compaction_min_merge_width, 4,
             "The minimum number of files in a single compaction run.");
DEFINE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec, 100 * 1024 * 1024,
             "Use to control write rate of flush and compaction.");

DEFINE_int64(db_block_size_bytes, 32 * 1024,
             "Size of RocksDB block (in bytes).");

DEFINE_bool(use_docdb_aware_bloom_filter, false,
            "Whether to use the DocDbAwareFilterPolicy for both bloom storage and seeks.");
DEFINE_int32(max_nexts_to_avoid_seek, 8,
             "The number of next calls to try before doing resorting to do a rocksdb seek.");
DEFINE_bool(trace_docdb_calls, false, "Whether we should trace calls into the docdb.");

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using strings::Substitute;

namespace yb {
namespace docdb {

Status SeekToValidKvAtTs(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &search_key,
    HybridTime hybrid_time,
    SubDocKey *found_key,
    Value *found_value,
    bool *is_found) {
  *is_found = true;
  KeyBytes seek_key_bytes = KeyBytes(search_key);
  seek_key_bytes.AppendValueType(ValueType::kHybridTime);
  seek_key_bytes.AppendHybridTime(hybrid_time);

  // If we end up at a descendant of the search key, the timestamp may be > hybrid_time,
  // In a loop we want to skip over those cases.
  while (true) {
    ROCKSDB_SEEK(iter, seek_key_bytes.AsSlice());
    if (!iter->Valid() || !iter->key().starts_with(search_key)) {
      *is_found = false;
      return Status::OK();
    }
    if (iter->key().size() < kBytesPerHybridTime) {
      return STATUS_SUBSTITUTE(Corruption, "Keys from rocksdb must have $0 bytes, found $1",
          kBytesPerHybridTime, iter->key().size());
    }
    HybridTime ht_from_found_key = DecodeHybridTimeFromKey(
        iter->key(), iter->key().size() - kBytesPerHybridTime);
    if (ht_from_found_key <= hybrid_time) {
      break;
    }
    seek_key_bytes = KeyBytes(iter->key());
    seek_key_bytes.ReplaceLastHybridTime(hybrid_time);
  }
  rocksdb::Slice value = iter->value();
  RETURN_NOT_OK(found_key->FullyDecodeFrom(iter->key()));
  MonoDelta ttl;
  RETURN_NOT_OK(Value::DecodeTTL(&value, &ttl));
  if (!ttl.Equals(Value::kMaxTtl)) {
    const HybridTime expiry =
        server::HybridClock::AddPhysicalTimeToHybridTime(found_key->hybrid_time(), ttl);
    if (hybrid_time.CompareTo(expiry) > 0) {
      *found_value = Value(PrimitiveValue(ValueType::kTombstone));
      found_key->ReplaceMaxHybridTimeWith(expiry);
      return Status::OK();
    }
  }
  RETURN_NOT_OK(found_value->Decode(value));
  return Status::OK();
}

void SeekForward(const rocksdb::Slice& slice, rocksdb::Iterator *iter) {
  if (!iter->Valid() || iter->key().compare(slice) >= 0) {
    return;
  }
  ROCKSDB_SEEK(iter, slice);
}

void SeekForward(const KeyBytes& key_bytes, rocksdb::Iterator *iter) {
  SeekForward(key_bytes.AsSlice(), iter);
}

void PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &key,
    const char* file_name,
    int line) {
  if (key.size() == 0) {
    iter->SeekToFirst();
  } else if (!iter->Valid() || iter->key().compare(key) > 0) {
    iter->Seek(key);
  } else {
    for (int nexts = 0; nexts <= FLAGS_max_nexts_to_avoid_seek; nexts++) {
      if (!iter->Valid() || iter->key().compare(key) >= 0) {
          if (FLAGS_trace_docdb_calls) {
            TRACE("Did $0 Next(s) instead of a Seek", nexts);
          }
          break;
      }
      if (nexts < FLAGS_max_nexts_to_avoid_seek) {
        iter->Next();
      } else {
        if (FLAGS_trace_docdb_calls) {
          TRACE("Forced to do an actual Seek after $0 Next(s)", FLAGS_max_nexts_to_avoid_seek);
        }
        iter->Seek(key);
      }
    }
  }
  VLOG(4) << Substitute(
      "RocksDB seek:\n"
      "RocksDB seek at $0:$1:\n"
      "    Seek key:         $2\n"
      "    Seek key (raw):   $3\n"
      "    Actual key:       $4\n"
      "    Actual key (raw): $5\n"
      "    Actual value:     $6",
      file_name, line,
      BestEffortDocDBKeyToStr(KeyBytes(key)), FormatRocksDBSliceAsStr(key),
      iter->Valid() ? BestEffortDocDBKeyToStr(KeyBytes(iter->key())) : "N/A",
      iter->Valid() ? FormatRocksDBSliceAsStr(iter->key()) : "N/A",
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
  }

  options->table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

  // Compaction related options.

  // Enable universal style compactions.
  options->compaction_style = rocksdb::CompactionStyle::kCompactionStyleUniversal;
  // Set the number of levels to 1.
  options->num_levels = 1;

  options->base_background_compactions = FLAGS_rocksdb_base_background_compactions;
  options->max_background_compactions = FLAGS_rocksdb_max_background_compactions;
  options->max_background_flushes = FLAGS_rocksdb_max_background_flushes;
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
