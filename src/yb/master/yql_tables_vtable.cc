// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/yql_tables_vtable.h"

namespace yb {
namespace master {

YQLTablesVTable::YQLTablesVTable(const Schema& schema, Master* master)
    : YQLVirtualTable(schema),
      master_(master) {
}

Status YQLTablesVTable::RetrieveData(std::unique_ptr<YQLRowBlock>* vtable) const {
  vtable->reset(new YQLRowBlock(schema_));
  std::vector<scoped_refptr<TableInfo> > tables;
  master_->catalog_manager()->GetAllTables(&tables, true);
  for (scoped_refptr<TableInfo> table : tables) {
    // Get namespace for table.
    NamespaceIdentifierPB nsId;
    nsId.set_id(table->namespace_id());
    scoped_refptr<NamespaceInfo> nsInfo;
    RETURN_NOT_OK(master_->catalog_manager()->FindNamespace(nsId, &nsInfo));

    // Create appropriate row for the table;
    YQLRow& row = (*vtable)->Extend();
    YQLValuePB keyspace_name;
    YQLValuePB table_name;
    YQLValue::set_string_value(nsInfo->name(), &keyspace_name);
    YQLValue::set_string_value(table->name(), &table_name);
    *row.mutable_column(0) = keyspace_name; // keyspace_name
    *row.mutable_column(1) = table_name; // table_name

    // Create appropriate flags entry.
    YQLValuePB flags_set;
    YQLValue::set_set_value(&flags_set);
    YQLValuePB flags_elem;
    YQLValue::set_string_value("compound", &flags_elem);
    *YQLValue::add_set_elem(&flags_set) = flags_elem;
    *row.mutable_column(11) = flags_set; // flags
  }

  return Status::OK();
}

}  // namespace master
}  // namespace yb
