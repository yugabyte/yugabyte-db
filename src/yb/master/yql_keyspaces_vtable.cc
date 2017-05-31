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
  vtable->reset(new YQLRowBlock(schema_));
  std::vector<scoped_refptr<NamespaceInfo> > namespaces;
  master_->catalog_manager()->GetAllNamespaces(&namespaces);
  for (scoped_refptr<NamespaceInfo> ns : namespaces) {
    YQLRow& row = (*vtable)->Extend();
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, ns->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kDurableWrites, true, &row));
    RETURN_NOT_OK(SetColumnValue(kReplication, util::GetReplicationValue(), &row));
  }

  return Status::OK();
}

Schema YQLKeyspacesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kKeyspaceName, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kDurableWrites, YQLType::Create(DataType::BOOL)));
  // TODO: replication needs to be a frozen map.
  CHECK_OK(builder.AddColumn(kReplication,
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
