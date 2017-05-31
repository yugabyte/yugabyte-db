// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/yql_tables_vtable.h"

namespace yb {
namespace master {

YQLTablesVTable::YQLTablesVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemSchemaTablesTableName, master, CreateSchema()) {
}

Status YQLTablesVTable::RetrieveData(const YQLReadRequestPB& request,
                                     std::unique_ptr<YQLRowBlock>* vtable) const {
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
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, nsInfo->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kTableName, table->name(), &row));

    // Create appropriate flags entry.
    YQLValuePB flags_set;
    YQLValue::set_set_value(&flags_set);
    YQLValuePB flags_elem;
    YQLValue::set_string_value("compound", &flags_elem);
    *YQLValue::add_set_elem(&flags_set) = flags_elem;
    RETURN_NOT_OK(SetColumnValue(kFlags, flags_set, &row));

    // Create appropriate table uuid entry.
    Uuid uuid;
    // Note: table id is in host byte order.
    RETURN_NOT_OK(uuid.FromHexString(table->id()));
    RETURN_NOT_OK(SetColumnValue(kId, uuid, &row));
  }

  return Status::OK();
}

Schema YQLTablesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kKeyspaceName, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kBloomFilterChance, YQLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kCaching, YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCdc, YQLType::Create(DataType::BOOL)));
  CHECK_OK(builder.AddColumn(kComment, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCompaction,
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCompression,
                             YQLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  CHECK_OK(builder.AddColumn(kCrcCheck, YQLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kLocalReadRepair, YQLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kDefaultTimeToLive, YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kExtensions,
                             YQLType::CreateTypeMap(DataType::STRING, DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kFlags, YQLType::CreateTypeSet(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kGcGraceSeconds, YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kId, YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kMaxIndexInterval, YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kMemTableFlushPeriod, YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kMinIndexInterval, YQLType::Create(DataType::INT32)));
  CHECK_OK(builder.AddColumn(kReadRepairChance, YQLType::Create(DataType::DOUBLE)));
  CHECK_OK(builder.AddColumn(kSpeculativeRetry, YQLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
