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

#pragma once

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/optional.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/common/common.pb.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/memory/arena_list.h"

namespace yb::qlexpr {

// A class to maintain the information of an index.
class IndexInfo {
 public:
  explicit IndexInfo(const IndexInfoPB& pb);
  IndexInfo(const IndexInfo& rhs);
  IndexInfo(IndexInfo&& rhs);
  IndexInfo();
  ~IndexInfo();

  void ToPB(IndexInfoPB* pb) const;

  const TableId& table_id() const { return table_id_; }
  const TableId& indexed_table_id() const { return indexed_table_id_; }
  uint32_t schema_version() const { return schema_version_; }
  bool is_local() const { return is_local_; }
  bool is_unique() const { return is_unique_; }

  const std::vector<IndexColumn>& columns() const { return columns_; }
  const IndexColumn& column(const size_t idx) const;
  size_t hash_column_count() const { return hash_column_count_; }
  size_t range_column_count() const { return range_column_count_; }
  size_t key_column_count() const { return hash_column_count_ + range_column_count_; }

  const std::vector<ColumnId>& indexed_hash_column_ids() const {
    return indexed_hash_column_ids_;
  }
  const std::vector<ColumnId>& indexed_range_column_ids() const {
    return indexed_range_column_ids_;
  }
  IndexPermissions index_permissions() const { return index_permissions_; }

  std::shared_ptr<const IndexInfoPB_WherePredicateSpecPB>& where_predicate_spec() const {
    return where_predicate_spec_;
  }

  // Return column ids that are primary key columns of the indexed table.
  std::vector<ColumnId> index_key_column_ids() const;

  // Index primary key columns of the indexed table only?
  bool PrimaryKeyColumnsOnly(const Schema& indexed_schema) const;

  // Is column covered by this index? (Note: indexed columns are always covered)
  bool IsColumnCovered(ColumnId column_id) const;
  bool IsColumnCovered(const std::string& column_name) const;

  // Check if this INDEX contain the column being referenced by the given selected expression.
  // - If found, return the location of the column (columns_[loc]).
  // - Otherwise, return -1.
  int32_t IsExprCovered(const std::string& expr_content) const;

  // Are read operations allowed to use the index?  During CREATE INDEX, reads are not allowed until
  // the index backfill is successfully completed.
  bool HasReadPermission() const { return index_permissions_ == INDEX_PERM_READ_WRITE_AND_DELETE; }

  // Should write operations to the index update the index table?  This includes INSERT and UPDATE.
  bool HasWritePermission() const {
    return index_permissions_ >= INDEX_PERM_WRITE_AND_DELETE &&
           index_permissions_ <= INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING;
  }

  // Should delete operations to the index update the index table?  This includes DELETE and UPDATE.
  bool HasDeletePermission() const {
    return index_permissions_ >= INDEX_PERM_DELETE_ONLY &&
           index_permissions_ <= INDEX_PERM_DELETE_ONLY_WHILE_REMOVING;
  }

  // Is the index being backfilled?
  bool IsBackfilling() const { return index_permissions_ == INDEX_PERM_DO_BACKFILL; }

  const std::string& backfill_error_message() const {
    return backfill_error_message_;
  }

  uint64_t num_rows_processed_by_backfill_job() const {
    return num_rows_processed_by_backfill_job_;
  }

  std::string ToString() const;

  // Same as "IsExprCovered" but only search the key columns.
  boost::optional<size_t> FindKeyIndex(const std::string& key_name) const;

  bool use_mangled_column_name() const {
    return use_mangled_column_name_;
  }

  bool has_index_by_expr() const {
    return has_index_by_expr_;
  }

  // Check if this index is dependent on the given column.
  bool CheckColumnDependency(ColumnId column_id) const;

  // Should account for every field in IndexInfo.
  static bool TEST_Equals(const IndexInfo& lhs, const IndexInfo& rhs);

  size_t DynamicMemoryUsage() const;

  bool is_vector_idx() const;

  const PgVectorIdxOptionsPB &get_vector_idx_options() const;

 private:
  const TableId table_id_;            // Index table id.
  const TableId indexed_table_id_;    // Indexed table id.
  const uint32_t schema_version_ = 0; // Index table's schema version.
  const bool is_local_ = false;       // Whether this is a local index.
  const bool is_unique_ = false;      // Whether this is a unique index.
  const std::vector<IndexColumn> columns_; // Index columns.
  const size_t hash_column_count_ = 0;     // Number of hash columns in the index.
  const size_t range_column_count_ = 0;    // Number of range columns in the index.
  const std::vector<ColumnId> indexed_hash_column_ids_;  // Hash column ids in the indexed table.
  const std::vector<ColumnId> indexed_range_column_ids_; // Range column ids in the indexed table.
  const IndexPermissions index_permissions_ = INDEX_PERM_READ_WRITE_AND_DELETE;
  const std::string backfill_error_message_;
  // When the backfill job is cleared, this is set to the number of indexed table rows processed by
  // the backfill job. The (default) value is zero, otherwise. For partial indexes, this includes
  // non-matching rows of the indexed table.
  const uint64_t num_rows_processed_by_backfill_job_ = 0;

  // Column ids covered by the index (include indexed columns).
  std::unordered_set<ColumnId, boost::hash<ColumnIdRep>> covered_column_ids_;

  // Newer INDEX use mangled column name instead of ID.
  bool use_mangled_column_name_ = false;
  bool has_index_by_expr_ = false;

  mutable std::shared_ptr<const IndexInfoPB_WherePredicateSpecPB> where_predicate_spec_ = nullptr;

  bool has_vector_idx_options_ = false;
  PgVectorIdxOptionsPB vector_idx_options_;
};

// A map to look up an index by its index table id.
// TODO: Rewrite IndexMap be std::unordered_map instead of extending it.
class IndexMap : public std::unordered_map<TableId, IndexInfo> {
 public:
  explicit IndexMap(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes);
  explicit IndexMap(const ArenaList<LWIndexInfoPB>& indexes);
  IndexMap() {}

  void FromPB(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes);
  void ToPB(google::protobuf::RepeatedPtrField<IndexInfoPB>* indexes) const;

  Result<const IndexInfo*> FindIndex(const TableId& index_id) const;

  size_t DynamicMemoryUsage() const;

  // Has to be custom because IndexInfo does not define ==.
  static bool TEST_Equals(const IndexMap& lhs, const IndexMap& rhs);
};

}  // namespace yb::qlexpr
