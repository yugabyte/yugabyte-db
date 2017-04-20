// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/yql_columns_vtable.h"

namespace yb {
namespace master {

YQLColumnsVTable::YQLColumnsVTable(const Schema& schema, Master* master)
    : YQLVirtualTable(schema),
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
  *(row->mutable_column(0)) = keyspace_name; // keyspace_name
  *(row->mutable_column(1)) = table_name; // table_name
  *(row->mutable_column(2)) = GetStringValue(schema.column(col_idx).name()); // column_name
  // clustering_order.
  *(row->mutable_column(3)) = GetStringValue(schema.column(col_idx).sorting_type_string());
  *(row->mutable_column(6)) = GetStringValue(schema.column(col_idx).type().ToString()); // type
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
      *row.mutable_column(4) = GetStringValue("partition_key");
      *row.mutable_column(5) = GetIntValue(i); // position
    }

    // Now fill in the range columns
    size_t num_range_columns = schema.num_range_key_columns();
    for (size_t i = num_hash_columns; i < num_hash_columns + num_range_columns; i++) {
      YQLRow& row = (*vtable)->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i,
                                              &row));
      // kind (always clustering for range columns)
      *row.mutable_column(4) = GetStringValue("clustering");
      *row.mutable_column(5) = GetIntValue(i - num_hash_columns); // position
    }

    // Now fill in the rest of the columns.
    for (size_t i = num_hash_columns + num_range_columns; i < schema.num_columns(); i++) {
      YQLRow &row = (*vtable)->Extend();
      RETURN_NOT_OK(PopulateColumnInformation(schema, keyspace_name, table_name, i,
                                              &row));
      // kind (always regular for regular columns)
      *row.mutable_column(4) = GetStringValue("regular");
      *row.mutable_column(5) = GetIntValue(-1); // position (always -1 for regular columns)
    }
  }

  return Status::OK();
}

}  // namespace master
}  // namespace yb
