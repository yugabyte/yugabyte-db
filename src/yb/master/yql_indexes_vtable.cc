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
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("table_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("index_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("kind", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("options",
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
