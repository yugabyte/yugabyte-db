// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_triggers_vtable.h"

namespace yb {
namespace master {

YQLTriggersVTable::YQLTriggersVTable(const Master* const master)
    : YQLEmptyVTable(master::kSystemSchemaTriggersTableName, master, CreateSchema()) {
}

Schema YQLTriggersVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn("keyspace_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("table_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("trigger_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn("options",
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
