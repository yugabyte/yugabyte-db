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
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"

namespace yb::master {

bool IsTableEligibleForXClusterReplication(const master::TableInfo& table) {
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

  if (table.is_matview()) {
    // Materialized views need not be replicated, since they are not modified. Every time the view
    // is refreshed, new tablets are created. The same refresh can just run on the target universe.
    return false;
  }

  if (table.IsColocatedUserTable()) {
    // Only the colocated parent table needs to be replicated.
    return false;
  }

  if (table.IsSequencesSystemTable()) {
    // xCluster does not yet support replication of sequences.
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

Result<std::vector<TableInfoPtr>> GetTablesEligibleForXClusterReplication(
    const CatalogManager& catalog_manager, const NamespaceId& namespace_id) {
  auto table_infos = VERIFY_RESULT(catalog_manager.GetTableInfosForNamespace(namespace_id));
  EraseIf(
      [](const TableInfoPtr& table) { return !IsTableEligibleForXClusterReplication(*table); },
      &table_infos);
  return table_infos;
}

bool IsDbScoped(const SysUniverseReplicationEntryPB& replication_info) {
  return replication_info.has_db_scoped_info() &&
         replication_info.db_scoped_info().namespace_infos_size() > 0;
}
}  // namespace yb::master
