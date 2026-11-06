// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/system_tables.h"

#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"

#include "yb/dockv/partition.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/yql_aggregates_vtable.h"
#include "yb/master/yql_auth_resource_role_permissions_index.h"
#include "yb/master/yql_auth_role_permissions_vtable.h"
#include "yb/master/yql_auth_roles_vtable.h"
#include "yb/master/yql_columns_vtable.h"
#include "yb/master/yql_functions_vtable.h"
#include "yb/master/yql_indexes_vtable.h"
#include "yb/master/yql_keyspaces_vtable.h"
#include "yb/master/yql_local_vtable.h"
#include "yb/master/yql_partitions_vtable.h"
#include "yb/master/yql_peers_vtable.h"
#include "yb/master/yql_size_estimates_vtable.h"
#include "yb/master/yql_tables_vtable.h"
#include "yb/master/yql_triggers_vtable.h"
#include "yb/master/yql_types_vtable.h"
#include "yb/master/yql_views_vtable.h"

namespace yb::master {

namespace {

template <class T>
std::unique_ptr<YQLVirtualTable> MakeVTable(const SystemTableDef& def, Master* master) {
  return std::make_unique<T>(def.table_name, def.namespace_name, master);
}

template <class T>
SystemTableDef Def(const char* table_name, const char* namespace_name, const char* namespace_id) {
  return SystemTableDef{
      .table_name = table_name,
      .namespace_name = namespace_name,
      .namespace_id = namespace_id,
      .create_schema = &T::CreateSchema,
      .make_vtable = &MakeVTable<T>,
  };
}

Result<dockv::PartitionSchema> SinglePartitionSchema(
    const PartitionSchemaPB& pb, const Schema& schema) {
  dockv::PartitionSchema partition_schema;
  RETURN_NOT_OK(dockv::PartitionSchema::FromPB(pb, schema, &partition_schema));
  return partition_schema;
}

Result<PartitionPB> OnlyPartition(const dockv::PartitionSchema& partition_schema) {
  std::vector<dockv::Partition> partitions;
  RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));
  PartitionPB partition;
  partitions[0].ToPB(&partition);
  return partition;
}

}  // namespace

const std::vector<SystemTableDef>& SystemTableDefs() {
  static const auto* const defs = new std::vector<SystemTableDef>{
      Def<PeersVTable>(kSystemPeersTableName, kSystemNamespaceName, kSystemNamespaceId),
      Def<LocalVTable>(kSystemLocalTableName, kSystemNamespaceName, kSystemNamespaceId),
      Def<YQLKeyspacesVTable>(
          kSystemSchemaKeyspacesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLTablesVTable>(
          kSystemSchemaTablesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLColumnsVTable>(
          kSystemSchemaColumnsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLSizeEstimatesVTable>(
          kSystemSizeEstimatesTableName, kSystemNamespaceName, kSystemNamespaceId),
      // Empty tables.
      Def<YQLAggregatesVTable>(
          kSystemSchemaAggregatesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLFunctionsVTable>(
          kSystemSchemaFunctionsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLIndexesVTable>(
          kSystemSchemaIndexesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLTriggersVTable>(
          kSystemSchemaTriggersTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLViewsVTable>(
          kSystemSchemaViewsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<QLTypesVTable>(
          kSystemSchemaTypesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId),
      Def<YQLPartitionsVTable>(
          kSystemPartitionsTableName, kSystemNamespaceName, kSystemNamespaceId),
      // System auth tables.
      Def<YQLAuthRolesVTable>(
          kSystemAuthRolesTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId),
      Def<YQLAuthRolePermissionsVTable>(
          kSystemAuthRolePermissionsTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId),
      Def<YQLAuthResourceRolePermissionsIndexVTable>(
          kSystemAuthResourceRolePermissionsIndexTableName, kSystemAuthNamespaceName,
          kSystemAuthNamespaceId),
  };
  return *defs;
}

SysTablesEntryPB SysCatalogTableEntry(const Schema& sys_catalog_schema) {
  SysTablesEntryPB metadata;
  metadata.set_state(SysTablesEntryPB::RUNNING);
  metadata.set_namespace_id(kSystemSchemaNamespaceId);
  metadata.set_namespace_name(kSystemSchemaNamespaceName);
  metadata.set_name(kSysCatalogTableName);
  metadata.set_table_type(TableType::YQL_TABLE_TYPE);
  SchemaToPB(sys_catalog_schema, metadata.mutable_schema());
  metadata.set_version(0);
  return metadata;
}

Result<SysTabletsEntryPB> SysCatalogTabletEntry(
    const Schema& sys_catalog_schema, const SysTablesEntryPB& table) {
  SysTabletsEntryPB metadata;
  metadata.set_state(SysTabletsEntryPB::RUNNING);
  *metadata.mutable_partition() = VERIFY_RESULT(OnlyPartition(
      VERIFY_RESULT(SinglePartitionSchema(table.partition_schema(), sys_catalog_schema))));
  metadata.set_table_id(kSysCatalogTableId);
  metadata.add_table_ids(kSysCatalogTableId);
  return metadata;
}

Result<bool> UpdateSystemTableSchema(const Schema& schema, SysTablesEntryPB* table) {
  Schema persisted;
  RETURN_NOT_OK(SchemaFromPB(table->schema(), &persisted));
  if (persisted.Equals(schema)) {
    return false;
  }
  SchemaToPB(schema, table->mutable_schema());
  table->set_version(table->version() + 1);
  table->set_updates_only_index_permissions(false);
  return true;
}

Result<SysTablesEntryPB> NewSystemTableEntry(const SystemTableDef& def) {
  const auto schema = def.create_schema();
  PartitionSchemaPB partition_schema_pb;
  partition_schema_pb.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
  const auto partition_schema = VERIFY_RESULT(SinglePartitionSchema(partition_schema_pb, schema));
  CreateTableRequestPB req;
  req.set_name(def.table_name);
  req.set_table_type(TableType::YQL_TABLE_TYPE);
  SysTablesEntryPB metadata;
  FillNewTableEntry(
      req, schema, partition_schema, def.namespace_id, def.namespace_name, /* colocated= */ false,
      &metadata);
  metadata.set_state(SysTablesEntryPB::RUNNING);
  return metadata;
}

Result<SysTabletsEntryPB> NewSystemTabletEntry(
    const TableId& table_id, const SysTablesEntryPB& table, const TabletId& tablet_id) {
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(table.schema(), &schema));
  const auto partition = VERIFY_RESULT(
      OnlyPartition(VERIFY_RESULT(SinglePartitionSchema(table.partition_schema(), schema))));
  auto table_info = make_scoped_refptr<TableInfo>(table_id, /* colocated= */ false);
  table_info->mutable_metadata()->StartMutation();
  table_info->mutable_metadata()->mutable_dirty()->pb = table;
  table_info->mutable_metadata()->CommitMutation();
  table_info->set_is_system();
  auto tablet = CreateTabletInfo(table_info, partition, SysTabletsEntryPB::RUNNING, tablet_id);
  auto entry = tablet->mutable_metadata()->mutable_dirty()->pb;
  tablet->mutable_metadata()->AbortMutation();
  return entry;
}

}  // namespace yb::master
