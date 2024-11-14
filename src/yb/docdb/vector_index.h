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

#include <memory>

#include "yb/docdb/docdb_fwd.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/kv_util.h"

namespace yb::docdb {

struct VectorIndexInsertEntry {
  KeyBuffer key;
  ValueBuffer value;
};

struct VectorIndexSearchResultEntry {
  size_t encoded_distance;
  KeyBuffer key;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(encoded_distance, key);
  }
};

using VectorIndexInsertEntries = std::vector<VectorIndexInsertEntry>;
using VectorIndexSearchResult = std::vector<VectorIndexSearchResultEntry>;

class VectorIndex {
 public:
  virtual ~VectorIndex() = default;

  virtual ColumnId column_id() const = 0;
  virtual Status Insert(
      const VectorIndexInsertEntries& entries, HybridTime write_time,
      const rocksdb::UserFrontiers* frontiers) = 0;
  virtual Result<VectorIndexSearchResult> Search(Slice vector, size_t max_num_results) = 0;
};

Result<VectorIndexPtr> CreateVectorIndex(
    const std::string& data_root_dir, rpc::ThreadPool& thread_pool,
    const qlexpr::IndexInfo& index_info);

}  // namespace yb::docdb
