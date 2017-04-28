// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_types_vtable.h"

namespace yb {
namespace master {

YQLTypesVTable::YQLTypesVTable(const Master* const master)
    : YQLEmptyVTable(master::kSystemSchemaTypesTableName, master, CreateSchema()) {
}

Schema YQLTypesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("type_name", DataType::STRING));
  // TODO: field_names should be a frozen list.
  CHECK_OK(builder.AddColumn("field_names",
                             YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  // TODO: field_types should be a frozen list.
  CHECK_OK(builder.AddColumn("field_types",
                             YQLType(DataType::LIST, { YQLType(DataType::STRING) })));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
