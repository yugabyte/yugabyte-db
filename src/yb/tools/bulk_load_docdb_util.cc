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

#include "yb/docdb/doc_write_batch.h"

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/memtablerep.h"

#include "yb/tools/bulk_load_docdb_util.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"

DECLARE_int32(num_memtables);

namespace yb {
namespace tools {

BulkLoadDocDBUtil::BulkLoadDocDBUtil(const std::string& tablet_id,
                                     const std::string& base_dir,
                                     const size_t memtable_size,
                                     int num_memtables,
                                     int max_background_flushes)
    : // Using optional init markers here because bulk load is only supported for CQL as of
      // 12/03/2017.
      DocDBRocksDBUtil(docdb::InitMarkerBehavior::kOptional),
      tablet_id_(tablet_id),
      base_dir_(base_dir),
      memtable_size_(memtable_size),
      num_memtables_(num_memtables),
      max_background_flushes_(max_background_flushes) {
}

Status BulkLoadDocDBUtil::InitRocksDBDir() {
  rocksdb_dir_ = JoinPathSegments(base_dir_, tablet_id_);
  RETURN_NOT_OK(Env::Default()->DeleteRecursively(rocksdb_dir_));
  return Status::OK();
}

Status BulkLoadDocDBUtil::InitRocksDBOptions() {
  RETURN_NOT_OK(InitCommonRocksDBOptionsForBulkLoad(tablet_id()));
  regular_db_options_.max_write_buffer_number = num_memtables_;
  regular_db_options_.write_buffer_size = memtable_size_;
  regular_db_options_.allow_concurrent_memtable_write = true;
  regular_db_options_.enable_write_thread_adaptive_yield = true;
  regular_db_options_.max_background_flushes = max_background_flushes_;
  regular_db_options_.env->SetBackgroundThreads(
      max_background_flushes_, rocksdb::Env::Priority::HIGH);
  // We need to set level0_file_num_compaction_trigger even in case compaction is disabled, because
  // RocksDB SanitizeOptions function is increasing level0_slowdown_writes_trigger to be greater
  // or equal to level0_file_num_compaction_trigger.
  regular_db_options_.level0_file_num_compaction_trigger = -1;
  regular_db_options_.level0_slowdown_writes_trigger = -1;
  regular_db_options_.level0_stop_writes_trigger = std::numeric_limits<int>::max();
  regular_db_options_.delayed_write_rate = std::numeric_limits<int>::max();

  regular_db_options_.memtable_factory = std::make_shared<rocksdb::SkipListFactory>(
      0 /* lookahead */, rocksdb::ConcurrentWrites::kTrue);

  // TODO - we might consider also set disableDataSync to true and do manual sync after bulk load,
  // see yb/rocksdb/options.h.

  return Status::OK();
}

std::string BulkLoadDocDBUtil::tablet_id() {
  return tablet_id_;
}

const std::string& BulkLoadDocDBUtil::rocksdb_dir() {
  return rocksdb_dir_;
}

} // namespace tools
} // namespace yb
