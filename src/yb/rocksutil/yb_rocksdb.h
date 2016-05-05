// Copyright (c) YugaByte, Inc.

#ifndef YB_YB_ROCKSDB_H
#define YB_YB_ROCKSDB_H

#include <string>
#include "rocksdb/env.h"
#include "rocksdb/include/rocksdb/options.h"

namespace yb {

// Initialize the RocksDB 'options' object for tablet identified by 'tablet_id'. The
// 'statistics' object provided by the caller will be used by RocksDB to maintain
// the stats for the tablet specified by 'tablet_id'.
void InitRocksDBOptions(rocksdb::Options *options,
                        const std::string& tablet_id,
                        const std::shared_ptr<rocksdb::Statistics>& statistics);

}

#endif // YB_YB_ROCKSDB_H
