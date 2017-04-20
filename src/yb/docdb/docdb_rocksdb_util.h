// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_
#define YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_

#include "rocksdb/cache.h"
#include "rocksdb/db.h"
#include "rocksdb/include/rocksdb/options.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/value.h"

namespace yb {
namespace docdb {

// Seek to a given prefix and max_hybrid_time while ignoring expired values.
Status SeekToValidKvAtTs(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &search_key,
    HybridTime hybrid_time,
    SubDocKey *found_key,
    Value *found_value,
    bool *is_found);

// See to a rocksdb point that is at least sub_doc_key.
// If the iterator is already positioned far enough, does not perform a seek.
void SeekForward(const rocksdb::Slice& slice, rocksdb::Iterator *iter);

void SeekForward(const KeyBytes& key_bytes, rocksdb::Iterator *iter);

// Debug mode: allow printing detailed information about RocksDB seeks.
void PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &key,
    const char* file_name,
    int line);

// TODO: is there too much overhead in passing file name and line here in release mode?
#define ROCKSDB_SEEK(iter, key) \
  do { \
    PerformRocksDBSeek((iter), (key), __FILE__, __LINE__); \
  } while (0)

std::unique_ptr<rocksdb::Iterator> CreateRocksDBIterator(
    rocksdb::DB* rocksdb, bool use_bloom_on_scan = true);

// Initialize the RocksDB 'options' object for tablet identified by 'tablet_id'. The
// 'statistics' object provided by the caller will be used by RocksDB to maintain
// the stats for the tablet specified by 'tablet_id'.
void InitRocksDBOptions(
    rocksdb::Options* options, const std::string& tablet_id,
    const std::shared_ptr<rocksdb::Statistics>& statistics,
    const std::shared_ptr<rocksdb::Cache>& block_cache);

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_
