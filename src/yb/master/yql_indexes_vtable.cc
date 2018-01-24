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

#include "yb/common/ql_value.h"
#include "yb/master/master_defaults.h"
#include "yb/master/yql_indexes_vtable.h"
#include "yb/master/catalog_manager.h"

namespace yb {
namespace master {

YQLIndexesVTable::YQLIndexesVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemSchemaIndexesTableName, master, CreateSchema()) {
}

namespace {

const string& ColumnName(const Schema& schema, const ColumnId id) {
  auto column = schema.column_by_id(id);
  DCHECK(column.ok());
  return column->name();
}

} // namespace

Status YQLIndexesVTable::RetrieveData(const QLReadRequestPB& request,
                                      std::unique_ptr<QLRowBlock>* vtable) const {
  vtable->reset(new QLRowBlock(schema_));
  std::vector<scoped_refptr<TableInfo>> tables;
  CatalogManager* catalog_manager = master_->catalog_manager();
  catalog_manager->GetAllTables(&tables, true);
  for (scoped_refptr<TableInfo> table : tables) {

    const auto indexed_table_id = table->indexed_table_id();
    if (indexed_table_id.empty()) {
      continue;
    }
    scoped_refptr<TableInfo> indexed_table = catalog_manager->GetTableInfo(indexed_table_id);
    Schema indexed_schema;
    RETURN_NOT_OK(indexed_table->GetSchema(&indexed_schema));

    // Get namespace for table.
    NamespaceIdentifierPB nsId;
    nsId.set_id(table->namespace_id());
    scoped_refptr<NamespaceInfo> nsInfo;
    RETURN_NOT_OK(master_->catalog_manager()->FindNamespace(nsId, &nsInfo));

    // Create appropriate row for the table;
    QLRow& row = (*vtable)->Extend();
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, nsInfo->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kTableName, indexed_table->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kIndexName, table->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kKind, "COMPOSITES", &row));

    string target;
    IndexInfo index_info = indexed_table->GetIndexInfo(table->id());
    for (size_t i = 0; i < index_info.hash_column_count(); i++) {
      target += ColumnName(indexed_schema, index_info.columns()[i].indexed_column_id);
      if (i != index_info.hash_column_count() - 1) {
        target += ", ";
      }
    }
    if (index_info.hash_column_count() > 1) {
      target = '(' + target + ')';
    }
    for (size_t i = index_info.hash_column_count();
         i < index_info.hash_column_count() + index_info.range_column_count(); i++) {
      target += ", ";
      target += ColumnName(indexed_schema, index_info.columns()[i].indexed_column_id);
    }

    string covering;
    for (size_t i = index_info.hash_column_count() + index_info.range_column_count();
         i < index_info.columns().size(); i++) {
      covering += ColumnName(indexed_schema, index_info.columns()[i].indexed_column_id);
      if (i != index_info.columns().size() - 1) {
        covering += ", ";
      }
    }

    QLValue options;
    options.set_map_value();
    options.add_map_key()->set_string_value("target");
    options.add_map_value()->set_string_value(target);
    options.add_map_key()->set_string_value("covering");
    options.add_map_value()->set_string_value(covering);
    RETURN_NOT_OK(SetColumnValue(kOptions, options.value(), &row));

    // Create appropriate table uuids.
    Uuid uuid;
    // Note: table id is in host byte order.
    RETURN_NOT_OK(uuid.FromHexString(indexed_table_id));
    RETURN_NOT_OK(SetColumnValue(kTableId, uuid, &row));
    RETURN_NOT_OK(uuid.FromHexString(table->id()));
    RETURN_NOT_OK(SetColumnValue(kIndexId, uuid, &row));
  }

  return Status::OK();
}

Schema YQLIndexesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kIndexName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kKind, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kOptions,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kTableId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kIndexId, QLType::Create(DataType::UUID)));

  return builder.Build();
}

}  // namespace master
}  // namespace yb
