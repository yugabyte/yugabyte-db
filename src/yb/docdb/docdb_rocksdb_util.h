// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_
#define YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_

#include "rocksdb/cache.h"
#include "rocksdb/db.h"
#include "rocksdb/include/rocksdb/cache.h"
#include "rocksdb/include/rocksdb/options.h"
#include "rocksdb/slice.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/value.h"
#include "yb/util/slice.h"

namespace yb {
namespace docdb {

// Seek to a given prefix and hybrid_time. If an expired value is found, it is still considered
// "valid" for the purpose of this function, but the value get transformed into a tombstone (a
// deletion marker).
//
// search_key is an encoded form of a SubDocKey with no timestamp. This identifies a subdocument
// that we're trying to find the next RocksDB key/value pair.
Status SeekToValidKvAtTs(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &search_key,
    HybridTime hybrid_time,
    SubDocKey *found_key,
    bool *is_found = nullptr,
    Value *found_value = nullptr);

// See to a rocksdb point that is at least sub_doc_key.
// If the iterator is already positioned far enough, does not perform a seek.
void SeekForward(const rocksdb::Slice& slice, rocksdb::Iterator *iter);

void SeekForward(const KeyBytes& key_bytes, rocksdb::Iterator *iter);

// A wrapper around the RocksDB seek operation that uses Next() up to the configured number of
// times to avoid invalidating iterator state. In debug mode it also allows printing detailed
// information about RocksDB seeks.
void PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &seek_key,
    const char* file_name,
    int line);

// TODO: is there too much overhead in passing file name and line here in release mode?
#define ROCKSDB_SEEK(iter, key) \
  do { \
    PerformRocksDBSeek((iter), (key), __FILE__, __LINE__); \
  } while (0)

enum class BloomFilterMode {
  USE_BLOOM_FILTER,
  DONT_USE_BLOOM_FILTER,
};

// It is only allowed to use bloom filters on scans within the same hashed components of the key,
// because BloomFilterAwareIterator relies on it and ignores SST file completely if there are no
// keys with the same hashed components as key specified for seek operation.
// Note: bloom_filter_mode should be specified explicitly to avoid using it incorrectly by default.
std::unique_ptr<rocksdb::Iterator> CreateRocksDBIterator(
    rocksdb::DB* rocksdb,
    BloomFilterMode bloom_filter_mode,
    const rocksdb::QueryId query_id,
    rocksdb::ReadFileFilter file_filter = rocksdb::ReadFileFilter());

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
