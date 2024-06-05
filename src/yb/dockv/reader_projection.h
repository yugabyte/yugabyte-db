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

#include "yb/dockv/key_entry_value.h"

namespace yb::dockv {

inline ColumnId GetColumnId(ColumnIdRep column_id) {
  return ColumnId(column_id);
}

inline ColumnId GetColumnId(ColumnId column_id) {
  return column_id;
}

template <class PB>
inline ColumnId GetColumnId(const PB& pb) {
  return ColumnId(pb.column_id());
}

struct ProjectedColumn {
  ColumnId id;
  KeyEntryValue subkey; // id converted to KeyEntryValue
  DataType data_type;

  std::string ToString() const;
};

inline bool operator==(const ProjectedColumn& lhs, const ProjectedColumn& rhs) {
  return YB_STRUCT_EQUALS(id, data_type);
}

struct ReaderProjection {
  static constexpr size_t kNotFoundIndex = std::numeric_limits<size_t>::max();

  size_t num_key_columns = 0;
  // Columns are ordered by id and do not contain duplicates.
  // It is guaranteed by our system that key columns have smaller ids than value columns.
  std::vector<ProjectedColumn> columns;

  ReaderProjection() = default;

  explicit ReaderProjection(const Schema& schema);

  template <class ColumnIds>
  ReaderProjection(const Schema& schema, const ColumnIds& column_refs) {
    Init(schema, column_refs);
  }

  ReaderProjection(const Schema& schema, const std::initializer_list<ColumnIdRep>& column_refs) {
    Init(schema, column_refs);
  }

  auto key_columns() const {
    return boost::make_iterator_range(columns.begin(), columns.begin() + num_key_columns);
  }

  auto value_columns_begin() const {
    return columns.begin() + num_key_columns;
  }

  auto value_columns_end() const {
    return columns.end();
  }

  auto value_columns() const {
    return boost::make_iterator_range(value_columns_begin(), value_columns_end());
  }

  bool has_value_columns() const {
    return columns.size() != num_key_columns;
  }

  size_t num_value_columns() const {
    return columns.size() - num_key_columns;
  }

  const ProjectedColumn& value_column(size_t idx) const {
    return columns[num_key_columns + idx];
  }

  size_t size() const {
    return columns.size();
  }

  template <class... Args>
  void Reset(Args&&... args) {
    num_key_columns = 0;
    columns.clear();
    Init(std::forward<Args>(args)...);
  }

  void Init(const Schema& schema, const std::initializer_list<ColumnIdRep>& column_refs) {
    DoInit(schema, column_refs);
  }

  template <class... ColumnIds>
  void Init(const Schema& schema, ColumnIds&&... column_refs) {
    columns.reserve(SumSizes(std::forward<ColumnIds>(column_refs)...));
    CompleteInit(DoAddColumns(schema, std::forward<ColumnIds>(column_refs)...));
  }

  size_t ColumnIdxById(ColumnId column_id) const;

  std::string ToString() const;

 private:
  template <class ColumnIds>
  void DoInit(const Schema& schema, const ColumnIds& column_refs) {
    DCHECK(columns.empty());
    columns.reserve(column_refs.size());
    CompleteInit(AddColumns(schema, column_refs));
  }

  template <class ColumnIds>
  ColumnId AddColumns(const Schema& schema, const ColumnIds& column_refs) {
    auto first_non_key_column = kInvalidColumnId;
    for (const auto& column_ref : column_refs) {
      AddColumn(schema, GetColumnId(column_ref), &first_non_key_column);
    }
    return first_non_key_column;
  }

  static size_t SumSizes() {
    return 0;
  }

  template <class ColumnIds1, class... ColumnIds>
  static size_t SumSizes(const ColumnIds1& column_refs1, ColumnIds&&... column_refs) {
    return column_refs1.size() + SumSizes(std::forward<ColumnIds>(column_refs)...);
  }

  ColumnId DoAddColumns(const Schema& schema) {
    return kInvalidColumnId;
  }

  template <class ColumnIds1, class... ColumnIds>
  ColumnId DoAddColumns(
      const Schema& schema, const ColumnIds1& column_refs1, ColumnIds&&... column_refs) {
    return std::min(AddColumns(schema, column_refs1),
                    DoAddColumns(schema, std::forward<ColumnIds>(column_refs)...));
  }

  void CompleteInit(ColumnId first_non_key_column);

  void AddColumn(const Schema& schema, ColumnId column_id, ColumnId* first_non_key_column);
};

inline bool operator==(const ReaderProjection& lhs, const ReaderProjection& rhs) {
  return YB_STRUCT_EQUALS(columns);
}

inline std::ostream& operator<<(std::ostream& out, const ReaderProjection& value) {
  return out << value.ToString();
}

}  // namespace yb::dockv
