//--------------------------------------------------------------------------------------------------
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
// Classes that implement secondary index.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_INDEX_H_
#define YB_COMMON_INDEX_H_

#include <unordered_map>
#include <vector>

#include "yb/common/entity_ids.h"
#include "yb/common/schema.h"

namespace yb {

// A class to maintain the information of an index.
class IndexInfo {
 public:
  // Index column mapping.
  struct IndexColumn {
    ColumnId column_id;         // Column id in the index table.
    ColumnId indexed_column_id; // Corresponding column id in indexed table.

    explicit IndexColumn(const IndexInfoPB::IndexColumnPB& pb);
    IndexColumn() {}
  };

  explicit IndexInfo(const IndexInfoPB& pb);
  IndexInfo() {}

  const TableId& table_id() const { return table_id_; }
  uint32_t schema_version() const { return schema_version_; }
  bool is_local() const { return is_local_; }

  const std::vector<IndexColumn>& columns() const { return columns_; }
  const IndexColumn& column(const size_t idx) const { return columns_[idx]; }
  size_t hash_column_count() const { return hash_column_count_; }
  size_t range_column_count() const { return range_column_count_; }
  size_t key_column_count() const { return hash_column_count_ + range_column_count_; }

 private:
  const TableId table_id_;            // Index table id.
  const uint32_t schema_version_ = 0; // Index table's schema version.
  const bool is_local_ = false;       // Whether index is local.
  const std::vector<IndexColumn> columns_; // Index columns.
  const size_t hash_column_count_ = 0;     // Number of hash columns in the index.
  const size_t range_column_count_ = 0;    // Number of range columns in the index.
};

// A map to look up an index by its index table id.
using IndexMap = std::unordered_map<TableId, IndexInfo>;

}  // namespace yb

#endif  // YB_COMMON_INDEX_H_
