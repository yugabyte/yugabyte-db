// Copyright (c) YugaByte, Inc.

#include <string>

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"

using std::shared_ptr;
using std::string;
using strings::Substitute;

using yb::util::FormatBytesAsStr;
using yb::util::QuotesType;

namespace yb {

void InitRocksDBWriteOptions(rocksdb::WriteOptions* write_options) {
  // We disable the WAL in RocksDB because we already have the Raft log and we should
  // replay it during recovery.
  write_options->disableWAL = true;
  write_options->sync = false;
}

std::string FormatRocksDBSliceAsStr(const rocksdb::Slice& rocksdb_slice,
                                    const size_t max_length) {
  return FormatBytesAsStr(rocksdb_slice.cdata(),
                          rocksdb_slice.size(),
                          QuotesType::kDoubleQuotes,
                          max_length);
}

}  // namespace yb
