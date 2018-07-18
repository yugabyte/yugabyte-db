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

#include "yb/common/index.h"
#include "yb/common/common.pb.h"

using std::vector;
using std::unordered_map;
using google::protobuf::RepeatedField;
using google::protobuf::RepeatedPtrField;
using google::protobuf::uint32;

namespace yb {

IndexInfo::IndexColumn::IndexColumn(const IndexInfoPB::IndexColumnPB& pb)
    : column_id(ColumnId(pb.column_id())),
      indexed_column_id(ColumnId(pb.indexed_column_id())) {
}

void IndexInfo::IndexColumn::ToPB(IndexInfoPB::IndexColumnPB* pb) const {
  pb->set_column_id(column_id);
  pb->set_indexed_column_id(indexed_column_id);
}

namespace {

vector<IndexInfo::IndexColumn> IndexColumnFromPB(
    const RepeatedPtrField<IndexInfoPB::IndexColumnPB>& columns) {
  vector<IndexInfo::IndexColumn> cols;
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
      indexed_range_column_ids_(ColumnIdsFromPB(pb.indexed_range_column_ids())) {
  for (const IndexInfo::IndexColumn &index_col : columns_) {
    covered_column_ids_.insert(index_col.indexed_column_id);
  }
}

void IndexInfo::ToPB(IndexInfoPB* pb) const {
  pb->set_table_id(table_id_);
  pb->set_indexed_table_id(indexed_table_id_);
  pb->set_version(schema_version_);
  pb->set_is_local(is_local_);
  pb->set_is_unique(is_unique_);
  for (const auto& column : columns_) {
    column.ToPB(pb->add_columns());
  }
  pb->set_hash_column_count(hash_column_count_);
  pb->set_range_column_count(range_column_count_);
  for (const auto id : indexed_hash_column_ids_) {
    pb->add_indexed_hash_column_ids(id);
  }
  for (const auto id : indexed_range_column_ids_) {
    pb->add_indexed_range_column_ids(id);
  }
}

vector<ColumnId> IndexInfo::index_key_column_ids() const {
  unordered_map<ColumnId, ColumnId> map;
  for (const auto column : columns_) {
    map[column.indexed_column_id] = column.column_id;
  }
  vector<ColumnId> ids;
  ids.reserve(indexed_hash_column_ids_.size() + indexed_range_column_ids_.size());
  for (const auto id : indexed_hash_column_ids_) {
    ids.push_back(map[id]);
  }
  for (const auto id : indexed_range_column_ids_) {
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

IndexMap::IndexMap(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes) {
  FromPB(indexes);
}

void IndexMap::FromPB(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes) {
  clear();
  for (const auto& index : indexes) {
    emplace(index.table_id(), IndexInfo(index));
  }
}

void IndexMap::ToPB(google::protobuf::RepeatedPtrField<IndexInfoPB>* indexes) const {
  indexes->Clear();
  for (const auto itr : *this) {
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

}  // namespace yb
