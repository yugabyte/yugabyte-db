// Copyright (c) YugaByte, Inc.

#ifndef YB_YB_ROCKSDB_H
#define YB_YB_ROCKSDB_H

#include <string>
#include <cstddef>

#include "yb/util/slice.h"
#include "rocksdb/env.h"
#include "rocksdb/include/rocksdb/options.h"

namespace yb {

// Initialize the RocksDB 'options' object for tablet identified by 'tablet_id'. The
// 'statistics' object provided by the caller will be used by RocksDB to maintain
// the stats for the tablet specified by 'tablet_id'.
void InitRocksDBOptions(rocksdb::Options* options,
                        const std::string& tablet_id,
                        const std::shared_ptr<rocksdb::Statistics>& statistics);

void InitRocksDBWriteOptions(rocksdb::WriteOptions* write_options);

std::string FormatRocksDBSliceAsStr(const rocksdb::Slice& rocksdb_slice);

inline rocksdb::Slice YBToRocksDBSlice(const yb::Slice& yb_slice) {
  return rocksdb::Slice(reinterpret_cast<const char*>(yb_slice.data()), yb_slice.size());
}

inline yb::Slice RocksDBToYBSlice(const rocksdb::Slice& rocksdb_slice) {
  return yb::Slice(reinterpret_cast<const uint8_t*>(rocksdb_slice.data()), rocksdb_slice.size());
}

}

#endif // YB_YB_ROCKSDB_H
