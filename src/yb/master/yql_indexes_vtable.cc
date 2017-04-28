// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_indexes_vtable.h"

namespace yb {
namespace master {

YQLIndexesVTable::YQLIndexesVTable(const Master* const master)
    : YQLEmptyVTable(master::kSystemSchemaIndexesTableName, master, CreateSchema()) {
}

Schema YQLIndexesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("table_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("index_name", DataType::STRING));
  CHECK_OK(builder.AddColumn("kind", DataType::STRING));
  CHECK_OK(builder.AddColumn(
      "options",
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
