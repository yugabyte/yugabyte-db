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
//

#include "yb/master/xcluster/master_xcluster_util.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/xcluster_util.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/xcluster/xcluster_manager.h"

DECLARE_uint32(xcluster_ysql_statement_timeout_sec);

namespace yb::master {

static const auto kXClusterDDLExtensionName = xcluster::kDDLQueuePgSchemaName;

bool IsTableEligibleForXClusterReplication(
    const master::TableInfo& table, bool is_automatic_ddl_mode) {
  if (!table.LockForRead()->visible_to_client()) {
    // Ignore dropped tables.
    return false;
  }

  if (table.GetTableType() != PGSQL_TABLE_TYPE || table.is_system()) {
    // DB Scoped replication Limited to ysql databases.
    // System tables are not replicated. DDLs statements will be replicated and executed on the
    // target universe to handle catalog changes.
    return false;
  }

  if (table.IsColocationParentTable()) {
    // The colocated parent table needs to be replicated.
    return true;
  }

  if (table.is_matview() && !is_automatic_ddl_mode) {
    // For non-automatic modes, materialized views are not replicated, since they are not modified
    // after creation. Refresh materialized view will recreate the view, and needs to be rerun on
    // the target.
    return false;
  }

  if (table.IsSecondaryTable()) {
    // Only the colocated parent table needs to be replicated.
    return false;
  }

  if (table.IsSequencesSystemTable()) {
    // The sequences_data table is treated specially elsewhere.
    return false;
  }

  if (table.IsXClusterDDLReplicationReplicatedDDLsTable()) {
    // replicated_ddls is only used on the target, so we do not want to replicate it.
    return false;
  }

  return true;
}

std::string GetFullTableName(const TableInfo& table_info) {
  const auto& schema_name = table_info.pgschema_name();
  if (schema_name.empty()) {
    return table_info.name();
  }

  return Format("$0.$1", schema_name, table_info.name());
}

TableDesignator::TableDesignator(TableInfoPtr table_info)
    : id(table_info->id()), table_info(std::move(table_info)) {
  DCHECK(!this->table_info->IsSequencesSystemTable());
}

TableDesignator::TableDesignator(TableInfoPtr sequence_table_info, const NamespaceId& namespace_id)
    : id(xcluster::GetSequencesDataAliasForNamespace(namespace_id)),
      table_info(std::move(sequence_table_info)) {
  DCHECK(table_info->IsSequencesSystemTable());
}

TableDesignator TableDesignator::CreateSequenceTableDesignator(
    TableInfoPtr sequence_table_info, const NamespaceId& namespace_id) {
  return TableDesignator(sequence_table_info, namespace_id);
}

std::string TableDesignator::name() const { return table_info->name(); }

std::string TableDesignator::pgschema_name() const { return table_info->pgschema_name(); }

std::string TableDesignator::ToString() const {
  return strings::Substitute("$0.$1 [id=$2]", pgschema_name(), name(), id);
}

Result<std::vector<TableDesignator>> GetTablesEligibleForXClusterReplication(
    const CatalogManager& catalog_manager, const NamespaceId& namespace_id,
    bool automatic_ddl_mode) {
  auto table_infos = VERIFY_RESULT(catalog_manager.GetTableInfosForNamespace(namespace_id));

  std::vector<TableDesignator> table_designators{};
  for (const auto& table_info : table_infos) {
    if (IsTableEligibleForXClusterReplication(*table_info, automatic_ddl_mode)) {
      table_designators.emplace_back(table_info);
    }
  }

  if (automatic_ddl_mode) {
    auto sequence_table_info = catalog_manager.GetTableInfo(kPgSequencesDataTableId);
    if (sequence_table_info) {
      // Due to a bug with the CreateTable code, it is possible for GetTableInfo to return a
      // TableInfo for a table being created still in-state UNKNOWN.  If we see this, just ignore
      // the table as it is still being created and we can pretend we looked before it started being
      // created.
      if (sequence_table_info.get()->LockForRead()->pb.state() != SysTablesEntryPB::UNKNOWN) {
        table_designators.emplace_back(
            TableDesignator::CreateSequenceTableDesignator(sequence_table_info, namespace_id));
      }
    }
  }
  return table_designators;
}

bool IsDbScoped(const SysUniverseReplicationEntryPB& replication_info) {
  return replication_info.has_db_scoped_info() &&
         replication_info.db_scoped_info().namespace_infos_size() > 0;
}

bool IsAutomaticDdlMode(const SysUniverseReplicationEntryPB& replication_info) {
  return replication_info.has_db_scoped_info() &&
         replication_info.db_scoped_info().automatic_ddl_mode();
}

Status SetupDDLReplicationExtension(
    CatalogManagerIf& catalog_manager, const NamespaceId& namespace_id,
    XClusterDDLReplicationRole role, StdStatusCallback callback) {
  auto namespace_name = VERIFY_RESULT(catalog_manager.FindNamespaceById(namespace_id))->name();
  LOG(INFO) << "Creating if exists " << kXClusterDDLExtensionName << " extension for namespace "
            << namespace_id << " (" << namespace_name << ")";

  auto statement = Format("CREATE EXTENSION IF NOT EXISTS $0", kXClusterDDLExtensionName);
  RETURN_NOT_OK(ExecutePgsqlStatements(
      namespace_name, {statement}, catalog_manager,
      CoarseMonoClock::now() + MonoDelta::FromSeconds(FLAGS_xcluster_ysql_statement_timeout_sec),
      std::move(callback)));

  auto* xcluster_manager = catalog_manager.GetXClusterManagerImpl();
  return xcluster_manager->SetXClusterRole(catalog_manager.GetLeaderEpochInternal(),
      namespace_id, role == XClusterDDLReplicationRole::kSource
                        ? XClusterNamespaceInfoPB_XClusterRole_AUTOMATIC_SOURCE
                        : XClusterNamespaceInfoPB_XClusterRole_AUTOMATIC_TARGET);
}

Status DropDDLReplicationExtensionIfExists(
    CatalogManagerIf& catalog_manager, const NamespaceId& namespace_id,
    StdStatusCallback callback) {
  auto namespace_name = VERIFY_RESULT(catalog_manager.FindNamespaceById(namespace_id))->name();
  LOG(INFO) << "Dropping if exists " << kXClusterDDLExtensionName << " extension for namespace "
            << namespace_id << " (" << namespace_name << ")";

  // Disable the extension first to prevent any conflicts with later setups.
  auto* xcluster_manager = catalog_manager.GetXClusterManagerImpl();
  RETURN_NOT_OK(xcluster_manager->SetXClusterRole(
      catalog_manager.GetLeaderEpochInternal(), namespace_id,
      XClusterNamespaceInfoPB_XClusterRole_NOT_AUTOMATIC_MODE));

  auto statement = Format("DROP EXTENSION IF EXISTS $0", kXClusterDDLExtensionName);
  return ExecutePgsqlStatements(
      namespace_name, {statement}, catalog_manager,
      CoarseMonoClock::now() + MonoDelta::FromSeconds(FLAGS_xcluster_ysql_statement_timeout_sec),
      std::move(callback));
}

}  // namespace yb::master
