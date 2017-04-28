// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_views_vtable.h"

namespace yb {
namespace master {

YQLViewsVTable::YQLViewsVTable(const Master* const master)
    : YQLEmptyVTable(master::kSystemSchemaViewsTableName, master, CreateSchema()) {
}

Schema YQLViewsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("view_name", DataType::STRING));
  CHECK_OK(builder.AddColumn("base_table_id", DataType::UUID));
  CHECK_OK(builder.AddColumn("base_table_name", DataType::STRING));
  CHECK_OK(builder.AddColumn("bloom_filter_fp_chance", DataType::DOUBLE));
  // TODO: caching needs to be a frozen map.
  CHECK_OK(builder.AddColumn(
      "caching",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn("cdc", DataType::BOOL));
  CHECK_OK(builder.AddColumn("comment", DataType::STRING));
  // TODO: compaction needs to be a frozen map.
  CHECK_OK(builder.AddColumn(
      "compaction",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  // TODO: compression needs to be a frozen map.
  CHECK_OK(builder.AddColumn(
      "compression",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn("crc_check_chance", DataType::DOUBLE));
  CHECK_OK(builder.AddColumn("dclocal_read_repair_chance", DataType::DOUBLE));
  CHECK_OK(builder.AddColumn("default_time_to_live", DataType::INT32));
  CHECK_OK(builder.AddColumn(
      "extensions",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::BINARY) })));
  CHECK_OK(builder.AddColumn("gc_grace_seconds", DataType::INT32));
  CHECK_OK(builder.AddColumn("id", DataType::UUID));
  CHECK_OK(builder.AddColumn("include_all_columns", DataType::BOOL));
  CHECK_OK(builder.AddColumn("max_index_interval", DataType::INT32));
  CHECK_OK(builder.AddColumn("memtable_flush_period_in_ms", DataType::INT32));
  CHECK_OK(builder.AddColumn("min_index_interval", DataType::INT32));
  CHECK_OK(builder.AddColumn("read_repair_chance", DataType::DOUBLE));
  CHECK_OK(builder.AddColumn("speculative_retry", DataType::STRING));
  CHECK_OK(builder.AddColumn("where_clause", DataType::STRING));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
