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

#include "yb/common/ql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/yql_partitions_vtable.h"

namespace yb {
namespace master {

YQLPartitionsVTable::YQLPartitionsVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemSchemaPartitionsTableName, master, CreateSchema()) {
}

Status YQLPartitionsVTable::RetrieveData(const QLReadRequestPB& request,
                                         std::unique_ptr<QLRowBlock>* vtable) const {
  vtable->reset(new QLRowBlock(schema_));
  std::vector<scoped_refptr<TableInfo> > tables;
  CatalogManager* catalog_manager = master_->catalog_manager();
  catalog_manager->GetAllTables(&tables, true /* includeOnlyRunningTables */);
  for (const scoped_refptr<TableInfo>& table : tables) {

    // Get namespace for table.
    NamespaceIdentifierPB nsId;
    nsId.set_id(table->namespace_id());
    scoped_refptr<NamespaceInfo> nsInfo;
    RETURN_NOT_OK(catalog_manager->FindNamespace(nsId, &nsInfo));

    // Get tablets for table.
    std::vector<scoped_refptr<TabletInfo> > tablets;
    table->GetAllTablets(&tablets);
    for (const scoped_refptr<TabletInfo>& tablet : tablets) {

      QLRow& row = (*vtable)->Extend();
      RETURN_NOT_OK(SetColumnValue(kKeyspaceName, nsInfo->name(), &row));
      RETURN_NOT_OK(SetColumnValue(kTableName, table->name(), &row));

      TabletLocationsPB tabletLocationsPB;
      RETURN_NOT_OK(catalog_manager->GetTabletLocations(tablet->id(), &tabletLocationsPB));

      const PartitionPB& partition = tabletLocationsPB.partition();
      RETURN_NOT_OK(SetColumnValue(kStartKey, partition.partition_key_start(), &row));
      RETURN_NOT_OK(SetColumnValue(kEndKey, partition.partition_key_end(), &row));

      // Note: tablet id is in host byte order.
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromHexString(tablet->id()));
      RETURN_NOT_OK(SetColumnValue(kId, uuid, &row));

      // Get replicas for tablet.
      QLValuePB replica_addresses;
      QLValue::set_map_value(&replica_addresses);
      for (const auto replica : tabletLocationsPB.replicas()) {
        InetAddress addr;
        RETURN_NOT_OK(addr.FromString(replica.ts_info().rpc_addresses(0).host()));
        QLValue::set_inetaddress_value(addr, QLValue::add_map_key(&replica_addresses));
        const string& role = consensus::RaftPeerPB::Role_Name(replica.role());
        QLValue::set_string_value(role, QLValue::add_map_value(&replica_addresses));
      }
      RETURN_NOT_OK(SetColumnValue(kReplicaAddresses, replica_addresses, &row));
    }
  }

  return Status::OK();
}

Schema YQLPartitionsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kStartKey, QLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kEndKey, QLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kReplicaAddresses,
                             QLType::CreateTypeMap(DataType::INET, DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
