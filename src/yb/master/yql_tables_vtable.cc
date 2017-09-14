// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

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

    // Set the values for the table properties.
    Schema schema;
    RETURN_NOT_OK(table->GetSchema(&schema));

    // Adjusting precision, we use milliseconds internally, CQL uses seconds.
    // Sanity check, larger TTL values should be caught during analysis.
    DCHECK_LE(schema.table_properties().DefaultTimeToLive(),
              MonoTime::kMillisecondsPerSecond * std::numeric_limits<int32>::max());
    int32_t cql_ttl = static_cast<int32_t>(
        schema.table_properties().DefaultTimeToLive() / MonoTime::kMillisecondsPerSecond);
    RETURN_NOT_OK(SetColumnValue(kDefaultTimeToLive, cql_ttl, &row));
  }

  return Status::OK();
}

Schema YQLTablesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, YQLType::Create(DataType::STRING)));
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
