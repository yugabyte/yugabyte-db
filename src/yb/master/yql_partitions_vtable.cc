// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/yql_partitions_vtable.h"

namespace yb {
namespace master {

YQLPartitionsVTable::YQLPartitionsVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemPartitionsTableName, master, CreateSchema()) {
}

Status YQLPartitionsVTable::RetrieveData(const YQLReadRequestPB& request,
                                         std::unique_ptr<YQLRowBlock>* vtable) const {
  vtable->reset(new YQLRowBlock(schema_));
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

      YQLRow& row = (*vtable)->Extend();
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
      YQLValuePB replica_addresses;
      YQLValue::set_map_value(&replica_addresses);
      for (const auto replica : tabletLocationsPB.replicas()) {
        InetAddress addr;
        RETURN_NOT_OK(addr.FromString(replica.ts_info().rpc_addresses(0).host()));
        YQLValue::set_inetaddress_value(addr, YQLValue::add_map_key(&replica_addresses));
        const string& role = consensus::RaftPeerPB::Role_Name(replica.role());
        YQLValue::set_string_value(role, YQLValue::add_map_value(&replica_addresses));
      }
      RETURN_NOT_OK(SetColumnValue(kReplicaAddresses, replica_addresses, &row));
    }
  }

  return Status::OK();
}

Schema YQLPartitionsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, YQLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kStartKey, YQLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kEndKey, YQLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kId, YQLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kReplicaAddresses,
                             YQLType::CreateTypeMap(DataType::INET, DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
