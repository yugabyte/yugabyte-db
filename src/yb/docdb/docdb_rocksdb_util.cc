// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb_rocksdb_util.h"

#include <memory>

#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/key_bytes.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"

using std::unique_ptr;
using strings::Substitute;

namespace yb {
namespace docdb {

void PerformRocksDBSeek(
    rocksdb::Iterator *iter,
    const rocksdb::Slice &key,
    const char* file_name,
    int line) {
  iter->Seek(key);
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

unique_ptr<rocksdb::Iterator> CreateRocksDBIterator(rocksdb::DB* rocksdb) {
  // TODO: avoid instantiating ReadOptions every time. Pre-create it once and use for all iterators.
  //       We'll need some sort of a stateful wrapper class around RocksDB for that.
  rocksdb::ReadOptions read_opts;
  return unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
}

}  // namespace docdb
}  // namespace yb
