// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include "yb/docdb/docdb_util.h"

namespace yb {
namespace tools {

class BulkLoadDocDBUtil : public docdb::DocDBRocksDBUtil {
 public:
  BulkLoadDocDBUtil(const std::string& tablet_id, const std::string& base_dir,
                    size_t memtable_size, int num_memtables, int max_background_flushes);
  Status InitRocksDBDir() override;
  Status InitRocksDBOptions() override;
  std::string tablet_id() override;
  size_t block_cache_size() const override  { return 0; }
  const std::string& rocksdb_dir();
  Schema CreateSchema() override { return Schema(); }

 private:
  const std::string tablet_id_;
  const std::string base_dir_;
  const size_t memtable_size_;
  const int num_memtables_;
  const int max_background_flushes_;
};

} // namespace tools
} // namespace yb
