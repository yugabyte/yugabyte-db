// Copyright (c) YugaByte, Inc.

#ifndef YB_TOOLS_BULK_LOAD_DOCDB_UTIL_H
#define YB_TOOLS_BULK_LOAD_DOCDB_UTIL_H

#include "yb/docdb/docdb_util.h"

namespace yb {
namespace tools {

class BulkLoadDocDBUtil : public docdb::DocDBRocksDBUtil {
 public:
  BulkLoadDocDBUtil(const std::string& tablet_id, const std::string& base_dir,
                    size_t memtable_size, int num_memtables, int max_background_flushes);
  CHECKED_STATUS InitRocksDBDir() override;
  CHECKED_STATUS InitRocksDBOptions() override;
  std::string tablet_id() override;
  size_t block_cache_size() const override  { return 0; }
  const std::string& rocksdb_dir();

 private:
  const std::string tablet_id_;
  const std::string base_dir_;
  const size_t memtable_size_;
  const int num_memtables_;
  const int max_background_flushes_;
};

} // namespace tools
} // namespace yb

#endif // YB_TOOLS_BULK_LOAD_DOCDB_UTIL_H
