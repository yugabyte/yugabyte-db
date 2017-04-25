// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_empty_vtable.h"

namespace yb {
namespace master {

YQLEmptyVTable::YQLEmptyVTable(const std::string& table_name)
    : YQLVirtualTable(CreateSchema(table_name)) {
}

Status YQLEmptyVTable::RetrieveData(std::unique_ptr<YQLRowBlock> *vtable) const {
  // Empty rowblock.
  vtable->reset(new YQLRowBlock(schema_));
  return Status::OK();
}

Schema YQLEmptyVTable::CreateSchema(const std::string &table_name) const {
  Schema schema;
  if (table_name == master::kSystemSchemaAggregatesTableName) {
    CHECK_OK(CreateAggregatesSchema(&schema));
  } else if (table_name == master::kSystemSchemaFunctionsTableName) {
    CHECK_OK(CreateFunctionsSchema(&schema));
  } else if (table_name == master::kSystemSchemaIndexesTableName) {
    CHECK_OK(CreateIndexesSchema(&schema));
  } else if (table_name == master::kSystemSchemaTriggersTableName) {
    CHECK_OK(CreateTriggersSchema(&schema));
  } else if (table_name == master::kSystemSchemaTypesTableName) {
    CHECK_OK(CreateTypesSchema(&schema));
  } else if (table_name == master::kSystemSchemaViewsTableName) {
    CHECK_OK(CreateViewsSchema(&schema));
  } else {
    LOG (FATAL) << "Invalid table: " << table_name;
  }
  return schema;
}

Status YQLEmptyVTable::CreateAggregatesSchema(Schema* schema) const {
  // system_schema.aggregates table.
  SchemaBuilder builder;
  RETURN_NOT_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("aggregate_name", DataType::STRING));
  // TODO: argument_types should be part of the primary key, but since we don't support the CQL
  // 'frozen' type, we can't have collections in our primary key.
  RETURN_NOT_OK(builder.AddColumn("argument_types",
                                  YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  RETURN_NOT_OK(builder.AddColumn("final_func", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn("initcond", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn("state_func", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn("state_type", DataType::STRING));
  *schema = builder.Build();
  return Status::OK();
}

Status YQLEmptyVTable::CreateFunctionsSchema(Schema* schema) const {
  // system_schema.functions table.
  SchemaBuilder builder;
  RETURN_NOT_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("function_name", DataType::STRING));
  // TODO: argument_types should be part of the primary key, but since we don't support the CQL
  // 'frozen' type, we can't have collections in our primary key.
  RETURN_NOT_OK(builder.AddColumn("argument_types",
                                  YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  // TODO: argument_names should be a frozen list.
  RETURN_NOT_OK(builder.AddColumn("argument_names",
                                  YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  RETURN_NOT_OK(builder.AddColumn("called_on_null_input", DataType::BOOL));
  RETURN_NOT_OK(builder.AddColumn("language", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn("return_type", DataType::STRING));
  *schema = builder.Build();
  return Status::OK();
}

Status YQLEmptyVTable::CreateIndexesSchema(Schema* schema) const {
  // system_schema.indexes table.
  SchemaBuilder builder;
  RETURN_NOT_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("table_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("index_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn("kind", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn(
      "options",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  *schema = builder.Build();
  return Status::OK();
}

Status YQLEmptyVTable::CreateTriggersSchema(Schema* schema) const {
  // system_schema.triggers table.
  SchemaBuilder builder;
  RETURN_NOT_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("table_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("trigger_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn(
      "options",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  *schema = builder.Build();
  return Status::OK();
}

Status YQLEmptyVTable::CreateTypesSchema(Schema* schema) const {
  // system_schema.types table.
  SchemaBuilder builder;
  RETURN_NOT_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("type_name", DataType::STRING));
  // TODO: field_names should be a frozen list.
  RETURN_NOT_OK(builder.AddColumn("field_names",
                                  YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  // TODO: field_types should be a frozen list.
  RETURN_NOT_OK(builder.AddColumn("field_types",
                                  YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  *schema = builder.Build();
  return Status::OK();
}

Status YQLEmptyVTable::CreateViewsSchema(Schema* schema) const {
  // system_schema.views table.
  SchemaBuilder builder;
  RETURN_NOT_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddKeyColumn("view_name", DataType::STRING));
  // TODO: base_table_id is missing since we don't support uuid yet.
  RETURN_NOT_OK(builder.AddColumn("base_table_name", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn("bloom_filter_fp_chance", DataType::DOUBLE));
  // TODO: caching needs to be a frozen map.
  RETURN_NOT_OK(builder.AddColumn(
      "caching",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  RETURN_NOT_OK(builder.AddColumn("cdc", DataType::BOOL));
  RETURN_NOT_OK(builder.AddColumn("comment", DataType::STRING));
  // TODO: compaction needs to be a frozen map.
  RETURN_NOT_OK(builder.AddColumn(
      "compaction",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  // TODO: compression needs to be a frozen map.
  RETURN_NOT_OK(builder.AddColumn(
      "compression",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  RETURN_NOT_OK(builder.AddColumn("crc_check_chance", DataType::DOUBLE));
  RETURN_NOT_OK(builder.AddColumn("dclocal_read_repair_chance", DataType::DOUBLE));
  RETURN_NOT_OK(builder.AddColumn("default_time_to_live", DataType::INT32));
  RETURN_NOT_OK(builder.AddColumn(
      "extensions",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::BINARY) })));
  RETURN_NOT_OK(builder.AddColumn("gc_grace_seconds", DataType::INT32));
  // TODO: id is missing since we don't support uuid yet.
  RETURN_NOT_OK(builder.AddColumn("include_all_columns", DataType::BOOL));
  RETURN_NOT_OK(builder.AddColumn("max_index_interval", DataType::INT32));
  RETURN_NOT_OK(builder.AddColumn("memtable_flush_period_in_ms", DataType::INT32));
  RETURN_NOT_OK(builder.AddColumn("min_index_interval", DataType::INT32));
  RETURN_NOT_OK(builder.AddColumn("read_repair_chance", DataType::DOUBLE));
  RETURN_NOT_OK(builder.AddColumn("speculative_retry", DataType::STRING));
  RETURN_NOT_OK(builder.AddColumn("where_clause", DataType::STRING));
  *schema = builder.Build();
  return Status::OK();
}

}  // namespace master
}  // namespace yb
