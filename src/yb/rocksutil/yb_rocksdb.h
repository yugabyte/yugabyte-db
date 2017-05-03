// Copyright (c) YugaByte, Inc.

#ifndef YB_ROCKSUTIL_YB_ROCKSDB_H
#define YB_ROCKSUTIL_YB_ROCKSDB_H

#include <string>
#include <cstddef>

#include "rocksdb/env.h"
#include "rocksdb/include/rocksdb/options.h"

#include "yb/util/compare_util.h"
#include "yb/util/slice.h"
#include "yb/util/cast.h"

namespace yb {

void InitRocksDBWriteOptions(rocksdb::WriteOptions* write_options);

std::string FormatRocksDBSliceAsStr(const rocksdb::Slice& rocksdb_slice,
                                    size_t max_length = std::numeric_limits<size_t>::max());

inline rocksdb::Slice YBToRocksDBSlice(const yb::Slice& yb_slice) {
  return rocksdb::Slice(util::to_char_ptr(yb_slice.data()), yb_slice.size());
}

inline yb::Slice RocksDBToYBSlice(const rocksdb::Slice& rocksdb_slice) {
  return yb::Slice(util::to_uchar_ptr(rocksdb_slice.data()), rocksdb_slice.size());
}

}  // namespace yb

#endif // YB_ROCKSUTIL_YB_ROCKSDB_H
