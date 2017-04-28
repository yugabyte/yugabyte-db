// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_defaults.h"
#include "yb/master/yql_keyspaces_vtable.h"

namespace yb {
namespace master {

YQLKeyspacesVTable::YQLKeyspacesVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemSchemaKeyspacesTableName, master, CreateSchema()) {
}

Status YQLKeyspacesVTable::RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const {
  vtable->reset(new YQLRowBlock(schema_));
  std::vector<scoped_refptr<NamespaceInfo> > namespaces;
  master_->catalog_manager()->GetAllNamespaces(&namespaces);
  for (scoped_refptr<NamespaceInfo> ns : namespaces) {
    YQLRow& row = (*vtable)->Extend();
    YQLValuePB keyspace_name;
    YQLValuePB durable_writes;
    YQLValue::set_string_value(ns->name(), &keyspace_name);
    YQLValue::set_bool_value(true, &durable_writes);
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, keyspace_name, &row));
    RETURN_NOT_OK(SetColumnValue(kDurableWrites, durable_writes, &row));
  }

  return Status::OK();
}

Schema YQLKeyspacesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kKeyspaceName, DataType::STRING));
  CHECK_OK(builder.AddColumn(kDurableWrites, DataType::BOOL));
  // TODO: replication needs to be a frozen map.
  CHECK_OK(builder.AddColumn(
      kReplication,
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
