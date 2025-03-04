// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/entity_ids_types.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/kv_util.h"

#include "yb/vector_index/vector_index_fwd.h"

namespace yb::docdb {

using EncodedDistance = uint64_t;

struct VectorIndexInsertEntry {
  ValueBuffer value;
};

struct VectorIndexSearchResultEntry {
  EncodedDistance encoded_distance;
  KeyBuffer key;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(encoded_distance, key);
  }
};

class VectorIndex {
 public:
  virtual ~VectorIndex() = default;

  virtual const TableId& table_id() const = 0;
  virtual Slice indexed_table_key_prefix() const = 0;
  virtual ColumnId column_id() const = 0;
  virtual const std::string& path() const = 0;
  virtual HybridTime hybrid_time() const = 0;

  virtual Status Insert(
      const VectorIndexInsertEntries& entries, const rocksdb::UserFrontiers* frontiers) = 0;
  virtual Result<VectorIndexSearchResult> Search(
      Slice vector, const vector_index::SearchOptions& options) = 0;
  virtual Result<EncodedDistance> Distance(Slice lhs, Slice rhs) = 0;
  virtual Status Flush() = 0;
  virtual Status WaitForFlush() = 0;
  virtual docdb::ConsensusFrontierPtr GetFlushedFrontier() = 0;
  virtual rocksdb::FlushAbility GetFlushAbility() = 0;
  virtual Status CreateCheckpoint(const std::string& out) = 0;
  virtual const std::string& ToString() const = 0;
  virtual Result<bool> HasVectorId(const vector_index::VectorId& vector_id) const = 0;

  bool BackfillDone();

 private:
  std::atomic<bool> backfill_done_cache_{false};
};

Result<VectorIndexPtr> CreateVectorIndex(
    const std::string& log_prefix,
    const std::string& data_root_dir,
    rpc::ThreadPool& thread_pool,
    Slice indexed_table_key_prefix,
    HybridTime hybrid_time,
    const qlexpr::IndexInfo& index_info,
    const DocDB& doc_db);

extern const std::string kVectorIndexDirPrefix;

void AddVectorIndexReverseEntry(
    rocksdb::DirectWriteHandler& handler, Slice ybctid, Slice value, DocHybridTime write_ht);

}  // namespace yb::docdb
