// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_
#define YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_

#include "rocksdb/db.h"

namespace yb {
namespace docdb {

#ifdef NDEBUG

// Release mode
#define ROCKSDB_SEEK(iter, key) \
  do { \
    (iter)->Seek(key); \
  } while (0)

#else

// Debug mode: allow printing detailed information about RocksDB seeks.
void PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &key,
    const char* file_name,
    int line);

#define ROCKSDB_SEEK(iter, key) \
  do { \
    PerformRocksDBSeek((iter), (key), __FILE__, __LINE__); \
  } while (0)

#endif

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_ROCKSDB_UTIL_H_
