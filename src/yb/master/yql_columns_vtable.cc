// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/yql_columns_vtable.h"

namespace yb {
namespace master {

YQLColumnsVTable::YQLColumnsVTable(const Master* const master)
    : YQLVirtualTable(CreateSchema(master::kSystemSchemaColumnsTableName)),
      master_(master) {
}

namespace {
  YQLValuePB GetStringValue(const std::string& strval) {
    YQLValuePB value_pb;
    YQLValue::set_string_value(strval, &value_pb);
    return value_pb;
  }

  YQLValuePB GetIntValue(const int32_t intval) {
    YQLValuePB value_pb;
    YQLValue::set_int32_value(intval, &value_pb);
    return value_pb;
  }
} // anonymous namespace

Status YQLColumnsVTable::PopulateColumnInformation(const Schema& schema,
                                                   const YQLValuePB& keyspace_name,
                                                   const YQLValuePB& table_name,
                                                   const size_t col_idx,
                                                   YQLRow* const row) const {
  RETURN_NOT_OK(SetColumnValue(kKeyspaceName, keyspace_name, row));
  RETURN_NOT_OK(SetColumnValue(kTableName, table_name, row));
  RETURN_NOT_OK(SetColumnValue(kColumnName, GetStringValue(schema.column(col_idx).name()), row));
  RETURN_NOT_OK(SetColumnValue(kClusteringOrder,
                               GetStringValue(schema.column(col_idx).sorting_type_string()), row));
  RETURN_NOT_OK(SetColumnValue(kType,
                               GetStringValue(schema.column(col_idx).type().ToString()), row));
  return Status::OK();
}

Status YQLColumnsVTable::RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const {
  vtable->reset(new YQLRowBlock(schema_));
  std::vector<scoped_refptr<TableInfo> > tables;
  master_->catalog_manager()->GetAllTables(&tables, true);
  for (scoped_refptr<TableInfo> table : tables) {
    Schema schema;
    RETURN_NOT_OK(table->GetSchema(&schema));

    // Get namespace for table.
    NamespaceIdentifierPB nsId;
    nsId.set_id(table->namespace_id());
    scoped_refptr<NamespaceInfo> nsInfo;
    RETURN_NOT_OK(master_->catalog_manager()->FindNamespace(nsId, &nsInfo));

    YQLValuePB keyspace_name;
    YQLValuePB table_name;
    YQLValue::set_string_value(nsInfo->name(), &keyspace_name);
    YQLValue::set_string_value(table->name(), &table_name);

    // Fill in the hash keys first.
    size_t num_hash_columns = schema.num_hash_key_columns();
    for (size_t i = 0; i < num_hash_columns; i++) {
      YQLRow& row = (*vtable)->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i,
                                              &row));
      // kind (always partition_key for hash columns)
      RETURN_NOT_OK(SetColumnValue(kKind, GetStringValue("partition_key"), &row));
      RETURN_NOT_OK(SetColumnValue(kPosition, GetIntValue(i), &row));
    }

    // Now fill in the range columns
    size_t num_range_columns = schema.num_range_key_columns();
    for (size_t i = num_hash_columns; i < num_hash_columns + num_range_columns; i++) {
      YQLRow& row = (*vtable)->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i,
                                              &row));
      // kind (always clustering for range columns)
      RETURN_NOT_OK(SetColumnValue(kKind, GetStringValue("clustering"), &row));
      RETURN_NOT_OK(SetColumnValue(kPosition, GetIntValue(i - num_hash_columns), &row));
    }

    // Now fill in the rest of the columns.
    for (size_t i = num_hash_columns + num_range_columns; i < schema.num_columns(); i++) {
      YQLRow &row = (*vtable)->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i,
                                              &row));
      // kind (always regular for regular columns)
      RETURN_NOT_OK(SetColumnValue(kKind, GetStringValue("regular"), &row));
      RETURN_NOT_OK(SetColumnValue(kPosition, GetIntValue(-1), &row));
    }
  }

  return Status::OK();
}

Schema YQLColumnsVTable::CreateSchema(const std::string& table_name) const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kKeyspaceName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kTableName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kColumnName, DataType::STRING));
  CHECK_OK(builder.AddColumn(kClusteringOrder, DataType::STRING));
  CHECK_OK(builder.AddColumn(kColumnNameBytes, DataType::BINARY));
  CHECK_OK(builder.AddColumn(kKind, DataType::STRING));
  CHECK_OK(builder.AddColumn(kPosition, DataType::INT32));
  CHECK_OK(builder.AddColumn(kType, DataType::STRING));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
