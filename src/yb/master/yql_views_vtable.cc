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
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("view_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("base_table_id", YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn("base_table_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("bloom_filter_fp_chance", YQLType::Create(DataType::DOUBLE)));
  // TODO: caching needs to be a frozen map.
  CHECK_OK(builder.AddColumn("caching",
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn("cdc", YQLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn("comment", YQLType::Create(DataType::STRING)));
  // TODO: compaction needs to be a frozen map.
  CHECK_OK(builder.AddColumn("compaction",
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  // TODO: compression needs to be a frozen map.
  CHECK_OK(builder.AddColumn("compression",
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn("crc_check_chance", YQLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn("dclocal_read_repair_chance", YQLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn("default_time_to_live", YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("extensions",
                             YQLType::CreateTypeMap(DataType::STRING, DataType::BINARY)));
  CHECK_OK(builder.AddColumn("gc_grace_seconds", YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("id", YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn("include_all_columns", YQLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn("max_index_interval", YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("memtable_flush_period_in_ms", YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("min_index_interval", YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn("read_repair_chance", YQLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn("speculative_retry", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("where_clause", YQLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
