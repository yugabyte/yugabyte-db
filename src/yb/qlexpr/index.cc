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

#include "yb/qlexpr/index.h"

#include "yb/common/common.messages.h"
#include "yb/common/common.pb.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/qlexpr/index_column.h"

#include "yb/util/compare_util.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/result.h"

using std::vector;
using std::unordered_map;
using std::string;
using google::protobuf::RepeatedField;
using google::protobuf::RepeatedPtrField;
using google::protobuf::uint32;

namespace yb::qlexpr {

// When DocDB receive messages from older clients, those messages won't have "column_name" and
// "colexpr" attributes.
IndexColumn::IndexColumn(const IndexInfoPB::IndexColumnPB& pb)
    : column_id(ColumnId(pb.column_id())),
      column_name(pb.column_name()), // Default to empty.
      indexed_column_id(ColumnId(pb.indexed_column_id())),
      colexpr(pb.colexpr()) /* Default to empty message */ {
}

void IndexColumn::ToPB(IndexInfoPB::IndexColumnPB* pb) const {
  pb->set_column_id(column_id);
  pb->set_column_name(column_name);
  pb->set_indexed_column_id(indexed_column_id);
  pb->mutable_colexpr()->CopyFrom(colexpr);
}

std::string IndexColumn::ToString() const {
  return YB_STRUCT_TO_STRING(column_id, column_name, indexed_column_id, colexpr);
}

namespace {

vector<IndexColumn> IndexColumnFromPB(
    const RepeatedPtrField<IndexInfoPB::IndexColumnPB>& columns) {
  vector<IndexColumn> cols;
  cols.reserve(columns.size());
  for (const auto& column : columns) {
    cols.emplace_back(column);
  }
  return cols;
}

vector<ColumnId> ColumnIdsFromPB(const RepeatedField<uint32>& ids) {
  vector<ColumnId> column_ids;
  column_ids.reserve(ids.size());
  for (const auto& id : ids) {
    column_ids.emplace_back(id);
  }
  return column_ids;
}

} // namespace

IndexInfo::IndexInfo(const IndexInfoPB& pb)
    : table_id_(pb.table_id()),
      indexed_table_id_(pb.indexed_table_id()),
      schema_version_(pb.version()),
      is_local_(pb.is_local()),
      is_unique_(pb.is_unique()),
      columns_(IndexColumnFromPB(pb.columns())),
      hash_column_count_(pb.hash_column_count()),
      range_column_count_(pb.range_column_count()),
      indexed_hash_column_ids_(ColumnIdsFromPB(pb.indexed_hash_column_ids())),
      indexed_range_column_ids_(ColumnIdsFromPB(pb.indexed_range_column_ids())),
      index_permissions_(pb.index_permissions()),
      backfill_error_message_(pb.backfill_error_message()),
      num_rows_processed_by_backfill_job_(pb.num_rows_processed_by_backfill_job()),
      use_mangled_column_name_(pb.use_mangled_column_name()),
      where_predicate_spec_(pb.has_where_predicate_spec() ?
        std::make_shared<IndexInfoPB::WherePredicateSpecPB>(pb.where_predicate_spec()) : nullptr) {
  for (const auto& index_col : columns_) {
    // Mark column as covered if the index column is the column itself.
    // Do not mark a column as covered when indexing by an expression of that column.
    // - When an expression such as "jsonb->>'field'" is used, then the "jsonb" column should not
    //   be included in the covered list.
    // - Currently we only support "jsonb->>" expression, but this is true for all expressions.
    if (index_col.colexpr.expr_case() == QLExpressionPB::ExprCase::kColumnId ||
        index_col.colexpr.expr_case() == QLExpressionPB::ExprCase::EXPR_NOT_SET) {
      covered_column_ids_.insert(index_col.indexed_column_id);
    } else {
      has_index_by_expr_ = true;
    }
  }

  if (pb.has_vector_idx_options()) {
    vector_idx_options_ = pb.vector_idx_options();
  }
}

IndexInfo::IndexInfo() = default;

IndexInfo::IndexInfo(const IndexInfo& rhs) = default;
IndexInfo::IndexInfo(IndexInfo&& rhs) = default;

IndexInfo::~IndexInfo() = default;

void IndexInfo::ToPB(IndexInfoPB* pb) const {
  pb->set_table_id(table_id_);
  pb->set_indexed_table_id(indexed_table_id_);
  pb->set_version(schema_version_);
  pb->set_is_local(is_local_);
  pb->set_is_unique(is_unique_);
  for (const auto& column : columns_) {
    column.ToPB(pb->add_columns());
  }
  pb->set_hash_column_count(narrow_cast<uint32_t>(hash_column_count_));
  pb->set_range_column_count(narrow_cast<uint32_t>(range_column_count_));
  for (const auto& id : indexed_hash_column_ids_) {
    pb->add_indexed_hash_column_ids(id);
  }
  for (const auto& id : indexed_range_column_ids_) {
    pb->add_indexed_range_column_ids(id);
  }
  pb->set_index_permissions(index_permissions_);
  pb->set_backfill_error_message(backfill_error_message_);
  pb->set_num_rows_processed_by_backfill_job(num_rows_processed_by_backfill_job_);
  pb->set_use_mangled_column_name(use_mangled_column_name_);
  if (where_predicate_spec_) {
    pb->mutable_where_predicate_spec()->CopyFrom(*where_predicate_spec_);
  }

  if (is_vector_index()) {
    *pb->mutable_vector_idx_options() = vector_idx_options();
  }
}

vector<ColumnId> IndexInfo::index_key_column_ids() const {
  std::unordered_map<ColumnId, ColumnId, boost::hash<ColumnId>> map;
  for (const auto& column : columns_) {
    map[column.indexed_column_id] = column.column_id;
  }
  vector<ColumnId> ids;
  ids.reserve(indexed_hash_column_ids_.size() + indexed_range_column_ids_.size());
  for (const auto& id : indexed_hash_column_ids_) {
    ids.push_back(map[id]);
  }
  for (const auto& id : indexed_range_column_ids_) {
    ids.push_back(map[id]);
  }
  return ids;
}

bool IndexInfo::PrimaryKeyColumnsOnly(const Schema& indexed_schema) const {
  for (size_t i = 0; i < hash_column_count_ + range_column_count_; i++) {
    if (!indexed_schema.is_key_column(columns_[i].indexed_column_id)) {
      return false;
    }
  }
  return true;
}

bool IndexInfo::IsColumnCovered(const ColumnId column_id) const {
  return covered_column_ids_.find(column_id) != covered_column_ids_.end();
}

bool IndexInfo::IsColumnCovered(const std::string& column_name) const {
  for (const auto &col : columns_) {
    if (column_name == col.column_name) {
      return true;
    }
  }
  return false;
}

int32_t IndexInfo::IsExprCovered(const string& expr_name) const {
  // CHECKING if an expression is covered.
  // - If IndexColumn name is a substring of "expr_name", the given expression is covered. That is,
  //   it can be computed using the value of this column.
  //
  // - For this function to work properly, the column and expression name MUST be serialized in a
  //   way that guarantees their uniqueness. Function PTExpr::MangledName() resolves this issue.
  //
  // - Example:
  //     CREATE TABLE tab (pk int primary key, a int, j jsonb);
  //     CREATE INDEX a_index ON tab (a);
  //     SELECT pk FROM tab WHERE j->'b'->>'a' = '99';
  //   In this example, clearly "a_index" doesn't cover the seleted json expression, but the name
  //   "a" is a substring of "j->b->>a", and this function would return TRUE, which is wrong. To
  //   avoid this issue, <column names> and JSONB <attribute names> must be escaped uniquely and
  //   differently. To cover the above SELECT, the following index must be defined.
  //     CREATE INDEX jindex on tab(j->'b'->>'a');
  int32_t idx = 0;
  for (const auto &col : columns_) {
    if (!col.column_name.empty() && expr_name.find(col.column_name) != expr_name.npos) {
      return idx;
    }
    idx++;
  }

  return -1;
}

// Check for dependency is used for DDL operations, so it does not need to be fast. As a result,
// the dependency list does not need to be cached in a member id list for fast access.
bool IndexInfo::CheckColumnDependency(ColumnId column_id) const {
  for (const auto& index_col : columns_) {
    // The protobuf data contains IDs of all columns that this index is referencing.
    // Examples:
    // 1. Index by column
    // - INDEX ON tab (a_column)
    // - The ID of "a_column" is included in protobuf data.
    //
    // 2. Index by expression of column:
    // - INDEX ON tab (j_column->>'field')
    // - The ID of "j_column" is included in protobuf data.
    if (index_col.indexed_column_id == column_id) {
      return true;
    }
  }

  if (where_predicate_spec_) {
    for (auto indexed_col_id : where_predicate_spec_->column_ids()) {
      if (ColumnId(indexed_col_id) == column_id) return true;
    }
  }

  return false;
}

boost::optional<size_t> IndexInfo::FindKeyIndex(const string& key_expr_name) const {
  for (size_t idx = 0; idx < key_column_count(); idx++) {
    const auto& col = columns_[idx];
    if (!col.column_name.empty() && key_expr_name.find(col.column_name) != key_expr_name.npos) {
      // Return the found key column that is referenced by the expression.
      return idx;
    }
  }

  return boost::none;
}

std::string IndexInfo::ToString() const {
  IndexInfoPB pb;
  ToPB(&pb);
  return pb.ShortDebugString();
}

const IndexColumn& IndexInfo::column(const size_t idx) const {
  return columns_[idx];
}

bool IndexInfo::TEST_Equals(const IndexInfo& lhs, const IndexInfo& rhs) {
  return lhs.ToString() == rhs.ToString();
}

size_t IndexInfo::DynamicMemoryUsage() const {
  size_t size = sizeof(this);
  size += columns_.capacity() * sizeof(IndexColumn);
  size += (indexed_hash_column_ids_.capacity() + indexed_range_column_ids_.capacity()) *
          sizeof(ColumnId);
  size += DynamicMemoryUsageOf(backfill_error_message_);
  size += covered_column_ids_.size() * sizeof(ColumnId);
  return size;
}

bool IndexInfo::is_vector_index() const {
  return vector_idx_options_.has_value();
}

IndexMap::IndexMap(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes) {
  FromPB(indexes);
}

IndexMap::IndexMap(const ArenaList<LWIndexInfoPB>& indexes) {
  clear();
  for (const auto& index : indexes) {
    emplace(index.table_id(), IndexInfo(index.ToGoogleProtobuf()));
  }
}

void IndexMap::FromPB(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes) {
  clear();
  for (const auto& index : indexes) {
    emplace(index.table_id(), IndexInfo(index));
  }
}

void IndexMap::ToPB(google::protobuf::RepeatedPtrField<IndexInfoPB>* indexes) const {
  indexes->Clear();
  for (const auto& itr : *this) {
    itr.second.ToPB(indexes->Add());
  }
}

Result<const IndexInfo*> IndexMap::FindIndex(const TableId& index_id) const {
  const auto itr = find(index_id);
  if (itr == end()) {
    return STATUS(NotFound, Format("Index id $0 not found", index_id));
  }
  return &itr->second;
}

bool IndexMap::TEST_Equals(const IndexMap& lhs, const IndexMap& rhs) {
  // We can't use std::unordered_map's == because IndexInfo does not define ==.
  using MapType = std::unordered_map<TableId, IndexInfo>;
  return util::MapsEqual(static_cast<const MapType&>(lhs),
                         static_cast<const MapType&>(rhs),
                         &IndexInfo::TEST_Equals);
}

size_t IndexMap::DynamicMemoryUsage() const {
  size_t size = 0;
  for (const auto& index_pair : *this) {
    size += DynamicMemoryUsageOf(index_pair.first) + index_pair.second.DynamicMemoryUsage();
  }
  return size;
}

}  // namespace yb::qlexpr
