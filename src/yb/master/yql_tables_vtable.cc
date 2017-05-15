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
  using namespace util;
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
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, GetStringValue(nsInfo->name()), &row));
    RETURN_NOT_OK(SetColumnValue(kTableName, GetStringValue(table->name()), &row));

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
    RETURN_NOT_OK(SetColumnValue(kId, GetUuidValue(uuid), &row));
  }

  return Status::OK();
}

Schema YQLTablesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kKeyspaceName, DataType::STRING));
  CHECK_OK(builder.AddKeyColumn(kTableName, DataType::STRING));
  CHECK_OK(builder.AddColumn(kBloomFilterChance, DataType::DOUBLE));
  CHECK_OK(builder.AddColumn(
      kCaching,
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn(kCdc, DataType::BOOL));
  CHECK_OK(builder.AddColumn(kComment, DataType::STRING));
  CHECK_OK(builder.AddColumn(
      kCompaction,
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn(
      kCompression,
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn(kCrcCheck, DataType::DOUBLE));
  CHECK_OK(builder.AddColumn(kLocalReadRepair, DataType::DOUBLE));
  CHECK_OK(builder.AddColumn(kDefaultTimeToLive, DataType::INT32));
  CHECK_OK(builder.AddColumn(
      kExtensions,
      YQLType(DataType::MAP, { YQLType(DataType::STRING), YQLType(DataType::BINARY) })));
  CHECK_OK(builder.AddColumn(
      kFlags,
      YQLType(DataType::SET, { YQLType(DataType::STRING) })));
  CHECK_OK(builder.AddColumn(kGcGraceSeconds, DataType::INT32));
  CHECK_OK(builder.AddColumn(kId, DataType::UUID));
  CHECK_OK(builder.AddColumn(kMaxIndexInterval, DataType::INT32));
  CHECK_OK(builder.AddColumn(kMemTableFlushPeriod, DataType::INT32));
  CHECK_OK(builder.AddColumn(kMinIndexInterval, DataType::INT32));
  CHECK_OK(builder.AddColumn(kReadRepairChance, DataType::DOUBLE));
  CHECK_OK(builder.AddColumn(kSpeculativeRetry, DataType::STRING));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
