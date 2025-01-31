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

#include "yb/master/yql_columns_vtable.h"

#include <stdint.h>

#include "yb/util/logging.h"

#include "yb/qlexpr/ql_name.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"

#include "yb/util/status_log.h"
#include "yb/util/uuid.h"

using std::string;

namespace yb::master {

namespace {

std::string SortingTypeToString(SortingType sorting_type) {
  switch (sorting_type) {
    case SortingType::kNotSpecified:
      return "none";
    case SortingType::kAscending:
      return "asc";
    case SortingType::kDescending:
      return "desc";
    case SortingType::kAscendingNullsLast:
      return "asc nulls last";
    case SortingType::kDescendingNullsLast:
      return "desc nulls last";
  }
  FATAL_INVALID_ENUM_VALUE(SortingType, sorting_type);
}

} // namespace

YQLColumnsVTable::YQLColumnsVTable(const TableName& table_name,
                                   const NamespaceName& namespace_name,
                                   Master* const master)
    : YQLVirtualTable(table_name, namespace_name, master, CreateSchema()) {
}

Status YQLColumnsVTable::PopulateColumnInformation(const Schema& schema,
                                                   const string& keyspace_name,
                                                   const string& table_name,
                                                   const size_t col_idx,
                                                   qlexpr::QLRow* const row) const {
  RETURN_NOT_OK(SetColumnValue(kKeyspaceName, keyspace_name, row));
  RETURN_NOT_OK(SetColumnValue(kTableName, table_name, row));
  if (schema.table_properties().use_mangled_column_name()) {
    RETURN_NOT_OK(SetColumnValue(kColumnName,
                                 qlexpr::YcqlName::DemangleName(schema.column(col_idx).name()),
                                 row));
  } else {
    RETURN_NOT_OK(SetColumnValue(kColumnName, schema.column(col_idx).name(), row));
  }
  RETURN_NOT_OK(SetColumnValue(
      kClusteringOrder, SortingTypeToString(schema.column(col_idx).sorting_type()), row));
  const ColumnSchema& column = schema.column(col_idx);
  RETURN_NOT_OK(SetColumnValue(kType, column.is_counter() ? "counter" : column.type()->ToString(),
                               row));
  return Status::OK();
}

Result<VTableDataPtr> YQLColumnsVTable::RetrieveData(
    const QLReadRequestPB& request) const {
  auto vtable = std::make_shared<qlexpr::QLRowBlock>(schema());
  auto tables = catalog_manager().GetTables(GetTablesMode::kVisibleToClient);
  for (scoped_refptr<TableInfo> table : tables) {

    // Skip non-YQL tables.
    if (!IsYcqlTable(*table)) {
      continue;
    }

    auto schema = VERIFY_RESULT(table->GetSchema());

    // Get namespace for table.
    auto ns_info = VERIFY_RESULT(master_->catalog_manager()->FindNamespaceById(
        table->namespace_id()));

    const string& keyspace_name = ns_info->name();
    const string& table_name = table->name();

    // Fill in the hash keys first.
    auto num_hash_columns = narrow_cast<int32_t>(schema.num_hash_key_columns());
    for (int32_t i = 0; i < num_hash_columns; i++) {
      auto& row = vtable->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i, &row));
      // kind (always partition_key for hash columns)
      RETURN_NOT_OK(SetColumnValue(kKind, "partition_key", &row));
      RETURN_NOT_OK(SetColumnValue(kPosition, i, &row));
    }

    // Now fill in the range columns
    auto num_range_columns = narrow_cast<int32_t>(schema.num_range_key_columns());
    for (int32_t i = num_hash_columns; i < num_hash_columns + num_range_columns; i++) {
      auto& row = vtable->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i, &row));
      // kind (always clustering for range columns)
      RETURN_NOT_OK(SetColumnValue(kKind, "clustering", &row));
      RETURN_NOT_OK(SetColumnValue(kPosition, i - num_hash_columns, &row));
    }

    // Now fill in the rest of the columns.
    auto num_columns = narrow_cast<int32_t>(schema.num_columns());
    for (auto i = num_hash_columns + num_range_columns; i < num_columns; i++) {
      auto& row = vtable->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i, &row));
      // kind (always regular for regular columns)
      const ColumnSchema& column = schema.column(i);
      string kind = column.is_static() ? "static" : "regular";
      RETURN_NOT_OK(SetColumnValue(kKind, kind, &row));
      RETURN_NOT_OK(SetColumnValue(kPosition, -1, &row));
    }
  }

  return vtable;
}

Schema YQLColumnsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kTableName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kColumnName, DataType::STRING));
  CHECK_OK(builder.AddColumn(kClusteringOrder, DataType::STRING));
  CHECK_OK(builder.AddColumn(kColumnNameBytes, DataType::BINARY));
  CHECK_OK(builder.AddColumn(kKind, DataType::STRING));
  CHECK_OK(builder.AddColumn(kPosition, DataType::INT32));
  CHECK_OK(builder.AddColumn(kType, DataType::STRING));
  return builder.Build();
}

}  // namespace yb::master
