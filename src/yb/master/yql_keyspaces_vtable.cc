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

Status YQLKeyspacesVTable::RetrieveData(const YQLReadRequestPB& request,
                                        std::unique_ptr<YQLRowBlock>* vtable) const {
  using namespace util;
  vtable->reset(new YQLRowBlock(schema_));
  std::vector<scoped_refptr<NamespaceInfo> > namespaces;
  master_->catalog_manager()->GetAllNamespaces(&namespaces);
  for (scoped_refptr<NamespaceInfo> ns : namespaces) {
    YQLRow& row = (*vtable)->Extend();
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, GetStringValue(ns->name()), &row));
    RETURN_NOT_OK(SetColumnValue(kDurableWrites, GetBoolValue(true), &row));
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
