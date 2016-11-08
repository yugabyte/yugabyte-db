// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/key_bytes.h"
#include "yb/rocksutil/yb_rocksdb.h"

namespace yb {
namespace docdb {

#ifndef NDEBUG
void PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &key,
    const char* file_name,
    int line) {
  iter->Seek(key);
  DOCDB_DEBUG_LOG(
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
#endif

}  // namespace docdb
}  // namespace yb
