// Copyright (c) YugaByte, Inc.

#include "yb/master/master_defaults.h"
#include "yb/master/yql_types_vtable.h"
#include "yb/master/catalog_manager.h"

namespace yb {
namespace master {

YQLTypesVTable::YQLTypesVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemSchemaTypesTableName, master, CreateSchema()) {
}

Status YQLTypesVTable::RetrieveData(const YQLReadRequestPB& request,
                                    std::unique_ptr<YQLRowBlock>* vtable) const {

  vtable->reset(new YQLRowBlock(schema_));
  std::vector<scoped_refptr<UDTypeInfo> > types;
  master_->catalog_manager()->GetAllUDTypes(&types);

  for (scoped_refptr<UDTypeInfo> type : types) {
    // Get namespace for table.
    NamespaceIdentifierPB nsId;
    nsId.set_id(type->namespace_id());
    scoped_refptr<NamespaceInfo> nsInfo;
    RETURN_NOT_OK(master_->catalog_manager()->FindNamespace(nsId, &nsInfo));

    // Create appropriate row for the table;
    YQLRow& row = (*vtable)->Extend();
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, nsInfo->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kTypeName, type->name(), &row));

    // Create appropriate field_names entry.
    YQLValuePB field_names;
    YQLValue::set_list_value(&field_names);
    for (int i = 0; i < type->field_names_size(); i++) {
      YQLValuePB field_name;
      YQLValue::set_string_value(type->field_names(i), &field_name);
      *YQLValue::add_list_elem(&field_names) = field_name;
    }
    RETURN_NOT_OK(SetColumnValue(kFieldNames, field_names, &row));

    // Create appropriate field_types entry.
    YQLValuePB field_types;
    YQLValue::set_list_value(&field_types);
    for (int i = 0; i < type->field_types_size(); i++) {
      YQLValuePB field_type;
      const string& field_type_name = YQLType::FromYQLTypePB(type->field_types(i))->ToString();
      YQLValue::set_string_value(field_type_name, &field_type);
      *YQLValue::add_list_elem(&field_types) = field_type;
    }
    RETURN_NOT_OK(SetColumnValue(kFieldTypes, field_types, &row));
  }

  return Status::OK();
}

Schema YQLTypesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn("keyspace_name", YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn("type_name", YQLType::Create(DataType::STRING)));
  // TODO: field_names should be a frozen list.
  CHECK_OK(builder.AddColumn("field_names", YQLType::CreateTypeList(DataType::STRING)));
  // TODO: field_types should be a frozen list.
  CHECK_OK(builder.AddColumn("field_types", YQLType::CreateTypeList(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
