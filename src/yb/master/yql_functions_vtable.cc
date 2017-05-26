// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_functions_vtable.h"

namespace yb {
namespace master {

YQLFunctionsVTable::YQLFunctionsVTable(const Master* const master)
    : YQLEmptyVTable(master::kSystemSchemaFunctionsTableName, master, CreateSchema()) {
}

Schema YQLFunctionsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn("keyspace_name", DataType::STRING));
  CHECK_OK(builder.AddKeyColumn("function_name", DataType::STRING));
  // TODO: argument_types should be part of the primary key, but since we don't support the CQL
  // 'frozen' type, we can't have collections in our primary key.
  CHECK_OK(builder.AddColumn("argument_types", YQLType::CreateTypeList(DataType::STRING)));
  // TODO: argument_names should be a frozen list.
  CHECK_OK(builder.AddColumn("argument_names", YQLType::CreateTypeList(DataType::STRING)));
  CHECK_OK(builder.AddColumn("called_on_null_input", YQLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn("language", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("return_type", YQLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
