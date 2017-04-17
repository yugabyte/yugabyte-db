// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_defaults.h"
#include "yb/master/yql_keyspaces_vtable.h"

namespace yb {
namespace master {

YQLKeyspacesVTable::YQLKeyspacesVTable(const Schema& schema, Master* master)
    : YQLVirtualTable(schema),
      master_(master) {
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
    *row.mutable_column(0) = keyspace_name;
    *row.mutable_column(1) = durable_writes;
  }

  return Status::OK();
}

}  // namespace master
}  // namespace yb
