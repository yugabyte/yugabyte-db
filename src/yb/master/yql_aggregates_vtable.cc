// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_aggregates_vtable.h"

namespace yb {
namespace master {

YQLAggregatesVTable::YQLAggregatesVTable(const Master* const master)
    : YQLEmptyVTable(master::kSystemSchemaAggregatesTableName, master, CreateSchema()) {
}

Schema YQLAggregatesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("aggregate_name", DataType::STRING));
  // TODO: argument_types should be part of the primary key, but since we don't support the CQL
  // 'frozen' type, we can't have collections in our primary key.
  CHECK_OK(builder.AddColumn("argument_types",
                                  YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn("final_func", DataType::STRING));
  CHECK_OK(builder.AddColumn("initcond", DataType::STRING));
  CHECK_OK(builder.AddColumn("state_func", DataType::STRING));
  CHECK_OK(builder.AddColumn("state_type", DataType::STRING));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
