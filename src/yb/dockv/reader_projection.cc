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

#include "yb/dockv/reader_projection.h"

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

namespace yb::dockv {

std::string ProjectedColumn::ToString() const {
  return YB_STRUCT_TO_STRING(id, data_type);
}

ReaderProjection::ReaderProjection(const Schema& schema) {
  Init(schema, schema.column_ids());
}

void ReaderProjection::AddColumn(
    const Schema& schema, ColumnId column_id, ColumnId* first_non_key_column) {
  auto idx = schema.find_column_by_id(column_id);
  if (idx == Schema::kColumnNotFound) {
    return;
  }
  if (!schema.is_key_column(idx) && column_id < *first_non_key_column) {
    *first_non_key_column = column_id;
  }
  columns.push_back({
    .id = column_id,
    .subkey = dockv::KeyEntryValue::MakeColumnId(column_id),
    .data_type = schema.column(idx).type()->main()
  });
}

std::string ReaderProjection::ToString() const {
  return YB_STRUCT_TO_STRING(num_key_columns, columns);
}

void ReaderProjection::CompleteInit(ColumnId first_non_key_column) {
  std::sort(
      columns.begin(), columns.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
  // Since we add columns from various sources, they could contain duplicates. Cleanup them.
  auto stop = std::unique(columns.begin(), columns.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.id == rhs.id;
  });
  columns.erase(stop, columns.end());
  num_key_columns = std::count_if(
      columns.begin(), columns.end(), [first_non_key_column](const auto& col) {
    return col.id < first_non_key_column;
  });
}

size_t ReaderProjection::ColumnIdxById(ColumnId column_id) const {
  auto begin = columns.begin();
  auto end = columns.end();
  auto it = std::lower_bound(begin, end, column_id, [](const ProjectedColumn& lhs, ColumnId rhs) {
    return lhs.id < rhs;
  });
  return it != end && it->id == column_id ? it - begin : kNotFoundIndex;
}

}  // namespace yb::dockv
